# Runs a Blender Python script headless and exits with Blender's exit code.
#
# Blender runs in background mode with factory settings (no user add-ons or preferences), and
# --python-exit-code 1 so an uncaught Python error makes Blender exit non-zero.
#
# Usage (both forms work; tools/tests/test_skeleton_reference.py runs them):
#   pwsh -NoProfile -File tools/blender/run.ps1 -Script <script.py> [-Blend <file.blend>] [-- <script args>]
#   & tools/blender/run.ps1 -Script <script.py> [-Blend <file.blend>] [-- <script args>]   (from pwsh)
#
# -UserAddons keeps the user's preferences and extensions instead of factory settings: for a script
# that needs an installed extension (Art/Characters/Base/build_human.py needs MPFB).
#
# Everything after -- is passed to the script (it reads them from sys.argv after "--").
#
# Override the Blender location with $env:BLENDER_PATH
# (default C:\Program Files\Blender Foundation\Blender 5.1\blender.exe).

# Why there is no param() block: if this script declared parameters, "pwsh -File run.ps1 ... --"
# would fail with "parameter name '' is ambiguous" (pwsh -File treats a bare "--" as a parameter
# with an empty name). Without declared parameters every argument lands in $args, including the
# "--", and is parsed by hand below. Called in-process (& run.ps1 ...), PowerShell drops the "--"
# itself; the first argument that is not -Script or -Blend then starts the script arguments.
$Script = $null
$Blend = $null
$UserAddons = $false
$ScriptArgs = @()
for ($i = 0; $i -lt $args.Count; $i++) {
    $a = [string]$args[$i]
    if ($a -eq '--') { $ScriptArgs = @($args | Select-Object -Skip ($i + 1) | ForEach-Object { [string]$_ }); break }
    elseif ($a -ieq '-Script' -and $i + 1 -lt $args.Count) { $Script = [string]$args[++$i] }
    elseif ($a -ieq '-Blend' -and $i + 1 -lt $args.Count) { $Blend = [string]$args[++$i] }
    elseif ($a -ieq '-UserAddons') { $UserAddons = $true }
    else { $ScriptArgs = @($args | Select-Object -Skip $i | ForEach-Object { [string]$_ }); break }
}

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$DefaultBlender = 'C:\Program Files\Blender Foundation\Blender 5.1\blender.exe'
$Blender = if ($env:BLENDER_PATH) { $env:BLENDER_PATH } else { $DefaultBlender }

if (-not (Test-Path -LiteralPath $Blender)) {
    Write-Error "Blender not found at '$Blender'. Install Blender 5.1 or set BLENDER_PATH." -ErrorAction Continue
    exit 1
}
if (-not $Script) {
    Write-Error 'Usage: run.ps1 -Script <script.py> [-Blend <file.blend>] [-- <script args>]' -ErrorAction Continue
    exit 1
}
if (-not (Test-Path -LiteralPath $Script)) {
    Write-Error "Script not found: '$Script'." -ErrorAction Continue
    exit 1
}

$argList = @('-b')
if ($Blend) {
    if (-not (Test-Path -LiteralPath $Blend)) {
        Write-Error "Blend file not found: '$Blend'." -ErrorAction Continue
        exit 1
    }
    $argList += (Resolve-Path -LiteralPath $Blend).Path
}
if (-not $UserAddons) { $argList += '--factory-startup' }
$argList += @('--python-exit-code', '1', '--python', (Resolve-Path -LiteralPath $Script).Path)
if ($ScriptArgs.Count -gt 0) { $argList += @('--') + $ScriptArgs }

Write-Host ">> $Blender $($argList -join ' ')"
& $Blender @argList
exit $LASTEXITCODE
