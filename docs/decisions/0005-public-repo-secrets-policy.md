# 0005: Public-repo secrets policy

Status: Accepted
Date: 2026-11-22
Issue: none
Supersedes: none

## Context

The repo is public and the work involves Steamworks, screen recordings and local tooling that handles credentials.

## Decision

Never committed: `.env` and `.env.*` (except `.env.example`), `_scratch/`, `_release/`, `steam/KEYS*`, cookie files, `steam_appid.txt`, `.mcp.json` (`.mcp.json.example` is committed), `Saved/`, `Intermediate/`, `DerivedDataCache/`, `Binaries/` and packaged builds. App and Depot IDs and the build account come from environment variables (`STEAM_APPID`, `STEAM_DEPOT_WIN`, `STEAM_BUILD_ACCOUNT`), never literals in tracked files. `og secrets` scans for Steam keys, tokens, cookies, App and Depot IDs next to Steam keywords, and private email addresses, including images through metadata and OCR sidecars, and fails closed when it cannot read something. It runs in the pre-commit hook and in cloud CI. If the instructions Unreal MCP generates in `DefaultGame.ini` contain nothing private they are committed; otherwise they are kept in a local ini override. Steamworks partner pages are confidential to Valve, so screenshots of them are never committed.

## Consequences

- A blocked commit is the intended outcome of a finding.
- IDs must be supplied locally through environment variables for any release work.

## Alternatives

No alternatives were weighed; the owner set this directly.

## Links

- Tool: `tools/og/secrets.py`
