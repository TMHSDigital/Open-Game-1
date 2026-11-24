# Steam build workflow

One script does every Steam release step: `tools/steam/release.ps1`. Git has one branch, `main`. "development" and "default" are **Steam** branches only.

- `development`: password-protected beta branch. `release dev` sets every new build live on it.
- `default`: the public branch. Only the owner changes it, by hand in Steamworks. No script ever sets it live.

Steam setup still to do is in [SETUP-CHECKLIST.md](SETUP-CHECKLIST.md). The upload history is in [BUILDS.md](BUILDS.md).

## Who runs what

| Mode | Command | Who |
|---|---|---|
| `dry-run` | `pwsh tools/steam/release.ps1 dry-run` | anyone, Claude included |
| `status` | `pwsh tools/steam/release.ps1 status` | anyone |
| `promote <ver>` | `pwsh tools/steam/release.ps1 promote 0.1.4` | anyone, Claude included |
| `preview` | `pwsh tools/steam/release.ps1 preview` | Claude or owner |
| `upload-only` | `pwsh tools/steam/release.ps1 upload-only` | Claude or owner |
| `dev` | `pwsh tools/steam/release.ps1 dev` | Claude or owner |
| `promoted <ver>` | `pwsh tools/steam/release.ps1 promoted 0.1.4` | owner, own terminal |

Claude Code runs the development-branch steps (`dev`, `upload-only`, `preview`) since decision 0014. The script still refuses `promoted` when `CLAUDECODE` is set (exit 40), and no mode can set `default` live, so the public branch stays with the owner. steamcmd uses the owner's cached login; if Steam asks for a password or a Steam Guard code the upload fails and the owner logs in to steamcmd again.

## Modes

| Mode | What it does |
|---|---|
| `dev` | gates, version bump (local commit), package, stage, VDF, steamcmd, set live on `development`, ledger row (commit; the bump and the ledger are pushed together), `v<version>` GitHub prerelease, `steam-development` deployment |
| `upload-only` | the same, with SetLive blank and no deployment. Used for the first build, before the `development` branch exists |
| `dry-run` | gates, package, stage and VDF; prints the steamcmd line. Does not bump the version. Git gates 1 to 3 only warn, so you can dry-run before committing |
| `preview` | like `dry-run`, then steamcmd with `"Preview" "1"`: Steam checks the build and writes logs and a manifest, nothing is uploaded |
| `status` | the ledger versus Steam's real branch state (steamcmd `app_info_print` with `+@NoPromptForPassword 1`), plus the open setup checklist items. If steamcmd fails or prints no branches block, it reports "Steam state: unknown" instead of a mismatch |
| `promote <ver>` | drafts the promotion into the gitignored `_release/<ver>/`: `post.bbcode`, `patch-notes.md`, `github-release.md`, `checklist.md`. Changes nothing on Steam or in tracked files |
| `promoted <ver>` | after the owner set the build live on `default` and published the post: secret-scans every file in `_release/<ver>/` (a finding exits 1 with nothing recorded), then sets `default_live_utc` in the ledger, stamps the previous default build as replaced, turns the GitHub prerelease into a full release marked Latest with the published text, records a `steam-default` deployment |

Switches for every mode: `-SkipPackage` (reuse the last package in `E:\CombatSimulatorBuilds\Windows`), `-Notes "text"` (added to the build description, the ledger and the release notes; secret-scanned in gate 7, before any upload).

`-Branch` is not a choice: the only accepted value is `development` (surrounding spaces are trimmed). `default` and `public` in any case, any other branch name, and anything that is not a plain lowercase name are refused with exit 40, because no script sets `default` live and the ledger only has `development` and `default` columns.

## Gates

The gates run in this order on the tree as it is (before the version bump), and the first failure stops the run with exit code `10 + n`:

| n | Gate | Exit |
|---|---|---|
| 1 | on `main` | 11 |
| 2 | clean tree: no tracked change and no untracked file that is not ignored. Ignored files (`_release/`, `_scratch/`, `.env`) do not count | 12 |
| 3 | HEAD pushed (contained in `origin/main` after a fetch) | 13 |
| 4 | Unreal Engine is 5.8.x (`Engine/Build/Build.version` under `UE_ROOT`) | 14 |
| 5 | the next version is not already in `BUILDS.md` | 15 |
| 6 | tests pass: `tools/ue/test.ps1` (Unreal automation) and the repo's pytest suite | 16 |
| 7 | secret scan is clean: `og check --only secrets --tracked` (the index content of every tracked file, LFS content verified against its pointer) plus the `-Notes` text | 17 |
| 8 | asset ledger and decisions are consistent: `og check --only assets --only decisions` | 18 |
| 9 | `STEAM_APPID`, `STEAM_DEPOT_WIN`, `STEAM_BUILD_ACCOUNT` are set and plausible (numeric, not 480, depot not equal to app). Only names are printed, never values | 19 |
| 10 | the staging folder `_scratch/steam/content` was deleted and recreated empty | 20 |

