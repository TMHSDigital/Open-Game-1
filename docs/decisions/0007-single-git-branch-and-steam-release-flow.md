# 0007: Single git branch; Steam release flow and branch tracking

Status: Accepted
Date: 2026-11-22
Issue: none
Supersedes: none
Amended by: [0014](0014-claude-runs-steam-development-uploads.md) (Claude Code now runs uploads to the `development` branch; the script no longer refuses them)

## Context

Work is public the moment it is pushed, the owner works alone, and Steam uploads need the owner's login. Two things are easy to confuse: git branches and Steam branches. Anything that can set a Steam branch live must be hard to trigger by accident.

## Decision

Git has one branch, `main`. `development` and `default` are Steam branches only. `release dev` runs numbered gates (on `main`, clean tree, HEAD pushed, UE 5.8.x, version not already released, tests passed, secret scan and asset check clean, required env vars present, fresh staging), packages with BuildCookRun, uploads with steamcmd and sets the build live on `development`. It fails on any `ERROR!` line or missing BuildID even when steamcmd exits 0, and it refuses `default`. No script sets `default` live: `release promote` only drafts the announcement and checklist into the gitignored `_release/` folder, and the owner promotes in Steamworks, then runs `release promoted`, which records the `steam-default` deployment, turns the prerelease into a full release marked Latest, copies the published post into the release body and appends `default` to the build row. As a project safety rule, real steamcmd uploads refuse to run when `CLAUDECODE` is set, so the owner runs them in their own terminal. What was on which Steam branch and since when is tracked three ways: `docs/steam/BUILDS.md` (one row per upload), GitHub Releases (a `v<version>` tag and prerelease, never the packaged game), and GitHub Deployments in the `steam-development` and `steam-default` environments. Until the first build exists on Steam, the pipeline is tested against a fake steamcmd and supports an upload-only mode.

## Consequences

- The owner is always the one who makes a build public.
- What was live on each Steam branch, and when, is visible in three places, and `release status` can compare the history with Steam.
- The steamcmd step is replaced when steam-mcp#60 ships SteamPipe upload tools.

## Alternatives

Git branch options shown to the owner: `dev` as the default branch; `main` as default with docs committed there; `main` only (chosen). The owner said: "I don't want multiple gh branches, but I want thorough tracking of what is on which Steamworks branch, and when."

Release automation options (from the design):

- A self-hosted GitHub Actions runner on push: on a public repo, strangers' pull requests could run code on the machine that holds the Steam login.
- A local git push hook that runs the release: deferred as a later convenience, because pushes would wait on a long Unreal build.

## Links

- Checklist for the owner: `docs/steam/SETUP-CHECKLIST.md`
- Build history: `docs/steam/BUILDS.md`
