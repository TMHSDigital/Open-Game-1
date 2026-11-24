# Tests tools/steam/release.ps1 without Steam, GitHub or Unreal: a throwaway git repo with a bare
# "origin", a fake packaged build (with stale files from an older Development package), a fake UE
# root, fake check commands, tools/steam/fake-steamcmd.ps1, and a `gh` recorder placed first on
# PATH. Nothing here talks to the real GitHub or the real steamcmd.
#
# Usage: pwsh tools/steam/test-release.ps1

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$release = Join-Path $PSScriptRoot 'release.ps1'
$fakeSteamcmd = Join-Path $PSScriptRoot 'fake-steamcmd.ps1'
$realRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path

$script:failures = 0
$script:passes = 0
function Check([string]$Name, [bool]$Condition, [string]$Detail = '') {
    if ($Condition) { $script:passes++; Write-Host "PASS $Name" }
    else {
        $script:failures++
        Write-Host "FAIL $Name"
        if ($Detail) { Write-Host ($Detail -split "`n" | Select-Object -Last 25 | ForEach-Object { "     | $_" } | Out-String) }
    }
}

function Invoke-Git([string]$Dir) {
    # Remaining args are passed to git; output returned; throws on failure.
    $gitArgs = $args
    $out = & git -C $Dir @gitArgs 2>&1
    if ($LASTEXITCODE -ne 0) { throw "git $($gitArgs -join ' ') failed in ${Dir}: $out" }
    $out
}

$tmp = Join-Path ([IO.Path]::GetTempPath()) ('cs-release-test-' + [guid]::NewGuid().ToString('N').Substring(0, 8))
New-Item -ItemType Directory -Force -Path $tmp | Out-Null

$ledgerHeader = @'
# Steam build ledger

| version | build_id | git_sha | uploaded_utc | development_live_utc | development_replaced_utc | default_live_utc | default_replaced_utc | notes |
|---|---|---|---|---|---|---|---|---|
'@

# --- Shared fixtures (created once) ------------------------------------------------------------

# Fake UE roots: one 5.8.3, one 5.7.4.
function New-FakeUe([string]$Name, [int]$Minor) {
    $root = Join-Path $tmp $Name
    New-Item -ItemType Directory -Force -Path (Join-Path $root 'Engine\Build') | Out-Null
    Set-Content (Join-Path $root 'Engine\Build\Build.version') (@{ MajorVersion = 5; MinorVersion = $Minor; PatchVersion = 3; Changelist = 1 } | ConvertTo-Json)
    $root
}
$ue58 = New-FakeUe 'ue58' 8
$ue57 = New-FakeUe 'ue57' 7

# Fake package archive. It mimics the real archive dir, which is not cleaned between configs:
# a stale Development exe and .pdb files sit next to the Shipping build, plus run output.
$build = Join-Path $tmp 'archive\Windows'
$bin = Join-Path $build 'CombatSimulator\Binaries\Win64'
New-Item -ItemType Directory -Force -Path $bin, (Join-Path $build 'CombatSimulator\Content\Paks'), (Join-Path $build 'CombatSimulator\Saved\Logs'), (Join-Path $build 'CombatSimulator\Saved\Crashes\UECC-1'), (Join-Path $build 'Engine\Saved\Config'), (Join-Path $build 'CombatSimulator\Intermediate') | Out-Null
Set-Content (Join-Path $build 'CombatSimulator.exe') 'launcher'
Set-Content (Join-Path $bin 'CombatSimulator-Win64-Shipping.exe') 'shipping'
Set-Content (Join-Path $bin 'CombatSimulator-Win64-Shipping.pdb') 'pdb'
Set-Content (Join-Path $bin 'CombatSimulator.exe') 'STALE development exe'
Set-Content (Join-Path $bin 'CombatSimulator.pdb') 'STALE development pdb'
Set-Content (Join-Path $bin 'CombatSimulator-Win64-DebugGame.exe') 'STALE debuggame exe'
Set-Content (Join-Path $bin 'tbb12.dll') 'dll'
Set-Content (Join-Path $build 'CombatSimulator\Content\Paks\CombatSimulator-Windows.pak') 'pak'
Set-Content (Join-Path $build 'CombatSimulator\Saved\Logs\CombatSimulator.log') 'log'
Set-Content (Join-Path $build 'CombatSimulator\Saved\Crashes\UECC-1\UEMinidump.dmp') 'dump'
Set-Content (Join-Path $build 'Engine\Saved\Config\Manifest.ini') 'saved'
Set-Content (Join-Path $build 'CombatSimulator\Intermediate\x.txt') 'intermediate'
Set-Content (Join-Path $build 'steam_appid.txt') '480'
Set-Content (Join-Path $build 'Manifest_NonUFSFiles_Win64.txt') 'manifest'
Set-Content (Join-Path $build 'crashreporter.sym') 'symbols'
Set-Content (Join-Path $build 'NOTICES.txt') 'notices'

# Fake check command: gates 6 (tests), 7 (secrets), 8 (assets) and the promoted draft scan run
# it with the check name. release.ps1 puts the real command in RELEASE_REAL_CHECK (JSON); with
# FAKE_CHECK_REAL=<name> the fake runs that real command instead of pretending.
$checkCmd = Join-Path $tmp 'fake-check.ps1'
Set-Content $checkCmd @'
param([string]$Name)
if ($env:FAKE_CHECK_FAIL -eq $Name) { Write-Output "fake $Name check failed"; exit 1 }
if ($env:FAKE_CHECK_REAL -eq $Name) {
    $real = @($env:RELEASE_REAL_CHECK | ConvertFrom-Json)
    $exe = $real[0]; $rest = @($real | Select-Object -Skip 1)
    & $exe @rest
    exit $LASTEXITCODE
}
Write-Output "fake $Name check ok"
exit 0
'@

