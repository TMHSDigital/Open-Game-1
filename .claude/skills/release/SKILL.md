---
name: release
description: Steam releases for Combat Simulator through tools/steam/release.ps1. Claude runs dry-run, status and promote (and fills the promotion post from the devlogs); Claude also runs development-branch uploads (decision 0014); recording a promotion is run by the owner in their own terminal, and this skill prints the exact command. Use when the user says release, ship a build, upload to Steam, promote to default, what is on Steam, or Steam setup status.
allowed-tools: Bash(pwsh tools/steam/release.ps1 dry-run:*) Bash(pwsh tools/steam/release.ps1 status:*) Bash(pwsh tools/steam/release.ps1 promote :*) Bash(pwsh tools/steam/test-release.ps1:*) PowerShell(pwsh tools/steam/release.ps1 dry-run:*) PowerShell(pwsh tools/steam/release.ps1 status:*) PowerShell(pwsh tools/steam/release.ps1 promote :*) Read Edit Grep Bash(git log:*)
---

# Release to Steam

Full reference: `docs/steam/BUILD-WORKFLOW.md`. History: `docs/steam/BUILDS.md`. Owner setup: `docs/steam/SETUP-CHECKLIST.md`.

## Rules

- Claude may run only: `dry-run`, `status`, `promote <ver>`, and the test script `tools/steam/test-release.ps1`.
- Claude runs `dev`, `upload-only` and `preview` itself, always through `tools/steam/release.ps1` and never by calling steamcmd directly or skipping a gate (decision 0014). Claude never runs `promoted` (the script refuses it inside Claude Code, exit 40) and never sets `default` live. If steamcmd asks for a password or a Steam Guard code, stop and ask the owner to log in to steamcmd; never handle the credentials.
- Never pass the script's test-only switches (`-SkipGitGates`, `-CheckCommand`, `-LedgerPath`, `-RepoDir`, `-NoGitHub`). They exist for `tools/steam/test-release.ps1`; the script refuses them anywhere else (exit 40).
- Never set the `default` branch live, and never suggest a command that does.
- Never read `.env`, never print or write App IDs, Depot IDs or the build account name. The script prints only variable names.
- Never attach game files to a GitHub Release.

## Ship a development build

1. Run `pwsh tools/steam/release.ps1 status` and report open setup items. If checklist step 7 is still open (no build yet), the first upload is `upload-only`, not `dev`.
2. Run `pwsh tools/steam/release.ps1 dry-run`. It runs gates 4 to 10 for real, warns on gates 1 to 3 (branch, clean tree, pushed), packages a Shipping build and renders the VDFs. It does not bump the version.
3. On a gate failure, read the exit code (`10 + n` is gate n) and fix that gate: commit or push, rebuild and retest, clean the secret or asset finding (gate 7 scans every tracked file with `og secrets --tracked` and the `-Notes` text), or tell the owner which env var names are missing.
4. When the dry run passes and the tree is committed and pushed, run it (the owner may also run it in their own terminal):

   ```powershell
   pwsh tools/steam/release.ps1 dev
   ```

   Do not add `-SkipPackage` here: `dev` bumps the version before packaging, and a reused package would report the previous version in game. For the very first build print `pwsh tools/steam/release.ps1 upload-only` and remind the owner of checklist step 7.

5. Read the result: exit 0 means the ledger row is committed and pushed, the prerelease and deployment exist. Exit 30 or 31 means nothing was recorded; read `_scratch/steam/steamcmd.log` (share the relevant lines with the owner). Exit 30 mentioning a branch usually means the `development` branch does not exist yet. Exit 1 with "Upload done as BuildID N; do not re-run" means the upload worked but the ledger commit or push failed: commit and push `docs/steam/BUILDS.md` by hand, never re-run the upload.

## Promote a build to default

1. Pick the version (ask, or take the newest `development` row in `docs/steam/BUILDS.md`).
2. Run `pwsh tools/steam/release.ps1 promote <ver>`. It writes `_release/<ver>/` (gitignored): `post.bbcode`, `patch-notes.md`, `github-release.md`, `checklist.md`.
3. Fill the `[narrative]` marker in both `post.bbcode` and `github-release.md`:
   - Read the devlogs listed under "Sources" in `patch-notes.md` (these are the devlogs since the last default build) and the commit list.
   - Write 1 to 3 short paragraphs for players: what changed and why it matters. Plain words, no emojis, no em dashes, no IDs, no internal paths, nothing from Steamworks pages.
   - BBCode in `post.bbcode` (`[p]`, `[b]`, `[list][*]`), Markdown in `github-release.md`.
   - Also tidy the generated change lists (drop pure tooling commits if they mean nothing to players) and fill Known issues from the devlogs.
4. Show the owner the drafts and `checklist.md`. The owner sets the build live on `default` in Steamworks and publishes the post. Then print:

   ```powershell
   pwsh tools/steam/release.ps1 promoted <ver>
   ```

   It refuses while a `[narrative]` marker is left or when `og secrets` finds anything in `_release/<ver>/` (keep IDs, emails and Steamworks text out of the drafts), records `default_live_utc`, turns the prerelease into the Latest full release with `github-release.md` as its body, and records a `steam-default` deployment.

## Check what is on Steam

`pwsh tools/steam/release.ps1 status` shows the last ledger rows, the open setup items, and (outside Claude Code only) a ledger-versus-Steam comparison with `MISMATCH` lines. Inside Claude Code the Steam query is skipped; ask the owner to run it in their terminal for the drift check.
