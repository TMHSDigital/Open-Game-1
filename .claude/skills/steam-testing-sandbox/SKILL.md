---
name: steam-testing-sandbox
description: Develop and test Combat Simulator's Steam features with App ID 480 (Spacewar) through UE 5.8's OnlineSubsystemSteam, so real achievements and stats are never touched. Covers DefaultEngine.ini setup, what works on 480, testing with two Steam accounts, and the dev-to-production switch. Use when setting up or debugging Steam in the Unreal project.
---

# Steam testing sandbox (UE 5.8)

Adapted from the Steam-Cursor-Plugin `steam-testing-sandbox` skill for Unreal. Captures and tests always run under App ID 480 (Spacewar) or a stub Steam backend. Steam features are not part of the project yet; add them through a decision (`decide` skill) first.

## Unreal setup on App ID 480

1. Enable plugins in `Game/CombatSimulator.uproject`: `OnlineSubsystemSteam`, `SteamSockets`. (Editor: Edit, Plugins, search "Online Subsystem Steam" and "Steam Sockets", tick both, restart.)
2. `Game/Source/CombatSimulator/CombatSimulator.Build.cs`: add `"OnlineSubsystem", "OnlineSubsystemUtils"` to public dependencies and `DynamicallyLoadedModuleNames.Add("OnlineSubsystemSteam");`.
3. `Game/Config/DefaultEngine.ini`:
   ```ini
   [/Script/Engine.GameEngine]
   !NetDriverDefinitions=ClearArray
   +NetDriverDefinitions=(DefName="GameNetDriver",DriverClassName="/Script/SteamSockets.SteamSocketsNetDriver",DriverClassNameFallback="/Script/OnlineSubsystemUtils.IpNetDriver")

   [OnlineSubsystem]
   DefaultPlatformService=Steam

   [OnlineSubsystemSteam]
   bEnabled=true
   SteamDevAppId=480
   bUseSteamNetworking=true
   bAllowP2PPacketRelay=true
   bInitServerOnClient=true
   ```
4. The Steamworks SDK is bundled with the engine (`E:\UE_5.8\Engine\Binaries\ThirdParty\Steamworks\`). Never download or commit another copy.

**Steam only initializes in Standalone or packaged builds**, not in PIE. Test with `tools/ue/package.ps1` output or the editor's "Standalone Game" mode, with the Steam client running and logged in. Look for `LogOnline: STEAM: Steam API initialized` in the log.

## What works on 480

Works: init and overlay, lobbies and sessions (`CreateSession`, `FindSessions`, `JoinSession` through OSS), Steam Sockets relay, friends and presence, Spacewar's own achievements and stats.

Does not work: our own achievements and stats names, Workshop, Inventory, DLC checks, store page. These need the real App ID.

Session searches on 480 return **other developers' sessions too**. Filter with a custom session setting (for example `SETTING_GAMEMODE = "CombatSimulator_Dev"`).

## Multiplayer testing

- Each player needs a separate Steam account; one account cannot join itself.
- With the real app: Steamworks, Request Keys, generate developer comp keys, activate on each test account. Keys are secrets: never in the repo.
- Before Steam is wired in, test networking with PIE "Play As Listen Server" and 2 players (no Steam involved).

## Steam client console

`steam://open/console` in a browser, or launch Steam with `-console`. Useful: `app_info_print <appid>`, `app_status <appid>`, `set_spew_level 4 4`.

## Dev to production checklist

| Item | Dev | Production |
|---|---|---|
| `SteamDevAppId` | 480 | The real App ID, from the owner's environment at build time; never committed (this repo is public) |
| `steam_appid.txt` | Next to the exe for dev runs (gitignored) | Never shipped: staging and the depot VDF exclude it |
| Achievements | Spacewar's | Ours, configured and published in Steamworks |
| Test data | Anything | Reset achievements, stats and leaderboards before launch |

How the real App ID reaches a packaged build without being committed is an open question: record it as a decision before enabling Steam in Shipping builds.

## MCP (steam server, read-only use)

`steam_getAppDetails({ appid: 480 })` confirms the app is reachable. Publisher-key tools stay off unless the owner deliberately runs a test session; never ask for or store the publisher key.
