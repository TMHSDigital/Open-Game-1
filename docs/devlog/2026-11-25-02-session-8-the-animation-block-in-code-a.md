---
id: 2026-11-25-02
date: 2026-11-25
goal: "Session 8: the animation block in code: a physically driven get-up and locomotion that matches the ground"
start_sha: 4a2448b9ac21b400015a831dcfccf2470c87dfab
end_sha: 5b43bd578382249042ac9088cdcd1bdd1454bae9
decisions: [ADR-0017, OD-017]
cameras: []
clip: null
---

# 2026-11-25-02: Session 8: the animation block in code: a physically driven get-up and locomotion that matches the ground

## Narrative

This session did the animation block that decisions 0015 and 0016 had put off, in the way decision 0017 settled at its start: in code, with no new clips. The stand-in pack has no get-up clip and art direction is open, so clips made now would have had no style to aim at. It was not recorded.

Getting up is now physical. Before, the body was frozen where the ragdoll lay and blended to the standing pose over half a second, every bone on a straight line, and a foot could pass through the floor. Now the bodies stay simulated, with gravity and the floor, and every one of them, pelvis and legs included, is pulled toward the animated pose by the same motors that hold the upper body in normal play, made stronger. The pose to aim for is the crouch first and then standing, so the body gathers itself low and rises. A short blend closes whatever gap the motors leave. In the test the pelvis climbs from 29 cm to 94 cm of a standing 95 in about a second and no body goes under the floor. Three values are sliders: how hard the body pulls and how long each part takes. With the pull at 0 the old blend is all there is.

Walk, jog and sprint. The feet already matched the ground through the play rate, but the clips are far from the tuned speeds: the walk had to play at 1.85 times its speed and the jog and sprint at about 0.7, which is the hurried walk and the slow, bounding run. The usual answer is the engine's stride warping, which needs IK foot bones this skeleton does not have. So the project has a small node of its own: it moves each foot along the way the character walks, from the point under its hip, and bends the leg to reach it, working from the feet the clip gives. The rate the feet need is split between play rate and the length of the steps: the rate stays inside a band, the stride takes the rest up to its own limits, and what is left over goes back to the rate. A jog at the tuned speed now plays at 0.89 with steps three quarters as long, where it played at 0.67.

Two things went wrong. Run from the editor's binaries without cooking, the game showed every character in the reference pose: the node's graph class lived in a module that only loads in the editor, so the animation Blueprint failed to load. That module now loads in uncooked games as well, as the engine's own node modules do. A packaged build never had the problem, and the packaged Shipping build was started and looked at before the upload for that reason. And an older test measured the feet by play rate alone and had to learn about the stride.

A fresh review found nothing serious and three things worth fixing, all fixed: the stride applied to the landing pose; it jumped by a quarter in one frame when starting or stopping, and is now eased and comes in over the first of the walk speed; and a longer step than the leg could reach lifted the foot, and is now cut short along the ground instead.

Seen in the running game: after a ragdoll the character is low in a crouch about a second later and standing in the next still; idle and jog poses are right, in the uncooked game and in the packaged one. Not judged: how the get-up and the new stride look in motion. Stills cannot show that and the third-person camera does not show the legs, so both need the owner's eye. All 112 tests pass, two of them new. Issues #37 and #38 are closed.

Uploaded as build 0.1.8 on the development branch. Next: the owner plays builds 0.1.6 to 0.1.8, which nobody but the tests and a few stills has seen, and says what is wrong. After that the next block needs a decision; the brief points at an opponent that fights back.

## Changes

- b704d8b docs: decision 0017, the animation block is done in code (OD-017)
- 332e6f7 feat: a physically driven get-up and a stride that matches the ground (decision 0017)
- afa5b38 fix: stride review points, and the get-up and stride in the systems map
- b5a1086 chore: release 0.1.8
- 5b43bd5 docs: record Steam build 0.1.8 (BuildID 26922995)

## Decisions touched

- ADR-0017
- OD-017

## Captures

_No captures._

## Clip

_No clip._
