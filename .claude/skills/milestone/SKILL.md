---
name: milestone
description: Tag a Combat Simulator milestone and write its before/after capture page. Use when the user says "milestone", "tag m<N>", or wants a progress comparison against the previous milestone.
allowed-tools: Bash(uv run --project tools python -m og milestone:*) Bash(pwsh tools/ue/capture.ps1:*) Bash(git:*) Read Edit
---

# Milestone

A milestone is the git tag `m<N>-<slug>` plus `docs/milestones/m<N>-<slug>.md`, which shows each camera's latest shot next to the shot from the previous `m*` tag (read from `docs/captures/INDEX.md` at that tag).

1. Make sure the working tree is committed and the latest captures are in `docs/captures/INDEX.md` (run the `session` routine, or the `capture` skill, first if not).
2. Run `uv run --project tools python -m og milestone <N> <slug>`. It creates the annotated tag at `HEAD`, writes the page, scans it for secrets, and commits `docs: milestone m<N>-<slug>`. Use `--no-commit` to review the page first.
3. Add a two or three sentence summary of what changed since the previous milestone at the top of the page. Plain prose, no emojis. Commit it as a new commit; do not move the tag.
4. Do not push the tag unless the user asks.
