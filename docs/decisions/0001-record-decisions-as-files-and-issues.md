# 0001: Record decisions as numbered files, an open-decisions table and Issues

Status: Accepted
Date: 2026-11-22
Issue: none
Supersedes: none

## Context

The project will answer many design and process questions over its life. Decisions should be findable with their reasons, and unanswered questions visible and open to public comment.

## Decision

Settled decisions are numbered files in `docs/decisions/NNNN-kebab-title.md` with Status, Date, Issue, Supersedes, Context, Decision, Consequences, Alternatives and Links. Files are never renumbered or deleted; a changed decision gets a new file that supersedes the old one. Unsettled questions are rows `OD-NNN` in `docs/OPEN-DECISIONS.md`, each with a GitHub Issue labelled `decision` where the public can comment. Closing a decision means writing the file, closing the Issue with a link to it and moving the row to the Resolved section. `og decisions` creates the files and Issues from templates and `og decisions check` verifies the table, the files and the Issues agree.

## Consequences

- Reasons stay next to the decision and in git history.
- Open questions are public and can be commented on.
- `og decisions check` verifies the table, the files and the Issues agree.

## Alternatives

Options offered to the owner:

- Repo files plus GitHub Issues (chosen).
- Repo files only: fully versioned, but hard for the public to comment on.
- GitHub Issues or Discussions only: social, but the record lives outside the repo.

## Links

- Tool: `tools/og/decisions.py`
- Open decisions: `docs/OPEN-DECISIONS.md`
