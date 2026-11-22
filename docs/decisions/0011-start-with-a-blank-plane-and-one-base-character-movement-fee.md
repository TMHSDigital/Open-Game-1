# 0011: Start with a blank plane and one base character; movement feel first

Status: Accepted
Date: 2026-11-22
Issue: [#1](https://github.com/TMHSDigital/Open-Game-1/issues/1)
Supersedes: none

## Context

OD-001 asked what a player does in the first ten minutes. Every later choice (modding surface, art, multiplayer, combat) depends on it. The framework was finished and the first Steam build (the empty capture stage) was live, so the next work needed a concrete target. The owner set the starting point directly rather than picking a full core loop.

## Decision

The game starts on a blank landscape or flat plane with one character. That character is a faceless base model: every later model and skin is built from it. The first development goal is the movement and feel of this character, before combat, spawning or modding.

## Consequences

- The first prototype is small and testable: one map, one character, movement only. Success is judged by how the character feels to control, not by features.
- The base model becomes a shared foundation. Its skeleton, proportions and scale are hard to change later, because every model and skin will be built on it; they deserve care up front.
- The base model must be original (built in Blender) or come in under the asset policy (0004). Epic's sample mannequins cannot be committed to this public repository (0003), so they can only serve as a temporary local reference.
- Combat, spawning, scenarios and modding wait until movement feels right. Their open decisions (OD-003, OD-004, OD-005) stay open.
- The full core loop is not settled by this decision; it will be shaped by what the movement work reveals.

## Alternatives

Options listed in OD-001 before the decision:

- Physics toy box (spawn, manipulate, destroy).
- Objective-driven scenarios.
- Open arena combat against AI.
- Sandbox first with scenarios layered on later.

Options proposed in discussion on 2026-11-22:

- First person in the fight, against training dummies and NPC fighters, with a spawn menu.
- A director view that stages fights between ragdoll fighters.
- A builder mode for contraptions and weapons, tested on dummies.

The owner chose none of these as the starting point and set a narrower first goal instead: one character, and its movement and feel.

## Links

- Open decision: OD-001 ([#1](https://github.com/TMHSDigital/Open-Game-1/issues/1))
- [0003: Engine: Unreal Engine 5.8, DCC: Blender](0003-engine-unreal-5-8-dcc-blender.md)
- [0004: Third-party asset license policy](0004-third-party-asset-license-policy.md)
