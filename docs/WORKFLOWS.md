# Workflows

Each routine below is a numbered list. Every step names the skill, script and MCP tool it uses. Skills live in `.claude/skills/`; `og` means `uv run --project tools python -m og`. Tool setup is in [TOOLS.md](TOOLS.md). Every routine works without the Unreal editor and without MCP servers unless a step says otherwise.

## Start a session

Skill: `session`.

1. Ask for the goal in one line, and whether to record. Recording is opt-in every time.
2. If recording: screencast MCP `list_targets`, then `start_recording` (target `window:exe=UnrealEditor.exe` or `full`).
3. `og session start "<goal>"` (script `tools/og/session.py`). Add `--commit` to leave a `Session-Start:` marker commit that survives a lost state file. It creates `docs/devlog/<id>-<slug>.md` from `docs/devlog/TEMPLATE.md`, saves state to `_scratch/session.json`, and lists open items from `docs/steam/SETUP-CHECKLIST.md`.
4. If recording: write the screencast session id into `_scratch/session.json`.
5. Work. Decisions go through the `decide` skill, assets through `asset-import`, builds through `ue-build-test`.
6. To end: if recording, screencast MCP `stop_recording`, `find_text` with `redact: true`, `redact_region`, `pipeline` (recipe `pr_demo`), `contact_sheet`.
7. `og session end --capture [--clip ...] [--contact-sheet ...] --co-author "..."`. It runs `tools/ue/capture.ps1` for the fixed cameras, converts stills to WebP under `docs/devlog/media/<id>/`, updates `docs/captures/INDEX.md`, runs `og secrets` on every new file, uploads any clip to the `media-YYYY-MM` prerelease with `gh`, and commits.
8. Write the narrative paragraph at the `<!-- narrative -->` marker and commit it as `docs: session <id> narrative`.

## Import a third-party asset

Skill: `asset-import`. Policy: `docs/assets/ASSET-POLICY.md`.

1. Find the entry in the Free-Game-Dev-Assets data (the catalog JSON, or its MCP server). Note the `id`.
2. Confirm the license at the source. Refuse NC, ND, SA, GPL, LGPL, AGPL and blank licenses.
3. Download into `_scratch/` (gitignored).
4. Copy into a `ThirdParty/` path, with the license text if the license needs it carried.
5. `og assets add --from-catalog <id> --paths "<glob>"` writes the row in `assets/THIRD_PARTY.csv`.
6. `og assets credits` regenerates `CREDITS.md`.
7. `og assets check` must exit 0.
8. Commit assets, ledger and credits together; the `tools/hooks/pre-commit` hook runs the checks again.

## Make a Blender asset

Skills: `blender-to-unreal-export`, and `asset-import` for anything not made from scratch. Tools: Blender (4.4 or 5.1 installed), optional Blender MCP or Blender-Developer-Tools skills.

1. Model or clean up in Blender under `Art/` (Metric, unit scale 1.0). Blender-Developer-Tools skills help with cleanup, LODs and collision.
2. Apply transforms and use the project names: `SM_`, `SK_`, `A_`, `UCX_`, `SOCKET_`.
3. Check without writing: `blender -b <file>.blend --python-exit-code 1 --python .claude/skills/blender-to-unreal-export/export_unreal_fbx.py -- --out Art/Export/<name>.fbx --kind static --all --check-only`.
4. Export with the same command minus `--check-only`.
5. Import into the Content Browser by hand following the skill's steps (a Blender or Unreal MCP import is not wired up yet), then File, Save All.
6. Verify the size against the mannequin in a level; add a capture camera with the `capture` skill if the asset should show in the devlog.
7. Commit `.blend`, `.fbx` and `.uasset` files (tracked through Git LFS) and run `og secrets`.

## Open a decision

Skill: `decide`. Tool: `gh`.

1. State the question so it stands alone for a public reader.
2. `og decisions new-open "<question>" --why "..." --blocks "..." --options "a; b; c"`. It appends the next `OD-NNN` row to `docs/OPEN-DECISIONS.md` and creates the `decision`-labelled Issue with `gh`.
3. Commit the table change, mentioning the OD number in the message.
4. Discuss options with the owner; record useful points as Issue comments. Do not decide for the owner.

