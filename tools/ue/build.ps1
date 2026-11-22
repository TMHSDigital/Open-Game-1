# Builds the CombatSimulatorEditor target (the C++ module the editor loads).
#
# A running editor locks the module DLLs, and Live Coding blocks command-line builds, so this script
# first asks any editor that has CombatSimulator open to close. It never force-kills the editor (that
# could lose unsaved work). If the editor is still open after the timeout, the build fails with a message.
#
# Usage: pwsh tools/ue/build.ps1 [-Configuration Development|DebugGame] [-CloseTimeoutSec 60]

param(
    [ValidateSet('Development', 'DebugGame')]
    [string]$Configuration = 'Development',
    [int]$CloseTimeoutSec = 60
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Close-ProjectEditor {
    param([int]$TimeoutSec)
    $editors = Get-ProjectEditorProcess
    if ($editors.Count -eq 0) { return }

    Write-Host "UnrealEditor is running with $ProjectName open. Asking it to close..."
    foreach ($e in $editors) {
        $p = Get-Process -Id $e.ProcessId -ErrorAction SilentlyContinue
        # CloseMainWindow is the same as clicking the window's X button: the editor gets a chance
        # to prompt for unsaved changes instead of being killed.
        if ($p) { [void]$p.CloseMainWindow() }
    }

    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    while ((Get-Date) -lt $deadline) {
        if ((Get-ProjectEditorProcess).Count -eq 0) {
            Write-Host 'Editor closed.'
            return
        }
        Start-Sleep -Seconds 2
    }
    throw "UnrealEditor is still running after $TimeoutSec s (it may be waiting on a 'save changes' dialog). Save and close the editor, then run the build again."
}

try {
    . "$PSScriptRoot\env.ps1"
    Assert-Engine
    Assert-Project

    Close-ProjectEditor -TimeoutSec $CloseTimeoutSec
    Assert-NoHeadlessEditor

    $target = "${ProjectName}Editor"
    Invoke-Native $UBT @($target, 'Win64', $Configuration, "-Project=$Proj", '-WaitMutex', '-FromMsBuild')
    Write-Host "Build succeeded: $target Win64 $Configuration"
}
catch {
    Write-Error $_.Exception.Message -ErrorAction Continue
    Write-Host "UBT logs: $env:LOCALAPPDATA\UnrealBuildTool\Log.txt"
    exit 1
}
