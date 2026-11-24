# Tools

The outside tools this project uses, how to install them, the environment variable names they read, which routines use them (see [WORKFLOWS.md](WORKFLOWS.md)), and what they cannot do yet. Values never go in the repo: names only, real values live in `.env` or in your own MCP config (`.mcp.json` is gitignored; `.mcp.json.example` is the committed template).

Licensing note: the owner's tool repos below are CC-BY-NC-ND-4.0 (the Blender-Developer-Tools snippets and templates are MIT). Game code may copy from MIT snippets and templates only; everything else is used as a tool, not copied into the game.

## steam-mcp (0.9.0)

- **What:** MCP server over the Steam Web API and Steamworks partner endpoints: store and community lookups, publisher data, SDK reference.
- **Needs:** Node 22 or newer.
- **Install:** `claude mcp add steam -e STEAM_API_KEY=<your-key> -- cmd /c npx -y @tmhs/steam-mcp`
- **Env names:** `STEAM_API_KEY`; `STEAM_MCP_READ_ONLY` (set to `1` in `.mcp.json.example`).
- **Toolsets:** store, community, publisher, sdk, mutations.
- **Used by:** release (status checks), store readiness, promote (what is live).
- **Limits:**
  - Mutations are off by default, and writes are dry-run unless called with `confirm: true`.
  - No SteamPipe uploads yet, tracked in TMHSDigital/steam-mcp#60. Uploads go through `tools/steam/release.ps1`, run by the owner.
  - Never paste a key into a command that gets committed or logged.

## Steam-Cursor-Plugin (1.3.6)

- **What:** the owner's Steam skills and rules plugin.
- **Install:** Cursor only. Claude Code support is tracked in TMHSDigital/Steam-Cursor-Plugin#28.
- **Env names:** none of its own.
- **In this repo:** its skills were adapted into `.claude/skills/steam-build-automation`, `steam-steamcmd-helper`, `steam-store-readiness` and `steam-testing-sandbox`, with Combat Simulator specifics. Treat those copies as the working versions; re-copy from the plugin when #28 lands.
- **Limits:** the copies drift from upstream until the plugin supports Claude Code.

## screencast-mcp (0.54.0)

- **What:** Windows screen recording, contact sheets, clips, OCR secret detection (`find_text`), redaction and bug reports.
- **Needs:** Windows, Node 22 or newer, ffmpeg 8 or newer.
- **Install:** `claude mcp add screencast -- cmd /c npx -y @tmhs/screencast-mcp`
- **Env names:** `SCREENCAST_HOME`, `SCREENCAST_ALLOWED_DIRS`, `SCREENCAST_WHISPER_MODEL`, `FFMPEG_PATH`.
- **Used by:** session start and end (optional recording), milestone.
- **Limits:**
  - Never records without being asked, and neither does this project: recording is opt-in every session.
  - Raw recordings stay in `_scratch/recordings/` (gitignored). Only WebP stills and a link to the clip are committed.
  - Run `find_text` with `redact: true` and `redact_region` before any clip or image leaves the machine.

## Blender-Developer-Tools (0.159.0)

- **What:** the owner's Blender skills, snippets and templates, plus an optional live-session add-on.
- **Install:**
  - `/plugin marketplace add TMHSDigital/Blender-Developer-Tools@plugin-dist`
  - `/plugin install blender-developer-tools@blender-developer-tools`
  - Optional: `blender-live-session`.
- **Env names:** none. The Blender executable path is passed on the command line in the export skill.
- **Used by:** make a Blender asset (see the `blender-to-unreal-export` skill).
- **Limits:**
  - Targets Blender 4.5 and 5.2 LTS. Installed here: 4.4 and 5.1. Whether to move to 5.2 is OD-009.
  - Its `engine-export-presets` skill uses different axis and scale settings from the project preset; the project preset in `.claude/skills/blender-to-unreal-export/` wins.
  - The `blender` entry in `.mcp.json.example` is the Blender Lab live-session MCP; pin the commit you have reviewed.

## Unreal MCP

