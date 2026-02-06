/*
 * This file is part of the AzerothCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "Chat.h"
#include "Config.h"
#include "DatabaseEnv.h"
#include "GameTime.h"
#include "Player.h"
#include "Random.h"
#include "ScriptMgr.h"
#include "ScriptedGossip.h"
#include "Spell.h"
#include "StringFormat.h"
#include "Unit.h"
#include "World.h"

using namespace std::chrono_literals;

namespace
{
    constexpr uint32 kMenuId = 92100;
    constexpr uint32 kActionGetChallenge = GOSSIP_ACTION_INFO_DEF + 1;
    constexpr uint32 kActionClaimReward = GOSSIP_ACTION_INFO_DEF + 2;
    constexpr uint32 kActionShowStatus = GOSSIP_ACTION_INFO_DEF + 3;
    constexpr uint32 kActionCancelChallenge = GOSSIP_ACTION_INFO_DEF + 4;
    constexpr uint32 kActionClose = GOSSIP_ACTION_INFO_DEF + 5;

    constexpr uint32 kChallengeKillCreatures = 1;
    constexpr uint32 kChallengeHealPenalty = 2;

    struct ChallengeState
    {
        uint32 lastReset = 0;
        uint32 challengeId = 0;
        uint32 progress = 0;
        bool completed = false;
        bool rewarded = false;
        bool canceled = false;
    };

    bool IsModuleEnabled()
    {
        return sConfigMgr->GetOption<bool>("DailyChallenge.Enable", true);
    }

    uint32 GetChallengeSpellId()
    {
        return sConfigMgr->GetOption<uint32>("DailyChallenge.SpellId", 0);
    }

    uint32 GetKillTargetCount()
    {
        return sConfigMgr->GetOption<uint32>("DailyChallenge.KillTarget", 10);
    }

    int32 GetKillRewardMoney()
    {
        return sConfigMgr->GetOption<int32>("DailyChallenge.RewardMoney", 10000);
    }

    int32 GetHealPenaltyHonorReward()
    {
        return sConfigMgr->GetOption<int32>("DailyChallenge.HealPenaltyRewardHonor", 30000);
    }

    uint32 GetHealPenaltyPercent()
    {
        return sConfigMgr->GetOption<uint32>("DailyChallenge.HealPenaltyPercent", 50);
    }

    bool IsKillChallengeEnabled()
    {
        return sConfigMgr->GetOption<bool>("DailyChallenge.EnableKillChallenge", true);
    }

    bool IsHealPenaltyChallengeEnabled()
    {
        return sConfigMgr->GetOption<bool>("DailyChallenge.EnableHealPenaltyChallenge", true);
    }

    uint32 GetCurrentResetStart()
    {
        auto resetTime = sWorld->GetNextDailyQuestsResetTime();
        auto startTime = resetTime - 1_days;
        return static_cast<uint32>(startTime.count());
    }

    ChallengeState LoadState(ObjectGuid::LowType guid)
    {
        ChallengeState state;
        QueryResult result = CharacterDatabase.Query(
            "SELECT last_reset, challenge_id, progress, completed, rewarded, canceled "
            "FROM mod_daily_challenge WHERE guid = {}",
            guid);

        if (!result)
            return state;

        Field* fields = result->Fetch();
        state.lastReset = fields[0].Get<uint32>();
        state.challengeId = fields[1].Get<uint32>();
        state.progress = fields[2].Get<uint32>();
        state.completed = fields[3].Get<uint8>() != 0;
        state.rewarded = fields[4].Get<uint8>() != 0;
        state.canceled = fields[5].Get<uint8>() != 0;
        return state;
    }

    void SaveState(ObjectGuid::LowType guid, ChallengeState const& state)
    {
        CharacterDatabase.Execute(
            "REPLACE INTO mod_daily_challenge "
            "(guid, last_reset, challenge_id, progress, completed, rewarded, canceled) "
            "VALUES ({}, {}, {}, {}, {}, {}, {})",
            guid,
            state.lastReset,
            state.challengeId,
            state.progress,
            state.completed ? 1 : 0,
            state.rewarded ? 1 : 0,
            state.canceled ? 1 : 0);
    }

    void RewardHealPenaltyChallenge(Player* player)
    {
        int32 honorReward = GetHealPenaltyHonorReward();
        if (honorReward <= 0)
            return;

        player->ModifyHonorPoints(honorReward);
        SendPlayerMessage(player, "Daily challenge completed! Honor points have been awarded.");
    }

    bool EnsureCurrentCycle(Player* player, ObjectGuid::LowType guid, ChallengeState& state)
    {
        uint32 currentReset = GetCurrentResetStart();
        if (state.lastReset == currentReset)
            return false;

        if (player && state.challengeId == kChallengeHealPenalty && !state.canceled && !state.rewarded)
        {
            RewardHealPenaltyChallenge(player);
        }

        state.lastReset = currentReset;
        state.challengeId = 0;
        state.progress = 0;
        state.completed = false;
        state.rewarded = false;
        state.canceled = false;
        SaveState(guid, state);
        return true;
    }

    std::string BuildChallengeDescription(uint32 challengeId, uint32 progress)
    {
        if (challengeId == kChallengeKillCreatures)
        {
            uint32 target = GetKillTargetCount();
            return Acore::StringFormat("Kill {} creatures ({}/{})", target, progress, target);
        }

        if (challengeId == kChallengeHealPenalty)
        {
            uint32 penalty = GetHealPenaltyPercent();
            int32 honorReward = GetHealPenaltyHonorReward();
            return Acore::StringFormat("Receive {}% less healing from other players today. Reward: {} honor points after reset.", penalty, honorReward);
        }

        return "No challenge assigned";
    }

    void SendPlayerMessage(Player* player, std::string const& message)
    {
        ChatHandler(player->GetSession()).SendSysMessage(message);
    }

    void ShowChallengeMenu(Player* player)
    {
        ObjectGuid::LowType guid = player->GetGUID().GetCounter();
        ChallengeState state = LoadState(guid);
        EnsureCurrentCycle(player, guid, state);

        ClearGossipMenuFor(player);
        player->PlayerTalkClass->GetGossipMenu().SetMenuId(kMenuId);

        if (state.canceled)
        {
            AddGossipItemFor(player, GOSSIP_ICON_CHAT, "Daily challenge canceled for today.", GOSSIP_SENDER_MAIN, kActionShowStatus);
        }
        else if (state.challengeId == 0 && !state.completed)
        {
            AddGossipItemFor(player, GOSSIP_ICON_CHAT, "Get today’s challenge", GOSSIP_SENDER_MAIN, kActionGetChallenge);
        }
        else
        {
            std::string description = BuildChallengeDescription(state.challengeId, state.progress);
            AddGossipItemFor(player, GOSSIP_ICON_CHAT, description, GOSSIP_SENDER_MAIN, kActionShowStatus);
        }

        if (state.completed && !state.rewarded)
        {
            AddGossipItemFor(player, GOSSIP_ICON_MONEY_BAG, "Claim reward", GOSSIP_SENDER_MAIN, kActionClaimReward);
        }

        if (state.challengeId != 0 && !state.rewarded)
        {
            AddGossipItemFor(player, GOSSIP_ICON_CHAT, "Cancel today’s challenge", GOSSIP_SENDER_MAIN, kActionCancelChallenge);
        }

        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "Close", GOSSIP_SENDER_MAIN, kActionClose);
        player->PlayerTalkClass->SendGossipMenu(DEFAULT_GOSSIP_MESSAGE, player->GetGUID());
    }

    std::vector<uint32> GetEnabledChallenges()
    {
        std::vector<uint32> challenges;
        if (IsKillChallengeEnabled())
            challenges.push_back(kChallengeKillCreatures);
        if (IsHealPenaltyChallengeEnabled())
            challenges.push_back(kChallengeHealPenalty);
        return challenges;
    }

    void AssignDailyChallenge(Player* player)
    {
        ObjectGuid::LowType guid = player->GetGUID().GetCounter();
        ChallengeState state = LoadState(guid);
        EnsureCurrentCycle(player, guid, state);

        if (state.canceled)
        {
            SendPlayerMessage(player, "You canceled today’s challenge and cannot accept another until the next reset.");
            return;
        }

        if (state.challengeId != 0 || state.completed)
        {
            SendPlayerMessage(player, "You already received today’s challenge.");
            return;
        }

        std::vector<uint32> challenges = GetEnabledChallenges();
        if (challenges.empty())
        {
            SendPlayerMessage(player, "No daily challenges are enabled right now.");
            return;
        }

        state.challengeId = challenges[urand(0u, challenges.size() - 1)];
        state.progress = 0;
        state.completed = false;
        state.rewarded = false;
        state.canceled = false;
        SaveState(guid, state);

        SendPlayerMessage(player, "Daily challenge assigned.");
    }

    void TryClaimReward(Player* player)
    {
        ObjectGuid::LowType guid = player->GetGUID().GetCounter();
        ChallengeState state = LoadState(guid);
        EnsureCurrentCycle(player, guid, state);

        if (!state.completed)
        {
            SendPlayerMessage(player, "Complete the challenge before claiming the reward.");
            return;
        }

        if (state.rewarded)
        {
            SendPlayerMessage(player, "Reward already claimed for today.");
            return;
        }

        if (state.challengeId == kChallengeKillCreatures)
        {
            int32 rewardMoney = GetKillRewardMoney();
            if (rewardMoney > 0)
                player->ModifyMoney(rewardMoney);
        }
        else if (state.challengeId == kChallengeHealPenalty)
        {
            RewardHealPenaltyChallenge(player);
        }

        state.rewarded = true;
        SaveState(guid, state);

        SendPlayerMessage(player, "Reward claimed. See you tomorrow!");
    }

    void CancelChallenge(Player* player)
    {
        ObjectGuid::LowType guid = player->GetGUID().GetCounter();
        ChallengeState state = LoadState(guid);
        EnsureCurrentCycle(player, guid, state);

        if (state.challengeId == 0)
        {
            SendPlayerMessage(player, "No active challenge to cancel.");
            return;
        }

        state.challengeId = 0;
        state.progress = 0;
        state.completed = false;
        state.rewarded = false;
        state.canceled = true;
        SaveState(guid, state);

        SendPlayerMessage(player, "Daily challenge canceled. You will not receive a reward today.");
    }

    void UpdateChallengeProgress(Player* player)
    {
        ObjectGuid::LowType guid = player->GetGUID().GetCounter();
        ChallengeState state = LoadState(guid);
        if (EnsureCurrentCycle(player, guid, state))
            return;

        if (state.challengeId != kChallengeKillCreatures || state.completed || state.canceled)
            return;

        uint32 target = GetKillTargetCount();
        if (state.progress >= target)
            return;

        ++state.progress;
        if (state.progress >= target)
        {
            state.completed = true;
            SendPlayerMessage(player, "Daily challenge completed! Open the menu to claim your reward.");
        }

        SaveState(guid, state);
    }
}

class mod_daily_challenge_player : public PlayerScript
{
public:
    mod_daily_challenge_player()
        : PlayerScript("mod_daily_challenge_player",
            {
                PLAYERHOOK_ON_SPELL_CAST,
                PLAYERHOOK_ON_GOSSIP_SELECT,
                PLAYERHOOK_ON_CREATURE_KILL
            })
    {
    }

    void OnPlayerSpellCast(Player* player, Spell* spell, bool /*skipCheck*/) override
    {
        if (!IsModuleEnabled())
            return;

        uint32 spellId = GetChallengeSpellId();
        if (!spellId || spell->GetSpellInfo()->Id != spellId)
            return;

        ShowChallengeMenu(player);
    }

    void OnPlayerGossipSelect(Player* player, uint32 menuId, uint32 /*sender*/, uint32 action) override
    {
        if (menuId != kMenuId)
            return;

        switch (action)
        {
            case kActionGetChallenge:
                AssignDailyChallenge(player);
                ShowChallengeMenu(player);
                break;
            case kActionClaimReward:
                TryClaimReward(player);
                ShowChallengeMenu(player);
                break;
            case kActionShowStatus:
            {
                ObjectGuid::LowType guid = player->GetGUID().GetCounter();
                ChallengeState state = LoadState(guid);
                EnsureCurrentCycle(player, guid, state);
                if (state.canceled)
                {
                    SendPlayerMessage(player, "Daily challenge canceled for today.");
                }
                else
                {
                    SendPlayerMessage(player, BuildChallengeDescription(state.challengeId, state.progress));
                }
                ShowChallengeMenu(player);
                break;
            }
            case kActionCancelChallenge:
                CancelChallenge(player);
                ShowChallengeMenu(player);
                break;
            case kActionClose:
                CloseGossipMenuFor(player);
                break;
            default:
                break;
        }
    }

    void OnPlayerCreatureKill(Player* killer, Creature* /*killed*/) override
    {
        if (!IsModuleEnabled())
            return;

        UpdateChallengeProgress(killer);
    }
};

