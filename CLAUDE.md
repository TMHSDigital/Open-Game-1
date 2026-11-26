# Combat Simulator: rules for Claude

1. This is a PUBLIC repo. Anything committed is published.
2. No IDs or secrets in any file: no Steam App or Depot IDs, keys, cookies, tokens, personal emails. Steam values come from env vars only (see `.env.example`).
3. Before building a system, check `E:\Game-Systems\catalogues\views\INDEX.md` and `E:\Game-Systems\docs\specs\`.
4. Every session starts and ends with the `session` skill.
5. Decisions go through the `decide` skill.
6. Steam: Claude runs uploads to the `development` branch through `tools/steam/release.ps1` (`dev`, `upload-only`, `preview`), never by calling steamcmd directly and never skipping a gate. Never set the `default` branch live and never record a promotion: those stay with the owner (decision 0014).
7. Assets enter only through the `asset-import` skill.
8. The Unreal project lives in `Game/`.
9. No emojis, in docs, devlogs, commits or code.

Tooling: `uv run --project tools python -m og <command>`. Enable the commit hook once with `git config core.hooksPath tools/hooks`.

To see a change in the running game, play it with `pwsh tools/ue/playtest.ps1` (skill `playtest`): it records scripted scenarios and writes clips, contact sheets and a summary to `_scratch/playtest/`.
