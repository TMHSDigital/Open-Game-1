---
id: 2026-11-27-06
date: 2026-11-27
goal: "A sideways step for circling in the stance"
start_sha: fbfa8e2d74c1be400f256f601e27bfc621b329ab
end_sha: d0a049e8f447f6b183f0a7750ef25bbe9da1dfd8
decisions: []
cameras: []
clip: null
---

# 2026-11-27-06: A sideways step for circling in the stance

## Narrative

Fighters now circle with a side step: an authored side shuffle (the right foot steps out, the left follows, the feet never cross) that the legs take when moving sideways in the stance, played backward for the left. Reviewing it showed the outer arm flung out while circling: the simulated arms trailed the guard. In the stance the upper body now shows its animation over the simulation, and a hit shows on the bodies it struck by their hit weight, so a head shot snaps the head while the arms keep the guard (a first try, handing the whole upper body back on a hit, dropped the arms on every jab). Movement close to another fighter is footwork, at most 200 cm/s, except a strike's step in. The gym's box lane now goes through a box instead of passing it, because only a swinging arm had ever moved one. The development upload then failed its test gate on the punch from 150 cm, which missed about one run in five: the wind-up stretched to step in was being treated as an opponent's tell and stood still. Fixed after this entry was written (d5f4d07), 8 of 8, and build 0.1.18 is on the development branch with it. The gym box check still fails now and then (Issue #78). Next: motion capture boxing in place of the keyframed clips, and gloves.

## Changes

- b521577 feat: a side step for circling, and a guard that holds through footwork and hits
- 1a38736 docs: TOOLS lists the side shuffle among the authored clips
- d0a049e docs: README points at session 2026-11-27-06

## Decisions touched

_None._

## Captures

_No captures._

## Clip

_No clip._
