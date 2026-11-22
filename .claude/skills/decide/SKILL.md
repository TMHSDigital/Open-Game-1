---
name: decide
description: Open, discuss and close project decisions. Use when a design or process question comes up that is not settled (open it), when the owner is weighing options (discuss it), or when the owner has chosen (close it and write the decision record).
---

# Decisions

The record has three parts that must agree: the table `docs/OPEN-DECISIONS.md`, the numbered files in `docs/decisions/`, and one GitHub Issue per open decision, labelled `decision`. Run all commands from the repo root as `uv run --project tools python -m og decisions ...`. Settled rationale: `docs/decisions/0001-record-decisions-as-files-and-issues.md`.

## Open a decision

Use this when a question blocks or shapes work and the owner has not decided it.

```
og decisions new-open "<question>" --why "<why it matters>" --blocks "<what waits on it>" --options "<option a; option b; option c>"
```

This appends the next `OD-NNN` row, creates the `decision` label if needed, creates the public Issue and writes the Issue number into the row. Write for a public reader who has not seen the conversation: the question stands alone, "why" says what goes wrong if it is left open, "blocks" names concrete work, "options" lists what is known so far, separated by semicolons. Commit the table change.

## Discuss

Lay out the options and their costs, ask the owner, and record useful points in the Issue as comments. Do not decide for the owner. If the discussion reveals a new question, open it as its own decision instead of widening this one.

## Close a decision

Only after the owner has stated the decision.

```
og decisions close OD-NNN --title "<short title>" --decision "<what was decided, in plain sentences>"
```

This writes the next numbered file (numbers are never reused or renumbered), moves the row to Resolved with a link to it, and closes the Issue with a comment linking the file. Then open the new file and improve it before committing:

- **Context**: the situation and constraints that forced the choice, not a restatement of the title.
- **Decision**: what we will do, specific enough that someone can tell later whether it was followed.
- **Consequences**: what gets easier, what gets harder, what must now be true. Include the costs.
- **Alternatives**: every real option that was rejected and the reason. This is the part future readers need most.
- **Links**: the Issue and any related decision or OD numbers.

The generated Consequences line is a placeholder; replace it. Reasons matter more than the outcome: write why, not only what.

## Changing a settled decision

Never edit an old decision to say something new. Write a new file by copying `docs/decisions/TEMPLATE.md` with the next number, set `Supersedes: NNNN`, and change the old file's status to `Superseded by NNNN`. If the question needs more discussion first, open an OD row.

## Check

```
og decisions check            # table, files and Issues must agree (needs gh)
og decisions check --offline  # files only; CI uses this
og decisions sync-issues --dry-run   # show which rows lack an Issue
```

Fix every reported problem before committing. Commit messages that touch a decision should mention its `OD-NNN` or ADR number so session devlogs can list it.

Rules: no emojis, no personal names or email addresses, no secrets or Steam IDs in decision files or Issues. These are public.
