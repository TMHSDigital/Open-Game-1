# 0014: Claude runs Steam development uploads

Status: Superseded by 0024
Date: 2026-11-23
Issue: [#15](https://github.com/TMHSDigital/Open-Game-1/issues/15)
Supersedes: none (amends who runs uploads in 0007 and 0010)

## Context

Until now the release script refused every real Steam step inside Claude Code, and the project rules said the owner runs uploads in their own terminal (0007, 0010). In practice each build then waited for the owner to run a command by hand. With build 0.1.2 (base character and movement) ready, the owner said they do not want to upload their own builds and that uploading is part of Claude's job.

## Decision

Claude Code runs the real Steam steps for the `development` branch: `release.ps1 dev`, `upload-only` and `preview`. They still go through every numbered gate, use the owner's cached steamcmd login (0010), and can only set `development` live.

The `default` branch does not change hands. No script sets `default` live; the owner does that in Steamworks, and recording it (`promoted`) is still refused inside Claude Code.

## Consequences

- A build reaches the development branch as soon as it passes the gates, without waiting for the owner.
- The safety of an upload now rests on the gates (clean tree, pushed HEAD, tests, secrets scan, branch allowlist), not on a person typing the command. The gates must stay strict.
- steamcmd runs without a person at the keyboard. If Steam asks for a login or a Steam Guard code, the upload fails and the owner has to log in to steamcmd again; Claude never handles the password or the code.
- Players only ever get what the owner promotes: the public `default` branch is still a manual step.
- CLAUDE.md rule 6, the release script guard and its tests, the build workflow page and the Steam skills change to match.

## Alternatives

- The owner keeps running uploads in their own terminal (the rule until now). Rejected by the owner: they do not want that job.
- Claude also sets `default` live or records promotions. Not asked for; 0007 keeps promotion to `default` manual.

## Links

- Open decision: OD-014 ([#15](https://github.com/TMHSDigital/Open-Game-1/issues/15))
- [0007: Single git branch and Steam release flow](0007-single-git-branch-and-steam-release-flow.md)
- [0010: Steam uploads use the owner's account](0010-steam-uploads-use-owner-account.md)
