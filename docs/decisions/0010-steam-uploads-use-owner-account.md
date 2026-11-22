# 0010: Steam uploads use the owner's own Steam account

Status: Accepted
Date: 2026-11-22
Issue: none
Supersedes: none

## Context

The release flow (0007) uploads builds with steamcmd from the owner's PC. The setup checklist originally asked for a separate Steam account with SteamPipe upload rights only, so that the session steamcmd caches on the PC could do nothing but upload.

## Decision

There is no separate build account. `STEAM_BUILD_ACCOUNT` is the owner's own Steam account name, set only in the local `.env`. The owner logs in to steamcmd once on this PC; steamcmd caches that session in its own folder.

## Consequences

- The cached steamcmd session on this PC carries the owner's full account rights, not upload rights only. The PC and the steamcmd folder are treated as sensitive.
- Real uploads still run only in the owner's own terminal; `release.ps1` refuses them when started by Claude Code (0007).
- No Steam credentials or steamcmd sessions are ever placed in GitHub Secrets or any CI runner, so automated uploads from CI are off the table under this decision.
- The account name stays out of the repo: it lives only in `.env`, and the secrets scanner flags a literal `STEAM_BUILD_ACCOUNT=` value.

## Alternatives

- A dedicated build-only account with SteamPipe upload rights only (the original checklist step). The owner declined it.

## Links

- [0005: Public-repo secrets policy](0005-public-repo-secrets-policy.md)
- [0007: Single git branch; Steam release flow and branch tracking](0007-single-git-branch-and-steam-release-flow.md)
- [Steam setup checklist](../steam/SETUP-CHECKLIST.md)
