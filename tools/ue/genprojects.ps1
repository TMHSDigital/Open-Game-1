# Generates Visual Studio / Rider project files (Game/CombatSimulator.sln) from Game/CombatSimulator.uproject.
# Re-run after adding or removing C++ files, or after changing a .Build.cs file.
#
# Usage: pwsh tools/ue/genprojects.ps1

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

try {
    . "$PSScriptRoot\env.ps1"
    Assert-Engine
    Assert-Project

    # -rocket tells UBT this is an installed (Launcher) engine, not a source build.
    Invoke-Native $UBT @('-projectfiles', "-project=$Proj", '-game', '-rocket', '-progress')
    Write-Host 'Project files generated.'
}
catch {
    Write-Error $_.Exception.Message -ErrorAction Continue
    exit 1
}
