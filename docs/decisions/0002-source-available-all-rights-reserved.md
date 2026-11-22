# 0002: Source-available, all rights reserved

Status: Accepted
Date: 2026-11-22
Issue: none
Supersedes: none

## Context

The repo is public so the process can be watched, but the game may be sold and the owner has not decided how modders will be licensed. A permissive or copyleft license would give away options that cannot be taken back.

## Decision

`LICENSE` states that the source is available to read, that all rights are reserved, and that viewing on GitHub grants no other rights. Forking on GitHub under GitHub's terms grants no license to use the code or art. Third-party assets keep their own licenses, recorded in the asset ledger (see 0004). Mod terms are a separate grant that comes later and are tracked as OD-002. Contributor terms are not needed while there are no outside contributors (OD-011).

## Consequences

- The code and art cannot be reused by others without permission, while the work stays fully visible.
- Mod terms can be written later without conflicting with an existing grant.
- This is not an open source license, and the README says so.
- Accepting outside contributions later needs its own decision (OD-011).

## Alternatives

- An open source license (MIT, Apache-2.0): irrevocable, and would permit others to sell the game.
- A copyleft license (GPL family): incompatible with combining engine content and with a possible sale.
- A non-commercial source license such as CC BY-NC: would not fit a game that may be sold.

## Links

- Open decisions: OD-002 (mod license terms), OD-011 (outside contributions)
