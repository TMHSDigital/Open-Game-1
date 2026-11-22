# Gated Steam release for Combat Simulator, and Steam branch tracking. One entry point:
#
#   pwsh tools/steam/release.ps1 dry-run            gates, package, stage, VDFs; prints the steamcmd line
#   pwsh tools/steam/release.ps1 dev                gates, bump version, package, upload, set live on development,
#                                                   ledger row, GitHub prerelease, steam-development deployment
#   pwsh tools/steam/release.ps1 upload-only        the same with no SetLive and no deployment (first build)
#   pwsh tools/steam/release.ps1 preview            gates, package, stage, steamcmd with Preview 1 (nothing uploaded)
#   pwsh tools/steam/release.ps1 status             ledger versus Steam, plus open SETUP-CHECKLIST items
#   pwsh tools/steam/release.ps1 promote <ver>      drafts the default-branch promotion into _release/<ver>/
#   pwsh tools/steam/release.ps1 promoted <ver>     records a promotion the owner made in Steamworks
#
# Switches: -SkipPackage (reuse the last package), -Notes "text" (secret-scanned in gate 7).
# -Branch exists only to be refused: anything but 'development' exits 40.
# Test overrides: -BuildDir -LedgerPath -SteamCmd -SkipGitGates -NoGitHub -RepoDir -WorkDir -CheckCommand.
#
# Exit codes: 0 ok, 1 other error, 10+n gate n failed, 30 steamcmd error, 31 no BuildID,
#             40 refused (a -Branch other than development, or CLAUDECODE set for a real Steam step).
#
# The owner runs every real Steam step (dev, upload-only, preview, promoted) in their own terminal:
# the script refuses them when CLAUDECODE is set. No script ever sets the default branch live.
#
# IDs come from environment variables. If one is not set, only these keys are read from .env:
# STEAM_APPID, STEAM_DEPOT_WIN, STEAM_BUILD_ACCOUNT, STEAMCMD_PATH. Values are never printed.
# No password is stored anywhere: steamcmd asks for it (and Steam Guard) on first login and caches a
# session in its own folder.