## Close a decision

Skill: `decide`. Tool: `gh`.

1. Only after the owner states the decision.
2. `og decisions close OD-NNN --title "..." --decision "..."`. It writes `docs/decisions/NNNN-<slug>.md`, moves the row to Resolved and closes the Issue.
3. Open the new file and write Context, Consequences and Alternatives properly (the generated Consequences line is a placeholder).
4. `og decisions check` must exit 0 (`--offline` skips the Issue comparison).
5. Commit, naming the OD and ADR numbers so session devlogs list them.

## Release to dev

Skill: `release` (reference: `docs/steam/BUILD-WORKFLOW.md`). Script: `tools/steam/release.ps1`. Tools: steamcmd (owner only), `gh`, optional steam-mcp for checks.

1. `pwsh tools/steam/release.ps1 status` to see open setup items and the ledger.
2. `pwsh tools/steam/release.ps1 dry-run` runs gates 4 to 10 for real: UE 5.8.x, version not in the ledger, tests (`tools/ue/test.ps1`), `og secrets --tracked` (every tracked file, plus the `-Notes` text), `og assets check`, Steam env var names, fresh staging. It also packages a Shipping build (`tools/ue/package.ps1`) and renders the VDFs from `steam/templates/`. Gates 1 to 3 (on main, clean tree, HEAD pushed) only warn in a dry run.
3. Fix whichever gate failed (exit code `10 + n` is gate n); the tree must be committed and pushed before a real run.
4. Claude prints the command; the owner runs `pwsh tools/steam/release.ps1 dev` in their own terminal (`upload-only` for the very first build). Real uploads refuse to run inside Claude Code.
5. On success the script commits a row to `docs/steam/BUILDS.md`, creates a `v<ver>` prerelease and a `steam-development` deployment with `gh`, and pushes. If steamcmd printed an error or no BuildID, nothing is recorded.

## Promote

Skill: `release`. Script: `tools/steam/release.ps1 promote`. Tools: `gh`, owner in Steamworks.

1. `pwsh tools/steam/release.ps1 promote <ver>` writes `_release/<ver>/` (gitignored): `post.bbcode`, `patch-notes.md`, `github-release.md`, `checklist.md`.
2. Claude fills the `[narrative]` marker in `post.bbcode` and `github-release.md` from the devlogs listed in `patch-notes.md`: player-facing, no IDs, no internal paths.
3. The owner reads the drafts and `checklist.md`, sets the build live on `default` in Steamworks and publishes the post. No script sets `default` live.
4. The owner runs `pwsh tools/steam/release.ps1 promoted <ver>`: it records `default_live_utc`, makes the GitHub Release the Latest full release, and records a `steam-default` deployment.

## Cut a milestone

Skill: `milestone`. Tools: `git`, optionally screencast MCP.

1. Commit the tree and make sure the latest shots are in `docs/captures/INDEX.md` (run the `session` routine or the `capture` skill first).
2. `og milestone <N> <slug>` creates the annotated tag `m<N>-<slug>`, writes `docs/milestones/m<N>-<slug>.md` (each camera now versus the previous milestone), scans it with `og secrets`, and commits.
3. Add a two or three sentence summary at the top of the page and commit it separately. Never move the tag.
4. Push the tag only when the owner asks.

## Contribute a system to Game-Systems

Skill: Game-Systems `catalogue-sync` (run from `E:\Game-Systems`). Tools: `git`, `python scripts/catalogue.py`.

1. Build the system as a self-contained plugin under `Game/Plugins/CS<Name>/`: no game-actor assumptions, its own tests, a short README.
2. Commit it here. Uncommitted work is never catalogued.
3. In Game-Systems run `/catalogue-sync open-game-1` (the first time was `new E:/Open-Game-1`). It adds or updates entries in `catalogues/open-game-1.md` and never copies code.
4. `python scripts/catalogue.py build`, then `python scripts/catalogue.py check` must report 0 errors.
5. Commit in Game-Systems (no emojis, no em dashes, no Steam IDs). Push only when the owner says so.
6. Update the row in `docs/systems/README.md` here (Status `contributes` once catalogued).
