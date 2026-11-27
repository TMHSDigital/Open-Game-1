---
id: 2026-11-26-06
date: 2026-11-26
goal: "Fix the open audit issues, starting with #67 (saved tuning lost on a stale entry) and #69 (kicks counted as punches)"
start_sha: 42f377f0a2a4d9c9bbac1bf1fa8d117a2250f8c2
end_sha: d7dbb3960fc27de8ca25692c53fd338369f99f09
decisions: []
cameras: []
clip: null
---

# 2026-11-26-06: Fix the open audit issues, starting with #67 (saved tuning lost on a stale entry) and #69 (kicks counted as punches)

## Narrative

<!-- narrative -->

## Changes

- 88bfeb8 fix: release status no longer reports a false development MISMATCH (#59)
- 3a745db fix: saved tuning changes survive a stale entry after an update (#67)
- 3e34968 fix: run results call strikes strikes, not punches (#69)
- 8632a9f refactor: bind character input from one table (#32)
- bd8c79d feat: rebind keys from the pause screen's Keys tab (#68)
- 857f4c6 feat: release starts the staged game once before upload (#39)
- 1da5cde refactor: one writer and reader for each devlog format (#35)
- 2af1374 refactor: physics body follows tuning changes by event (#34)
- d34515a refactor: one test world and one input rig for every test (#33)
- c37b02e refactor: one repo-root lookup and one template filler in og (#50)
- 653dc9d test: cover the session and decisions CLI and their error paths (#52)
- 79b861f ci: lint the Python with ruff and the PowerShell with PSScriptAnalyzer (#51)
- 8e428c6 fix: settings save on release, and pause-screen edge cases (#60)
- 1f259bc fix: reset brings back lost dummies; ragdoll contacts leave the last hit (#61)
- 7c99300 feat: keep a history of finished runs and the best per setup (#70)
- b2b54bc test: log why a play-test punch missed (#71)
- b702536 build: keep editor-only content out of the Steam build (#53)
- d7dbb39 docs: README points at session 2026-11-26-06

## Decisions touched

_None._

## Captures

_No captures._

## Clip

_No clip._
