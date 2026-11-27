---
id: 2026-11-27-02
date: 2026-11-27
goal: "Fit the realistic base character to the game and switch over (#74)"
start_sha: 33bfdc4e536a5fff0133f6fc030566799b6c40b2
end_sha: 1f121134fac251a8c0416c7c576e5e5c0f52dc41
decisions: [ADR-0022]
cameras: []
clip: null
---

# 2026-11-27-02: Fit the realistic base character to the game and switch over (#74)

## Narrative

The realistic MakeHuman body is now the game's character. The trial import had short arms, hands bent into hooks and punches falling short, and the root cause was one builder flag: without MPFB's joint helpers the rig never fitted the body. With the helpers kept, the arms posed to the game's rest pose and every bone given the old body's frames, the clips play naturally. Three game-side fixes followed. Ragdoll hinges now take their axis side to side, because a realistic, slightly knock-kneed leg tilted the old axis 18 degrees and the knees overshot. The motors are stiffer, with the arm strength scaled to keep punches reaching. And the strike aims no higher than the upper chest: on this spine the old top bone sat at the neck line, so level punches glanced off the neck and head, and the punch tests failed three runs in four. After that every automation test passes (133 of 133) and every play-test check passes, and build 0.1.13 is on the development branch. Open: a punch with the target pressed against the player still does not land (the test warns, #76), unchecked punches still miss now and then (#71), and the body has no skin, eyes or hair yet and still moves on the stand-in clips until motion capture (decision 0023).

## Changes

- 0c0d479 fix: realistic body takes the game's rest pose and frames; reimport updates the skeleton
- 72ce89f feat: the realistic MakeHuman body is the game's character (decision 0022)
- fda993a chore: release 0.1.13
- 89e2578 docs: record Steam build 0.1.13 (BuildID 26982668)
- 1f12113 docs: README points at session 2026-11-27-02

## Decisions touched

- ADR-0022

## Captures

_No captures._

## Clip

_No clip._
