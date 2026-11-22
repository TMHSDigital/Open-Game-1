# 0001: Record decisions as numbered files, an open-decisions table and Issues

Status: Accepted
Date: 2026-11-22
Issue: none
Supersedes: none

## Context

The project will answer many design and process questions over its life, mostly in conversation between the owner and Claude. Conversations are lost; a public repo is better served by decisions anyone can find, with the reasons attached and the unanswered questions visible.

## Decision

Settled decisions are numbered files in `docs/decisions/NNNN-kebab-title.md` with Status, Date, Issue, Supersedes, Context, Decision, Consequences, Alternatives and Links. Files are never renumbered or deleted; a changed decision gets a new file that supersedes the old one. Unsettled questions are rows `OD-NNN` in `docs/OPEN-DECISIONS.md`, each with a GitHub Issue labelled `decision` where the public can comment. Closing a decision means writing the file, closing the Issue with a link to it and moving the row to the Resolved section. `og decisions` creates the files and Issues from templates and `og decisions check` verifies the table, the files and the Issues agree.

## Consequences

- Reasons stay next to the decision and in git history.
- Open questions are public and can be commented on.
- Drift between the three places is caught by `og decisions check` (offline in CI, online by hand).
- Small cost: each decision needs a short file.

## Alternatives

- Decisions only in Issues: no stable numbering, hard to read as a whole, and tied to GitHub.
- A single running DECISIONS.md: grows without structure and loses the history of changed minds.
- No record: the reasons are lost the next time Claude or the owner starts a fresh session.

## Links

- Tool: `tools/og/decisions.py`
- Open decisions: `docs/OPEN-DECISIONS.md`
