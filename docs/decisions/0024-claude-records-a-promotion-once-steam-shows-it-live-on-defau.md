# 0024: Claude records a promotion once Steam shows it live on default

Status: Accepted
Date: 2026-11-27
Issue: [#75](https://github.com/TMHSDigital/Open-Game-1/issues/75)
Supersedes: 0014

## Context

Decision 0014 had Claude run the development-branch uploads (`dev`, `upload-only`, `preview`), and kept two things with the owner: setting the `default` branch live, and recording a promotion with `release.ps1 promoted`. The script refused `promoted` whenever `CLAUDECODE` was set.

On 2026-11-27 the owner set build 0.1.11 live on default in Steamworks and asked Claude to record it. The script refused, and the owner said: "I need you to be able to run that." Until a promotion is recorded, the build ledger, the GitHub "Latest" release and the `steam-default` deployment lag what is on Steam, and the public release page shows an old build.

Recording is bookkeeping after the fact: it changes nothing on Steam. The risk is recording a promotion that did not happen, which would make the ledger and the public release page wrong. The script can rule that out: `steamcmd +app_info_print` shows which BuildID is live on the public (default) branch, and `release.ps1 status` already reads it.

## Decision

Claude runs release.ps1 promoted after the owner has set a build live on the default branch, but only when steamcmd shows that build's BuildID live on the default (public) branch; otherwise the script refuses and records nothing. Setting a build live on default stays the owner's, in Steamworks, and no script does it. Claude keeps running the development-branch uploads as in decision 0014, which this decision replaces.

## Consequences

- The record catches up as soon as the owner promotes. The owner sets the build live and says so, and Claude records it without the owner switching to another terminal.
- Inside Claude Code, `promoted` first asks steamcmd for the app's branches. It refuses, recording nothing, with exit 40 in three cases: the public branch shows another BuildID; steamcmd cannot say (not logged in, or no branches block); or the version has no BuildID in the ledger. The owner, in their own terminal, can still run `promoted` without the check.
- The draft checks (`[narrative]` filled in, the secrets scan of `_release/<ver>/`) still run first.
- `CLAUDE.md` rule 6, the release and steam-build-automation skills and the script header now say this. The line that no script sets the default branch live is unchanged.
- steamcmd must be logged in on this machine for the check. It already must be for development uploads.

## Alternatives

- Keep recording with the owner (decision 0014). Rejected: the owner asked Claude to do it, and it left the public record behind Steam.
- Let Claude record a promotion whenever asked, without checking Steam. Rejected: one wrong instruction or misread message would publish a false "latest" release, and the check costs one steamcmd call.

## Links

- Open decision: OD-023 (Issue #75)
- Supersedes: decision 0014 (Claude runs Steam development uploads)
- Related: decision 0010 (Steam uploads use the owner's account)
