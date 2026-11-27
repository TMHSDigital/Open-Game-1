# 0016: Next block: a simulation run skeleton, then the first melee attack

Status: Accepted
Date: 2026-11-24
Issue: [#62](https://github.com/TMHSDigital/Open-Game-1/issues/62)
Supersedes: none

Note: the physics-steered punch this decision introduced is superseded by decision 0025 (strikes are authored motion); the run skeleton still holds.

## Context

Decision 0015 ended with build 0.1.5: a target dummy, a thrown ball, a launcher and hits that come from contact. The character can be hit and can hit a dummy with a ball, but nothing says what a session of play is. The maps start as they were placed, a key puts them back, and nothing is counted.

The owner then worked through a design interview to fix the direction of the game. Its outcome, as far as this decision needs it: the game is played as runs that the player sets up, and close combat comes before ranged combat. A choice was needed on which of those to build first, and whether the animation block that decision 0015 named as next still comes first.

## Decision

After the dummy and contact hits (decision 0015) the next block is, in order: a simulation run skeleton, then the first melee attack. A run is a described setup that the player configures on a screen, starts, ends and can rerun, with statistics collected while it runs and shown when it ends. The first version is built only from what exists: the two maps, target dummies, the thrown ball and the launcher. The first melee attack follows as its own build and delivers its hits through the same entry point contact hits use. It ships as two builds on the development branch.

## Consequences

- The game gets its first structure: a run has a description, a start, an end and a result. Later weapons, opponents and maps are added as things a run description can name, so each new piece of content has a place to go.
- A run description is plain data. It can be saved, edited by hand, compared and, later, supplied by a mod (OD-003), so its format needs a version from the first build.
- Statistics are collected from the one hit entry point that decision 0015 created. Anything that hits a character is counted without knowing about runs, and the first melee attack is counted the day it exists.
- The first runs are thin: dummies that stand still, a ball and a launcher. The screen will look more capable than the content behind it until the melee build and later opponents arrive.
- The setup screen is the second screen in the game and is written in C++ like the pause screen, unstyled until art direction is decided (OD-006).
- The debug reset key and the map switch key overlap with starting and ending a run and have to be reconciled with it.
- The animation block is pushed back again. The stand-in walk, jog and sprint cycles and the plain get-up blend (#37, #38) stay, and the first melee attack will be judged on a character that still looks barebones. The attack needs at least one motion of its own, which is the first animation made for this project.
- Two packages and uploads instead of one, so the run skeleton reaches the owner before the attack is done.

## Alternatives

- The first melee attack alone. Rejected as the first build: it would make the build feel like a combat game sooner, but the attack would land in a map with no start, end or count, and the structure would have to be fitted around it afterwards.
- The run skeleton alone, with the next block left open. Rejected: the skeleton has little to show without a way to fight in it, so the attack is committed to now as the second build.
- Own animations for the stand-in set, as decision 0015 named. Rejected for now: it improves how the character looks and adds nothing to what the player does. It stays the block after this one unless a later decision moves it.
- One build with both. Rejected for the reason decision 0015 gave: the first half would wait on the second.

## Links

- Open decision: OD-016
- Issues: #37, #38
- Builds on: decision 0015 (dummy and contact hits), decision 0014 (development uploads)
- Related open decisions: OD-003 (modding technology), OD-006 (art direction)
