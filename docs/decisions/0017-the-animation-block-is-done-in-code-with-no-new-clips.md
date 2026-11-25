# 0017: The animation block is done in code, with no new clips

Status: Accepted
Date: 2026-11-25
Issue: [#63](https://github.com/TMHSDigital/Open-Game-1/issues/63)
Supersedes: none

## Context

Decisions 0015 and 0016 both put the animation block next: the character still moves on stand-in walk, jog and sprint cycles, and getting up after a ragdoll is a half-second blend in which every bone travels a straight line from lying to standing (#37, #38). Neither decision said how the block would be done.

Three things narrowed it. The stand-in pack has 43 clips and none of them is a get-up, so that motion cannot come from the pack. Art direction is open (OD-006), so clips authored now would be made with no style to aim at and might be thrown away. And the punch from decision 0016 was built with no animation at all, by steering a simulated body, and it works.

## Decision

The animation block (decisions 0015 and 0016) is done in code with no new animation clips. Getting up after a ragdoll becomes physically driven: the body is pulled from where it lies to standing by motors toward the animated pose, by way of the crouch pose the project already has, instead of a blend between two poses. Walk, jog and sprint are made to match the ground speed with the clips the project already has. It ships as one build on the development branch. Original clips wait for art direction (OD-006).

## Consequences

- The get-up becomes physical. A body that pulls itself up against the floor cannot put a foot through it, which a blend between two poses can.
- The character keeps one way of moving its body: animation gives the pose to aim for and physics gets it there. The punch, the stagger and the get-up now all work that way, so a later weapon or reaction has a pattern to follow.
- No new assets, nothing through the asset import, and nothing that art direction can invalidate.
- How it looks is limited by the stand-in clips it aims for and by what motors can do. It will not look authored, and it is judged in the running game, not in a test.
- More of the look now depends on tuning values, which are sliders in the build, so the owner can change it without a new build.
- The recovery is the most heavily tested part of the physics body. Changing it puts those tests at risk, and the old blend stays as the fallback for a character with no physics body.
- The project still has no animation of its own. That waits for art direction.

## Alternatives

- Original clips authored in Blender. Rejected for now: slow, of uncertain quality, and aimed at no style. It is what the project will want once art direction is decided.
- More clips from the stand-in pack (hit reactions, a roll, punches). Rejected: it adds stand-ins instead of removing them, and the pack still has no get-up.
- Move the animation block back again and build an opponent that fights back first. Rejected: the look has been put off twice, and the get-up glitch is seen every time anyone is knocked down.

## Links

- Open decision: OD-017
- Issues: #37, #38
- Builds on: decision 0015, decision 0016 (the punch steered through the physics body)
- Related open decision: OD-006 (art direction)
