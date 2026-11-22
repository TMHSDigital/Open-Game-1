---
name: steam-store-readiness
description: Check how close the Combat Simulator Steam store page is to going live (Coming Soon, Next Fest) and what is blocking it, from the repo side. Reads the setup checklist and build ledger, lists missing store work and the next owner actions. Use when the user asks about the store page, Next Fest readiness, what Steam still needs, or store assets status.
---

# Steam store readiness

Adapted from the Steam-Cursor-Plugin `steam-store-readiness` skill. This repo has no store asset tracker yet; this skill reports from what exists and says what is missing.

## Steps

1. Run `pwsh tools/steam/release.ps1 status` and read the open items from `docs/steam/SETUP-CHECKLIST.md` and the last ledger rows.
2. Verify what can be verified from the repo:
   - Steam env vars: do not read `.env`. Run `pwsh tools/steam/release.ps1 dry-run -SkipPackage` and read gate 9: it names missing variables without printing values.
   - Build: `docs/steam/BUILDS.md` has a row live on `development` (and later on `default`).
   - Store copy and capsule art: none tracked in the repo yet. If the owner wants them drafted, ask where they should live first (store art is an asset and enters only through the `asset-import` skill).
3. Report: what is done, what is blocking Coming Soon, what is blocking Next Fest, and the next 3 actions with owners (owner or Claude).

## Notes

- Coming Soon needs: the app published, store page text, capsules, at least 5 screenshots, and a passed store review (2 to 3 business days). A trailer is strongly recommended.
- Next Fest needs a public store page by the registration deadline, and a demo build reviewed in time.
- Steamworks partner pages carry Valve's confidentiality marking: never save or commit screenshots of them.
- Claude may draft store copy into files the owner picks; the owner pastes it into Steamworks. There is no publish tool and Claude never logs in to the partner site.
