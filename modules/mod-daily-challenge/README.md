# Daily Challenge module

This module adds a simple daily challenge system that players access via a spell-triggered gossip menu.
Each day, a player can accept a kill challenge, track progress, and claim a reward.

## Features
- Spell-based menu to receive and track a daily challenge.
- Progress tracking on creature kills.
- Reward claim after completion.

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
DailyChallenge.SpellId = 0
DailyChallenge.KillTarget = 10
DailyChallenge.RewardMoney = 10000
```
- `DailyChallenge.SpellId`: Spell ID that opens the menu.
- `DailyChallenge.KillTarget`: Number of creature kills required.
- `DailyChallenge.RewardMoney`: Reward in copper (10000 = 1 gold).

## Notes
- The menu opens only when the configured spell is cast.
- Rewards can be claimed once per daily reset.
