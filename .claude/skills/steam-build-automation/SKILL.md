---
name: steam-build-automation
description: How Combat Simulator gets packaged and uploaded to Steam through SteamPipe with tools/steam/release.ps1 (numbered gates, Shipping package, fresh staging, VDFs rendered from steam/templates, development branch), branch strategy, and future CI. Claude runs dry runs and uploads to the development branch (decision 0014); the default branch stays with the owner. Use when preparing a Steam build, debugging a SteamPipe upload, or planning CI for builds.
---

# Steam build automation (Combat Simulator)

Adapted from the Steam-Cursor-Plugin `steam-build-automation` skill, for Unreal on Windows and this repo's rule that **Claude never calls steamcmd directly and never handles Steam credentials** (uploads to `development` go through `release.ps1`, which Claude may run since decision 0014). Day-to-day steps are in the `release` skill; the full reference is `docs/steam/BUILD-WORKFLOW.md`.

## One entry point

```powershell
pwsh tools/steam/release.ps1 dry-run       # Claude or owner: gates, package, stage, render VDFs, print the steamcmd line
pwsh tools/steam/release.ps1 preview       # Claude or owner: steamcmd preview (logs and manifest, nothing uploaded)
pwsh tools/steam/release.ps1 upload-only   # Claude or owner: first build, no branch set live
pwsh tools/steam/release.ps1 dev           # Claude or owner: upload and set live on the development branch
pwsh tools/steam/test-release.ps1          # tests the script with a fake build, fake steamcmd and a gh recorder
```

What `dev` does, in order:
1. **Gates 1 to 10** (exit `10 + n` on the first failure): on `main`, clean tree, HEAD pushed, UE 5.8.x, next version not in `docs/steam/BUILDS.md`, tests, secret scan, asset check, Steam env vars, staging recreated.
2. **Bump** `ProjectVersion` (`0.<milestone>.<build>`) in `Game/Config/DefaultGame.ini`, commit, push.
3. **Package** with `tools/ue/package.ps1 -Config Shipping` (skip with `-SkipPackage`).
4. **Stage** the Shipping package into `_scratch/steam/content`, dropping symbols, logs, `Saved`, `Intermediate`, `steam_appid.txt`, `Manifest_*.txt` and any stale non-Shipping exe left in the archive folder.
   **Gate 11** (exit 21): start the staged Shipping exe once with `-CSSmokeTest=<file>`. `UCSStartupCheck` checks the player's character (mesh, physics asset, animation, tuning, mapping context, pause action), writes PASS or FAIL and quits. On a failure the bump is undone and nothing is uploaded.
5. **Render** `steam/templates/*.vdf` into `_scratch/steam/` with IDs from env vars, single backslashes, `SetLive` and `Preview`.
6. **Upload** (Claude or owner, `development` only; decision 0014). Any `ERROR!` line is exit 30, no BuildID is exit 31, and nothing is recorded.
7. **Record**: ledger row (committed and pushed), `v<version>` GitHub prerelease with no game files, `steam-development` deployment.

## IDs and credentials

- Only these keys are read from the repo's `.env`: `STEAM_APPID`, `STEAM_DEPOT_WIN`, `STEAM_BUILD_ACCOUNT`, `STEAMCMD_PATH`. A non-empty `.env` value wins over an inherited environment variable (with a warning naming the variable), so a user-level `STEAM_APPID` from another project can never redirect an upload; environment variables are used only for keys `.env` leaves empty. Templates never hold real IDs, and the script prints names, never values.
- No password anywhere: steamcmd prompts for it and Steam Guard on first login, then caches a session in its own folder (`config\config.vdf`, `ssfn*`: secrets, never in the repo).
- Uploads use the owner's own Steam account (decision 0010); there is no separate build account. The cached steamcmd session carries full account rights, so never copy it anywhere.

## Branches

- Every `dev` upload goes live on the password-protected `development` branch. `upload-only` sets nothing live.
- The script accepts only `-Branch development` (trimmed) and refuses anything else with exit 40: `default` or `public` in any case, other branch names such as `qa`, and malformed names. Promotion to `default` is by hand in Steamworks; `release.ps1 promote` drafts the steps and `promoted` records them.
- A branch name that does not exist in Steamworks makes the set-live step fail after the upload: the script exits 30 and points at setup checklist step 7.

## CI (later)

- Hosted runners cannot build Unreal practically. If CI is wanted, use a **self-hosted Windows runner** on the dev PC running `tools/ue/build.ps1`, `tools/ue/test.ps1` and `tools/steam/release.ps1 dry-run`.
- Use `lfs: false` plus caching in checkout steps to avoid spending LFS bandwidth.
- No automated upload job: under decision 0010 Steam credentials and steamcmd sessions never go into GitHub Secrets or a CI runner.

## Pitfalls

1. Every steamcmd chain ends with `+quit`, or it hangs.
2. The depot VDF has no `ContentRoot`: it inherits the app's. Adding one appends paths and fails.
3. Uploading is not releasing: the public branch only changes by hand.
4. Never echo credentials into logs; the script never prints the account name.
5. Launch options must exist in Steamworks (Installation, General) or the client has nothing to run.
6. The package archive folder is not cleaned between configurations. Never point SteamPipe at it directly; always stage.
