---
id: 2026-11-26-05
date: 2026-11-26
goal: "README and Pages site upgrade, with CI for both"
start_sha: ac1906b63638b87b8ad706636bb34cddf8a66507
end_sha: 11d2f3ee996170507b7f3b6c369f5cf99a95c5f6
decisions: []
cameras: [char-front, char-side, char-three-quarter, fp-look-down, fp-owner-ahead, fp-owner-look-down, fp-owner-pitch-60, gym-lane, gym-ledges, gym-overview, gym-physics-boxes, gym-ragdoll, gym-ramps, gym-stairs-gaps, plane-overview, stage-closeup, stage-overview, tp-owner-front, tp-owner-shoulder]
clip: null
---

# 2026-11-26-05: README and Pages site upgrade, with CI for both

## Narrative

The README and the public site were reworked, and both are now checked on every push. The site used to open on the devlog list on default browser styles. It now opens on a full-width still of a front kick from a recorded play-test of build 0.1.11. It is dark by default, in the game's own colours: the grey grid floor under the header, the dummy's orange as the accent, the sky's blue for links. The home page says what the game is in plain terms. It shows where things stand (the newest decision, build and session, and the open questions), what is in the build and the controls, recent sessions, how work moves, and the fixed cameras' latest shots. The status is read from docs/ and the build contents from the README, so the page cannot claim what the repository does not. The first draft looked like a stock landing page, and the owner asked for it darker, with a bigger hero and less generic writing; the second draft follows that. Every page gained a description, social preview tags and a favicon, and the build now writes a 404 page, a sitemap and robots.txt.

The README leads with the same kick, then a short plain account of the game and two more stills, and its lists read as sentences instead of bold lead-ins. A new `og docs check` fails when the README's status lags the newest decision or Steam build, or a doc links to a missing file. CI runs it, together with a full site build that fails on a broken link, on every push and pull request. The release script now updates the README's build line in the same commit as the ledger row, so a release cannot leave the README behind. The live site was checked after the deploy: every page served, with no console errors.

## Changes

- 2c140e9 Site: a home page for the game, a dark look, and the basics
- 02f8754 docs: a README that leads with the game
- 11d2f3e ci: check the README and the site on every push and pull request

## Decisions touched

_None._

## Captures

![char-front](media/2026-11-26-05/char-front.webp)
![char-side](media/2026-11-26-05/char-side.webp)
![char-three-quarter](media/2026-11-26-05/char-three-quarter.webp)
![fp-look-down](media/2026-11-26-05/fp-look-down.webp)
![fp-owner-ahead](media/2026-11-26-05/fp-owner-ahead.webp)
![fp-owner-look-down](media/2026-11-26-05/fp-owner-look-down.webp)
![fp-owner-pitch-60](media/2026-11-26-05/fp-owner-pitch-60.webp)
![gym-lane](media/2026-11-26-05/gym-lane.webp)
![gym-ledges](media/2026-11-26-05/gym-ledges.webp)
![gym-overview](media/2026-11-26-05/gym-overview.webp)
![gym-physics-boxes](media/2026-11-26-05/gym-physics-boxes.webp)
![gym-ragdoll](media/2026-11-26-05/gym-ragdoll.webp)
![gym-ramps](media/2026-11-26-05/gym-ramps.webp)
![gym-stairs-gaps](media/2026-11-26-05/gym-stairs-gaps.webp)
![plane-overview](media/2026-11-26-05/plane-overview.webp)
![stage-closeup](media/2026-11-26-05/stage-closeup.webp)
![stage-overview](media/2026-11-26-05/stage-overview.webp)
![tp-owner-front](media/2026-11-26-05/tp-owner-front.webp)
![tp-owner-shoulder](media/2026-11-26-05/tp-owner-shoulder.webp)

## Clip

_No clip._