# gh recorder, first on PATH: one JSON line per call, with --input and --notes-file contents.
$shimDir = Join-Path $tmp 'shim'
New-Item -ItemType Directory -Force -Path $shimDir | Out-Null
Set-Content (Join-Path $shimDir 'gh.ps1') @'
$entry = [ordered]@{ args = @($args | ForEach-Object { "$_" }); input = ''; notes = '' }
$i = [array]::IndexOf($args, '--input')
if ($i -ge 0) { $entry.input = Get-Content -LiteralPath $args[$i + 1] -Raw }
$n = [array]::IndexOf($args, '--notes-file')
if ($n -ge 0) { $entry.notes = Get-Content -LiteralPath $args[$n + 1] -Raw }
Add-Content -LiteralPath $env:GH_RECORD -Value ($entry | ConvertTo-Json -Compress -Depth 5)
if (@($args | Where-Object { "$_" -match '/deployments$' }).Count -gt 0) { Write-Output '4242' }
exit 0
'@

# --- Per-scenario fixture: a git repo with a bare origin -------------------------------------
$script:fx = $null
function New-Fixture {
    $root = Join-Path $tmp ('fx-' + [guid]::NewGuid().ToString('N').Substring(0, 6))
    $remote = Join-Path $root 'origin.git'
    $repo = Join-Path $root 'repo'
    $work = Join-Path $root 'work'
    New-Item -ItemType Directory -Force -Path $repo | Out-Null
    & git init -q --bare -b main $remote
    Invoke-Git $repo init -q -b main | Out-Null
    Invoke-Git $repo config user.name 'Release Test' | Out-Null
    Invoke-Git $repo config user.email 'release-test@example.com' | Out-Null
    Invoke-Git $repo config commit.gpgsign false | Out-Null
    Invoke-Git $repo config core.hooksPath .no-hooks | Out-Null
    Invoke-Git $repo remote add origin $remote | Out-Null

    Copy-Item (Join-Path $realRoot '.gitignore') (Join-Path $repo '.gitignore')
    New-Item -ItemType Directory -Force -Path (Join-Path $repo 'Game\Config'), (Join-Path $repo 'docs\steam'), (Join-Path $repo 'docs\devlog'), (Join-Path $repo 'steam') | Out-Null
    Set-Content (Join-Path $repo 'Game\Config\DefaultGame.ini') "[/Script/EngineSettings.GeneralProjectSettings]`r`nProjectName=CombatSimulator`r`nProjectVersion=0.1.0`r`n"
    Set-Content (Join-Path $repo 'docs\steam\BUILDS.md') $ledgerHeader
    Set-Content (Join-Path $repo 'docs\steam\SETUP-CHECKLIST.md') "# Setup`r`n`r`n1. Create the app. *Done 2026-11-01.*`r`n2. Add the depot. *Pending.*`r`n3. Build account. *Blocked on owner.*`r`n"
    Set-Content (Join-Path $repo 'docs\devlog\2026-11-20-01-first.md') "# First devlog`r`n"
    Copy-Item (Join-Path $realRoot 'steam\templates') (Join-Path $repo 'steam\templates') -Recurse
    Invoke-Git $repo add -A | Out-Null
    Invoke-Git $repo commit -q -m 'initial' | Out-Null
    Set-Content (Join-Path $repo 'docs\devlog\2026-11-21-01-second.md') "# Second devlog`r`n"
    Invoke-Git $repo add -A | Out-Null
    Invoke-Git $repo commit -q -m 'feat: second change' | Out-Null
    Invoke-Git $repo push -q -u origin main | Out-Null

    $script:fx = [pscustomobject]@{
        Root = $root; Remote = $remote; Repo = $repo; Work = $work
        Ledger = Join-Path $repo 'docs\steam\BUILDS.md'
        Record = Join-Path $root 'gh-record.jsonl'
        FirstSha = (Invoke-Git $repo rev-list --max-parents=0 HEAD | Select-Object -First 1).Trim()
        Head = (Invoke-Git $repo rev-parse HEAD).Trim()
    }
    $script:fx
}

# Runs release.ps1 in a child pwsh with test env vars; restores the parent env afterwards.
function Invoke-Release([string[]]$Arguments, [hashtable]$EnvOverride = @{}, [switch]$NoDefaults) {
    $envVars = [ordered]@{
        STEAM_APPID = '1234560'; STEAM_DEPOT_WIN = '1234561'; STEAM_BUILD_ACCOUNT = 'fake_builder'  # og-secrets: allow (fake test IDs)
        STEAMCMD_PATH = $null; CLAUDECODE = $null; UE_ROOT = $ue58
        FAKE_STEAMCMD_MODE = 'ok'; FAKE_STEAMCMD_BUILDID = '98765'; FAKE_STEAMCMD_BRANCHES = $null
        FAKE_CHECK_FAIL = $null; FAKE_CHECK_REAL = $null; FAKE_STEAMCMD_INFO = $null; FAKE_STEAMCMD_LOG = $null
        GH_RECORD = $script:fx.Record; PATH = "$shimDir;$env:PATH"
    }
    foreach ($k in $EnvOverride.Keys) { $envVars[$k] = $EnvOverride[$k] }
    $saved = @{}
    foreach ($k in $envVars.Keys) {
        $saved[$k] = [Environment]::GetEnvironmentVariable($k)
        [Environment]::SetEnvironmentVariable($k, $envVars[$k], 'Process')
    }
    $base = @()
    if (-not $NoDefaults) {
        $base = @('-RepoDir', $script:fx.Repo, '-WorkDir', $script:fx.Work, '-BuildDir', $build,
            '-SteamCmd', $fakeSteamcmd, '-CheckCommand', $checkCmd, '-SkipPackage')
    }
    try {
        $out = & pwsh -NoProfile -File $release @Arguments @base 2>&1
        [pscustomobject]@{ Code = $LASTEXITCODE; Output = ($out | Out-String) }
    }
    finally {
        foreach ($k in $saved.Keys) { [Environment]::SetEnvironmentVariable($k, $saved[$k], 'Process') }
    }
}

function Get-GhCalls {
    if (-not (Test-Path -LiteralPath $script:fx.Record)) { return , @() }
    , @(Get-Content -LiteralPath $script:fx.Record | Where-Object { $_ } | ForEach-Object { $_ | ConvertFrom-Json })
}