class mod_daily_challenge_unit : public UnitScript
{
public:
    mod_daily_challenge_unit()
        : UnitScript("mod_daily_challenge_unit", true, { UNITHOOK_MODIFY_HEAL_RECEIVED })
    {
    }

    void ModifyHealReceived(Unit* target, Unit* healer, uint32& heal, SpellInfo const* /*spellInfo*/) override
    {
        if (!IsModuleEnabled())
            return;

        if (!target || !healer)
            return;

        Player* targetPlayer = target->ToPlayer();
        Player* healerPlayer = healer->ToPlayer();
        if (!targetPlayer || !healerPlayer || targetPlayer == healerPlayer)
            return;

        ObjectGuid::LowType guid = targetPlayer->GetGUID().GetCounter();
        ChallengeState state = LoadState(guid);
        EnsureCurrentCycle(targetPlayer, guid, state);

        if (state.challengeId != kChallengeHealPenalty || state.canceled)
            return;

        uint32 penalty = GetHealPenaltyPercent();
        if (penalty >= 100)
        {
            heal = 0;
            return;
        }

        heal = uint32(float(heal) * (1.0f - (penalty / 100.0f)));
    }
};

void Addmod_daily_challengeScripts()
{
    new mod_daily_challenge_player();
    new mod_daily_challenge_unit();
}
