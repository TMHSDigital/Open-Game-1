# Cooks and packages a playable Windows build into $PackageDir (default E:\CombatSimulatorBuilds\Windows,
# outside the repo; override with CS_PACKAGE_DIR).
#
# "Cooking" converts editor assets into the compact runtime format the shipped game loads.
# BuildCookRun does build + cook + stage + pak + archive in one command.
#
# stdout is only the packaged directory path (all tool output goes to stderr), so callers
# can do: $dir = pwsh tools/ue/package.ps1 -Config Development
#
# Usage: pwsh tools/ue/package.ps1 [-Config Development|Shipping]

param(
    [ValidateSet('Development', 'Shipping')]
    [string]$Config = 'Development'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

try {
    . "$PSScriptRoot\env.ps1"
    Assert-Engine
    Assert-PathExists $UAT 'RunUAT.bat'
    Assert-Project

    # UAT archives into <archivedirectory>\Windows, so archive into the parent of $PackageDir.
    if ((Split-Path -Leaf $PackageDir) -ne 'Windows') {
        throw "PackageDir '$PackageDir' must end in \Windows (UAT appends the platform folder to the archive directory)."
    }
    $archiveRoot = Split-Path -Parent $PackageDir
    New-Item -ItemType Directory -Force -Path $archiveRoot | Out-Null

    Invoke-Native -ToStdErr $UAT @(
        'BuildCookRun',
        "-project=$Proj",
        '-platform=Win64',
        "-clientconfig=$Config",
        '-build', '-cook', '-stage', '-pak', '-archive',
        "-archivedirectory=$archiveRoot",
        '-utf8output', '-unattended', '-nop4'
    )

    $exe = Join-Path $PackageDir "$ProjectName.exe"
    if (-not (Test-Path -LiteralPath $exe)) {
        throw "UAT succeeded but $exe was not produced."
    }
    [Console]::Error.WriteLine("Packaged $Config build written to $PackageDir")
    Write-Output $PackageDir
}
catch {
    Write-Error $_.Exception.Message -ErrorAction Continue
    exit 1
}
