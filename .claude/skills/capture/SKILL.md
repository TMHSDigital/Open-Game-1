---
name: capture
description: Render fixed-camera stills of Combat Simulator maps (named cameras in docs/captures/cameras.json) and convert them to devlog WebP images. Use when the user asks for screenshots or captures, when a devlog needs images of the current build, or when adding a capture camera.
allowed-tools: Bash(pwsh tools/ue/capture.ps1:*) Bash(pwsh tools/ue/test-capture.ps1:*) PowerShell(pwsh tools/ue/:*) Read Grep
---

# Fixed-camera capture

Cameras live in `docs/captures/cameras.json`; published images are listed in `docs/captures/INDEX.md`.

## Run

```powershell
pwsh tools/ue/capture.ps1 -Cameras all -OutDir _scratch/captures/<yyyyMMdd-HHmmss>
pwsh tools/ue/capture.ps1 -Cameras stage-overview,stage-closeup -OutDir _scratch/captures/<yyyyMMdd-HHmmss>
```

- Runs the editor binary in `-game` mode with the dev-only `CSCapture` module, one process per map, with a fixed timestep (`-benchmark -fps=30`). About a minute per map; the first run after an engine or shader change is slower.
- Needs a current editor build (`pwsh tools/ue/build.ps1`); the script checks for `UnrealEditor-CSCapture.dll`. Close the editor first if possible (it competes for the GPU).
- Output: `<camera>.png` per camera at the camera's resolution, and `capture.json` with the full commit SHA, `dirty` flag, date, UE version, the captured cameras and the `skipped` ones (a camera whose map does not exist is skipped, not an error).
- Raw PNGs go to `_scratch/` (gitignored), never to `docs/`.
- Check the result: open each PNG with Read and say what is in it. Black, grey or white images mean the scene was not ready (see failure hints).

## Publish to the devlog

Only WebP goes into git (`docs/devlog/media/`, max 1920 px long side, target 300 KB or less, plain git, not LFS):

```powershell
uv run --project tools python -c "from og.devlog import to_webp; print(to_webp('_scratch/captures/<id>/stage-overview.png', 'docs/devlog/media/<date>-stage-overview.webp'))"
```

`to_webp` scales down (never up) and lowers quality from 90 in steps of 5 to 50 until the file fits. Then add a row to `docs/captures/INDEX.md`: `| date | camera | image path | full 40-char SHA |` (the SHA from `capture.json`). Look at the image before committing: no secrets, tokens, or Steamworks pages in frame.

## Add a camera

1. In PIE (or `-game`), move the camera to the spot and type `cs.Capture.DumpView <name>` in the console (~). The log prints a ready `cameras.json` entry.
2. Paste it into `docs/captures/cameras.json`. Fields: `name` (lowercase kebab-case, unique), `map` (`/Game/...`), `location` `[x,y,z]` cm, `rotation` `[pitch,yaw,roll]` degrees, `fov` (horizontal degrees), `resolution` `[w,h]`, `since` (date added).
   Optional fields (see the header of `tools/ue/capture.ps1`): `hidePlayer` (default true; character cameras set false), `view` (`camera`, or `player` for the player's own view with owner-only visibility, where `rotation` is the aim and `location`/`fov` are unused), `commands` (console commands at the start of the shot, e.g. `ke * SetView FirstPerson`, `ke * EnterRagdoll`), `settleSeconds`. The game mode spawns the character at each map's PlayerStart (L_CaptureStage: feet at (300, 300, 0), yaw 180).
3. Run `pwsh tools/ue/capture.ps1 -Cameras <name> -OutDir _scratch/captures/<id>` and look at the image.

Never rename or delete a camera that has published images; add a new one instead, so images stay comparable across dates.

## Test

```powershell
pwsh tools/ue/test-capture.ps1     # real capture of the seed cameras plus a missing-map camera
pwsh tools/ue/test.ps1 -Filter CombatSimulator.Capture   # job parsing and the editor gate
```

## Shipping

`CSCapture` is `UncookedOnly` in `Game/CombatSimulator.uproject` and listed only in the Editor target, so it is never in a packaged or Shipping build. Gameplay code must never depend on it.

## Failure hints

| Symptom | Fix |
|---|---|
| `CSCapture module binary` not found | `pwsh tools/ue/build.ps1` |
| Run times out or has no result file | Read `Game/Saved/Logs/Capture_<map>.log`; look for `LogCSCapture` errors and shader compilation (raise `-TimeoutSec`). |
| Black, grey or white images | Raise `SettleSeconds` in `tools/ue/capture.ps1` (it changes every later capture: say so in the commit). |
| Wrong image size | The GPU refused the resolution; the log says `exceeds the GPU's maximum texture size`. |
