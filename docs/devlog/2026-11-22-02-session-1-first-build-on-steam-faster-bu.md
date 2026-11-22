---
id: 2026-11-22-02
date: 2026-11-22
goal: "Session 1: first build on Steam, faster builds, start OD-001"
start_sha: b20c17a72d79a788a0966613f2a135de02ed24ef
end_sha: 7e528b426c07ce50e592a60727d051454b9a4988
decisions: [ADR-0004, ADR-0010]
cameras: [stage-closeup, stage-overview]
clip: null
---

# 2026-11-22-02: Session 1: first build on Steam, faster builds, start OD-001

## Narrative

<!-- narrative -->

## Changes

- b921664 fix: release gate 7 scans every tracked file; scan published release text; strict -Branch
- 25cbb7f fix: session end requires OCR text for desktop media; commit only its outputs
- 58a2a82 fix: review cleanups: BSD SPDX ids, MCP auto-start off, Pages scope, hook modes, docs
- 7ab8d2a fix: CI secrets scan keeps lfs: false via --lfs-pointers-ok; ADR 0004 names BSD ids
- 5adfd35 docs: new README; no separate Steam build account (ADR 0010)
- f11336c docs: Steam setup steps 1 and 3 done (OS, launch option, publish)
- d91daf9 fix: repo .env wins over inherited Steam env vars in release.ps1
- 52c68c7 docs: Steam setup step 6 done (.env)
- f303eb6 docs: Steam setup step 5 done (steamcmd login)
- b74e7ac fix: site skips unsafe devlog ids and non-http clip links; exact index dedupe
- 959dba5 chore: release 0.1.1
- db6d3d9 docs: record Steam build 0.1.1 (BuildID 26854724)
- ca2ddc6 docs: development branch created; 0.1.1 live on development; Steam setup complete
- 0528be1 feat: Claude may record a confirmed promotion (promoted -OwnerConfirmed)
- f895a0c Revert "feat: Claude may record a confirmed promotion (promoted -OwnerConfirmed)"
- 7e528b4 perf: incremental cooking for packaging

## Decisions touched

- ADR-0004
- ADR-0010

## Captures

![stage-closeup](media/2026-11-22-02/stage-closeup.webp)
![stage-overview](media/2026-11-22-02/stage-overview.webp)

## Clip

_No clip._
