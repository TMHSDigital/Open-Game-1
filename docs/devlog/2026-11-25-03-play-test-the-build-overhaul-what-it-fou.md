---
id: 2026-11-25-03
date: 2026-11-25
goal: "Play-test the build, overhaul what it found, and script the play-test"
start_sha: 98cf89151cdc97d5d32b87c1b3a9559d85abd50c
end_sha: 04600a712a877bba80ffa121ae4a15224bd4487c
decisions: [ADR-0018, OD-018]
cameras: [char-front, char-side, char-three-quarter, fp-look-down, fp-owner-ahead, fp-owner-look-down, fp-owner-pitch-60, gym-lane, gym-ledges, gym-overview, gym-physics-boxes, gym-ragdoll, gym-ramps, gym-stairs-gaps, plane-overview, stage-closeup, stage-overview, tp-owner-front, tp-owner-shoulder]
clip: null
---

# 2026-11-25-03: Play-test the build, overhaul what it found, and script the play-test

## Narrative

The session began by playing build 0.1.8 on a recording and reviewing it frame by frame. The headline finding was that no punch landed: the fist went for wherever the camera pointed, often over the dummy's shoulder, and the body turned away to follow the movement. Decision 0018 (OD-018) put the fixes and a repeatable play-test into one block.

A punch now picks the nearest standing opponent in front of the aim, aims at the part of its body the player looks at, and steps in when it is out of reach. Near an opponent the character holds a combat stance: it faces the aim, backs off slower than it walks forward, and a new leg-yaw node turns the legs toward the movement or plays the steps in reverse. The camera sits further back and follows the body down in a knockdown. Hits flash the struck body, leave a mark sized to the hit and briefly freeze the action. The run's results open a second after the run ends, and the dummies no longer idle in step. An independent review found three problems, all fixed: the legs could flip direction every frame when moving sideways, a step-in could carry the player off a ledge, and the script waited on the wrong process for a packaged game.

The play-test is now scripted. The game plays labelled scenarios itself (locomotion, stance, punch, knockdown) with a camera that frames the whole body, then logs timed events. `tools/ue/playtest.ps1` records the game window and cuts a clip, a contact sheet and a summary of the game's own numbers for each segment. The recording keeps the window on top and stops at the game's last event, so only the game is kept. On the packaged Shipping build every punch landed, 13 of 13, and the stance held to within a degree. That build went to the development branch as 0.1.9. The "Dummy spot" label now lies flat on the floor, where it is never mirrored.

Still open: the crouch walk is very deep, which comes from the clip and needs a different clip or a pose adjustment. A fast four-punch combo is less certain than a single punch, since each hit staggers the dummy somewhere new. Next is the next block of the melee design.

## Changes

- b867a18 docs: decision 0018, overhaul of the play-test findings and a scripted play-test (OD-018)
- e3e8865 feat: overhaul of the play-test findings, and a scripted play-test (decision 0018)
- aa016d3 fix: review findings on the overhaul, and the play-test scenario list
- 49aa875 chore: release 0.1.9
- 599b651 docs: record Steam build 0.1.9 (BuildID 26928152)
- 04600a7 fix: play-test recordings show only the game, and the dummy label is never mirrored

## Decisions touched

- ADR-0018
- OD-018

## Captures

![char-front](media/2026-11-25-03/char-front.webp)
![char-side](media/2026-11-25-03/char-side.webp)
![char-three-quarter](media/2026-11-25-03/char-three-quarter.webp)
![fp-look-down](media/2026-11-25-03/fp-look-down.webp)
![fp-owner-ahead](media/2026-11-25-03/fp-owner-ahead.webp)
![fp-owner-look-down](media/2026-11-25-03/fp-owner-look-down.webp)
![fp-owner-pitch-60](media/2026-11-25-03/fp-owner-pitch-60.webp)
![gym-lane](media/2026-11-25-03/gym-lane.webp)
![gym-ledges](media/2026-11-25-03/gym-ledges.webp)
![gym-overview](media/2026-11-25-03/gym-overview.webp)
![gym-physics-boxes](media/2026-11-25-03/gym-physics-boxes.webp)
![gym-ragdoll](media/2026-11-25-03/gym-ragdoll.webp)
![gym-ramps](media/2026-11-25-03/gym-ramps.webp)
![gym-stairs-gaps](media/2026-11-25-03/gym-stairs-gaps.webp)
![plane-overview](media/2026-11-25-03/plane-overview.webp)
![stage-closeup](media/2026-11-25-03/stage-closeup.webp)
![stage-overview](media/2026-11-25-03/stage-overview.webp)
![tp-owner-front](media/2026-11-25-03/tp-owner-front.webp)
![tp-owner-shoulder](media/2026-11-25-03/tp-owner-shoulder.webp)

## Clip

_No clip._
