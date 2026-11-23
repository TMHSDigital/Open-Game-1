---
id: 2026-11-22-03
date: 2026-11-22
goal: "Session 2: base character and movement feel (plan execution)"
start_sha: 37c95d96b5491afc16477f7d98cf188df7506723
end_sha: c193011213fa72bf60c1f57da069aa951911363f
decisions: [ADR-0012, OD-013]
cameras: [char-front, char-side, char-three-quarter, fp-look-down, fp-owner-ahead, fp-owner-look-down, fp-owner-pitch-60, gym-lane, gym-ledges, gym-overview, gym-physics-boxes, gym-ragdoll, gym-ramps, gym-stairs-gaps, plane-overview, stage-closeup, stage-overview, tp-owner-front, tp-owner-shoulder]
clip: null
---

# 2026-11-22-03: Session 2: base character and movement feel (plan execution)

## Narrative

Combat Simulator now has its first playable character. The faceless base model is built entirely by a Blender script: an 89-bone skeleton with the standard UE5 humanoid layout, a 180 cm mannequin of about 21,000 triangles, with separated fingers and shoulders, hips and hands reworked over several review rounds until they bend cleanly at 90 degrees. In Unreal it runs on a new self-contained CSCharacter plugin. The plugin covers weighty movement: a sprint that builds over 0.6 s, braking that slides, landings that cost speed, a wider turn at a sprint. It adds first and third person on V, with the head hidden only from your own view and no head bob, and a physics-driven body that sways, staggers, ragdolls and gets back up. Every feel value lives in one tuning asset that F5 reloads live. Free CC0 Quaternius animations stand in until the project has its own. The game now starts on L_Plane, a plain grid plane, and F2 switches to L_MovementGym, a measured course of ramps, stairs, gaps, ledges and boxes (decision 0013). The secrets scan stopped treating random bytes in binary assets as email addresses, and skeletal exports moved to a centimetre scene (decision 0012).

Known in this build: the stand-in run and sprint cycles look slow and bounding, and walking looks hurried. A foot can dip through the floor while the body gets up. With the arm raised overhead the shoulder still folds. Looking steeply down in first person shows no hands in the idle pose. In first person your own floor shadow has no head. Live tuning only changes values in the editor; a Steam build re-applies its packaged values. Next: the owner plays the build from the Steam development branch and judges the feel; the milestone is tagged when it feels right.

## Changes

- c39fc5e feat: UE5 humanoid skeleton reference (names and hierarchy only) and Blender runner
- 0b9a55f fix: pin the skeleton reference bone list and tighten its anti-Epic guards
- 1191645 feat: faceless base character built by Blender script
- f48b84b fix: export skeletal meshes from a centimetre scene
- f4c529f fix: base character hands, one closed mesh, centimetre export
- e515666 docs: ADR 0012, skeletal meshes export from a centimetre scene
- 14d367d fix: base character thumb, fist, shoulders and pelvis; no overwrite on failure
- 6fda7c9 fix: base character shoulder ramp, glutes, crotch and thenar
- fdbdd94 fix: base character shoulder junction and thumb root
- 239b670 fix: base character shoulder at true 90 degree flexion and abduction
- af1828b fix: base character shoulder weights by surface distance, armpit hollow filled
- b4284ab feat: CSCharacter plugin, base character import, weighty movement
- 62a7428 fix: secrets scan skips the email rule on binary assets; no crash on cp1252 consoles
- ef9c14f fix: tunable turn grip with a wider sprint turn; landing speed set before listeners
- 3095556 feat: base character assets (SK_BaseCharacter, PHYS_BaseCharacter, DA_MovementTuning)
- 84825af feat: Enhanced Input, dev tools, tuning reload and feel overlay
- aaa38a7 fix: F5 tuning reload key with an on-screen result
- 2ed3407 feat: stand-in CC0 locomotion retargeted to the base character
- 3ed91f2 feat: first and third person camera rig on one key
- aa4d9d4 fix: rename the Quaternius source IK rig to IK_UAL1_Source
- 705f9d2 fix: camera feel values in the tuning asset; no head bob in first person
- 973f12b feat: physics-driven body with stagger, ragdoll and recovery
- 5ffcfe3 fix: ragdoll damping, steep-slope recovery fallback and a real input test
- 1e97778 fix: ground-only ragdoll damping and a real no-space recovery check
- 7641a5f feat: L_Plane start map, movement gym and character captures
- bd5556a fix: headless own shadow in first person, no player shadow in stage captures, 44 deg ramp margin
- 58c4797 docs: character movement workflows, tools and systems map
- def8b77 fix: jump while crouched stands up and jumps
- 34c27b9 feat: ship the movement gym with an F2 dev map switch, respawn after a fall
- c193011 docs: dev keys, F2 map switch, live tuning scope and Steam status

## Decisions touched

- ADR-0012
- OD-013

## Captures

![char-front](media/2026-11-22-03/char-front.webp)
![char-side](media/2026-11-22-03/char-side.webp)
![char-three-quarter](media/2026-11-22-03/char-three-quarter.webp)
![fp-look-down](media/2026-11-22-03/fp-look-down.webp)
![fp-owner-ahead](media/2026-11-22-03/fp-owner-ahead.webp)
![fp-owner-look-down](media/2026-11-22-03/fp-owner-look-down.webp)
![fp-owner-pitch-60](media/2026-11-22-03/fp-owner-pitch-60.webp)
![gym-lane](media/2026-11-22-03/gym-lane.webp)
![gym-ledges](media/2026-11-22-03/gym-ledges.webp)
![gym-overview](media/2026-11-22-03/gym-overview.webp)
![gym-physics-boxes](media/2026-11-22-03/gym-physics-boxes.webp)
![gym-ragdoll](media/2026-11-22-03/gym-ragdoll.webp)
![gym-ramps](media/2026-11-22-03/gym-ramps.webp)
![gym-stairs-gaps](media/2026-11-22-03/gym-stairs-gaps.webp)
![plane-overview](media/2026-11-22-03/plane-overview.webp)
![stage-closeup](media/2026-11-22-03/stage-closeup.webp)
![stage-overview](media/2026-11-22-03/stage-overview.webp)
![tp-owner-front](media/2026-11-22-03/tp-owner-front.webp)
![tp-owner-shoulder](media/2026-11-22-03/tp-owner-shoulder.webp)

## Clip

_No clip._
