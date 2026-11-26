---
id: 2026-11-26-04
date: 2026-11-26
goal: "Attacks and stamina, and the opponent's whiffs"
start_sha: 6bd4d6ceaf489a2ab4c6e39a3f7c0bce2c8e1f07
end_sha: 00efb4e188747f22b74c8bd722000ffd461a4b54
decisions: [ADR-0020, OD-020]
cameras: [char-front, char-side, char-three-quarter, fp-look-down, fp-owner-ahead, fp-owner-look-down, fp-owner-pitch-60, gym-lane, gym-ledges, gym-overview, gym-physics-boxes, gym-ragdoll, gym-ramps, gym-stairs-gaps, plane-overview, stage-closeup, stage-overview, tp-owner-front, tp-owner-shoulder]
clip: null
---

# 2026-11-26-04: Attacks and stamina, and the opponent's whiffs

## Narrative

Decision 0020 (OD-020) chose more attacks and stamina. Strikes are now data: a row per move scales the punch tuning and sets a stamina cost, and the player and the opponent read the same rows. The attack key throws a jab, then a straight if pressed again straight after: a one-two. Pressed while moving sideways near an opponent, it throws a hook from that side. A new key throws a front kick. It is the first time a leg is simulated in normal play: the right leg is held loosely to its pose while the foot and knee are steered, and the kick counts only on the body from the upper thigh up. Stamina is spent by strikes, dodges and hits taken on the guard, and it comes back after a pause. A strike without enough is slower and weaker, and at zero the guard drops until some is back. Bars show it for the player and over each fighting opponent. Runs can switch stamina off and scale its recovery, and the results list strikes by move.

The opponent now throws a jab, a one-two, a hook and a kick in turn, and holds off when it is short of stamina. The play-test found the kick dragging along the floor, held down by motors as strong as the upper body's. It also found the kick catching the dummy's feet, and the jab too short to step in from 150 cm. The 'whiffs' from last time turned out to be partly the scenario's own doing: it moved the player away while the opponent was mid-punch. It now restarts the opponent each segment. Against a player standing still the opponent lands every strike, a held block takes them all, and a dodge on the tell makes them all miss. A review found two traps that only tuning could reach: a too-costly attack that never starts, and a recovery of zero. Both were fixed. All 124 tests pass. Build 0.1.11 went to the development branch after a packaged play-test, uploaded with this session still open now that a session leaves the tree clean.

Next: the guard and the kick are code poses on stand-in clips, and the opponent is a fixed pattern. A first weapon, a smarter opponent or real clips are the candidates.

## Changes

- 5764dd8 docs: decision 0020, more attacks and stamina (OD-020)
- f7b8837 feat: more attacks and stamina (decision 0020)
- 6142d35 fix: review findings on attacks and stamina
- d5c3cc2 chore: release 0.1.11
- 32ee0f4 docs: record Steam build 0.1.11 (BuildID 26954922)
- 00efb4e docs: README names build 0.1.11

## Decisions touched

- ADR-0020
- OD-020

## Captures

![char-front](media/2026-11-26-04/char-front.webp)
![char-side](media/2026-11-26-04/char-side.webp)
![char-three-quarter](media/2026-11-26-04/char-three-quarter.webp)
![fp-look-down](media/2026-11-26-04/fp-look-down.webp)
![fp-owner-ahead](media/2026-11-26-04/fp-owner-ahead.webp)
![fp-owner-look-down](media/2026-11-26-04/fp-owner-look-down.webp)
![fp-owner-pitch-60](media/2026-11-26-04/fp-owner-pitch-60.webp)
![gym-lane](media/2026-11-26-04/gym-lane.webp)
![gym-ledges](media/2026-11-26-04/gym-ledges.webp)
![gym-overview](media/2026-11-26-04/gym-overview.webp)
![gym-physics-boxes](media/2026-11-26-04/gym-physics-boxes.webp)
![gym-ragdoll](media/2026-11-26-04/gym-ragdoll.webp)
![gym-ramps](media/2026-11-26-04/gym-ramps.webp)
![gym-stairs-gaps](media/2026-11-26-04/gym-stairs-gaps.webp)
![plane-overview](media/2026-11-26-04/plane-overview.webp)
![stage-closeup](media/2026-11-26-04/stage-closeup.webp)
![stage-overview](media/2026-11-26-04/stage-overview.webp)
![tp-owner-front](media/2026-11-26-04/tp-owner-front.webp)
![tp-owner-shoulder](media/2026-11-26-04/tp-owner-shoulder.webp)

## Clip

_No clip._
