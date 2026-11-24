---
id: 2026-11-23-01
date: 2026-11-23
goal: "Session 3: fix the audit issues (tooling, release safety, docs)"
start_sha: 58aacc021b16112e86246fbc7db9281d7e0d3409
end_sha: d9830af09cf47012a50458832ac4e8820ab68b4f
decisions: [ADR-0014]
cameras: []
clip: null
---

# 2026-11-23-01: Session 3: fix the audit issues (tooling, release safety, docs)

## Narrative

This session worked through the issues that a full audit of the repository filed the same day. Twenty-five are closed.

The secrets scan, which is the only thing between private Steam values and a public repository, had real holes. Run on a folder or with no arguments it scanned nothing and reported success. It also missed Steam store links, `steam://` links and lines pasted from steamcmd. It now fails closed, knows those forms, matches this machine's own Steam values in any form, and also scans commit messages, file names, Issue text and decision text before they are published. The allow marker only works in test fixtures.

The release script is safer to run unattended. It masks the account name and IDs in steamcmd output, refuses its test-only switches on a real upload, and no longer leaves a pushed version bump behind when an upload fails. CI now runs the release script's tests on Windows, the workflow actions are current and pinned, and one `og check` command defines the repo checks for the commit hook, CI and the release gates.

In the game, a character spawned in the middle of a frame no longer crashes: a new test reproduced the crash and the physics body now starts one frame later in that case. The feel overlay shows the body state, the build version and the dev keys, and a tuning reload can turn dev tools back on.

The public site got working links between decisions, a builds page that links each commit and its changes, and an Atom feed. The README explains how to set up a clone.

Still open: the larger game features (things that can hit the character, pause and settings, tuning in the Steam build, a get-up motion), four refactors, and the owner's play-test of the movement. Next is a new build on the development branch with these fixes.

## Changes

- e25c706 fix: og secrets fails closed on empty, missing and directory inputs (#21)
- 00702e7 fix: secrets scan covers Steam URLs, steamcmd output and local values (#22)
- 523caab fix: release.ps1 masks steamcmd output on the console and in the log (#23)
- be7e949 fix: release.ps1 refuses test-only switches outside the test harness (#24)
- 0c3c718 fix: a failed release no longer leaves a pushed version bump (#25)
- dc1a385 fix: site rewrites links to .md docs and fails on broken links (#20)
- 807a760 ci: upgrade workflow actions to current majors (#28)
- 6ed55f5 ci: pin workflow actions to commit SHAs (#28)
- cb96d2c ci: enforce uv.lock in CI and the hook, add Dependabot (#42)
- aa0f2bf ci: run the release script tests and pytest on Windows (#26)
- ae95cbd refactor: one og check command for the hook, CI and release gates (#27)
- e7b3557 docs: skills and decisions 0007 and 0010 match decision 0014 (#29)
- 4dce3fb docs: clone setup in the README; session start warns without the hook (#30)
- fd3b7ab fix: tools/ue/env.ps1 reads UE_ROOT and CS_PACKAGE_DIR from .env (#45)
- ca7f3c1 fix: og session start refuses while a session is open (#46)
- a2efc3b fix: og edge cases in decision refs, front matter, site build and errors (#47)
- 0269b4a fix: scan commit messages, file names, issue text and clip OCR (#41)
- f368bb2 test: env.ps1 checks do not depend on the default engine drive (#45)
- 4893005 chore: pin the MCP servers in .mcp.json.example (#44)
- faeacdb docs: export skill status, Game-Systems note and comments without plan refs (#54)
- f80b642 feat: builds page links the commit, the changes and the release notes (#40)
- 791d396 feat: Atom feed for the devlog site (#55)
- b8c8e41 perf: secrets scan batches git reads and counts NULs in C; one git log per session (#31)
- 2a4b7a1 fix: a tuning reload can turn dev tools back on (#48)
- ec8709d fix: a character spawned mid-frame starts its physics body a frame later (#19)
- d9830af feat: feel overlay shows body state, build version and the dev keys (#36)

## Decisions touched

- ADR-0014

## Captures

_No captures._

## Clip

_No clip._
