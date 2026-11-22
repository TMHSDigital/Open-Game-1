---
name: session
description: Start and end a Combat Simulator work session with a devlog entry. Use when the user says "start a session", "end the session", "wrap up", or asks for a devlog entry. Optionally records the editor with the screencast MCP; works fine without recording or the editor.
allowed-tools: Bash(uv run --project tools python -m og session:*) Bash(pwsh tools/ue/capture.ps1:*) Bash(git:*) Read Edit Grep
---

# Session routine

A session is one goal, one devlog entry at `docs/devlog/<id>-<slug>.md` (`id` is `YYYY-MM-DD-NN`), and optionally one screen recording. State lives in `_scratch/session.json` (gitignored).

Rules:
- Never record without asking the user first. Recording is always opt-in, every session.
- The routine works without recording and without the Unreal editor: skip the steps marked "recording" or "captures" and the entry is still written from the git history.
- Raw recordings never go in git. Only the WebP stills and the clip link are committed; the clip goes to the monthly `media-YYYY-MM` GitHub release (a prerelease that is not a game build) via `og session end --clip`.

## Start

1. Ask for the goal in one line. Ask whether to record this session (yes or no).
2. If yes: screencast `start_recording` with target `window:exe=UnrealEditor.exe` (or `full` if the editor is not the subject). Note the returned session id.
3. Run `uv run --project tools python -m og session start "<goal>"`. Add `--commit` to also create an empty `chore: session <id> start` commit carrying a `Session-Start: <id>` trailer; this lets `end` recover the start point if the state file is lost (for example after a restart).
4. It prints the id, then every open item from `docs/steam/SETUP-CHECKLIST.md` (if that file exists). Mention open items to the user.
5. If recording, write the screencast session id into `_scratch/session.json` as `screencast_session`.

## End

Skip steps 1 to 4 if there is no recording.

1. screencast `stop_recording`.
2. screencast `find_text` with `redact: true` over the recording, then `redact_region` on every hit (tokens, App or Depot IDs next to "Steam", confidentiality banners, Steamworks partner pages, emails).
3. screencast `pipeline` with recipe `pr_demo` to produce a clip of 90 seconds or less.
4. screencast `contact_sheet` of the clip. Save the OCR text for each image next to it as a `.ocr.txt` sidecar (`<name>.ocr.txt`); `og session end` scans those sidecars and refuses to commit an image whose text contains a secret.
5. If the editor is available, capture stills: `pwsh tools/ue/capture.ps1 -Cameras all -OutDir _scratch/capture` (or let `og session end --capture` run it).
6. Run:
   `uv run --project tools python -m og session end --capture --clip <clip.mp4> --contact-sheet <sheet.png> --co-author "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"`
   Leave out `--capture`, `--clip`, `--contact-sheet` when you do not have them (use `--capture-dir <dir>` for stills already rendered). `end` fills Changes and Decisions touched from `git log`, converts stills to `docs/devlog/media/<id>/*.webp`, updates `docs/captures/INDEX.md`, scans every new image and text file, uploads the clip, and commits `docs: session <id> devlog`.
   If it exits non-zero it found a secret: nothing was committed. Fix the source (re-redact, re-capture), then run it again. Never use `--no-verify`.
7. Open the new entry and replace the `<!-- narrative -->` marker with a short paragraph: what was done, what was decided, what comes next. Plain prose, no emojis.
8. Amend nothing. Commit the narrative as a new commit: `docs: session <id> narrative`, ending with the `Co-Authored-By` trailer.

If `end` printed a WARNING that it fell back to `HEAD~1`, keep the warning line in the entry and tell the user the Changes list may be incomplete.
