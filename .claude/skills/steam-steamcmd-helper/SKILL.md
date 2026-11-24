---
name: steam-steamcmd-helper
description: steamcmd reference for Combat Simulator on Windows - commands, Steam Guard, troubleshooting, and the release.ps1 upload path. Claude explains steamcmd and never calls it directly; uploads go through release.ps1. Use when the user asks about steamcmd, SteamPipe uploads, or installing a dedicated server tool app.
---

# steamcmd helper (Windows)

Adapted from the Steam-Cursor-Plugin `steam-steamcmd-helper` skill. **Claude never calls `steamcmd` directly.** Claude may explain commands and output; uploads go only through `tools/steam/release.ps1` (see the `release` skill).

## Install (owner)

```powershell
New-Item -ItemType Directory -Force C:\steamcmd | Out-Null
Invoke-WebRequest https://steamcdn-a.akamaihd.net/client/installer/steamcmd.zip -OutFile $env:TEMP\steamcmd.zip
Expand-Archive $env:TEMP\steamcmd.zip -DestinationPath C:\steamcmd -Force
C:\steamcmd\steamcmd.exe +quit   # first run self-updates
```

Do not install it under `Program Files` (it needs write access to its own folder). If it lives elsewhere, set `STEAMCMD_PATH` in `.env`.

## Commands

| Command | Use |
|---|---|
| `+login <user>` | Interactive login; prompts for password and Steam Guard. Never pass the password on the command line. |
| `+login anonymous` | Downloads of free dedicated server tools only |
| `+run_app_build <vdf>` | Upload a build through SteamPipe |
| `+force_install_dir <path>` | Must come **before** `+app_update` |
| `+app_update <appid> [-beta <branch>] validate` | Install or update an app |
| `+app_info_update 1` then `+app_info_print <appid>` | Print depots, branches and their BuildIDs (what `release.ps1 status` parses) |
| `+quit` | Always last, or steamcmd waits forever |

## Uploading a build

The only upload path is `tools/steam/release.ps1`, run by Claude or the owner (decision 0014; `development` only). It enforces the gates, bumps the version, packages, stages and fills the VDFs before steamcmd runs:

```powershell
pwsh tools/steam/release.ps1 dry-run   # gates, package, stage, VDFs; no upload (Claude may run this)
pwsh tools/steam/release.ps1 preview   # Claude or owner: steamcmd preview, nothing uploaded
pwsh tools/steam/release.ps1 dev       # Claude or owner: upload, set live on development
```

Never write or recommend a wrapper that calls `+run_app_build` directly: it would skip every gate and the ledger.

steamcmd prompts for the password and Steam Guard code interactively the first time (setup checklist step 5). Credentials are cached in steamcmd's own folder (`config\config.vdf`, `ssfn*`); those are secrets and never go near the repo.

## Dedicated server (later)

A Combat Simulator dedicated server would need a source-built engine and its own "Dedicated Server" tool app on Steam, downloadable anonymously:

```powershell
& C:\steamcmd\steamcmd.exe +login anonymous +force_install_dir D:\CombatSimulatorServer +app_update <server_tool_appid> validate +quit
```

## Troubleshooting

| Error | Fix |
|---|---|
| `Invalid Password` / Steam Guard expired | Log in interactively again |
| `Rate Limit Exceeded` | Wait 30 to 60 minutes |
| `No subscription` | Account lacks access to the app; check the build account's permissions |
| `Not enough disk space` | Free space or change `force_install_dir` |
| Stuck at `Update state (0x3)` | Delete steamcmd's `steamapps\downloading\`, rerun with `validate` |
| `Failed to commit build` / branch errors after upload | The `development` branch does not exist yet (setup checklist step 7) |
| Paths with spaces fail | Quote them |
| Hangs | Missing `+quit` |