param(
    [Parameter(Position = 0)][string]$Mode,
    [Parameter(Position = 1)][string]$Version,
    [switch]$SkipPackage,
    [string]$Notes = '',
    [string]$Branch = 'development',
    # Test overrides (tools/steam/test-release.ps1).
    [string]$BuildDir,
    [string]$LedgerPath,
    [string]$SteamCmd,
    [switch]$SkipGitGates,   # skips gates 1-3 and every git commit and push
    [switch]$NoGitHub,       # skips every gh call
    [string]$RepoDir,
    [string]$WorkDir,
    [string]$CheckCommand    # replaces gates 6-8 and the promoted draft scan with:
                             # pwsh -File <CheckCommand> tests|secrets|assets (real command in $env:RELEASE_REAL_CHECK)
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$SteamEnvKeys = 'STEAM_APPID', 'STEAM_DEPOT_WIN', 'STEAM_BUILD_ACCOUNT', 'STEAMCMD_PATH'
$RequiredEnvKeys = 'STEAM_APPID', 'STEAM_DEPOT_WIN', 'STEAM_BUILD_ACCOUNT'
$LedgerColumns = 'version', 'build_id', 'git_sha', 'uploaded_utc', 'development_live_utc', 'development_replaced_utc', 'default_live_utc', 'default_replaced_utc', 'notes'
$Modes = 'dev', 'upload-only', 'dry-run', 'preview', 'status', 'promote', 'promoted'
$script:ExitCode = 1

# --- Helpers (dot-sourced by the test) ------------------------------------------------------------

# Sets allowlisted keys from a .env file into this process only, without printing them, and only
# where the environment does not already have a value.
function Import-SteamEnv([string]$EnvFile) {
    if (-not (Test-Path -LiteralPath $EnvFile)) { return }
    foreach ($line in Get-Content -LiteralPath $EnvFile) {
        if ($line -match '^\s*([A-Z0-9_]+)\s*=\s*(.*?)\s*$' -and $Matches[1] -in $SteamEnvKeys) {
            $key = $Matches[1]; $value = $Matches[2].Trim('"', "'")
            if ($value -and -not [Environment]::GetEnvironmentVariable($key)) {
                [Environment]::SetEnvironmentVariable($key, $value, 'Process')
            }
        }
    }
}

# Returns a list of problems with the App and Depot IDs (empty when usable for an upload).
# Messages name the variables, never their values.
function Test-SteamIds([string]$AppId, [string]$DepotId) {
    $problems = @()
    if ($AppId -notmatch '^\d+$') { $problems += 'STEAM_APPID is not a numeric App ID.' }
    elseif ($AppId -eq '480') { $problems += 'STEAM_APPID is 480 (Spacewar), the shared test app. It cannot receive uploads.' }
    if ($DepotId -notmatch '^\d+$') { $problems += 'STEAM_DEPOT_WIN is not a numeric Depot ID.' }
    elseif ($DepotId -eq $AppId) { $problems += 'STEAM_DEPOT_WIN equals STEAM_APPID; the depot has its own ID.' }
    return , $problems
}

# Returns $null when the Steam branch is usable, else why it is refused. The ledger only has
# development and default columns, and no script sets default live, so only 'development' passes.
function Test-ReleaseBranch([string]$Name) {
    $b = $Name.Trim()
    if ($b -in 'default', 'public') { return "Refusing to set the default branch live. Upload to development, test, then promote by hand in Steamworks (release.ps1 promote drafts the steps)." }
    if ($b -cnotmatch '^[a-z0-9_-]+$') { return "Steam branch '$b' is not a valid name (lowercase letters, digits, '_' and '-')." }
    if ($b -cne 'development') { return "Steam branch '$b' is refused: releases go live on 'development' only (the ledger tracks development and default)." }
    $null
}

function Stop-Release([int]$Code, [string]$Message) {
    $script:ExitCode = $Code
    throw $Message
}

function Get-ProjectVersion([string]$GameIni) {
    $line = Select-String -LiteralPath $GameIni -Pattern '^\s*ProjectVersion\s*=\s*(\S+)' | Select-Object -First 1
    if (-not $line) { throw "No ProjectVersion in $GameIni." }
    $line.Matches[0].Groups[1].Value
}

function Set-ProjectVersion([string]$GameIni, [string]$NewVersion) {
    $text = [IO.File]::ReadAllText($GameIni)
    $text = [regex]::Replace($text, '(?m)^(\s*ProjectVersion\s*=\s*)\S+', "`${1}$NewVersion", 1)
    [IO.File]::WriteAllText($GameIni, $text, [Text.UTF8Encoding]::new($false))
}

# 0.<milestone>.<build>: the next release bumps the build number.
function Get-NextVersion([string]$Current) {
    if ($Current -notmatch '^0\.(\d+)\.(\d+)$') { throw "ProjectVersion '$Current' is not shaped 0.<milestone>.<build>." }
    "0.$($Matches[1]).$([int]$Matches[2] + 1)"
}

function Get-UtcNow { (Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ssZ') }

# Ledger: the markdown table in docs/steam/BUILDS.md. Rows keep their line index so a rewrite
# changes only the table rows.
function Read-Ledger([string]$Path) {
    $lines = [System.Collections.Generic.List[string]]::new()
    if (Test-Path -LiteralPath $Path) { foreach ($l in Get-Content -LiteralPath $Path) { $lines.Add($l) } }
    $rows = [System.Collections.Generic.List[object]]::new()
    for ($i = 0; $i -lt $lines.Count; $i++) {
        $cells = @($lines[$i] -split '\|' | ForEach-Object { $_.Trim() })
        if ($cells.Count -ge ($LedgerColumns.Count + 2) -and $cells[1] -match '^\d+\.\d+\.\d+$') {
            $row = [ordered]@{ Line = $i }
            for ($c = 0; $c -lt $LedgerColumns.Count; $c++) { $row[$LedgerColumns[$c]] = $cells[$c + 1] }
            $rows.Add([pscustomobject]$row)
        }
    }
    [pscustomobject]@{ Path = $Path; Lines = $lines; Rows = $rows }
}

function Format-LedgerRow($Row) {
    $vals = foreach ($c in $LedgerColumns) { ([string]$Row.$c) -replace '\|', '/' -replace '[\r\n]+', ' ' }
    '| ' + ($vals -join ' | ') + ' |'
}

function Write-Ledger($Ledger) {
    foreach ($row in $Ledger.Rows) {
        $text = Format-LedgerRow $row
        if ($row.Line -ge 0) { $Ledger.Lines[$row.Line] = $text }
        else { $Ledger.Lines.Add($text); $row.Line = $Ledger.Lines.Count - 1 }
    }
    Set-Content -LiteralPath $Ledger.Path -Value $Ledger.Lines -Encoding utf8
}

function New-LedgerRow([hashtable]$Values) {
    $row = [ordered]@{ Line = -1 }
    foreach ($c in $LedgerColumns) { $row[$c] = if ($Values.ContainsKey($c)) { [string]$Values[$c] } else { '' } }
    [pscustomobject]$row
}

# The build currently live on a Steam branch according to the ledger ('development' or 'default').
function Get-LiveRow($Ledger, [string]$SteamBranch) {
    $live = "${SteamBranch}_live_utc"; $replaced = "${SteamBranch}_replaced_utc"
    @($Ledger.Rows | Where-Object { $_.$live -and -not $_.$replaced } | Sort-Object { $_.$live }) | Select-Object -Last 1
}

# Numbered lines whose italic status does not start with Done (same rule as `og session start`).
function Get-OpenChecklistItems([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path)) { return }
    $items = @()
    foreach ($line in Get-Content -LiteralPath $Path) {
        if ($line -notmatch '^\s*\d+\.\s') { continue }
        $m = [regex]::Match($line, '\*([A-Za-z][^*]*)\*')
        if ($m.Success -and $m.Groups[1].Value.Trim().ToLower().StartsWith('done')) { continue }
        $items += $line.Trim()
    }
    $items
}

function Get-BuildIdFromLog([string[]]$Lines) {
    $m = $Lines | Select-String -Pattern 'BuildID\s+(\d+)' | Select-Object -Last 1
    if ($m) { $m.Matches[0].Groups[1].Value } else { $null }
}

# True when a file from the package archive must not be staged. The archive dir is not cleaned
# between configs, so besides symbols, logs and run output it can hold a stale non-Shipping exe.
function Test-StagingExcluded([string]$RelPath, [string]$Project) {
    $name = Split-Path -Leaf $RelPath
    $segments = @($RelPath -split '[\\/]')
    if ([IO.Path]::GetExtension($name) -in '.pdb', '.sym', '.psym', '.debug', '.dmp', '.log') { return $true }
    if ($name -eq 'steam_appid.txt' -or $name -like 'Manifest_*.txt') { return $true }
    if ($segments -contains 'Saved' -or $segments -contains 'Intermediate') { return $true }
    if ($segments -contains 'Binaries' -and $name -match "^$([regex]::Escape($Project))(-Win64-\w+)?\.exe$" -and $name -ne "$Project-Win64-Shipping.exe") { return $true }
    $false
}

# Copies the Shipping package into an (already fresh) staging dir, skipping excluded files.
function Copy-PackageToStaging([string]$From, [string]$To, [string]$Project) {
    $ship = Join-Path $From "$Project\Binaries\Win64\$Project-Win64-Shipping.exe"
    if (-not (Test-Path -LiteralPath (Join-Path $From "$Project.exe"))) { throw "No $Project.exe in $From. Package first or check -BuildDir." }
    if (-not (Test-Path -LiteralPath $ship)) { throw "$From is not a Shipping build (no $Project-Win64-Shipping.exe). Run tools/ue/package.ps1 -Config Shipping." }
    $copied = 0; $skipped = 0; $bytes = 0L
    foreach ($f in Get-ChildItem -LiteralPath $From -Recurse -File) {
        $rel = [IO.Path]::GetRelativePath($From, $f.FullName)
        if (Test-StagingExcluded $rel $Project) { $skipped++; continue }
        $dest = Join-Path $To $rel
        New-Item -ItemType Directory -Force -Path (Split-Path $dest) | Out-Null
        Copy-Item -LiteralPath $f.FullName -Destination $dest
        $copied++; $bytes += $f.Length
    }
    [pscustomobject]@{ Files = $copied; Skipped = $skipped; Bytes = $bytes }
}

# Fills a template's {{KEY}} placeholders. Throws if any placeholder is left.
function Expand-VdfTemplate([string]$TemplatePath, [hashtable]$Values, [string]$OutPath) {
    $text = Get-Content -LiteralPath $TemplatePath -Raw
    foreach ($k in $Values.Keys) { $text = $text.Replace("{{$k}}", [string]$Values[$k]) }
    if ($text -match '\{\{[A-Z_]+\}\}') { throw "Unfilled placeholder $($Matches[0]) in $OutPath." }
    New-Item -ItemType Directory -Force -Path (Split-Path $OutPath) | Out-Null
    Set-Content -LiteralPath $OutPath -Value $text -Encoding utf8 -NoNewline
}

# VDF strings here use absolute paths with single backslashes; folders end with a separator.
function ConvertTo-VdfPath([string]$Path, [switch]$Folder) {
    $p = [IO.Path]::GetFullPath($Path)
    if ($Folder -and -not $p.EndsWith('\')) { $p += '\' }
    $p
}

# Branch name -> BuildID from steamcmd app_info_print output.
function Get-SteamBranchBuilds([string[]]$Lines) {
    $text = $Lines -join "`n"
    $at = $text.IndexOf('"branches"')
    $result = @{}
    if ($at -lt 0) { return $result }
    foreach ($m in [regex]::Matches($text.Substring($at), '"([^"]+)"\s*\{[^{}]*?"buildid"\s*"(\d+)"')) {
        $result[$m.Groups[1].Value] = $m.Groups[2].Value
    }
    $result
}

if ($MyInvocation.InvocationName -eq '.') { return }   # Dot-sourced by the test: functions only.

# --- Main ------------------------------------------------------------------------------------------

function Invoke-Git {
    $gitArgs = $args
    $out = & git -C $RepoDir @gitArgs 2>&1
    if ($LASTEXITCODE -ne 0) { throw "git $($gitArgs -join ' ') failed: $($out | Out-String)" }
    $out
}

function Invoke-Gh {
    $ghArgs = $args
    $out = & gh @ghArgs 2>&1
    if ($LASTEXITCODE -ne 0) { throw "gh $($ghArgs[0..1] -join ' ') failed: $($out | Out-String)" }
    ($out | Out-String).Trim()
}

function Invoke-Gate([int]$N, [string]$Name, [scriptblock]$Check, [switch]$WarnOnly) {
    $problem = & $Check
    if (-not $problem) { Write-Host "gate $N ok: $Name"; return }
    if ($WarnOnly) { Write-Warning "gate $N ($Name) would stop a real upload: $problem"; return }
    Stop-Release (10 + $N) "Gate $N failed ($Name): $problem"
}

# Runs a check command with output shown; returns its exit code.
function Invoke-Check([string]$Gate, [string[]]$Command) {
    Write-Host ">> $($Command -join ' ')$(if ($CheckCommand) { " (test override: $CheckCommand $Gate)" })"
    if ($CheckCommand) {
        $env:RELEASE_REAL_CHECK = ConvertTo-Json -InputObject @($Command) -Compress
        $Command = @('pwsh', '-NoProfile', '-File', $CheckCommand, $Gate)
    }
    $exe = $Command[0]; $rest = @($Command | Select-Object -Skip 1)
    & $exe @rest | Out-Host
    $LASTEXITCODE
}

function Invoke-SteamCmd([string[]]$SteamArgs, [string]$Log) {
    $lines = [System.Collections.Generic.List[string]]::new()
    $exe = $SteamCmd; $pre = @()
    if ($SteamCmd -like '*.ps1') { $exe = 'pwsh'; $pre = @('-NoProfile', '-File', $SteamCmd) }
    $all = @($pre) + @($SteamArgs)
    & $exe @all 2>&1 | ForEach-Object { $s = "$_"; Write-Host $s; $lines.Add($s) }
    $code = $LASTEXITCODE
    if ($Log) {
        New-Item -ItemType Directory -Force -Path (Split-Path $Log) | Out-Null
        Set-Content -LiteralPath $Log -Value $lines -Encoding utf8
    }
    [pscustomobject]@{ Code = $code; Lines = $lines.ToArray() }
}

function New-Deployment([string]$Environment, [string]$Ref, [string]$Description) {
    $payload = Join-Path $WorkDir "deployment-$Environment.json"
    @{ ref = $Ref; environment = $Environment; description = $Description; auto_merge = $false
        required_contexts = @(); transient_environment = $false; production_environment = ($Environment -eq 'steam-default')
    } | ConvertTo-Json | Set-Content -LiteralPath $payload -Encoding utf8
    $id = Invoke-Gh api 'repos/:owner/:repo/deployments' --method POST --input $payload --jq '.id'
    if ($id -notmatch '^\d+$') { throw "gh did not return a deployment id (got '$id')." }
    $statusPayload = Join-Path $WorkDir "deployment-$Environment-status.json"
    @{ state = 'success'; description = $Description } | ConvertTo-Json | Set-Content -LiteralPath $statusPayload -Encoding utf8
    Invoke-Gh api "repos/:owner/:repo/deployments/$id/statuses" --method POST --input $statusPayload | Out-Null
    Write-Host "Recorded a $Environment deployment for $Ref."
}

function Get-OgSecretsCommand { @('uv', 'run', '--project', $ToolsDir, 'python', '-m', 'og', 'secrets') }

function Save-LedgerCommit([string]$Message) {
    if ($SkipGitGates) { Write-Host "Ledger updated (not committed: -SkipGitGates): $LedgerPath"; return }
    $rel = [IO.Path]::GetRelativePath($RepoDir, $LedgerPath)
    if ($rel.StartsWith('..')) { Write-Warning "Ledger $LedgerPath is outside the repo; not committed."; return }
    Invoke-Git add '--' $rel | Out-Null
    Invoke-Git commit -q -m $Message '--' $rel | Out-Null
    Invoke-Git push -q origin HEAD:main | Out-Null
    Write-Host "Committed and pushed the ledger: $Message"
}

function Show-Usage {
    Write-Host 'Usage: pwsh tools/steam/release.ps1 <dev|upload-only|dry-run|preview|status|promote <ver>|promoted <ver>> [-SkipPackage] [-Notes "text"]'
}

function Invoke-Upload {
    $realUpload = $Mode -in 'dev', 'upload-only'
    $gitWarnOnly = $Mode -in 'dry-run', 'preview'
    $iniPath = Join-Path $RepoDir 'Game\Config\DefaultGame.ini'
    $ledger = Read-Ledger $LedgerPath

    # --- Gates, in order; the first failure stops the run -----------------------------------
    if ($SkipGitGates) { Write-Host 'gates 1-3 skipped (-SkipGitGates)' }
    else {
        Invoke-Gate 1 'on main' {
            $b = ((Invoke-Git rev-parse --abbrev-ref HEAD) | Out-String).Trim()
            if ($b -ne 'main') { "on '$b'; releases are cut from main only." }
        } -WarnOnly:$gitWarnOnly
        Invoke-Gate 2 'clean tree' {
            $dirty = @(Invoke-Git status --porcelain | Where-Object { $_ })
            if ($dirty.Count) { "$($dirty.Count) uncommitted or untracked path(s): $(($dirty | Select-Object -First 5 | ForEach-Object { $_.Substring(3) }) -join ', ')" }
        } -WarnOnly:$gitWarnOnly
        Invoke-Gate 3 'HEAD pushed' {
            & git -C $RepoDir fetch --quiet origin main 2>&1 | Out-Null
            if ($LASTEXITCODE -ne 0) { return 'could not fetch origin main.' }
            & git -C $RepoDir merge-base --is-ancestor HEAD origin/main 2>&1 | Out-Null
            if ($LASTEXITCODE -ne 0) { 'HEAD is not on origin/main. Push first, so every build maps to a public commit.' }
        } -WarnOnly:$gitWarnOnly
    }
    Invoke-Gate 4 'UE 5.8.x' {
        $bv = Join-Path $UeRoot 'Engine\Build\Build.version'
        if (-not (Test-Path -LiteralPath $bv)) { return "no Engine\Build\Build.version under $UeRoot (set UE_ROOT)." }
        $v = Get-Content -LiteralPath $bv -Raw | ConvertFrom-Json
        if ($v.MajorVersion -ne 5 -or $v.MinorVersion -ne 8) { "engine at $UeRoot is $($v.MajorVersion).$($v.MinorVersion).$($v.PatchVersion); need 5.8.x." }
    }
    $script:currentVersion = $null; $script:nextVersion = $null
    Invoke-Gate 5 'version not in ledger' {
        try { $script:currentVersion = Get-ProjectVersion $iniPath; $script:nextVersion = Get-NextVersion $script:currentVersion }
        catch { return $_.Exception.Message }
        if (@($ledger.Rows | Where-Object version -eq $script:nextVersion).Count) { "next version $($script:nextVersion) is already in $LedgerPath. Fix ProjectVersion in Game/Config/DefaultGame.ini." }
    }
    Invoke-Gate 6 'tests passed' {
        $code = Invoke-Check 'tests' @('pwsh', '-NoProfile', '-File', (Join-Path $PSScriptRoot '..\ue\test.ps1'))
        if ($code -ne 0) { return "Unreal automation tests failed (exit $code)." }
        if ($CheckCommand) { return }
        $code = Invoke-Check 'tests' @('uv', 'run', '--project', $ToolsDir, 'python', '-m', 'pytest', (Join-Path $ToolsDir 'tests'), '-q')
        if ($code -ne 0) { "repo checks (pytest) failed (exit $code)." }
    }
    Invoke-Gate 7 'secret scan clean' {
        # Every tracked file, plus the -Notes text that goes into the build description, the
        # ledger and the GitHub release body.
        $scan = @(Get-OgSecretsCommand) + '--tracked'
        if ($Notes) {
            New-Item -ItemType Directory -Force -Path $WorkDir | Out-Null
            $notesCheck = Join-Path $WorkDir 'release-notes-check.txt'
            Set-Content -LiteralPath $notesCheck -Value $Notes -Encoding utf8
            $scan += $notesCheck
        }
        $code = Invoke-Check 'secrets' $scan
        if ($code -ne 0) { "og secrets reported findings in tracked files or -Notes (exit $code)." }
    }
    Invoke-Gate 8 'asset check clean' {
        $code = Invoke-Check 'assets' @('uv', 'run', '--project', $ToolsDir, 'python', '-m', 'og', 'assets', 'check')
        if ($code -ne 0) { "og assets check failed (exit $code)." }
    }
    Invoke-Gate 9 'Steam env vars' {
        $missing = @($RequiredEnvKeys | Where-Object { -not [Environment]::GetEnvironmentVariable($_) })
        if ($missing.Count) { return "missing $($missing -join ', '). Set them in .env (see .env.example and docs/steam/SETUP-CHECKLIST.md)." }
        $idProblems = Test-SteamIds ([string]$env:STEAM_APPID) ([string]$env:STEAM_DEPOT_WIN)
        if ($idProblems.Count) { $idProblems -join ' ' }
    }
    $staging = Join-Path $WorkDir 'content'
    Invoke-Gate 10 'staging freshly recreated' {
        try {
            if (Test-Path -LiteralPath $staging) { Remove-Item -LiteralPath $staging -Recurse -Force }
            New-Item -ItemType Directory -Force -Path $staging | Out-Null
        }
        catch { return "could not recreate ${staging}: $($_.Exception.Message)" }
        if (@(Get-ChildItem -LiteralPath $staging -Force).Count) { "$staging is not empty after recreating it." }
    }

    # --- Version: dev and upload-only bump, commit and push before packaging ------------------
    $version = $script:nextVersion
    if ($realUpload) {
        Set-ProjectVersion $iniPath $version
        if ($SkipGitGates) { Write-Host "ProjectVersion bumped to $version (not committed: -SkipGitGates)." }
        else {
            Invoke-Git add '--' 'Game/Config/DefaultGame.ini' | Out-Null
            Invoke-Git commit -q -m "chore: release $version" '--' 'Game/Config/DefaultGame.ini' | Out-Null
            Invoke-Git push -q origin HEAD:main | Out-Null
            Write-Host "ProjectVersion bumped to $version, committed and pushed."
        }
    }
    else { Write-Host "Version for this run: $version (ProjectVersion stays $($script:currentVersion); $Mode does not bump)." }
    $sha = ((Invoke-Git rev-parse HEAD) | Out-String).Trim()

    # --- Package and stage ---------------------------------------------------------------------
    if (-not $SkipPackage) {
        if ($BuildDir) { $env:CS_PACKAGE_DIR = $BuildDir }
        $out = & pwsh -NoProfile -File (Join-Path $PSScriptRoot '..\ue\package.ps1') -Config Shipping
        if ($LASTEXITCODE -ne 0) { throw "Packaging failed (exit $LASTEXITCODE)." }
        $BuildDir = (@($out) | Where-Object { $_ } | Select-Object -Last 1).ToString().Trim()
    }
    else {
        if (-not $BuildDir) { $BuildDir = $PackageDir }
        if ($realUpload) { Write-Warning "-SkipPackage: $BuildDir was packaged before the bump, so the game reports the previous ProjectVersion." }
    }
    $stage = Copy-PackageToStaging -From $BuildDir -To $staging -Project $ProjectName
    Write-Host ("Staged {0} files ({1:N0} MB) from {2} into {3}; {4} excluded." -f $stage.Files, ($stage.Bytes / 1MB), $BuildDir, $staging, $stage.Skipped)

    # --- Render the run VDFs -------------------------------------------------------------------
    $depotVdf = Join-Path $WorkDir 'depot_build_run.vdf'
    $appVdf = Join-Path $WorkDir 'app_build_run.vdf'
    $desc = "Combat Simulator $version ($($sha.Substring(0, 10)))" + $(if ($Notes) { " $Notes" } else { '' })
    $values = @{
        APP_ID       = [string]$env:STEAM_APPID
        DEPOT_ID     = [string]$env:STEAM_DEPOT_WIN
        DESC         = $desc -replace '["\\{}]', ''
        CONTENT_ROOT = ConvertTo-VdfPath $staging -Folder
        BUILD_OUTPUT = ConvertTo-VdfPath (Join-Path $WorkDir 'output') -Folder
        SETLIVE      = $(if ($Mode -in 'dev', 'dry-run') { $Branch } else { '' })
        PREVIEW      = $(if ($Mode -eq 'preview') { '1' } else { '0' })
        DEPOT_VDF    = ConvertTo-VdfPath $depotVdf
    }
    $templates = Join-Path $RepoDir 'steam\templates'
    Expand-VdfTemplate (Join-Path $templates 'depot_build.vdf') $values $depotVdf
    Expand-VdfTemplate (Join-Path $templates 'app_build.vdf') $values $appVdf
    Write-Host "Rendered $appVdf (SetLive '$($values.SETLIVE)', Preview $($values.PREVIEW))."

    $steamLine = "& '$SteamCmd' +login `$env:STEAM_BUILD_ACCOUNT +run_app_build '$appVdf' +quit"
    if ($Mode -eq 'dry-run') {
        Write-Host ''
        Write-Host 'Dry run complete. Nothing was uploaded and ProjectVersion was not bumped.'
        Write-Host "The upload step would run: $steamLine"
        Write-Host 'To release, run in your own terminal: pwsh tools/steam/release.ps1 dev'
        return
    }

    # --- steamcmd (owner only) -----------------------------------------------------------------
    if (-not (Test-Path -LiteralPath $SteamCmd)) { Stop-Release 30 "steamcmd not found at $SteamCmd. Install it or set STEAMCMD_PATH." }
    $run = Invoke-SteamCmd @('+login', $env:STEAM_BUILD_ACCOUNT, '+run_app_build', $appVdf, '+quit') (Join-Path $WorkDir 'steamcmd.log')
    $errors = @($run.Lines | Where-Object { $_ -match 'ERROR!' })
    $buildId = Get-BuildIdFromLog $run.Lines
    if ($run.Code -ne 0 -or $errors.Count) {
        $msg = "steamcmd failed (exit $($run.Code), $($errors.Count) ERROR! line(s)). Nothing was recorded. Log: $(Join-Path $WorkDir 'steamcmd.log')"
        if ($values.SETLIVE -and ($errors -match '(?i)branch')) {
            $msg += " The build may have uploaded$(if ($buildId) { " as BuildID $buildId" }) but could not be set live on '$($values.SETLIVE)'. Does the $($values.SETLIVE) beta branch exist in Steamworks? See docs/steam/SETUP-CHECKLIST.md step 7."
        }
        Stop-Release 30 $msg
    }
    if (-not $buildId) { Stop-Release 31 "steamcmd printed no BuildID. Nothing was recorded. Log: $(Join-Path $WorkDir 'steamcmd.log')" }

    if ($Mode -eq 'preview') {
        Write-Host "Preview build $buildId finished: nothing uploaded. Manifest and logs in $(Join-Path $WorkDir 'output')."
        return
    }

    # --- Record: ledger, then GitHub -----------------------------------------------------------
    $now = Get-UtcNow
    $setLive = $Mode -eq 'dev'
    if ($setLive) {
        foreach ($row in $ledger.Rows) { if ($row.development_live_utc -and -not $row.development_replaced_utc) { $row.development_replaced_utc = $now } }
    }
    $ledger.Rows.Add((New-LedgerRow @{ version = $version; build_id = $buildId; git_sha = $sha; uploaded_utc = $now
                development_live_utc = $(if ($setLive) { $now } else { '' }); notes = $Notes }))
    Write-Ledger $ledger
    Write-Host "Uploaded build $buildId ($version, $sha)$(if ($setLive) { ", live on $Branch" } else { ', not set live' }). Ledger: $LedgerPath"
    try { Save-LedgerCommit "docs: record Steam build $version (BuildID $buildId)" }
    catch {
        Stop-Release 1 "Upload done as BuildID $buildId; do not re-run. The ledger row is written to $LedgerPath but its commit or push failed: commit and push docs/steam/BUILDS.md by hand, then create the GitHub release if wanted. ($($_.Exception.Message.Trim()))"
    }

    if ($NoGitHub) { Write-Host 'GitHub release and deployment skipped (-NoGitHub).'; return }
    $notesFile = Join-Path $WorkDir "github-release-$version.md"
    $body = @(
        "Combat Simulator $version, uploaded to Steam as BuildID $buildId$(if ($setLive) { " and set live on the ``$Branch`` branch" } else { ' (not set live on any branch)' }).",
        '',
        "Commit: $sha"
    )
    if ($Notes) { $body += @('', $Notes) }
    $body += @('', 'This release holds no game files: builds ship only through Steam.')
    New-Item -ItemType Directory -Force -Path $WorkDir | Out-Null
    Set-Content -LiteralPath $notesFile -Value $body -Encoding utf8
    try {
        Invoke-Gh release create "v$version" --target $sha --prerelease --title "Combat Simulator $version" --notes-file $notesFile | Out-Null
        Write-Host "Created GitHub prerelease v$version."
        if ($setLive) { New-Deployment 'steam-development' $sha "BuildID $buildId live on $Branch" }
    }
    catch {
        Stop-Release 1 "The upload and ledger row are done, but a GitHub step failed: $($_.Exception.Message) Create the release or deployment by hand; do not re-run the upload."
    }
}

function Invoke-Status {
    $ledger = Read-Ledger $LedgerPath
    Write-Host "Ledger: $LedgerPath ($($ledger.Rows.Count) build(s))"
    foreach ($row in @($ledger.Rows | Select-Object -Last 5)) {
        Write-Host ("  {0}  BuildID {1}  development {2}  default {3}" -f $row.version, $row.build_id,
            $(if ($row.development_live_utc) { "$($row.development_live_utc)..$($row.development_replaced_utc)" } else { '-' }),
            $(if ($row.default_live_utc) { "$($row.default_live_utc)..$($row.default_replaced_utc)" } else { '-' }))
    }
    $expected = [ordered]@{ development = Get-LiveRow $ledger 'development'; default = Get-LiveRow $ledger 'default' }

    $reason = $null
    if ($env:CLAUDECODE) { $reason = 'skipped inside Claude Code (steamcmd may prompt for a login). Run status in your own terminal for the drift check.' }
    elseif (-not $env:STEAM_APPID -or -not $env:STEAM_BUILD_ACCOUNT) { $reason = 'STEAM_APPID or STEAM_BUILD_ACCOUNT not set.' }
    elseif (-not (Test-Path -LiteralPath $SteamCmd)) { $reason = "steamcmd not found at $SteamCmd." }
    Write-Host ''
    if (-not $reason) {
        $run = Invoke-SteamCmdQuiet @('+@NoPromptForPassword', '1', '+login', $env:STEAM_BUILD_ACCOUNT, '+app_info_update', '1', '+app_info_print', $env:STEAM_APPID, '+quit')
        $steam = Get-SteamBranchBuilds $run.Lines
        if ($run.Code -ne 0) { $reason = "steamcmd exited $($run.Code); is steamcmd logged in? Run steamcmd once in your terminal." }
        elseif (($run.Lines -join "`n") -notmatch '"branches"') { $reason = 'steamcmd printed no branches block for the app.' }
    }
    if ($reason) {
        Write-Host "Steam state: unknown ($reason)"
        foreach ($b in $expected.Keys) { Write-Host "  ${b}: ledger says $(if ($expected[$b]) { "BuildID $($expected[$b].build_id) ($($expected[$b].version))" } else { 'nothing live' })" }
    }
    else {
        Write-Host 'Steam branch    ledger      Steam'
        foreach ($b in $expected.Keys) {
            $steamName = if ($b -eq 'default') { 'public' } else { $b }
            $want = if ($expected[$b]) { $expected[$b].build_id } else { '' }
            $have = if ($steam.ContainsKey($steamName)) { $steam[$steamName] } else { '' }
            $flag = if ($want -ne $have) { '  MISMATCH' } else { '' }
            Write-Host ("  {0,-13} {1,-11} {2}{3}" -f $b, $(if ($want) { $want } else { '-' }), $(if ($have) { $have } else { '-' }), $flag)
            if ($flag) { Write-Host "  MISMATCH on ${b}: the ledger says $(if ($want) { "BuildID $want" } else { 'nothing live' }), Steam says $(if ($have) { "BuildID $have" } else { 'nothing live' })." }
        }
    }

    $open = @(Get-OpenChecklistItems (Join-Path $RepoDir 'docs\steam\SETUP-CHECKLIST.md'))
    Write-Host ''
    if ($open.Count) { foreach ($item in $open) { Write-Host "Open setup item: $item" } }
    else { Write-Host 'Setup checklist: all done.' }
}

# steamcmd for status: output parsed, not echoed (it includes app details).
function Invoke-SteamCmdQuiet([string[]]$SteamArgs) {
    $exe = $SteamCmd; $pre = @()
    if ($SteamCmd -like '*.ps1') { $exe = 'pwsh'; $pre = @('-NoProfile', '-File', $SteamCmd) }
    $all = @($pre) + @($SteamArgs)
    $lines = @(& $exe @all 2>&1 | ForEach-Object { "$_" })
    [pscustomobject]@{ Code = $LASTEXITCODE; Lines = $lines }
}

function Get-RowOrStop($Ledger, [string]$Ver) {
    if ($Ver -notmatch '^\d+\.\d+\.\d+$') { Stop-Release 1 "Give a version, for example: pwsh tools/steam/release.ps1 $Mode 0.1.3" }
    $row = @($Ledger.Rows | Where-Object version -eq $Ver) | Select-Object -Last 1
    if (-not $row -or -not $row.build_id) { Stop-Release 1 "Version $Ver has no build in $LedgerPath." }
    $row
}

function Invoke-Promote {
    $ledger = Read-Ledger $LedgerPath
    $row = Get-RowOrStop $ledger $Version
    $draft = Join-Path $RepoDir "_release\$Version"
    if (Test-Path -LiteralPath $draft) { Stop-Release 1 "$draft already exists. Delete it to draft again." }
    $base = @($ledger.Rows | Where-Object { $_.default_live_utc -and $_.version -ne $Version } | Sort-Object { $_.default_live_utc }) | Select-Object -Last 1

    $range = @(if ($base) { "$($base.git_sha)..$($row.git_sha)" } else { '-n', '200', $row.git_sha })
    $subjects = @(Invoke-Git log --no-merges --format=%s @range | Where-Object { $_ -and $_ -notmatch '^(chore: release |docs: record Steam build |chore: session )' })
    if ($base) { $devlogs = @(Invoke-Git diff --name-only --diff-filter=A $base.git_sha $row.git_sha '--' 'docs/devlog') }
    else { $devlogs = @(Invoke-Git ls-tree -r --name-only $row.git_sha '--' 'docs/devlog') }
    $devlogs = @($devlogs | Where-Object { $_ -match '^docs/devlog/\d{4}-\d\d-\d\d-[^/]+\.md$' } | Sort-Object)
    $since = if ($base) { "since $($base.version) (the last default build)" } else { 'since the start (no default build yet)' }

    New-Item -ItemType Directory -Force -Path $draft | Out-Null
    $post = @("[h1]Combat Simulator $Version[/h1]", '', '[narrative]', '', '[h2]Changes[/h2]', '[list]')
    $post += @($subjects | ForEach-Object { "[*]$_" })
    $post += @('[/list]', '', '[h2]Known issues[/h2]', '[list]', '[*]None reported yet.', '[/list]')
    Set-Content -LiteralPath (Join-Path $draft 'post.bbcode') -Value $post -Encoding utf8

    $patch = @("# Combat Simulator $Version patch notes", '', "Steam BuildID $($row.build_id), commit $($row.git_sha). Changes $since.", '', '## Changes', '')
    $patch += @($subjects | ForEach-Object { "- $_" })
    $patch += @('', '## Sources: devlogs in this range', '')
    $patch += $(if ($devlogs.Count) { @($devlogs | ForEach-Object { "- $_" }) } else { @('- none') })
    Set-Content -LiteralPath (Join-Path $draft 'patch-notes.md') -Value $patch -Encoding utf8

    $gh = @('[narrative]', '', '## Changes', '')
    $gh += @($subjects | ForEach-Object { "- $_" })
    $gh += @('', "Steam BuildID $($row.build_id), now on the default branch. Commit $($row.git_sha).", '')
    if ($devlogs.Count) { $gh += @('Devlogs:', '') + @($devlogs | ForEach-Object { "- $_" }) + @('') }
    $gh += 'This release holds no game files: builds ship only through Steam.'
    Set-Content -LiteralPath (Join-Path $draft 'github-release.md') -Value $gh -Encoding utf8

    $check = @(
        "# Promote $Version (BuildID $($row.build_id)) to default",
        '',
        'Nothing here has changed Steam. The owner does steps 1 to 3.',
        '',
        '1. Fill the [narrative] marker in post.bbcode and github-release.md (the release skill drafts it from the devlogs listed in patch-notes.md).',
        "2. Steamworks, SteamPipe, Builds: set BuildID $($row.build_id) live on the default branch, then publish. No script ever does this.",
        '3. Publish post.bbcode as a Steam event (Community, Events and Announcements).',
        "4. Then run, in your own terminal: pwsh tools/steam/release.ps1 promoted $Version"
    )
    Set-Content -LiteralPath (Join-Path $draft 'checklist.md') -Value $check -Encoding utf8
    Write-Host "Drafted $draft (post.bbcode, patch-notes.md, github-release.md, checklist.md): $($subjects.Count) commit(s), $($devlogs.Count) devlog(s) $since."
    Write-Host 'Next: fill [narrative], then follow checklist.md.'
}

function Invoke-Promoted {
    $ledger = Read-Ledger $LedgerPath
    $row = Get-RowOrStop $ledger $Version
    if ($row.default_live_utc -and -not $row.default_replaced_utc) { Stop-Release 1 "$Version is already recorded as live on default ($($row.default_live_utc))." }
    $draft = Join-Path $RepoDir "_release\$Version"
    $notesFile = Join-Path $draft 'github-release.md'
    if (Test-Path -LiteralPath $draft) {
        $unfilled = @('post.bbcode', 'github-release.md' | Where-Object { (Test-Path -LiteralPath (Join-Path $draft $_)) -and ((Get-Content -LiteralPath (Join-Path $draft $_) -Raw) -match '\[narrative\]') })
        if ($unfilled.Count) { Stop-Release 1 "Fill the [narrative] marker in $($unfilled -join ' and ') under $draft first. Nothing was recorded." }
    }
    if (-not (Test-Path -LiteralPath $notesFile)) { Write-Warning "No $notesFile; the GitHub Release body is left as it is."; $notesFile = $null }
    if (Test-Path -LiteralPath $draft) {
        # Everything in the draft is published text (Steam post, GitHub release body): scan it first.
        $draftFiles = @(Get-ChildItem -LiteralPath $draft -Recurse -File | ForEach-Object { $_.FullName })
        if ($draftFiles.Count) {
            $code = Invoke-Check 'secrets' (@(Get-OgSecretsCommand) + $draftFiles)
            if ($code -ne 0) { Stop-Release 1 "og secrets reported findings in $draft (exit $code). Nothing was recorded." }
        }
    }

    $now = Get-UtcNow
    foreach ($r in $ledger.Rows) { if ($r -ne $row -and $r.default_live_utc -and -not $r.default_replaced_utc) { $r.default_replaced_utc = $now } }
    $row.default_live_utc = $now
    $row.default_replaced_utc = ''
    Write-Ledger $ledger
    Write-Host "Recorded $Version (BuildID $($row.build_id)) live on default at $now."
    Save-LedgerCommit "docs: record Steam build $Version live on default"

    if ($NoGitHub) { Write-Host 'GitHub release edit and deployment skipped (-NoGitHub).'; return }
    try {
        $editArgs = @('release', 'edit', "v$Version", '--prerelease=false', '--latest')
        if ($notesFile) { $editArgs += @('--notes-file', $notesFile) }
        Invoke-Gh @editArgs | Out-Null
        Write-Host "GitHub release v$Version is now a full release and Latest."
        New-Item -ItemType Directory -Force -Path $WorkDir | Out-Null
        New-Deployment 'steam-default' $row.git_sha "BuildID $($row.build_id) live on default"
    }
    catch { Stop-Release 1 "The ledger is updated, but a GitHub step failed: $($_.Exception.Message)" }
}

try {
    if (-not $Mode -or $Mode -notin $Modes) { Show-Usage; Stop-Release 1 "Unknown or missing mode '$Mode'." }
    . (Join-Path $PSScriptRoot '..\ue\env.ps1')
    $ToolsDir = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
    if (-not $RepoDir) { $RepoDir = $RepoRoot }
    $RepoDir = (Resolve-Path -LiteralPath $RepoDir).Path
    if (-not $LedgerPath) { $LedgerPath = Join-Path $RepoDir 'docs\steam\BUILDS.md' }
    if (-not $WorkDir) { $WorkDir = Join-Path $RepoDir '_scratch\steam' }
    $WorkDir = [IO.Path]::GetFullPath($WorkDir)
    Import-SteamEnv (Join-Path $RepoDir '.env')
    if (-not $SteamCmd) { $SteamCmd = if ($env:STEAMCMD_PATH) { $env:STEAMCMD_PATH } else { 'C:\steamcmd\steamcmd.exe' } }

    $branchProblem = Test-ReleaseBranch $Branch
    if ($branchProblem) { Stop-Release 40 $branchProblem }
    $Branch = $Branch.Trim()
    if ($Mode -in 'dev', 'upload-only', 'preview', 'promoted' -and $env:CLAUDECODE) {
        $cmd = "pwsh tools/steam/release.ps1 $Mode$(if ($Version) { " $Version" })"
        Stop-Release 40 "Refusing '$Mode' inside Claude Code (CLAUDECODE is set). The owner runs it in their own terminal: $cmd"
    }

    Push-Location $RepoDir
    try {
        switch ($Mode) {
            'status' { Invoke-Status }
            'promote' { Invoke-Promote }
            'promoted' { Invoke-Promoted }
            default { Invoke-Upload }
        }
    }
    finally { Pop-Location }
    exit 0
}
catch {
    Write-Error $_.Exception.Message -ErrorAction Continue
    exit $script:ExitCode
}
