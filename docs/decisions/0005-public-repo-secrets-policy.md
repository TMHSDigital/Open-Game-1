# 0005: Public-repo secrets policy

Status: Accepted
Date: 2026-11-22
Issue: none
Supersedes: none

## Context

The repo is public and the work involves Steamworks, screen recordings and local tooling that handles credentials. A single leaked key, cookie, App or Depot ID, or confidential screenshot is public forever once pushed.

## Decision

Never committed: `.env` and `.env.*` (except `.env.example`), `_scratch/`, `_release/`, `steam/KEYS*`, cookie files, `steam_appid.txt`, `.mcp.json` (`.mcp.json.example` is committed), `Saved/`, `Intermediate/`, `DerivedDataCache/`, `Binaries/` and packaged builds. App and Depot IDs and the build account come from environment variables (`STEAM_APPID`, `STEAM_DEPOT_WIN`, `STEAM_BUILD_ACCOUNT`), never literals in tracked files. `og secrets` scans for Steam keys, tokens, cookies, App and Depot IDs next to Steam keywords, and private email addresses, including images through metadata and OCR sidecars, and fails closed when it cannot read something. It runs in the pre-commit hook and in cloud CI. Steamworks partner pages are confidential to Valve, so screenshots of them are never committed.

## Consequences

- A blocked commit is the intended outcome of a finding, not an error to bypass.
- The scanner can produce false positives; they are resolved by changing the content, not by skipping the hook.
- IDs must be supplied locally for any release work.

## Alternatives

- Rely on `.gitignore` alone: does not catch secrets pasted into tracked files or images.
- Scan only in CI: the secret is already public by the time CI runs.
- A private repo: contradicts the show-the-work goal.

## Links

- Tool: `tools/og/secrets.py`