function Get-LedgerRows([string]$Path) {
    $rows = @()
    foreach ($line in Get-Content -LiteralPath $Path) {
        $c = @($line -split '\|' | ForEach-Object { $_.Trim() })
        if ($c.Count -ge 11 -and $c[1] -match '^\d+\.\d+\.\d+$') {
            $rows += [pscustomobject]@{ version = $c[1]; build_id = $c[2]; git_sha = $c[3]; uploaded_utc = $c[4]
                development_live_utc = $c[5]; development_replaced_utc = $c[6]; default_live_utc = $c[7]; default_replaced_utc = $c[8]; notes = $c[9] }
        }
    }
    , $rows
}

function New-TempLedger([string[]]$Rows) {
    $p = Join-Path $script:fx.Root ('ledger-' + [guid]::NewGuid().ToString('N').Substring(0, 6) + '.md')
    Set-Content -LiteralPath $p -Value ($ledgerHeader + "`r`n" + ($Rows -join "`r`n"))
    $p
}

try {
    # --- Unit checks on pure functions (dot-sourced: functions only) ---------------------------
    . $release
    Check 'Next version bumps the build number' ((Get-NextVersion '0.1.0') -eq '0.1.1')
    Check 'Next version keeps the milestone' ((Get-NextVersion '0.3.17') -eq '0.3.18')
    $threw = $false; try { Get-NextVersion '1.2' | Out-Null } catch { $threw = $true }
    Check 'Next version refuses a version not shaped 0.<milestone>.<build>' $threw
    Check 'IDs: missing App ID is a problem' ((Test-SteamIds '' '1234561').Count -gt 0)
    Check 'IDs: App ID 480 is refused' ((Test-SteamIds '480' '481').Count -gt 0)
    Check 'IDs: depot equal to app is refused' ((Test-SteamIds '1234560' '1234560').Count -gt 0)
    Check 'IDs: plausible IDs pass' ((Test-SteamIds '1234560' '1234561').Count -eq 0)
    Check 'BuildID parsed from steamcmd output' ((Get-BuildIdFromLog @('noise', 'Successfully finished AppID build (BuildID 4242).')) -eq '4242')
    Check 'No BuildID gives null' ($null -eq (Get-BuildIdFromLog @('noise only')))
    $cl = Join-Path $tmp 'checklist.md'
    Set-Content $cl "# S`r`n`r`n1. One. *Done 2026-11-01.*`r`n2. Two. *Pending.*`r`n3. Three. *Blocked until the first build.*`r`n4. Four. *Done (depot exists); verify the OS.*`r`nNot an item. *Pending.*"
    $open = @(Get-OpenChecklistItems $cl)
    Check 'Checklist: open items are those not marked Done' ($open.Count -eq 2 -and $open[0] -match 'Two' -and $open[1] -match 'Three')

    $stage = Join-Path $tmp 'unit-stage'
    New-Item -ItemType Directory -Force -Path $stage | Out-Null
    Copy-PackageToStaging -From $build -To $stage -Project 'CombatSimulator' | Out-Null
    $staged = @(Get-ChildItem -LiteralPath $stage -Recurse -File | ForEach-Object { [IO.Path]::GetRelativePath($stage, $_.FullName) })
    Check 'Staging keeps the launcher, Shipping exe, pak and dlls' (($staged -contains 'CombatSimulator.exe') -and ($staged -contains 'CombatSimulator\Binaries\Win64\CombatSimulator-Win64-Shipping.exe') -and ($staged -contains 'CombatSimulator\Content\Paks\CombatSimulator-Windows.pak') -and ($staged -contains 'CombatSimulator\Binaries\Win64\tbb12.dll')) ($staged -join "`n")
    $leaked = @($staged | Where-Object { $_ -match '\.(pdb|log|sym|dmp)$' -or $_ -match '(^|\\)(Saved|Intermediate)\\' -or $_ -match 'steam_appid\.txt$' -or $_ -match '^Manifest_' -or $_ -eq 'CombatSimulator\Binaries\Win64\CombatSimulator.exe' -or $_ -match 'DebugGame' })
    Check 'Stale archive files (Development exe, pdb, logs, Saved, Intermediate, steam_appid.txt, symbols) never reach staging' ($leaked.Count -eq 0) ($leaked -join "`n")
    $noShip = Join-Path $tmp 'noship'
    New-Item -ItemType Directory -Force -Path (Join-Path $noShip 'CombatSimulator\Binaries\Win64') | Out-Null
    Set-Content (Join-Path $noShip 'CombatSimulator.exe') 'x'
    Set-Content (Join-Path $noShip 'CombatSimulator\Binaries\Win64\CombatSimulator.exe') 'dev'
    $threw = $false; try { Copy-PackageToStaging -From $noShip -To (Join-Path $tmp 'unit-stage2') -Project 'CombatSimulator' | Out-Null } catch { $threw = $true }
    Check 'Staging refuses an archive with no Shipping exe' $threw

    # --- Gates: each fails with its own exit code ------------------------------------------------
    New-Fixture | Out-Null
    Invoke-Git $fx.Repo checkout -q -b feature | Out-Null
    $r = Invoke-Release @('dev', '-NoGitHub')
    Check 'Gate 1: not on main exits 11' ($r.Code -eq 11) $r.Output
    Invoke-Git $fx.Repo checkout -q main | Out-Null

    Add-Content (Join-Path $fx.Repo 'docs\devlog\2026-11-20-01-first.md') 'edit'
    $r = Invoke-Release @('dev', '-NoGitHub')
    Check 'Gate 2: tracked change exits 12' ($r.Code -eq 12) $r.Output
    Invoke-Git $fx.Repo checkout -q -- . | Out-Null

    Set-Content (Join-Path $fx.Repo 'new-untracked.txt') 'x'
    $r = Invoke-Release @('dev', '-NoGitHub')
    Check 'Gate 2: untracked, not ignored file exits 12' ($r.Code -eq 12) $r.Output
    Remove-Item (Join-Path $fx.Repo 'new-untracked.txt')

    Invoke-Git $fx.Repo commit -q --allow-empty -m 'local only' | Out-Null
    $r = Invoke-Release @('dev', '-NoGitHub')
    Check 'Gate 3: HEAD not pushed exits 13' ($r.Code -eq 13) $r.Output
    Invoke-Git $fx.Repo push -q origin main | Out-Null

    $r = Invoke-Release @('dev', '-NoGitHub', '-SkipGitGates') @{ UE_ROOT = $ue57 }
    Check 'Gate 4: wrong UE version exits 14' ($r.Code -eq 14) $r.Output

    $usedLedger = New-TempLedger @("| 0.1.1 | 11111 | $($fx.FirstSha) | 2026-11-20T10:00:00Z | | | | | |")
    $r = Invoke-Release @('dev', '-NoGitHub', '-SkipGitGates', '-LedgerPath', $usedLedger)
    Check 'Gate 5: version already in ledger exits 15' ($r.Code -eq 15) $r.Output

    $r = Invoke-Release @('dev', '-NoGitHub', '-SkipGitGates') @{ FAKE_CHECK_FAIL = 'tests' }
    Check 'Gate 6: tests failed exits 16' ($r.Code -eq 16) $r.Output
    $r = Invoke-Release @('dev', '-NoGitHub', '-SkipGitGates') @{ FAKE_CHECK_FAIL = 'secrets' }
    Check 'Gate 7: secrets found exits 17' ($r.Code -eq 17) $r.Output
    # Gate 7 for real: og secrets --tracked over the fixture repo, and -Notes, before any upload.
    $r = Invoke-Release @('dry-run') @{ FAKE_CHECK_REAL = 'secrets' }
    Check 'Gate 7 (real og secrets --tracked): clean fixture passes' ($r.Code -eq 0 -and $r.Output -match 'gate 7 ok' -and $r.Output -match '--tracked') $r.Output
    $planted = 'STEAM_APPID=' + '7654321'  # og-secrets: allow (fake test ID)
    Set-Content (Join-Path $fx.Repo 'docs\leak.md') "config: $planted"
    Invoke-Git $fx.Repo add -A | Out-Null
    Invoke-Git $fx.Repo commit -q -m 'docs: planted fixture' | Out-Null
    $r = Invoke-Release @('dry-run') @{ FAKE_CHECK_REAL = 'secrets' }
    Check 'Gate 7 (real og secrets --tracked): planted tracked secret exits 17' ($r.Code -eq 17 -and $r.Output -match 'docs/leak\.md' -and $r.Output -notmatch '7654321') $r.Output
    Invoke-Git $fx.Repo rm -q 'docs/leak.md' | Out-Null
    Invoke-Git $fx.Repo commit -q -m 'docs: drop planted fixture' | Out-Null
    $steamLog = Join-Path $fx.Root 'steamcmd-args.log'
    $before = Get-Content -LiteralPath $fx.Ledger -Raw
    $r = Invoke-Release @('dev', '-SkipGitGates', '-Notes', "see $planted") @{ FAKE_CHECK_REAL = 'secrets'; FAKE_STEAMCMD_LOG = $steamLog }
    Check 'Gate 7: a secret in -Notes exits 17 before any upload' ($r.Code -eq 17 -and -not (Test-Path -LiteralPath $steamLog) -and $r.Output -notmatch '7654321') $r.Output
    Check 'Gate 7: -Notes finding records nothing' ((Get-Content -LiteralPath $fx.Ledger -Raw) -eq $before -and (Get-GhCalls).Count -eq 0)

    $r = Invoke-Release @('dev', '-NoGitHub', '-SkipGitGates') @{ FAKE_CHECK_FAIL = 'assets' }
    Check 'Gate 8: asset check failed exits 18' ($r.Code -eq 18) $r.Output

    $r = Invoke-Release @('dev', '-NoGitHub', '-SkipGitGates') @{ STEAM_APPID = $null }
    Check 'Gate 9: missing env var exits 19 and names it' ($r.Code -eq 19 -and $r.Output -match 'STEAM_APPID') $r.Output
    Check 'Gate 9: no values printed' ($r.Output -notmatch '1234561' -and $r.Output -notmatch 'fake_builder') $r.Output
    $r = Invoke-Release @('dry-run') @{ STEAM_APPID = $null; STEAM_DEPOT_WIN = $null; STEAM_BUILD_ACCOUNT = $null }
    Check 'Gate 9: dry-run also stops, naming every missing var' ($r.Code -eq 19 -and $r.Output -match 'STEAM_APPID' -and $r.Output -match 'STEAM_DEPOT_WIN' -and $r.Output -match 'STEAM_BUILD_ACCOUNT') $r.Output

    $stagingDir = Join-Path $fx.Work 'content'
    New-Item -ItemType Directory -Force -Path $stagingDir | Out-Null
    $lockPath = Join-Path $stagingDir 'locked.bin'
    Set-Content $lockPath 'held open'
    $lock = [IO.File]::Open($lockPath, 'Open', 'Read', 'None')
    try { $r = Invoke-Release @('dev', '-NoGitHub', '-SkipGitGates') }
    finally { $lock.Dispose() }
    Check 'Gate 10: staging not fresh exits 20' ($r.Code -eq 20) $r.Output

    # --- Review Focus 4: ignored-only dirt passes; full bump, commit, push path -----------------
    New-Fixture | Out-Null
    New-Item -ItemType Directory -Force -Path (Join-Path $fx.Repo '_release\x'), (Join-Path $fx.Repo '_scratch') | Out-Null
    Set-Content (Join-Path $fx.Repo '_release\x\post.bbcode') 'draft'
    Set-Content (Join-Path $fx.Repo '_scratch\note.txt') 'scratch'
    Set-Content (Join-Path $fx.Repo '.env') 'UNRELATED_SETTING=1'
    $r = Invoke-Release @('dev', '-NoGitHub')
    Check 'Review Focus 4: ignored-only dirty tree (_release/x, .env) passes the clean-tree gate' ($r.Code -eq 0) $r.Output
    $steamLog = Get-Content -LiteralPath (Join-Path $fx.Work 'steamcmd.log') -Raw
    $leak = '1234560|1234561|fake_builder|55555555'  # og-secrets: allow (fake test IDs)
    Check 'steamcmd output on the console is masked (no IDs, account name or SteamID)' ($r.Output -notmatch $leak -and $r.Output -match '<STEAM_APPID>' -and $r.Output -match "Logging in user '<STEAM_BUILD_ACCOUNT>'") $r.Output
    Check 'steamcmd.log is masked the same way' ($steamLog -notmatch $leak -and $steamLog -match '<STEAM_DEPOT_WIN>' -and $steamLog -match '\[U:1:\*\*\*\*\]') $steamLog
    $remoteIni = (Invoke-Git $fx.Remote show 'main:Game/Config/DefaultGame.ini') -join "`n"
    Check 'dev bumps ProjectVersion to 0.1.1, commits and pushes it' ($remoteIni -match 'ProjectVersion=0\.1\.1')
    $remoteLedger = (Invoke-Git $fx.Remote show 'main:docs/steam/BUILDS.md') -join "`n"
    Check 'dev commits and pushes the ledger row' ($remoteLedger -match '\| 0\.1\.1 \| 98765 \|')
    $bumpSha = (Invoke-Git $fx.Remote log -1 --format=%H 'main~1').Trim()
    Check 'ledger git_sha is the bump commit' ($remoteLedger -match [regex]::Escape($bumpSha))
    Check 'tree is clean after a dev run' (-not (Invoke-Git $fx.Repo status --porcelain))

    # --- Ledger commit or push fails after a real upload ---------------------------------------
    New-Fixture | Out-Null
    $hook = Join-Path $fx.Remote 'hooks\pre-receive'
    [IO.File]::WriteAllText($hook, "#!/bin/sh`nwhile read old new ref; do`n  if git log -1 --format=%s `"`$new`" | grep -q '^docs: record Steam build'; then echo 'ledger push rejected'; exit 1; fi`ndone`nexit 0`n")
    $r = Invoke-Release @('dev')
    Check 'Ledger push failure after upload exits 1 and says not to re-run' ($r.Code -eq 1 -and $r.Output -match 'BuildID 98765' -and $r.Output -match 'do not re-run' -and $r.Output -match 'BUILDS\.md') $r.Output
    Check 'Ledger push failure: no GitHub release or deployment' ((Get-GhCalls).Count -eq 0)

    # --- Review Focus 3: steamcmd lies with exit 0 -----------------------------------------------
    New-Fixture | Out-Null
    $before = Get-Content -LiteralPath $fx.Ledger -Raw
    $r = Invoke-Release @('dev', '-SkipGitGates') @{ FAKE_STEAMCMD_MODE = 'error-exit0' }
    Check 'Review Focus 3: ERROR! with exit 0 gives exit 30' ($r.Code -eq 30) $r.Output
    Check 'Review Focus 3: error-exit0 leaves the ledger unchanged' ((Get-Content -LiteralPath $fx.Ledger -Raw) -eq $before)
    Check 'Review Focus 3: error-exit0 makes no gh calls' ((Get-GhCalls).Count -eq 0)
    $r = Invoke-Release @('dev', '-SkipGitGates') @{ FAKE_STEAMCMD_MODE = 'no-buildid' }
    Check 'Review Focus 3: no BuildID gives exit 31' ($r.Code -eq 31) $r.Output
    Check 'Review Focus 3: no-buildid leaves the ledger unchanged' ((Get-Content -LiteralPath $fx.Ledger -Raw) -eq $before)
    Check 'Review Focus 3: no-buildid makes no gh calls' ((Get-GhCalls).Count -eq 0)
    $r = Invoke-Release @('dev', '-SkipGitGates') @{ FAKE_STEAMCMD_MODE = 'bad-branch' }
    Check 'bad-branch gives exit 30 and points at the missing development branch' ($r.Code -eq 30 -and $r.Output -match 'development' -and $r.Output -match 'SETUP-CHECKLIST') $r.Output
    Check 'bad-branch leaves the ledger unchanged and makes no gh calls' ((Get-Content -LiteralPath $fx.Ledger -Raw) -eq $before -and (Get-GhCalls).Count -eq 0)

    # --- Rendered VDF and staging from a dry run --------------------------------------------------
    New-Fixture | Out-Null
    New-Item -ItemType Directory -Force -Path (Join-Path $fx.Work 'content') | Out-Null
    Set-Content (Join-Path $fx.Work 'content\left-over-from-last-run.txt') 'old'
    $iniBefore = Get-Content (Join-Path $fx.Repo 'Game\Config\DefaultGame.ini') -Raw
    $r = Invoke-Release @('dry-run')
    Check 'dry-run exits 0' ($r.Code -eq 0) $r.Output
    $appVdf = Join-Path $fx.Work 'app_build_run.vdf'
    $depotVdf = Join-Path $fx.Work 'depot_build_run.vdf'
    $appText = if (Test-Path $appVdf) { Get-Content $appVdf -Raw } else { '' }
    $depotText = if (Test-Path $depotVdf) { Get-Content $depotVdf -Raw } else { '' }
    Check 'VDF: no {{ placeholders left' ($appText -and $depotText -and $appText -notmatch '\{\{' -and $depotText -notmatch '\{\{')
    Check 'VDF: single backslashes only' ($appText -notmatch '\\\\' -and $depotText -notmatch '\\\\' -and $appText -match [regex]::Escape('"ContentRoot" "' + (Join-Path $fx.Work 'content') + '\"')) $appText
    $excl = '*.pdb', '*.log', 'steam_appid.txt', 'Saved\*', 'Intermediate\*'
    Check 'VDF: depot excludes *.pdb, *.log, steam_appid.txt, Saved\*, Intermediate\*' (@($excl | Where-Object { $depotText -notmatch [regex]::Escape("`"FileExclusion`" `"$_`"") }).Count -eq 0) $depotText
    Check 'VDF: IDs come from env, SetLive development, Preview 0' ($appText -match '"AppID" "1234560"' -and $depotText -match '"DepotID" "1234561"' -and $appText -match '"SetLive" "development"' -and $appText -match '"Preview" "0"') $appText  # og-secrets: allow (fake test IDs)
    Check 'dry-run prints the steamcmd line without the account name' ($r.Output -match '\+run_app_build' -and $r.Output -notmatch 'fake_builder') $r.Output

    # The repo's .env wins over a conflicting inherited environment variable (another project's App ID).
    Set-Content (Join-Path $fx.Repo '.env') "STEAM_APPID=7777770`nSTEAM_DEPOT_WIN=7777771"  # og-secrets: allow (fake test IDs)
    $r = Invoke-Release @('dry-run')
    $appText = if (Test-Path $appVdf) { Get-Content $appVdf -Raw } else { '' }
    $depotText = if (Test-Path $depotVdf) { Get-Content $depotVdf -Raw } else { '' }
    Check '.env overrides a conflicting inherited STEAM_APPID / STEAM_DEPOT_WIN' ($r.Code -eq 0 -and $appText -match '"AppID" "7777770"' -and $depotText -match '"DepotID" "7777771"') $appText  # og-secrets: allow (fake test IDs)
    Check '.env override warns by variable name, never value' ($r.Output -match 'STEAM_APPID' -and $r.Output -match 'overrides' -and $r.Output -notmatch '1234560|7777770') $r.Output  # og-secrets: allow (fake test IDs)
    Remove-Item (Join-Path $fx.Repo '.env')
    Check 'dry-run does not bump the version' ((Get-Content (Join-Path $fx.Repo 'Game\Config\DefaultGame.ini') -Raw) -eq $iniBefore)
    $staged = @(Get-ChildItem -LiteralPath (Join-Path $fx.Work 'content') -Recurse -File | ForEach-Object { [IO.Path]::GetRelativePath((Join-Path $fx.Work 'content'), $_.FullName) })
    Check 'Staging was recreated (left-over file gone)' ($staged -notcontains 'left-over-from-last-run.txt') ($staged -join "`n")
    Check 'Staging has the Shipping build and no stale Development exe or pdb' (($staged -contains 'CombatSimulator\Binaries\Win64\CombatSimulator-Win64-Shipping.exe') -and ($staged -notcontains 'CombatSimulator\Binaries\Win64\CombatSimulator.exe') -and -not ($staged -match '\.pdb$')) ($staged -join "`n")

    $r = Invoke-Release @('preview')
    $appText = Get-Content $appVdf -Raw
    Check 'preview: steamcmd runs with Preview 1 and no SetLive' ($r.Code -eq 0 -and $appText -match '"Preview" "1"' -and $appText -match '"SetLive" ""') $r.Output
    Check 'preview: no ledger row, no gh calls' ((Get-LedgerRows $fx.Ledger).Count -eq 0 -and (Get-GhCalls).Count -eq 0)

    # --- Refusals ---------------------------------------------------------------------------------
    foreach ($b in 'default', ' default', 'Default', 'PUBLIC', 'qa', 'dev elopment', 'development;x') {
        $r = Invoke-Release @('dev', '-SkipGitGates', '-Branch', $b)
        Check "-Branch '$b' exits 40" ($r.Code -eq 40) $r.Output
    }
    $r = Invoke-Release @('dry-run', '-Branch', ' development ')
    Check "-Branch ' development ' is trimmed and accepted" ($r.Code -eq 0 -and (Get-Content (Join-Path $fx.Work 'app_build_run.vdf') -Raw) -match '"SetLive" "development"') $r.Output
    # Decision 0014: Claude Code runs the development-branch steps; only 'promoted' is refused.
    $r = Invoke-Release @('preview') @{ CLAUDECODE = '1' }
    Check "CLAUDECODE=1 allows 'preview' (development steps are Claude's, decision 0014)" ($r.Code -eq 0 -and $r.Output -notmatch 'own terminal') $r.Output
    $r = Invoke-Release @('dev', '-SkipGitGates', '-Branch', 'default') @{ CLAUDECODE = '1' }
    Check "CLAUDECODE=1 still cannot target 'default' (40)" ($r.Code -eq 40) $r.Output
    $r = Invoke-Release @('promoted', '0.0.1', '-SkipGitGates') @{ CLAUDECODE = '1' }
    Check "CLAUDECODE=1 refuses 'promoted' with 40" ($r.Code -eq 40) $r.Output
    Check 'Refusals made no ledger row and no gh calls' ((Get-LedgerRows $fx.Ledger).Count -eq 0 -and (Get-GhCalls).Count -eq 0)

    # --- ok: ledger row, previous development build replaced, release then deployment ------------
    New-Fixture | Out-Null
    $okLedger = New-TempLedger @(
        "| 0.0.8 | 11100 | $($fx.FirstSha) | 2026-11-01T10:00:00Z | 2026-11-01T10:00:00Z | 2026-11-02T10:00:00Z | 2026-11-03T10:00:00Z | | first |",
        "| 0.0.9 | 11111 | $($fx.FirstSha) | 2026-11-02T10:00:00Z | 2026-11-02T10:00:00Z | | | | second |")
    $r = Invoke-Release @('dev', '-SkipGitGates', '-LedgerPath', $okLedger, '-Notes', 'test run')
    Check 'ok: dev exits 0' ($r.Code -eq 0) $r.Output
    $rows = Get-LedgerRows $okLedger
    $new = @($rows | Where-Object version -eq '0.1.1')
    $prev = @($rows | Where-Object version -eq '0.0.9')
    Check 'ok: ledger row appended with BuildID, git SHA and development_live_utc' ($new.Count -eq 1 -and $new[0].build_id -eq '98765' -and $new[0].git_sha -eq $fx.Head -and $new[0].development_live_utc -match '^\d{4}-\d\d-\d\dT' -and -not $new[0].development_replaced_utc -and $new[0].notes -eq 'test run') (Get-Content $okLedger -Raw)
    Check "ok: previous development build's development_replaced_utc stamped" ($prev.Count -eq 1 -and $prev[0].development_replaced_utc -match '^\d{4}-\d\d-\d\dT')
    Check 'ok: the older row is untouched' ((@($rows | Where-Object version -eq '0.0.8')[0].development_replaced_utc) -eq '2026-11-02T10:00:00Z')
    $calls = Get-GhCalls
    $rel = if ($calls.Count -ge 1) { @($calls[0].args) } else { @() }
    Check 'ok: first gh call is release create v0.1.1 --prerelease' ($rel.Count -gt 3 -and $rel[0] -eq 'release' -and $rel[1] -eq 'create' -and $rel[2] -eq 'v0.1.1' -and $rel -contains '--prerelease') ($rel -join ' ')
    $notesIdx = [array]::IndexOf($rel, '--notes-file')
    $positional = @(for ($i = 3; $i -lt $rel.Count; $i++) { if ($rel[$i] -like '--*') { if ($rel[$i] -notmatch '=' -and $rel[$i] -notin '--prerelease', '--latest') { $i++ } } else { $rel[$i] } })
    Check 'ok: release has no binary assets' ($positional.Count -eq 0 -and -not ($rel -match '\.(exe|zip|pak|7z|ucas|utoc)$')) ($rel -join ' ')
    Check 'ok: release notes carry the BuildID' ($notesIdx -ge 0 -and $calls[0].notes -match '98765')
    $dep = if ($calls.Count -ge 2) { $calls[1] } else { $null }
    Check 'ok: then gh api repos/:owner/:repo/deployments for steam-development' ($null -ne $dep -and $dep.args[0] -eq 'api' -and ($dep.args -contains 'repos/:owner/:repo/deployments') -and ($dep.input | ConvertFrom-Json).environment -eq 'steam-development' -and ($dep.input | ConvertFrom-Json).ref -eq $fx.Head) ($calls | ConvertTo-Json -Depth 5)
    $st = if ($calls.Count -ge 3) { $calls[2] } else { $null }
    Check 'ok: then a success deployment status' ($null -ne $st -and ($st.args -contains 'repos/:owner/:repo/deployments/4242/statuses') -and ($st.input | ConvertFrom-Json).state -eq 'success') ($calls | ConvertTo-Json -Depth 5)
    Check 'ok: exactly three gh calls' ($calls.Count -eq 3)

    # --- upload-only: row with empty live columns, no deployment ---------------------------------
    New-Fixture | Out-Null
    $upLedger = New-TempLedger @("| 0.0.9 | 11111 | $($fx.FirstSha) | 2026-11-02T10:00:00Z | 2026-11-02T10:00:00Z | | | | |")
    $r = Invoke-Release @('upload-only', '-SkipGitGates', '-LedgerPath', $upLedger)
    Check 'upload-only exits 0' ($r.Code -eq 0) $r.Output
    $rows = Get-LedgerRows $upLedger
    $new = @($rows | Where-Object version -eq '0.1.1')
    Check 'upload-only: ledger row with empty live columns' ($new.Count -eq 1 -and $new[0].build_id -eq '98765' -and -not $new[0].development_live_utc -and -not $new[0].default_live_utc) (Get-Content $upLedger -Raw)
    Check 'upload-only: previous development build not marked replaced' (-not @($rows | Where-Object version -eq '0.0.9')[0].development_replaced_utc)
    $calls = Get-GhCalls
    Check 'upload-only: release created, no deployment call' ($calls.Count -eq 1 -and $calls[0].args[1] -eq 'create' -and -not (@($calls | ForEach-Object { $_.args }) -match 'deployments')) ($calls | ConvertTo-Json -Depth 5)
    Check 'upload-only: VDF SetLive is blank' ((Get-Content (Join-Path $fx.Work 'app_build_run.vdf') -Raw) -match '"SetLive" ""')

    # --- promote: drafts only, no tracked change --------------------------------------------------
    New-Fixture | Out-Null
    Set-Content (Join-Path $fx.Repo 'docs\devlog\2026-11-22-01-third.md') "# Third devlog`r`n"
    Invoke-Git $fx.Repo add -A | Out-Null
    Invoke-Git $fx.Repo commit -q -m 'feat: third change' | Out-Null
    $headSha = (Invoke-Git $fx.Repo rev-parse HEAD).Trim()
    $secondSha = (Invoke-Git $fx.Repo rev-parse HEAD~1).Trim()
    $rows = @(
        "| 0.0.9 | 11111 | $secondSha | 2026-11-02T10:00:00Z | 2026-11-02T10:00:00Z | 2026-11-03T10:00:00Z | 2026-11-02T12:00:00Z | | |",
        "| 0.1.1 | 98765 | $headSha | 2026-11-03T10:00:00Z | 2026-11-03T10:00:00Z | | | | |")
    Set-Content -LiteralPath $fx.Ledger -Value ($ledgerHeader + "`r`n" + ($rows -join "`r`n"))
    Invoke-Git $fx.Repo commit -q -am 'docs: ledger' | Out-Null
    $statusBefore = Invoke-Git $fx.Repo status --porcelain
    $r = Invoke-Release @('promote', '0.1.1')
    Check 'promote exits 0' ($r.Code -eq 0) $r.Output
    $draft = Join-Path $fx.Repo '_release\0.1.1'
    $names = 'post.bbcode', 'patch-notes.md', 'github-release.md', 'checklist.md'
    Check 'promote writes _release/<ver>/{post.bbcode,patch-notes.md,github-release.md,checklist.md}' (@($names | Where-Object { -not (Test-Path (Join-Path $draft $_)) }).Count -eq 0)
    $post = if (Test-Path (Join-Path $draft 'post.bbcode')) { Get-Content (Join-Path $draft 'post.bbcode') -Raw } else { '' }
    Check 'promote: post.bbcode has the [narrative] marker' ($post -match '\[narrative\]')
    $notes = if (Test-Path (Join-Path $draft 'patch-notes.md')) { Get-Content (Join-Path $draft 'patch-notes.md') -Raw } else { '' }
    Check 'promote: patch notes list commits and devlogs since the last default build' ($notes -match 'third change' -and $notes -notmatch 'second change' -and $notes -match '2026-11-22-01-third\.md') $notes
    $checklist = if (Test-Path (Join-Path $draft 'checklist.md')) { Get-Content (Join-Path $draft 'checklist.md') -Raw } else { '' }
    Check 'promote: checklist says set live on default, publish post, then run promoted' ($checklist -match 'default' -and $checklist -match 'post\.bbcode' -and $checklist -match 'release\.ps1 promoted 0\.1\.1') $checklist
    Check 'promote changes no tracked file' (-not (Invoke-Git $fx.Repo status --porcelain) -and -not $statusBefore)
    Check 'promote makes no gh calls' ((Get-GhCalls).Count -eq 0)

    # --- promoted --------------------------------------------------------------------------------
    $ledgerBefore = Get-Content -LiteralPath $fx.Ledger -Raw
    $r = Invoke-Release @('promoted', '0.1.1', '-SkipGitGates')
    Check 'promoted refuses while the [narrative] marker is unfilled, changing nothing' ($r.Code -eq 1 -and (Get-Content -LiteralPath $fx.Ledger -Raw) -eq $ledgerBefore -and (Get-GhCalls).Count -eq 0) $r.Output
    foreach ($f in 'post.bbcode', 'github-release.md') {
        $p = Join-Path $draft $f
        Set-Content -LiteralPath $p -Value ((Get-Content -LiteralPath $p -Raw).Replace('[narrative]', 'This build adds the third change.')) -NoNewline
    }
    $patchPath = Join-Path $draft 'patch-notes.md'
    $patchClean = Get-Content -LiteralPath $patchPath -Raw
    Add-Content -LiteralPath $patchPath -Value ('Contact: ' + 'tok ' + 'ghp_' + ('b' * 36))
    $r = Invoke-Release @('promoted', '0.1.1', '-SkipGitGates') @{ FAKE_CHECK_REAL = 'secrets' }
    Check 'promoted: a secret in _release/<ver>/ exits 1 and records nothing' ($r.Code -eq 1 -and (Get-Content -LiteralPath $fx.Ledger -Raw) -eq $ledgerBefore -and (Get-GhCalls).Count -eq 0 -and $r.Output -match 'patch-notes\.md') $r.Output
    Set-Content -LiteralPath $patchPath -Value $patchClean -NoNewline
    $r = Invoke-Release @('promoted', '0.1.1', '-SkipGitGates') @{ FAKE_CHECK_REAL = 'secrets' }
    Check 'promoted exits 0' ($r.Code -eq 0) $r.Output
    $rows = Get-LedgerRows $fx.Ledger
    $cur = @($rows | Where-Object version -eq '0.1.1')
    $old = @($rows | Where-Object version -eq '0.0.9')
    Check 'promoted: default_live_utc set' ($cur.Count -eq 1 -and $cur[0].default_live_utc -match '^\d{4}-\d\d-\d\dT') (Get-Content $fx.Ledger -Raw)
    Check "promoted: previous default's default_replaced_utc stamped" ($old.Count -eq 1 -and $old[0].default_replaced_utc -match '^\d{4}-\d\d-\d\dT')
    $calls = Get-GhCalls
    $edit = if ($calls.Count -ge 1) { @($calls[0].args) } else { @() }
    Check 'promoted: the clean draft passed the real secret scan' ($r.Output -match 'og secrets')
    Check 'promoted: release edited to not prerelease and --latest' ($edit.Count -gt 2 -and $edit[0] -eq 'release' -and $edit[1] -eq 'edit' -and $edit[2] -eq 'v0.1.1' -and $edit -contains '--prerelease=false' -and $edit -contains '--latest') ($edit -join ' ')
    Check 'promoted: release body is the published text' ($calls.Count -ge 1 -and $calls[0].notes -match 'third change')
    $dep = if ($calls.Count -ge 2) { $calls[1] } else { $null }
    Check 'promoted: steam-default deployment recorded' ($null -ne $dep -and ($dep.input | ConvertFrom-Json).environment -eq 'steam-default' -and ($dep.input | ConvertFrom-Json).ref -eq $headSha) ($calls | ConvertTo-Json -Depth 5)
    $st = if ($calls.Count -ge 3) { $calls[2] } else { $null }
    Check 'promoted: success deployment status' ($null -ne $st -and ($st.input | ConvertFrom-Json).state -eq 'success')

    # --- status: ledger versus Steam, and open setup items ----------------------------------------
    $r = Invoke-Release @('status') @{ FAKE_STEAMCMD_BRANCHES = 'public=11111,development=98765' }
    Check 'status: reports a mismatch when the ledger disagrees with Steam' ($r.Code -eq 0 -and $r.Output -match 'MISMATCH' -and $r.Output -match 'default') $r.Output
    $r = Invoke-Release @('status') @{ FAKE_STEAMCMD_BRANCHES = 'public=98765,development=98765' }
    Check 'status: no mismatch when they agree' ($r.Code -eq 0 -and $r.Output -notmatch 'MISMATCH') $r.Output
    Check 'status: lists open SETUP-CHECKLIST items only' ($r.Output -match 'Add the depot' -and $r.Output -match 'Build account' -and $r.Output -notmatch 'Create the app') $r.Output
    $statusLog = Join-Path $fx.Root 'status-args.log'
    $r = Invoke-Release @('status') @{ FAKE_STEAMCMD_BRANCHES = 'public=98765,development=98765'; FAKE_STEAMCMD_LOG = $statusLog }
    Check 'status: steamcmd never prompts for a password' ((Test-Path -LiteralPath $statusLog) -and (Get-Content -LiteralPath $statusLog -Raw) -match '\+@NoPromptForPassword 1') $r.Output
    $r = Invoke-Release @('status') @{ FAKE_STEAMCMD_INFO = 'fail' }
    Check 'status: steamcmd failing is "Steam state: unknown", not MISMATCH' ($r.Code -eq 0 -and $r.Output -match 'Steam state: unknown' -and $r.Output -notmatch 'MISMATCH') $r.Output
    $r = Invoke-Release @('status') @{ FAKE_STEAMCMD_INFO = 'nobranches' }
    Check 'status: no branches block is "Steam state: unknown", not MISMATCH' ($r.Code -eq 0 -and $r.Output -match 'Steam state: unknown' -and $r.Output -notmatch 'MISMATCH') $r.Output
    $r = Invoke-Release @('status') @{ STEAM_BUILD_ACCOUNT = $null }
    Check 'status without Steam access still shows the ledger and checklist' ($r.Code -eq 0 -and $r.Output -match 'Add the depot' -and $r.Output -match '0\.1\.1') $r.Output
}
finally {
    Remove-Item -LiteralPath $tmp -Recurse -Force -ErrorAction SilentlyContinue
}

Write-Host ''
if ($script:failures) { Write-Host "$($script:failures) release test(s) failed, $($script:passes) passed."; exit 1 }
Write-Host "All $($script:passes) release tests passed."
