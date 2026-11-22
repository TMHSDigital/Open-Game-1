# Runs automation tests headless (no window, no GPU) and fails if any test fails or none ran.
#
# Unreal's automation framework discovers tests by name, for example "CombatSimulator.Smoke.ProjectLoads".
# The filter matches by prefix, so the default runs every test under "CombatSimulator.".
#
# Refuses to run when a module's source is newer than its DLL (the results would be for old code);
# -Build runs tools/ue/build.ps1 first.
#
# Usage: pwsh tools/ue/test.ps1 [-Filter CombatSimulator.Smoke] [-Build]

param(
    [string]$Filter = 'CombatSimulator.',
    [switch]$Build
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

try {
    . "$PSScriptRoot\env.ps1"
    Assert-Engine
    Assert-PathExists $EditorCmd 'UnrealEditor-Cmd.exe'
    Assert-Project

    if ((Get-ProjectEditorProcess).Count -gt 0) {
        throw "UnrealEditor has $ProjectName open. Close it first (it locks the module DLLs)."
    }
    Assert-NoHeadlessEditor

    if ($Build) {
        & pwsh -NoProfile -File (Join-Path $PSScriptRoot 'build.ps1')
        if ($LASTEXITCODE -ne 0) { throw 'Build failed; not running tests against old binaries.' }
    }
    $dlls = Assert-BinariesFresh

    # Delete the previous report so a crash cannot leave stale results looking like a pass.
    if (Test-Path -LiteralPath $TestReports) { Remove-Item -LiteralPath $TestReports -Recurse -Force }

    $edArgs = @(
        $Proj,
        "-ExecCmds=Automation RunTests $Filter",
        '-TestExit=Automation Test Queue Empty',
        "-ReportExportPath=$TestReports",
        '-unattended', '-nullrhi', '-nosplash', '-nosound', '-NoP4',
        '-stdout', '-FullStdOutLogOutput', '-log=AutomationTests.log'
    )
    Write-Host ">> $EditorCmd $($edArgs -join ' ')"
    & $EditorCmd @edArgs
    $editorExit = $LASTEXITCODE

    $index = Join-Path $TestReports 'index.json'
    if (-not (Test-Path -LiteralPath $index)) {
        throw "No test report at $index (editor exit code $editorExit). Check $LogsDir\AutomationTests.log."
    }

    $report = Get-Content -LiteralPath $index -Raw | ConvertFrom-Json
    $tests  = @($report.tests)
    $failed = @($tests | Where-Object { $_.state -ne 'Success' })

    Write-Host ''
    foreach ($dll in $dlls) { Write-Host "Binary: $($dll.Name) built $($dll.LastWriteTime)" }
    Write-Host "$($tests.Count - $failed.Count) passed, $($failed.Count) failed"
    foreach ($t in $failed) {
        Write-Host "FAILED: $($t.fullTestPath) ($($t.state))"
        foreach ($entry in @($t.entries)) {
            if ($entry.event.type -eq 'Error') { Write-Host "    $($entry.event.message)" }
        }
    }

    if ($tests.Count -eq 0) { throw "No tests matched filter '$Filter'." }
    if ($failed.Count -gt 0) { throw "$($failed.Count) test(s) failed." }
    Write-Host 'All tests passed.'
}
catch {
    Write-Error $_.Exception.Message -ErrorAction Continue
    exit 1
}
