# 0006: Session-based devlog and capture history

Status: Accepted
Date: 2026-11-22
Issue: none
Supersedes: none

## Context

The framework exists to let the public watch the game take shape and to keep a permanent record of how it looked over time. A history that depends on remembering to write things down will have gaps.

## Decision

Every work session has a short start and end routine run as Claude Code skills that call plain scripts. Start creates `docs/devlog/YYYY-MM-DD-NN-<slug>.md` and records the starting commit, with a screen recording only if the owner opts in each time. End takes fixed-camera shots defined in `docs/captures/cameras.json`, builds a contact sheet and a short clip, fills the devlog entry from git, has Claude write the narrative and commits. Recordings are redacted before anything is committed; raw recordings stay local. Images are WebP, at most 1920 px on the long side and targeting 300 KB or less, in plain git (not LFS). Videos never go in git; clips are attached to a rolling monthly GitHub Release. Milestones get a tag `m<N>-<slug>` and a before/after page. A GitHub Pages site built in Actions presents the timeline, gallery and decisions.

## Consequences

- A rich, scrubbable history for a small routine cost per session.
- Plain-git images keep LFS bandwidth for Unreal and Blender files.
- Scripts work without Claude or the editor open, so a session can always be closed by hand.
- Repo size grows with devlog images and must be watched.

## Alternatives

- Milestone-only capture: lighter, but loses the process in between.
- Capture on every commit: noisy, needs the editor running and cannot run in cloud CI.
- Videos in git or LFS: quota and clone size problems.

## Links

- Related: 0005 (capture safety relies on the secrets policy)
