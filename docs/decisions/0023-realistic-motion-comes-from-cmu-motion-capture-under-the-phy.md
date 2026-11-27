# 0023: Realistic motion comes from CMU motion capture under the physics body

Status: Accepted
Date: 2026-11-27
Issue: [#73](https://github.com/TMHSDigital/Open-Game-1/issues/73)
Supersedes: 0017

## Context

Decision 0017 did the animation block in code with no new clips. It kept the CC0 Quaternius stand-in clips and put original animation off until art direction was decided. Decision 0021 has now decided it: realistic.

The 2026-11-27 play-test showed the stand-ins are the limit. The jog is hunched with the arms bent behind, and the idle has flared elbows. The `motion` scenario showed the same poses with the physics layer hidden, so they come from the clips as they land on this skeleton, not from the physics body. Correcting a stylized clip in code cannot make it realistic.

Realistic motion needs a source whose license allows a public repository and a commercial game. The CMU Graphics Lab motion capture database is free for use in commercially sold products; the one condition is not reselling the data itself, even in converted form. Copies converted by others, and the AMASS collection that includes it, carry stricter licenses.

## Decision

Realistic locomotion and idle motion come from the CMU Graphics Lab motion capture database, downloaded from the source, cleaned up, retargeted onto the base character and played under the existing physics body. The Quaternius stand-in clips are replaced clip by clip as the motion capture replacements work. This supersedes decision 0017's rule of no new clips.

## Consequences

- Motion comes from real people moving, which is what a realistic look needs. The physics body (decision 0017's lasting part: animation gives the pose to aim for, physics gets there) stays, so strikes, staggers and the get-up keep working as they do.
- The data is downloaded from the CMU site only, never from a converted copy or AMASS. It goes through the asset-import process (ledger and credits, with the lab's requested acknowledgement). The repository holds only what the game needs, as retargeted clips, not the raw database: committing the raw data could read as redistributing it.
- The capture data is old and uneven: foot sliding, noise and odd starts and ends. Each clip needs cleanup (trimming to a loop, foot contacts, smoothing), done by script in Blender, so it can be redone.
- CMU skeletons differ from the UE5 layout. Clips go BVH to Blender to FBX and then through the IK Retargeter onto the base character. The new character (decision 0022) is done first, so the retarget aims at the final body.
- The stride-matching code (play rate and stride scale) keeps working on the new clips, and its limits may need retuning.
- The parts of 0017 that are not about clips still hold: the physically driven get-up, and one way of moving the body.

## Alternatives

- Keep the CC0 stand-ins and correct them in code. Rejected: the quickest, but the motion stays stylized underneath, which works against decision 0021.
- Author clips in Blender by script. Rejected as the main source: full control, but realistic hand-made motion is slow to get right. It stays available for motions the database lacks.
- Epic's sample animation. Rejected: Unreal EULA content that cannot go in the public repository.
- Physics-driven procedural locomotion on a minimal clip set. Not chosen now: a large research project. The physics body already adds reaction on top of the clips.

## Links

- Open decision: OD-022 (Issue #73)
- Supersedes: decision 0017 (its rule of no new clips; the physical get-up and the physics body approach carry over)
- Builds on: decision 0021 (realistic art direction), decision 0022 (the realistic base character), decision 0004 (third-party asset license policy)
