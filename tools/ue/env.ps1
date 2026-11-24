# Single source of truth for engine and project paths.
#
# Other scripts dot-source this file (". $PSScriptRoot\env.ps1") so they share the same variables.
# Run it directly (pwsh tools/ue/env.ps1) to print the configuration and check that the engine exists.
#
# Overrides (no need to edit this file), as environment variables or as lines in the repo's
# gitignored .env file (the environment variable wins):
#   UE_ROOT         engine location (default E:\UE_5.8)
#   CS_PACKAGE_DIR  packaged build output (default E:\CombatSimulatorBuilds\Windows; must end in \Windows)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$DefaultUeRoot     = 'E:\UE_5.8'
$DefaultPackageDir = 'E:\CombatSimulatorBuilds\Windows'

$RepoRoot    = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path

# A machine setting by name: the environment variable if set, else that key's line in a .env file
# (default: the repo's), else nothing. Only the named key is read and nothing is printed.
function Get-LocalSetting([string]$Name, [string]$EnvFile = (Join-Path $RepoRoot '.env')) {
    $value = [Environment]::GetEnvironmentVariable($Name)
    if ($value) { return $value }
    if (Test-Path -LiteralPath $EnvFile) {
        foreach ($line in Get-Content -LiteralPath $EnvFile) {
            if ($line -match "^\s*$([regex]::Escape($Name))\s*=\s*(.*?)\s*$") {
                $value = $Matches[1].Trim('"', "'")
                if ($value) { return $value }
            }
        }
    }
    $null
}

$UeRoot      = Get-LocalSetting 'UE_ROOT'
if (-not $UeRoot) { $UeRoot = $DefaultUeRoot }
$ProjectName = 'CombatSimulator'
# The Unreal project lives in Game/, not at the repo root.
$GameDir     = Join-Path $RepoRoot 'Game'
$Proj        = Join-Path $GameDir "$ProjectName.uproject"

# UBT = UnrealBuildTool (compiles C++). UAT = AutomationTool (cooks and packages builds).
$UBT       = Join-Path $UeRoot 'Engine\Build\BatchFiles\Build.bat'
$UAT       = Join-Path $UeRoot 'Engine\Build\BatchFiles\RunUAT.bat'
$Editor    = Join-Path $UeRoot 'Engine\Binaries\Win64\UnrealEditor.exe'
# The -Cmd variant is a console build of the editor, used for headless tests and commandlets.
$EditorCmd = Join-Path $UeRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'

$McpUrl      = 'http://127.0.0.1:8000/mcp'
# Packaged builds go outside the repo: they are large and never committed.
$PackageDir  = Get-LocalSetting 'CS_PACKAGE_DIR'
if (-not $PackageDir) { $PackageDir = $DefaultPackageDir }
$TestReports = Join-Path $GameDir 'Saved\TestReports'
$LogsDir     = Join-Path $GameDir 'Saved\Logs'

# "5.8.3-58210709" from the engine's Build.version, for capture metadata.
function Get-UeVersion {
    $buildVersion = Join-Path $UeRoot 'Engine\Build\Build.version'
    if (-not (Test-Path -LiteralPath $buildVersion)) { return 'unknown' }
    $v = Get-Content -LiteralPath $buildVersion -Raw | ConvertFrom-Json
    "$($v.MajorVersion).$($v.MinorVersion).$($v.PatchVersion)-$($v.Changelist)"
}

function Assert-PathExists {
    param([Parameter(Mandatory)][string]$Path, [Parameter(Mandatory)][string]$What)
    if (-not (Test-Path -LiteralPath $Path)) {
        throw "$What not found at '$Path'."
    }
}

function Assert-Engine {
    Assert-PathExists $UeRoot 'Unreal Engine root (set UE_ROOT or edit $DefaultUeRoot in tools/ue/env.ps1)'
    Assert-PathExists $UBT 'Build.bat'
    Assert-PathExists $Editor 'UnrealEditor.exe'
}

function Assert-Project {
    Assert-PathExists $Proj "$ProjectName.uproject"
}

# Runs a native program (exe or bat) and throws if it returns a non-zero exit code.
# PowerShell does not stop on native exit codes by itself, so every external call goes through here.
# -ToStdErr sends the command line and the program's output to stderr, so a script whose stdout is
# a result (package.ps1 prints the packaged dir) is not polluted by tool logs. (Write-Host is not
# enough: when pwsh runs a script as a child process, host output lands on its stdout.)
function Invoke-Native {
    param(
        [Parameter(Mandatory)][string]$FilePath,
        [string[]]$ArgumentList = @(),
        [switch]$ToStdErr
    )
    $line = ">> $FilePath $($ArgumentList -join ' ')"
    if ($ToStdErr) {
        [Console]::Error.WriteLine($line)
        & $FilePath @ArgumentList | ForEach-Object { [Console]::Error.WriteLine($_) }
    }
    else {
        Write-Host $line
        & $FilePath @ArgumentList
    }
    if ($LASTEXITCODE -ne 0) {
        throw "'$([IO.Path]::GetFileName($FilePath))' failed with exit code $LASTEXITCODE."
    }
}

