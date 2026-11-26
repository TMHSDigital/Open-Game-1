---
id: 2026-11-26-01
date: 2026-11-26
goal: "Bring the docs up to date with the play-test overhaul and the duel"
start_sha: 6e35eeba630de3532056e1227b330d3b88fc5838
end_sha: d7cd6b7d0259c7e6232b37c6434a0691d687287f
decisions: [ADR-0011, ADR-0019]
cameras: [char-front, char-side, char-three-quarter, fp-look-down, fp-owner-ahead, fp-owner-look-down, fp-owner-pitch-60, gym-lane, gym-ledges, gym-overview, gym-physics-boxes, gym-ragdoll, gym-ramps, gym-stairs-gaps, plane-overview, stage-closeup, stage-overview, tp-owner-front, tp-owner-shoulder]
clip: null
---

# 2026-11-26-01: Bring the docs up to date with the play-test overhaul and the duel

## Narrative

A documentation session. The README still described a character that only walks on a plane, with build 0.1.2 and decision 0011 as the current goal. It now says what is in the build: simulation runs, the punch, the block and the dodge, and an opponent that fights back. It also lists the controls for keyboard and gamepad, points at decision 0019, build 0.1.10 and the latest devlog, shows a current capture with the dummy, and explains that changes are judged on scripted play-test recordings as well as tests. The workflows doc gained the fight keys, how the punch, stance, guard and opponent are tuned and seen, a packaged play-test step before each release, and the note that an open session's devlog stub blocks the release's clean-tree gate. The tools doc gained ffmpeg and the play-test script, the character plugin's README now covers the strike, the guard, the combat stance and the ball, and CLAUDE.md points at the play-test. No code changed, so the captures are the ones rendered at the end of the last session.

Next: the opponent's whiffs from just out of reach, and the next decision, between more attacks and stamina, a first weapon, or real animation clips.

## Changes

- 2ba5a8d docs: bring the README, workflows, tools and plugin docs up to date
- d7cd6b7 docs: point CLAUDE.md at the scripted play-test

## Decisions touched

- ADR-0011
- ADR-0019

## Captures

![char-front](media/2026-11-26-01/char-front.webp)
![char-side](media/2026-11-26-01/char-side.webp)
![char-three-quarter](media/2026-11-26-01/char-three-quarter.webp)
![fp-look-down](media/2026-11-26-01/fp-look-down.webp)
![fp-owner-ahead](media/2026-11-26-01/fp-owner-ahead.webp)
![fp-owner-look-down](media/2026-11-26-01/fp-owner-look-down.webp)
![fp-owner-pitch-60](media/2026-11-26-01/fp-owner-pitch-60.webp)
![gym-lane](media/2026-11-26-01/gym-lane.webp)
![gym-ledges](media/2026-11-26-01/gym-ledges.webp)
![gym-overview](media/2026-11-26-01/gym-overview.webp)
![gym-physics-boxes](media/2026-11-26-01/gym-physics-boxes.webp)
![gym-ragdoll](media/2026-11-26-01/gym-ragdoll.webp)
![gym-ramps](media/2026-11-26-01/gym-ramps.webp)
![gym-stairs-gaps](media/2026-11-26-01/gym-stairs-gaps.webp)
![plane-overview](media/2026-11-26-01/plane-overview.webp)
![stage-closeup](media/2026-11-26-01/stage-closeup.webp)
![stage-overview](media/2026-11-26-01/stage-overview.webp)
![tp-owner-front](media/2026-11-26-01/tp-owner-front.webp)
![tp-owner-shoulder](media/2026-11-26-01/tp-owner-shoulder.webp)

## Clip

_No clip._
