# 0008: Reuse via Game-Systems

Status: Accepted
Date: 2026-11-22
Issue: none
Supersedes: none

## Context

The owner keeps a catalogue of reusable game systems and specs in the Game-Systems repo, including several that came out of an earlier Unreal and Steam project. Rebuilding a catalogued system wastes effort and drifts from the shared version.

## Decision

Before building any system, check the Game-Systems catalogue index and its specs. `docs/systems/README.md` tracks each need with its Game-Systems reference, status (adopted, adapted, planned, contributes) and local path. Reusable code lives in self-contained Unreal plugins (`Game/Plugins/CS<Name>/`) with no game-actor assumptions, so it can be catalogued back. Text contributed to Game-Systems has no emojis and no em dashes.

## Consequences

- Less duplicated work, and this game feeds its own gaps back (mod support, a physics sandbox toolset).
- Plugin boundaries must be kept clean, which costs some design effort.
- Depends on the Game-Systems repo staying available to the owner.

## Alternatives

- Build everything from scratch in the game module: simpler at first, but nothing is reusable and the catalogue goes stale.
- Copy catalogued code into the game: loses the link to the source and its updates.

## Links

- Related: 0003