## Exit codes

| Code | Meaning |
|---|---|
| 0 | ok |
| 1 | any other error (bad arguments, packaging failed, a secret in the `_release/<ver>/` draft on `promoted`, a GitHub step failed after the upload). If the ledger commit or push fails after a real upload, the message says "Upload done as BuildID N; do not re-run": commit and push `docs/steam/BUILDS.md` by hand |
| 11 to 20 | gate `code - 10` failed |
| 30 | steamcmd error: non-zero exit, or any `ERROR!` line even when the exit code is 0 |
| 31 | steamcmd printed no BuildID |
| 40 | refused: a `-Branch` other than `development` (including `default`, `Default`, ` default`, `public`), or `promoted` with `CLAUDECODE` set |

On 30 and 31 nothing is recorded: no ledger row, no release, no deployment.

## Versions

`ProjectVersion` in `Game/Config/DefaultGame.ini` is `0.<milestone>.<build>`. `release dev` and `upload-only` bump the build number after the gates pass, commit `chore: release <version>` locally, then package, so the packaged game reports the version it was uploaded as. To start a new milestone, set `ProjectVersion` to `0.<milestone>.0` by hand and commit; the next release is `0.<milestone>.1`.

The bump commit stays local until steamcmd reports a BuildID. If packaging, staging or steamcmd fails first, the script undoes the bump and nothing is pushed, so the next run uses the same version. If the build uploaded but could not be set live, the bump is kept and pushed, because that version now exists on Steam. The ledger only holds versions that reached Steam. Gate 3 requires local `main` to equal `origin/main`: pull first if it is behind.

## Staging and the VDFs

`tools/ue/package.ps1 -Config Shipping` archives into `E:\CombatSimulatorBuilds\Windows`, which UAT does not clean between configurations: it can still hold a Development exe, `.pdb` symbols, logs and `Saved/` from test runs. The release therefore copies the Shipping package into a freshly recreated `_scratch/steam/content` and skips:

- `*.pdb`, `*.sym`, `*.psym`, `*.debug`, `*.dmp`, `*.log`
- `steam_appid.txt`, `Manifest_*.txt`
- anything under a `Saved` or `Intermediate` folder
- non-Shipping project executables under `Binaries` (for example a stale `CombatSimulator.exe` from a Development package)

The per-run VDFs are rendered into `_scratch/steam/` from `steam/templates/app_build.vdf` and `steam/templates/depot_build.vdf`, with IDs from the environment and single backslashes. The depot VDF excludes the same patterns again (`*.pdb`, `*.log`, `steam_appid.txt`, `Saved\*`, `Intermediate\*` and more). The templates never hold real IDs.

## First build

Follow [SETUP-CHECKLIST.md](SETUP-CHECKLIST.md). In short: one steamcmd login with your own Steam account (decision 0010), `.env`, then `release.ps1 upload-only` (no branch exists yet to set live), then create the `development` branch in Steamworks. From then on, use `release.ps1 dev`.

If `dev` fails with exit 30 and mentions a branch, the build may have uploaded but could not be set live: check that the `development` branch exists (checklist step 7).

## Promoting to default

1. `pwsh tools/steam/release.ps1 promote <version>` (Claude can run this through the `release` skill, which also fills the `[narrative]` marker from the devlogs since the last default build).
2. Follow `_release/<version>/checklist.md`: set the build live on `default` in Steamworks, publish `post.bbcode` as a Steam event.
3. In your own terminal: `pwsh tools/steam/release.ps1 promoted <version>`. It refuses while a `[narrative]` marker is still unfilled, and when `og secrets` finds anything in `_release/<version>/` (the text it publishes).

## Reading the Environments page

Each Steam branch change is also recorded as a GitHub Deployment, so the repo's **Environments** page (repo home, right sidebar, or `/deployments`) shows one timeline per Steam branch:

- `steam-development`: one entry per `release dev`, pointing at the commit that was uploaded. The newest successful entry is what testers on the `development` branch are running.
- `steam-default`: one entry per `release promoted`. The newest is what the public `default` branch runs.

Each entry's commit links to the code; the matching `v<version>` release (prerelease for development-only builds, full release and Latest once promoted) holds the notes and the Steam BuildID. Releases never contain game files: builds ship only through Steam.

If the Environments page, the releases, `BUILDS.md` and Steam disagree, trust Steam, then run `release.ps1 status` to see which ledger row is off.

## Testing the script

`pwsh tools/steam/test-release.ps1` runs every mode against a throwaway git repo, a fake package with stale files, `tools/steam/fake-steamcmd.ps1` and a recording `gh` shim. It never calls the real GitHub or the real steamcmd.
