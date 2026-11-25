---
id: 2026-11-24-04
date: 2026-11-24
goal: "Session 6: the simulation run skeleton (set up a run, start it, end it, stats, rerun)"
start_sha: 894ac46cb37f1ec31bc2d8c0c194755b59403aa8
end_sha: b4c4543eb18febdcc20917ce0de1e33d8819c1d8
decisions: [ADR-0016, OD-016]
cameras: []
clip: null
---

# 2026-11-24-04: Session 6: the simulation run skeleton (set up a run, start it, end it, stats, rerun)

## Narrative

This session built the first half of decision 0016: the game now has runs. It was not recorded.

A run is a setup put into the world. The setup is plain data: the map, how many target dummies, whether they stand in a line or a ring and how far away, how many ball launchers surround the start, a time limit and a knockdown goal. Starting a run puts the play space back, replaces the map's own dummies with the setup's, adds the launchers and starts counting. It ends by hand, at the time limit or at the knockdown goal. The world plays on after the end, but nothing more is counted and the pause screen opens with the results: time, balls thrown, accuracy, hits dealt and taken split into staggers and knockdowns, the biggest and average hit, and which body parts were struck. The same run can be started again, with Backspace or a button, or left, which puts the map back as it was made. A setup for the other map opens that map and starts there.

The pause screen has a new first tab, Run, with the state of the run, the buttons, the results and the setup of the next run as sliders and choices generated from the setup's range table. The setup is kept on this machine between sessions. While a run is on, one line at the top of the screen shows the time, hits and knockdowns.

Counting needs nothing from the characters. The character plugin now announces every contact hit and every ball thrown, and the run director listens: a hit on the player's character is taken, a hit by the player's body or by a ball the player threw is dealt, anything else is between others. The first melee attack will be counted the day it exists, because it will deliver its hits through the same entry.

A fresh review found no crashes and six gaps, all fixed: a saved setup naming a map the game no longer has did nothing when started; accuracy mixed hits with the body into ball hits; a run waiting for another map could start on the wrong one; a ball lost its thrower when the character that threw it respawned; left and right did not change a choice row; and focus stayed on the End run button after it disappeared.

Two build problems came up, both from the module being compiled as one unit. Two older test files had file-level using directives that made names in other test files ambiguous, and a local name in the value row hid one of the pause screen's, which only the Shipping package treats as an error. The first release dry run caught the second.

Seven new tests cover the setup's ranges and text form, the placement of dummies and launchers, the statistics, and three runs played on the real maps (start, count, end at a goal, start again, end by hand, leave; end at a time limit; open the other map). All 108 tests pass. Not checked by eye: the Run tab and the line on screen have never been seen in a window, because the machine was locked during the session. Their logic is tested; their layout is not.

Uploaded as build 0.1.6 on the development branch. Next: the owner plays it and looks at the Run tab, then the second half of decision 0016, the first melee attack.

## Changes

- 1feadf6 docs: decision 0016, run skeleton then first melee attack (OD-016)
- 714dc19 feat: simulation runs: set up, start, count, end, rerun (decision 0016)
- 2bf0a2e fix: run review points: stale map name, ball accuracy, carried runs, orphaned balls, choice keys, focus
- 25e5c11 fix: value row locals no longer hide the pause screen's names in a single-unit build
- 3fccf95 chore: release 0.1.6
- b4c4543 docs: record Steam build 0.1.6 (BuildID 26911177)

## Decisions touched

- ADR-0016
- OD-016

## Captures

_No captures._

## Clip

_No clip._
