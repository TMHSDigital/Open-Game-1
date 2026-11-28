---
id: 2026-11-27-04
date: 2026-11-27
goal: "Combat polish: no foot sliding on moving strikes, a blocking animation, and guard footwork"
start_sha: 7450c6c1611378ee2b8ca3f20e8b3ec5cb531e9a
end_sha: 7b5b1da57dbd7cb06d591ed85b8036b122a50f70
decisions: []
cameras: []
clip: null
---

# 2026-11-27-04: Combat polish: no foot sliding on moving strikes, a blocking animation, and guard footwork

## Narrative

Three things that still made the fighting look wrong were fixed. Blocking now raises a high guard, an authored clip with the fists at the brow, forearms in front of the face and the chin down, shown over the simulation instead of the guard steering the fists. In the combat stance the legs take a new Stance pose: stance idle, a step-drag shuffle (lead foot, then rear) and the jog by speed, with its own stride table, played backward when backing off, so fighters move like fighters instead of walking. A punch thrown while walking plays on the upper body so the legs keep walking instead of sliding; kicks and step-ins stay full-body, because a first try showed they lose their reach without the hips. All 133 tests and every play-test check pass with no punch missed, and build 0.1.16 is on the development branch. Next: a sideways step for circling (the legs only turn toward a lateral move now), motion capture boxing in place of the keyframed clips, and gloves.

## Changes

- 13a5876 feat: a high guard to block with, stance footwork, and punches thrown walking
- 555ce55 chore: release 0.1.16
- 1fb85ca docs: record Steam build 0.1.16 (BuildID 26985811)
- 7b5b1da docs: README points at session 2026-11-27-04

## Decisions touched

_None._

## Captures

_No captures._

## Clip

_No clip._
