# 0018: Next block: an overhaul of the play-test findings, and a scripted play-test

Status: Accepted
Date: 2026-11-25
Issue: [#64](https://github.com/TMHSDigital/Open-Game-1/issues/64)
Supersedes: none

## Context

Build 0.1.8 was played on a recording and reviewed frame by frame. Ten punches were thrown at a dummy and none landed. The punch went straight along the view from the middle of the chest, and nothing turned the player toward what it was punching. Stepping back turned the whole body to face the camera, and it stayed turned. The third-person camera cut the character off at the thighs and stayed at standing height during a ragdoll, so the legs, the new stride and the get-up could hardly be seen. A hit gave no feedback. The results of a run opened under the mouse while the player was still clicking.

The review itself was slow. Every step was a key pressed from outside the game, guessing at distances, with the camera the player has.

## Decision

The next block, chosen by the owner after the recorded play-test of build 0.1.8, is an overhaul of what that play-test found: the punch finds and reaches its target, the body faces the aim near an opponent so it can back off and circle while facing it, the camera frames the whole body and follows it to the ground, hits give feedback, and the results of a run no longer open under the player's clicks. With it comes a scripted play-test: one command runs the game through scenarios, records it, and writes clips, contact sheets and numbers for each part. It ships as one build on the development branch.

## Consequences

- The punch is meant to land: a strike looks for the nearest opponent in front of the aim and goes for it.
- Near an opponent, or just after attacking, the body faces the aim instead of the way it moves, so the player can back off and circle while facing it. The stand-in clips have no backward or sideways steps, so these are made in code from the forward ones, and will look like it.
- The camera frames the whole body and follows it down to the ground.
- Hits are seen: a flash on the struck body, a mark where the hit landed, and a short pause on a hit the player deals.
- A play-test can be run from one command: the game plays a scripted scenario, records itself, and writes clips, contact sheets and the numbers of each part. Reviewing a build stops depending on keys sent from outside or on the player's camera.
- No new assets: sounds and effects still wait for the asset import and art direction (OD-006).
- An opponent that fights back is pushed back once more, behind a punch that works.

## Alternatives

- An opponent that fights back first. Rejected: with a punch that does not land and a body that turns away, a fight would show the same problems with more moving parts.
- Original animation clips first. Rejected for the reason in decision 0017: no art direction to aim them at.
- Fix only the punch. Rejected: the review found the facing, the camera and the feedback just as much in the way, and the owner asked for an overhaul.

## Links

- Open decision: OD-018
- Builds on: decision 0016 (runs and the punch), decision 0017 (the get-up and the stride)
- Related open decision: OD-006 (art direction)