- **What:** Unreal Engine's experimental MCP server, running inside the editor.
- **Install:** enable the plugins ModelContextProtocol, ToolsetRegistry and AllToolsets in the project; it listens on loopback only, at `http://127.0.0.1:8000/mcp`. The `unreal` entry in `.mcp.json.example` points at it.
- **Starting it:** the project ships `bAutoStartServer=False` in `Game/Config/DefaultEditorPerProjectUserSettings.ini`, so opening the editor does not start the server for everyone. `pwsh tools/ue/editor.ps1` launches the editor with `-ModelContextProtocolStartServer` and waits for the endpoint. To have it start every time you open the editor yourself, turn on Editor Preferences, Plugins, Model Context Protocol, Auto Start Server: it is saved per user in `Game/Saved/Config/WindowsEditor/EditorPerProjectUserSettings.ini` (gitignored).
- **Env names:** none.
- **Used by:** capture, level setup and automation tests when the editor is open.
- **Limits:**
  - Experimental and editor-only. It is not part of shipped builds (OD-010 asks whether that stays true).
  - Every routine also has a plain script path (`tools/ue/*.ps1`) that works with the editor closed.
  - Check what its generated instructions write into `Game/Config/DefaultGame.ini` before committing it.

## Free-Game-Dev-Assets

- **What:** the owner's catalog of free, commercially usable game assets: links only, metadata is CC0. There is also an MCP server for it.
- **Data:** `https://tmhsdigital.github.io/Free-Game-Dev-Assets/data.json` (or `site/dist/data.json` in a local checkout).
- **Install:** none for the JSON; see its repo for the MCP server.
- **Env names:** none.
- **Used by:** import a third-party asset (the `asset-import` skill reads the entry, then `og assets add --from-catalog`).
- **Limits:**
  - A catalog entry is a reading of a license on its `verified` date. Confirm at the source before importing.
  - Aggregator entries (`per-item`, `varies`) need the license of the individual file.
  - This project refuses any NC, ND or SA license and GPL, LGPL and AGPL, even if the catalog lists the asset as free.

## Game-Systems

- **What:** the owner's private library of reusable game systems, catalogues of every game repo, and specs. It lives at `E:\Game-Systems` and is not public.
- **Install:** none; it is a sibling folder.
- **Commands:** `/catalogue-sync <repo>` or `/catalogue-sync new E:/Open-Game-1`, then `python scripts/catalogue.py build` and `python scripts/catalogue.py check`.
- **Used by:** before building any system (check `catalogues/views/INDEX.md` and `docs/specs/`), and contribute a system.
- **Limits:**
  - Private: nothing from it is quoted or linked as a path in committed files here, other than slugs and entry names in `docs/systems/README.md`.
  - Reusable code here lives in self-contained plugins under `Game/Plugins/CS<Name>/` so it can be catalogued.
  - Text contributed there has no em dashes and no emojis.

## gh (GitHub CLI)

- **What:** the GitHub command line, used for Issues, Releases and Actions runs.
- **Install:** `winget install GitHub.cli`, then `gh auth login`.
- **Env names:** `GH_TOKEN` (optional; `gh auth login` is enough locally).
- **Used by:** open and close a decision (`og decisions` creates and closes Issues), session end (monthly `media-YYYY-MM` prerelease for clips), release and promote (prereleases, deployments), checking the Pages workflow.
- **Limits:** needs network and a logged-in account; `og decisions check --offline` skips the Issue comparison for CI.

## Local scripts and the og CLI

Not external, but the other half of every routine:

- `uv run --project tools python -m og <secrets|assets|decisions|session|milestone|site>`: deterministic parts (scans, ledgers, devlog, site build).
- `tools/blender/run.ps1`: runs a Blender Python script headless (`pwsh -NoProfile -File tools/blender/run.ps1 -Script <script.py> [-Blend <file.blend>] [-- <script args>]`). Factory settings, so no user add-ons; an uncaught Python error gives a non-zero exit. Env name: `BLENDER_PATH` (default is the Blender 5.1 install). Used by the base character build and its tests.
- `tools/ue/*.ps1`: build, test, package and capture for the Unreal project. Env name: `UE_ROOT` (see `.env.example`).
- `tools/steam/release.ps1`: the gated Steam release. Env names: `STEAM_APPID`, `STEAM_DEPOT_WIN`, `STEAM_BUILD_ACCOUNT`, `STEAMCMD_PATH`. Claude Code runs uploads to the `development` branch (decision 0014); recording a promotion to `default` is refused inside Claude Code and stays with the owner.
