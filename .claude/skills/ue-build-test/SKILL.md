---
name: ue-build-test
description: Build the CombatSimulator editor target, run headless automation tests, and package a Windows build. Use after any C++ change, before committing C++ work, or when the user says build, compile, rebuild, run tests, or package.
allowed-tools: Bash(pwsh tools/ue/build.ps1:*) Bash(pwsh tools/ue/test.ps1:*) Bash(pwsh tools/ue/genprojects.ps1:*) Bash(pwsh tools/ue/package.ps1:*) PowerShell(pwsh tools/ue/:*) Read Grep
---

# Build and test CombatSimulator

The Unreal project is `Game/CombatSimulator.uproject`. All scripts live in `tools/ue/` and share paths from `tools/ue/env.ps1` (engine root overridable with `UE_ROOT`; run `pwsh tools/ue/env.ps1` to print and check the configuration).

## Before building

1. Check `Game/CombatSimulator.uproject` exists. If not, the project has not been created yet; stop and say so.
2. Decide whether this is a **header change**: any edit to a `.h` file, or any new or changed `UPROPERTY`, `UFUNCTION`, `UCLASS`, `USTRUCT`, `UENUM`. Header changes need the editor closed and a full build. Live Coding is only safe for `.cpp` function bodies.
3. If C++ files were added, removed or renamed, or a `.Build.cs` changed, run `pwsh tools/ue/genprojects.ps1` first.

## Build

```powershell
pwsh tools/ue/build.ps1
```

- The script asks a running editor with CombatSimulator open to close (like clicking X) and fails after 60 s if it is still open (usually a "save changes" dialog). Tell the human to save and close it; do not kill the process.
- On failure: read the output (and `%LOCALAPPDATA%\UnrealBuildTool\Log.txt` if the console output is cut off). Find the **first** `error` line. Fix only that one, rebuild, repeat. Later errors are often caused by the first.

Common first errors and what they mean:

| Error text | Usual cause |
|---|---|
| `Unable to find 'class', 'delegate', 'enum', or 'struct'` (UHT) | Missing or misplaced `GENERATED_BODY()`, or `.generated.h` not the last include |
| `LNK2019: unresolved external symbol` | Module missing from `CombatSimulator.Build.cs`, or a declared function has no definition |
| `UFUNCTION ... Server ... _Implementation` missing | Server RPC declared without `Foo_Implementation` (and `Foo_Validate` with `WithValidation`) |
| `cannot open include file` | Wrong include path, or module dependency missing |
| `Unable to build while Live Coding is active` | Editor still open with Live Coding; close it |

## Test

```powershell
pwsh tools/ue/test.ps1                                  # all tests under CombatSimulator.
pwsh tools/ue/test.ps1 -Filter CombatSimulator.Smoke
pwsh tools/ue/test.ps1 -Build                           # build first
```

- Runs `UnrealEditor-Cmd.exe` headless (`-nullrhi`, no window). Results are in `Game/Saved/TestReports/index.json`; the log is `Game/Saved/Logs/AutomationTests.log`.
- Refuses to run if any source file is newer than its editor DLL (results would be for old code).
- Prints `N passed, M failed` and exits non-zero if any test fails or if no test matched the filter.
- Summarize failures by full test name and the first error message of each.

## Package

```powershell
pwsh tools/ue/package.ps1 -Config Development    # or Shipping
```

- Build + cook + stage + pak + archive into `E:\CombatSimulatorBuilds\Windows` (outside the repo; override with `CS_PACKAGE_DIR`, which must end in `\Windows`). Takes many minutes; run it in the background.
- Stdout is only the packaged directory path; UAT output goes to stderr.
- Smoke-check the result: `& "<dir>\CombatSimulator.exe" -nullrhi -ExecCmds=quit` should exit 0.

## Editor and MCP

`pwsh tools/ue/editor.ps1` starts the editor and waits for the Unreal MCP endpoint (`http://127.0.0.1:8000/mcp`).

## Done means

Build exits 0 **and** tests exit 0. Report both. Do not claim success from a build alone.
