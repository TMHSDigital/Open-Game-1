---
id: 2026-11-27-05
date: 2026-11-27
goal: "Test and refine the combat animations frame by frame"
start_sha: 9657623da2038bb7831a16ea63cff7660c587638
end_sha: 7eeaa53a1c5c4d6bdde108810f453317962b5ab4
decisions: [ADR-0025]
cameras: []
clip: null
---

# 2026-11-27-05: Test and refine the combat animations frame by frame

## Narrative

The strikes were reviewed frame by frame in the running game. A new play-test scenario, strikes, throws each one at the dummy in slow motion (play-test steps can now set a time dilation) and films it from the side and from a close quarter view. Four things showed up that the clip previews could not. The guard sagged into the hands at the mouth, because the arm motors still ran at the 0.22 share tuned for punches steered through the physics body; at 0.6 the guard holds, and a stronger hit profile lets a struck arm or head give and come back instead of flopping. A cross to the jaw barely moved the head; the head snap is now nearly twice as strong and a landed punch rocks the whole upper body, more for a hook than a jab. A hook from 70 cm stepped in, met the target and slid around it; the step in now stops when the bodies meet. The rear fist sat at the mouth and now sits at the cheek. All 133 tests and every play-test check pass with no punch missed, and build 0.1.17 is on the development branch. Next: a sideways step for circling, motion capture in place of the keyframed clips, and gloves.

## Changes

- 28b5776 fix: a held guard, readable hit reactions, and hooks that do not run past
- 71aae5d chore: release 0.1.17
- a52812f docs: record Steam build 0.1.17 (BuildID 26986608)
- 7eeaa53 docs: README points at session 2026-11-27-05

## Decisions touched

- ADR-0025

## Captures

_No captures._

## Clip

_No clip._
