# 0007: Single git branch; Steam release flow and branch tracking

Status: Accepted
Date: 2026-11-22
Issue: none
Supersedes: none

## Context

Work is public the moment it is pushed, the owner works alone, and Steam uploads need the owner's login. Two things are easy to confuse: git branches and Steam branches. Anything that can set a Steam branch live must be hard to trigger by accident.

## Decision

Git has one branch, `main`. `development` and `default` are Steam branches only. `release dev` runs numbered gates (on `main`, clean tree, HEAD pushed, UE 5.8.x, version not already released, tests passed, secret scan and asset check clean, required env vars present, fresh staging), packages with BuildCookRun, uploads with steamcmd and sets the build live on `development`. It fails on any `ERROR!` line or missing BuildID even when steamcmd exits 0, and it refuses `default`. No script sets `default` live: `release promote` only drafts the announcement and checklist into the gitignored `_release/` folder, and the owner promotes in Steamworks, then runs `release promoted`. Real uploads refuse to run under Claude Code; the owner runs them in a terminal. What was on which Steam branch and since when is tracked three ways: `docs/steam/BUILDS.md` (one row per upload), GitHub Releases (a `v<version>` tag and prerelease, never the packaged game), and GitHub Deployments in the `steam-development` and `steam-default` environments. Until the first build exists on Steam, the pipeline is tested against a fake steamcmd and supports an upload-only mode.

## Consequences

- The owner is always the one who makes a build public.
- The history of what was live is visible in three independent places, and `release status` can compare it with Steam.
- Builds need the owner's PC and Steam login, so there is no fully automated release.
- The steamcmd step is replaced when steam-mcp#60 ships SteamPipe upload tools.

## Alternatives

- A self-hosted GitHub Actions runner triggered by push: on a public repo, strangers' pull requests could run code on the machine that holds the Steam login.
- A local git push hook that runs the release: makes every push wait on a long Unreal build; deferred as a later convenience.
- A separate `dev` git branch: adds merge work for a solo project and confuses git and Steam branches.

## Links

- Checklist for the owner: `docs/steam/SETUP-CHECKLIST.md`
- Build history: `docs/steam/BUILDS.md`
