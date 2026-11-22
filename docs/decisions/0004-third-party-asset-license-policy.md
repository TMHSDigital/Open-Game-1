# 0004: Third-party asset license policy

Status: Accepted
Date: 2026-11-22
Issue: none
Supersedes: none

## Context

Free assets speed up a solo build, but the game may be sold, and Unreal's EULA rules out copyleft material combined with the engine. A policy is needed before any asset is imported, plus a way to prove it was followed.

## Decision

Allowed: CC0, public domain equivalents, CC-BY-* (with credits), MIT, BSD-*, Apache-2.0 and OFL-1.1 (fonts). Refused: any `-NC`, `-ND` or `-SA` variant, and GPL, LGPL and AGPL; a blank license is refused and an unrecognised one is flagged, never allowed. Every imported asset has a row in `assets/THIRD_PARTY.csv` (id, name, source URL, author, SPDX license, license URL, verified date, credit line, local paths, catalog entry ID). `CREDITS.md` is generated from the ledger. `og assets check` fails when a file under a third-party root lacks a ledger row or a row has a refused license; it runs in the pre-commit hook and in cloud CI. Sources are the owner's Free-Game-Dev-Assets catalog (read, never vendored) or the asset's original source.

## Consequences

- Every asset has provenance and a credit line, and a bad license is caught before it reaches a commit.
- Some attractive assets are off limits.
- The catalog is a reading of the license on a verified date; the source is checked again at import.

## Alternatives

- Allow any free asset: risks an NC or GPL asset in a game that may be sold.
- Allow NC assets while unsold: the decision to sell is open, so the problem would arrive late and expensively.
- No ledger, rely on memory: provenance and credits cannot be reproduced.

## Links

- Policy document: `docs/assets/ASSET-POLICY.md`
- Tool: `tools/og/assets.py`
