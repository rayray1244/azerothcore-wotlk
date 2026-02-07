# Daily Challenge module

This module adds a daily challenge system that players access via a spell-triggered gossip menu.
Each day, a player can accept an enabled challenge, track progress, and claim rewards.

## Features
- Spell-based menu to receive and track a daily challenge.
- Creature-kill challenge with progress tracking and money rewards.
- Heal-penalty challenge (receive less healing from other players for a day) with honor rewards.
- Glass cannon challenge (take more damage from creatures for a fixed duration without dying).
- Undead hunter challenge (kill a set number of undead).
- Ability to cancel a challenge (no reward if canceled).

## Installation
1. Copy the module into your `modules/` folder (already in place if using this repo).
2. Apply the character database table:
   - `modules/mod-daily-challenge/sql/db-characters/mod_daily_challenge.sql`
3. Copy the config file:
   - `modules/mod-daily-challenge/conf/mod_daily_challenge.conf.dist` to your config directory
     (rename to `mod_daily_challenge.conf`).
4. Configure `DailyChallenge.SpellId` with the spell you want to use as the menu trigger.

## Configuration
```
DailyChallenge.Enable = 1
DailyChallenge.EnableKillChallenge = 1
DailyChallenge.EnableHealPenaltyChallenge = 1
DailyChallenge.EnableGlassCannonChallenge = 1
DailyChallenge.EnableUndeadHunterChallenge = 1
DailyChallenge.SpellId = 0
DailyChallenge.KillTarget = 10
DailyChallenge.RewardMoney = 10000
DailyChallenge.HealPenaltyPercent = 50
DailyChallenge.HealPenaltyRewardHonor = 30000
DailyChallenge.GlassCannonDurationHours = 12
DailyChallenge.GlassCannonDamageTakenPercent = 20
DailyChallenge.GlassCannonRewardHonor = 10000
DailyChallenge.UndeadKillTarget = 15
DailyChallenge.UndeadRewardMoney = 15000
```
- `DailyChallenge.SpellId`: Spell ID that opens the menu.
- `DailyChallenge.KillTarget`: Number of creature kills required.
- `DailyChallenge.RewardMoney`: Reward in copper (10000 = 1 gold).
- `DailyChallenge.EnableKillChallenge` / `DailyChallenge.EnableHealPenaltyChallenge`: Toggle specific challenges.
- `DailyChallenge.EnableGlassCannonChallenge` / `DailyChallenge.EnableUndeadHunterChallenge`: Toggle specific challenges.
- `DailyChallenge.HealPenaltyPercent`: Healing reduction from other players.
- `DailyChallenge.HealPenaltyRewardHonor`: Honor points awarded after the daily reset if not canceled.
- `DailyChallenge.GlassCannonDurationHours`: Duration (in hours) for the extra-damage challenge.
- `DailyChallenge.GlassCannonDamageTakenPercent`: Extra damage taken from creatures during the challenge.
- `DailyChallenge.GlassCannonRewardHonor`: Honor points awarded after completing the glass cannon challenge.
- `DailyChallenge.UndeadKillTarget`: Number of undead kills required.
- `DailyChallenge.UndeadRewardMoney`: Reward in copper for completing the undead hunter challenge.

## Notes
- The menu opens only when the configured spell is cast.
- Rewards can be claimed once per daily reset.
- If the heal-penalty challenge is active and not canceled, honor is awarded after the daily reset.
- Timed challenges send a completion message when they finish; open the menu to claim rewards.