# Returns UnrealEditor processes that have this project open. -Headless returns the console
# variant instead (UnrealEditor-Cmd.exe: test.ps1, commandlets), which locks the module DLLs just
# the same but has no window to close.
function Get-ProjectEditorProcess {
    param([switch]$Headless)
    $exe = if ($Headless) { 'UnrealEditor-Cmd.exe' } else { 'UnrealEditor.exe' }
    $all = Get-CimInstance Win32_Process -Filter "Name = '$exe'" -ErrorAction SilentlyContinue
    # The leading comma stops PowerShell unrolling an empty array to $null, which would
    # make callers' .Count throw under Set-StrictMode.
    , @($all | Where-Object { $_.CommandLine -and $_.CommandLine -like "*$ProjectName.uproject*" })
}

# Throws if a headless editor has this project open (it would make a build fail with "file in use").
function Assert-NoHeadlessEditor {
    if ((Get-ProjectEditorProcess -Headless).Count -gt 0) {
        throw "A headless editor (UnrealEditor-Cmd: tests or a commandlet) has $ProjectName open and locks the module DLLs. Wait for it to finish."
    }
}

# Throws if any module's source (Game/Source/<Module>/** or Game/Plugins/<Plugin>/Source/<Module>/**; .h, .cpp, .cs) is newer than its editor
# DLL, so tests never report results for old code. Returns the DLL files checked.
function Assert-BinariesFresh {
    $checked = @()
    # Game modules build into Game/Binaries; plugin modules into Game/Plugins/<Plugin>/Binaries.
    $sources = @(Get-ChildItem (Join-Path $GameDir 'Source') -Recurse -Filter '*.Build.cs' |
        ForEach-Object { [pscustomobject]@{ BuildCs = $_; BinDir = Join-Path $GameDir 'Binaries\Win64' } })
    $pluginsDir = Join-Path $GameDir 'Plugins'
    if (Test-Path -LiteralPath $pluginsDir) {
        foreach ($uplugin in Get-ChildItem $pluginsDir -Recurse -Filter '*.uplugin') {
            $sources += @(Get-ChildItem (Join-Path $uplugin.DirectoryName 'Source') -Recurse -Filter '*.Build.cs' -ErrorAction SilentlyContinue |
                ForEach-Object { [pscustomobject]@{ BuildCs = $_; BinDir = Join-Path $uplugin.DirectoryName 'Binaries\Win64' } })
        }
    }
    foreach ($src in $sources) {
        $buildCs = $src.BuildCs
        $module = $buildCs.Name -replace '\.Build\.cs$', ''
        $dll = Get-Item -LiteralPath (Join-Path $src.BinDir "UnrealEditor-$module.dll") -ErrorAction SilentlyContinue
        if (-not $dll) { throw "No UnrealEditor-$module.dll in $($src.BinDir). Run tools/ue/build.ps1 (or test.ps1 -Build)." }
        $newest = Get-ChildItem $buildCs.DirectoryName -Recurse -File -Include '*.h', '*.cpp', '*.cs' |
            Sort-Object LastWriteTime -Descending | Select-Object -First 1
        if ($newest -and $newest.LastWriteTime -gt $dll.LastWriteTime) {
            throw "Binaries older than source: $($newest.Name) ($($newest.LastWriteTime)) is newer than $($dll.Name) ($($dll.LastWriteTime)). Run tools/ue/build.ps1 (or test.ps1 -Build)."
        }
        $checked += $dll
    }
    , $checked
}

# When run directly (not dot-sourced), print the configuration and validate the engine.
if ($MyInvocation.InvocationName -ne '.') {
    Write-Host "UeRoot     : $UeRoot"
    Write-Host "RepoRoot   : $RepoRoot"
    Write-Host "Project    : $Proj"
    Write-Host "UBT        : $UBT"
    Write-Host "UAT        : $UAT"
    Write-Host "EditorCmd  : $EditorCmd"
    Write-Host "PackageDir : $PackageDir"
    try {
        Assert-Engine
        Write-Host 'Engine     : OK'
    }
    catch {
        Write-Error $_.Exception.Message -ErrorAction Continue
        exit 1
    }
    if (Test-Path -LiteralPath $Proj) { Write-Host 'Project    : OK' } else { Write-Host 'Project    : not created yet' }
}
