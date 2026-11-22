# 0008: Reuse via Game-Systems

Status: Accepted
Date: 2026-11-22
Issue: none
Supersedes: none

## Context

The owner keeps a catalogue of reusable game systems and specs in the Game-Systems repo.

## Decision

Before building any system, check the Game-Systems catalogue index and its specs. `docs/systems/README.md` tracks each need with its Game-Systems reference, status (adopted, adapted, planned, contributes) and local path. Reusable code lives in self-contained Unreal plugins (`Game/Plugins/CS<Name>/`) with no game-actor assumptions, so it can be catalogued back. Text contributed to Game-Systems has no emojis and no em dashes.

## Consequences

- This game feeds its own gaps back to the catalogue (mod support, a physics sandbox toolset).
- Reusable code is kept in self-contained plugins with no game-actor assumptions.

## Alternatives

No alternatives were weighed; the owner set this directly.

## Links

- Related: 0003
