---
id: 2026-11-27-01
date: 2026-11-27
goal: "Polish the base character: the model's sculpt and look, how it moves, and how it reacts to contact and the environment"
start_sha: 83d454af9c0a2f418c568d9db9d3b46455b1b957
end_sha: 568049c42dddb8d7287b0b92d4da927cc7849efa
decisions: [ADR-0021, OD-006, OD-021, OD-022, ADR-0022, ADR-0024, OD-023]
cameras: []
clip: null
---

# 2026-11-27-01: Polish the base character: the model's sculpt and look, how it moves, and how it reacts to contact and the environment

## Narrative

This session started from the release side and ended on the character. Build 0.1.12 went to the development branch with the audit fixes, and the release check now protects the README line the upload rewrites. Promotions can now be recorded from here once Steam confirms the build is live on default (decision 0024), and 0.1.11 was recorded that way. The scripted play-test learned to judge itself: steps state checks and a failed check fails the run. New scenarios cover the movement gym, the body checks from milestone 2, the pause screen and a motion comparison that showed the clunky running comes from the stand-in clips, not the physics. The owner then set the art direction: realistic, with gore and dismemberment later behind settings (decision 0021), a base character generated with MakeHuman by script (decision 0022), and motion from CMU motion capture (decision 0023, replacing 0017). A new builder generates the athletic 180 cm body on the game's 89-bone skeleton. A trial in the game showed strikes and one ragdoll knee do not work on it yet, so the game keeps the current body until that fit is done (issue #74). Next: the fit and the switch, then skin and eyes, then the motion capture.

## Changes

- 3bbb1db docs: README names 0.1.11 as the default build
- 116c983 docs: CLAUDE.md asks for a development upload when a session changed the game
- 20563d9 chore: release 0.1.12
- 17864d3 docs: record Steam build 0.1.12 (BuildID 26976897)
- 597cd87 docs: README names 0.1.12 on the development branch
- 700ff79 feat: play-test scenarios for the body checks and the movement gym (#57)
- d3e7c83 fix: docs check requires the README sentence the release rewrites
- 241e2bb feat: play-tests check their outcomes and script the pause screen
- 9116a5e chore: session 2026-11-27-01 start
- f02f557 feat: motion play-test shows each gait as shipped, with full arm motors, and animation only
- 35e9106 docs: decision 0021, realistic art direction with adjustable gore later (OD-006)
- 0427bd6 docs: open OD-021 (realistic character source) and OD-022 (realistic motion source)
- ad3dad3 docs: decisions 0022 (MakeHuman character) and 0023 (CMU motion capture), 0017 superseded (OD-021, OD-022)
- b6717a1 feat: script that builds the realistic base character with MakeHuman (decision 0022)
- a1fc5ef feat: Claude records a promotion once Steam shows it live on default (decision 0024, OD-023)
- 7ce3c77 docs: record Steam build 0.1.11 live on default
- 568049c docs: README points at session 2026-11-27-01

## Decisions touched

- ADR-0021
- OD-006
- OD-021
- OD-022
- ADR-0022
- ADR-0024
- OD-023

## Captures

_No captures._

## Clip

_No clip._
