# 0025: Strikes are authored motion, physics only for the hit reaction

Status: Accepted
Date: 2026-11-27
Issue: [#77](https://github.com/TMHSDigital/Open-Game-1/issues/77)
Supersedes: 0016 in part (the punch steered through the physics body)

## Context

Since decision 0016 a punch has had no animation: the hand is a simulated body pushed toward a point in front of the target, and the arm follows. With the realistic body (decision 0022) it lands reliably, but it looks and feels wrong. There is no stance, no snap out and back, no hip and shoulder rotation, no weight transfer, and a landed hit is sold only by a short freeze, a colour flash and a number on screen.

The owner wants combat to feel like a UFC-style striking game, without grappling or clinching: strikes that look like real strikes, and hits that are satisfying when they connect. Those games play authored strike motion and use physics, if at all, for the reaction.

## Decision

Combat aims at the feel of a UFC-style striking game, striking only.

- Strikes play as authored animation: motion capture where it is good enough (CMU, decision 0023), keyframed by script otherwise. A strike is steered a little toward the target (turning the body and scaling the reach), never pulled off its path.
- During its own strike the attacking limb follows the animation; the physics motors on it are strong enough that the authored motion is what is seen.
- A hit is found by sweeping the fist or foot along its animated path between frames, from the moment the strike becomes active to its peak.
- The struck body reacts through physics (the existing stagger, knockdown and get-up), with the impulse given by the strike's speed and weight.
- A hit is sold by one feedback layer scaled by hit size: hit-stop, camera shake, sound and a visible snap of the struck part. The colour flash and on-screen numbers become optional.
- Fighters stand in a fighting stance with a guard when an opponent is near.

## Consequences

- Strikes get real anticipation, rotation, retraction and footwork, which is most of what "unnatural" meant.
- Timing becomes data: each strike's start-up, active and recovery frames come from its clip, so moves can be tuned and compared like a fighting game's.
- The current reach logic (PunchReach as both the arm target and the drive distance, the lunge step, the aim clamp) is replaced. Tests written against the physics-steered fist (reach in cm, fist speed) are rewritten against the new model: does the strike land from its range, does it miss out of range, how big the hit is.
- Clips are needed for the stance, a jab, a cross, both hooks, a kick, the block and the dodge. They come in through the asset-import process when from CMU, and are committed as retargeted clips only.
- Sound needs assets: punches landing on the body and the head, a blocked hit, a whiff. They come in through asset import under the licence policy.
- Physics still decides what happens to the body that is hit, so the simulation the game is built on stays; only the attacker's own limb stops being steered by it.

## Alternatives

- Keep the physics-steered fist and polish its path, the body rotation and the feedback. Rejected: the swing would still be a hand pulled toward a point, without the authored timing that makes a strike read as a strike, and every improvement would fight the simulation.
- Fully animated combat with canned hit reactions and no physics on the struck body. Rejected: it throws away the physical reactions and knockdowns the game already has, and canned reactions repeat visibly.

## Links

- Open decision: OD-024
- Partly supersedes decision 0016 (the punch steered through the physics body); builds on 0021 (realistic art direction), 0022 (the realistic body) and 0023 (CMU motion capture)
