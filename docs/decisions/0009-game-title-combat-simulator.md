# 0009: Game title "Combat Simulator"

Status: Accepted
Date: 2026-11-22
Issue: none
Supersedes: none

## Context

The repo, the Unreal project, the Steam app and the public pages all need a consistent name before the game's design is settled. The Steamworks app is already created and named.

## Decision

The game's title is "Combat Simulator". The Unreal project is `CombatSimulator` and its plugins use the `CS` prefix. The title can be changed later by a decision that supersedes this one.

## Consequences

- The Steam app, Unreal project and docs agree on one name.
- The name does not commit the game to a particular core loop (OD-001).
- A rename later would touch the Unreal module, Steam app name and public pages, so it needs a superseding decision.

## Alternatives

- Use the repo name `Open-Game-1` as the title: it is a placeholder, not a title.
- Wait to name the project until the design is clearer: the Steam app and tooling need a name now.

## Links

- Open decisions: OD-001 (core loop), OD-006 (art direction)
