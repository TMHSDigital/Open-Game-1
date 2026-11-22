# End-to-end test for tools/ue/capture.ps1: runs a real capture of the seed cameras against
# L_CaptureStage, plus one camera whose map does not exist, and checks the results.
#
# Asserts:
#   - both seed cameras (stage-overview, stage-closeup) produce a PNG at their resolution;
#   - capture.json records a full 40-char commit SHA, the date, the UE version and the cameras;
#   - the camera with a missing map is listed under "skipped" and does not fail the run.
#
# Needs a current editor build (tools/ue/build.ps1). Takes a minute or two (one UE process).
# Output goes to _scratch/test-capture/<timestamp>/ (gitignored); it is kept for inspection.
#
# Usage: pwsh tools/ue/test-capture.ps1

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$failures = 0
function Check([string]$Name, [bool]$Ok, [string]$Detail = '') {
    if ($Ok) { Write-Host "ok   $Name" }
    else { Write-Host "FAIL $Name $Detail"; $script:failures++ }
}

# PNG width and height from the IHDR chunk (big-endian at bytes 16..23), without any image library.
function Get-PngSize([string]$Path) {
    $bytes = [IO.File]::ReadAllBytes($Path)
    $w = ([int]$bytes[16] -shl 24) -bor ([int]$bytes[17] -shl 16) -bor ([int]$bytes[18] -shl 8) -bor [int]$bytes[19]
    $h = ([int]$bytes[20] -shl 24) -bor ([int]$bytes[21] -shl 16) -bor ([int]$bytes[22] -shl 8) -bor [int]$bytes[23]
    , @($w, $h)
}

try {
    $seedFile = Join-Path $RepoRoot 'docs\captures\cameras.json'
    if (-not (Test-Path -LiteralPath $seedFile)) { throw "Missing $seedFile." }
    $seed = @(Get-Content -LiteralPath $seedFile -Raw | ConvertFrom-Json)
    $seedNames = @('stage-overview', 'stage-closeup')
    foreach ($n in $seedNames) {
        Check "cameras.json has $n" ([bool]($seed | Where-Object { $_.name -eq $n }))
    }

    $outDir = Join-Path $RepoRoot ("_scratch\test-capture\{0}" -f (Get-Date -Format 'yyyyMMdd-HHmmss'))
    New-Item -ItemType Directory -Force -Path $outDir | Out-Null

    # The seed cameras plus one on a map that does not exist.
    $missing = [pscustomobject]@{
        name = 'missing-map'; map = '/Game/Maps/L_DoesNotExist'
        location = @(0, 0, 300); rotation = @(-30, 0, 0); fov = 70; resolution = @(640, 360); since = '2026-11-22'
    }
    $camerasFile = Join-Path $outDir 'cameras.test.json'
    ConvertTo-Json -InputObject @(@($seed | Where-Object { $_.name -in $seedNames }) + $missing) -Depth 6 |
        Set-Content -LiteralPath $camerasFile -Encoding utf8

    & pwsh -NoProfile -File (Join-Path $PSScriptRoot 'capture.ps1') -Cameras all -OutDir $outDir -CamerasFile $camerasFile
    Check 'capture.ps1 exits 0 with a missing-map camera' ($LASTEXITCODE -eq 0) "(exit $LASTEXITCODE)"

    foreach ($n in $seedNames) {
        $png = Join-Path $outDir "$n.png"
        $exists = Test-Path -LiteralPath $png
        Check "$n.png exists" $exists
        if ($exists) {
            $cam = $seed | Where-Object { $_.name -eq $n }
            $size = Get-PngSize $png
            Check "$n.png is $($cam.resolution[0])x$($cam.resolution[1])" (($size[0] -eq $cam.resolution[0]) -and ($size[1] -eq $cam.resolution[1])) "(got $($size[0])x$($size[1]))"
        }
    }
    Check 'missing-map.png was not written' (-not (Test-Path -LiteralPath (Join-Path $outDir 'missing-map.png')))

    $metaPath = Join-Path $outDir 'capture.json'
    $hasMeta = Test-Path -LiteralPath $metaPath
    Check 'capture.json exists' $hasMeta
    if ($hasMeta) {
        $meta = Get-Content -LiteralPath $metaPath -Raw | ConvertFrom-Json
        Check 'capture.json commit is a 40-char SHA' ([string]$meta.commit -cmatch '^[0-9a-f]{40}$') "(got '$($meta.commit)')"
        Check 'capture.json has a date' (-not [string]::IsNullOrWhiteSpace([string]$meta.date))
        Check 'capture.json has the UE version' ([string]$meta.ueVersion -match '^5\.8\.')  "(got '$($meta.ueVersion)')"
        $captured = @($meta.cameras | ForEach-Object { $_.name })
        Check 'capture.json lists both seed cameras' ((@($seedNames | Where-Object { $_ -in $captured })).Count -eq 2) "(got $($captured -join ', '))"
        $skipped = @($meta.skipped | Where-Object { $_.name -eq 'missing-map' })
        Check 'missing-map is listed under skipped' ($skipped.Count -eq 1)
        Check 'missing-map is not listed as captured' ('missing-map' -notin $captured)
    }
    Write-Host "Output kept in $outDir"
}
catch {
    Write-Host "FAIL $($_.Exception.Message)"
    $failures++
}

if ($failures -gt 0) { Write-Error "$failures capture check(s) failed." -ErrorAction Continue; exit 1 }
Write-Host 'All capture checks passed.'
