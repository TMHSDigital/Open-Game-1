# Fixed-camera capture: renders a PNG per named camera in docs/captures/cameras.json.
#
# Runs the game (the editor binary in -game mode) with the dev-only CSCapture module, which reads a
# job file, puts a camera at each position, saves a PNG and quits. One UE process per map.
# -benchmark -fps=30 gives a fixed timestep, so the settle time before each image is the same in game
# time however slowly frames render.
#
# Optional camera fields:
#   hidePlayer     false to show the player's pawn (default true: the pawn is hidden so it does not
#                  block a map view). Character cameras set false.
#   view           "camera" (default): a fixed camera at location/rotation/fov. "player": the
#                  player's own view through its pawn's camera, so owner-only visibility applies (the
#                  first-person body with its head hidden); rotation becomes the aim, location and
#                  fov are not used.
#   commands       console commands run when the shot starts, e.g. "ke * SetView FirstPerson"
#                  (KE calls a function by name on every object in the world that has it).
#   settleSeconds  game seconds to wait at this shot before the image (default 1, or the profile's
#                  SettleSeconds for a map's first shot).
#
# A camera whose map does not exist (Game/Content/<path>.umap) is skipped and recorded under
# "skipped" in capture.json; it does not fail the run.
#
# Output in -OutDir: <camera>.png per captured camera and capture.json
# { commit (40-char SHA), dirty, date, ueVersion, cameras: [...], skipped: [...] }.
# Raw PNGs are large: write them to a scratch dir (_scratch/captures/<timestamp>/ is gitignored) and
# convert the ones worth publishing with og.devlog.to_webp.
#
# Usage:
#   pwsh tools/ue/capture.ps1 -Cameras all -OutDir _scratch/captures/20261122-1200
#   pwsh tools/ue/capture.ps1 -Cameras stage-overview,stage-closeup -OutDir <dir>

param(
    [string[]]$Cameras = @('all'),
    [Parameter(Mandatory)][string]$OutDir,
    # Alternative camera list (tests use this); default docs/captures/cameras.json.
    [string]$CamerasFile = '',
    [int]$TimeoutSec = 900,
    [int]$Retries = 1
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# Applied to every capture so images line up across dates. Changing these changes how every later
# capture looks: say so in the commit message.
$CaptureProfile = @{
    SettleSeconds = 3
    Fps           = 30
    Window        = @(1600, 900)
    Commands      = @(
        'sg.ViewDistanceQuality 3', 'sg.AntiAliasingQuality 3', 'sg.ShadowQuality 3',
        'sg.GlobalIlluminationQuality 3', 'sg.ReflectionQuality 3', 'sg.PostProcessQuality 3',
        'sg.TextureQuality 3', 'sg.EffectsQuality 3', 'sg.FoliageQuality 3', 'sg.ShadingQuality 3',
        'r.MotionBlurQuality 0', 'r.ScreenPercentage 100', 'r.HighResScreenshotDelay 4'
    )
}

# "/Game/Maps/L_CaptureStage" -> Game/Content/Maps/L_CaptureStage.umap
function Get-MapFile([string]$Map) {
    if ($Map -notmatch '^/Game/(.+)$') { return $null }
    Join-Path $GameDir ('Content\' + $Matches[1].Replace('/', '\') + '.umap')
}

function Assert-Camera($Cam) {
    foreach ($field in 'name', 'map', 'location', 'rotation', 'fov', 'resolution') {
        if (-not $Cam.PSObject.Properties[$field]) { throw "Camera '$($Cam.name)' has no '$field'." }
    }
    if ($Cam.name -notmatch '^[a-z0-9][a-z0-9-]*$') { throw "Camera name '$($Cam.name)' must be lowercase kebab-case." }
    if (@($Cam.location).Count -ne 3 -or @($Cam.rotation).Count -ne 3 -or @($Cam.resolution).Count -ne 2) {
        throw "Camera '$($Cam.name)': location and rotation need 3 numbers, resolution needs 2."
    }
    if ($Cam.PSObject.Properties['view'] -and $Cam.view -notin @('camera', 'player')) {
        throw "Camera '$($Cam.name)': view must be 'camera' or 'player', not '$($Cam.view)'."
    }
    if ($Cam.PSObject.Properties['view'] -and $Cam.view -eq 'player' -and
        $Cam.PSObject.Properties['hidePlayer'] -and $Cam.hidePlayer) {
        throw "Camera '$($Cam.name)': a player view cannot hide the player."
    }
}

# PNG width and height from the IHDR chunk (big-endian at bytes 16..23).
function Get-PngSize([string]$Path) {
    $b = [IO.File]::ReadAllBytes($Path)
    $w = ([int]$b[16] -shl 24) -bor ([int]$b[17] -shl 16) -bor ([int]$b[18] -shl 8) -bor [int]$b[19]
    $h = ([int]$b[20] -shl 24) -bor ([int]$b[21] -shl 16) -bor ([int]$b[22] -shl 8) -bor [int]$b[23]
    , @($w, $h)
}

function Invoke-CaptureRun([string]$Map, [hashtable]$Job, [string]$RunDir, [string]$RunName) {
    New-Item -ItemType Directory -Force -Path $RunDir | Out-Null
    $jobPath = Join-Path $RunDir 'job.json'
    $Job | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $jobPath -Encoding utf8
    New-Item -ItemType Directory -Force -Path $LogsDir | Out-Null
    $log = Join-Path $LogsDir "Capture_$RunName.log"

    for ($attempt = 1; $attempt -le $Retries + 1; $attempt++) {
        Remove-Item -LiteralPath (Join-Path $RunDir 'result.json') -ErrorAction SilentlyContinue
        $ueArgs = @(
            "`"$Proj`"", $Map, '-game', "-CSCapture=`"$jobPath`"",
            '-benchmark', "-fps=$($CaptureProfile.Fps)", '-FixedSeed',
            '-windowed', "-ResX=$($CaptureProfile.Window[0])", "-ResY=$($CaptureProfile.Window[1])",
            '-nosound', '-unattended', '-NoSplash', '-NoLoadingScreen', '-log', "-abslog=`"$log`""
        )
        Write-Host ">> capture run '$RunName' (attempt $attempt): $Map"
        $proc = Start-Process -FilePath $Editor -ArgumentList $ueArgs -PassThru
        if (-not $proc.WaitForExit($TimeoutSec * 1000)) {
            Write-Warning "Run '$RunName' timed out after $TimeoutSec s; killing it."
            $proc.Kill($true)
            continue
        }
        $resultPath = Join-Path $RunDir 'result.json'
        if (Test-Path -LiteralPath $resultPath) {
            $result = Get-Content -LiteralPath $resultPath -Raw | ConvertFrom-Json
            if ($result.success) { return $result }
            Write-Warning "Run '$RunName' reported failure: $($result.message)"
        }
        else {
            Write-Warning "Run '$RunName' exited ($($proc.ExitCode)) without a result file. Log: $log"
        }
    }
    throw "Capture run '$RunName' failed after $($Retries + 1) attempts. Log: $log"
}

try {
    . "$PSScriptRoot\env.ps1"
    Assert-Engine
    Assert-Project
    Assert-PathExists (Join-Path $GameDir 'Binaries\Win64\UnrealEditor-CSCapture.dll') 'CSCapture module binary (run tools/ue/build.ps1)'
    if ((Get-ProjectEditorProcess).Count -gt 0) {
        Write-Warning 'The editor has this project open. Capture still runs, but closing it frees GPU memory and avoids shader-compile contention.'
    }

    if (-not $CamerasFile) { $CamerasFile = Join-Path $RepoRoot 'docs\captures\cameras.json' }
    Assert-PathExists $CamerasFile 'Camera list'
    $all = @(Get-Content -LiteralPath $CamerasFile -Raw | ConvertFrom-Json)
    foreach ($cam in $all) { Assert-Camera $cam }
    $dupes = @($all | Group-Object name | Where-Object Count -gt 1)
    if ($dupes.Count -gt 0) { throw "Duplicate camera names: $(($dupes | ForEach-Object Name) -join ', ')." }

    # -Cameras accepts "all", several names, or one comma-separated string.
    $wanted = @($Cameras | ForEach-Object { $_ -split ',' } | ForEach-Object { $_.Trim() } | Where-Object { $_ })
    if ($wanted -contains 'all') { $selected = $all }
    else {
        $unknown = @($wanted | Where-Object { $_ -notin @($all | ForEach-Object name) })
        if ($unknown.Count -gt 0) { throw "Unknown camera(s): $($unknown -join ', '). Known: $(($all | ForEach-Object name) -join ', ')." }
        $selected = @($all | Where-Object { $_.name -in $wanted })
    }
    if ($selected.Count -eq 0) { throw 'No cameras selected.' }

    $OutDir = [IO.Path]::GetFullPath($OutDir, (Get-Location).Path)
    New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
    $runsDir = Join-Path $OutDir '_runs'

    $captured = [System.Collections.Generic.List[object]]::new()
    $skipped = [System.Collections.Generic.List[object]]::new()
    $present = [System.Collections.Generic.List[object]]::new()
    foreach ($cam in $selected) {
        $mapFile = Get-MapFile $cam.map
        if (-not $mapFile -or -not (Test-Path -LiteralPath $mapFile)) {
            Write-Warning "Skipping camera '$($cam.name)': map $($cam.map) not found."
            $skipped.Add([ordered]@{ name = $cam.name; map = $cam.map; reason = 'map not found' })
        }
        else { $present.Add($cam) }
    }

    foreach ($group in ($present | Group-Object map)) {
        $mapName = Split-Path -Leaf $group.Name
        $runDir = Join-Path $runsDir $mapName
        $shots = foreach ($cam in $group.Group) {
            $loc = @($cam.location); $rot = @($cam.rotation); $res = @($cam.resolution)
            $shot = @{
                name     = $cam.name
                location = @{ x = [double]$loc[0]; y = [double]$loc[1]; z = [double]$loc[2] }
                rotation = @{ pitch = [double]$rot[0]; yaw = [double]$rot[1]; roll = [double]$rot[2] }
                fov      = [double]$cam.fov
                width    = [int]$res[0]
                height   = [int]$res[1]
            }
            # Optional fields (see the header): only passed when the camera sets them.
            if ($cam.PSObject.Properties['hidePlayer']) { $shot.bHidePlayer = [bool]$cam.hidePlayer }
            if ($cam.PSObject.Properties['view']) { $shot.view = [string]$cam.view }
            if ($cam.PSObject.Properties['commands']) { $shot.commands = @($cam.commands | ForEach-Object { [string]$_ }) }
            if ($cam.PSObject.Properties['settleSeconds']) { $shot.settleSeconds = [double]$cam.settleSeconds }
            $shot
        }
        $job = @{
            outputDir     = $runDir.Replace('\', '/')
            settleSeconds = [double]$CaptureProfile.SettleSeconds
            commands      = @($CaptureProfile.Commands)
            shots         = @($shots)
        }
        $null = Invoke-CaptureRun $group.Name $job $runDir $mapName

        foreach ($cam in $group.Group) {
            $src = Join-Path $runDir "$($cam.name).png"
            Assert-PathExists $src "Image for camera '$($cam.name)'"
            $dst = Join-Path $OutDir "$($cam.name).png"
            Move-Item -LiteralPath $src -Destination $dst -Force
            $size = Get-PngSize $dst
            $res = @($cam.resolution)
            if ($size[0] -ne [int]$res[0] -or $size[1] -ne [int]$res[1]) {
                throw "Camera '$($cam.name)' image is $($size[0])x$($size[1]), expected $($res[0])x$($res[1])."
            }
            $captured.Add([ordered]@{
                name       = $cam.name
                map        = $cam.map
                file       = "$($cam.name).png"
                location   = @($cam.location)
                rotation   = @($cam.rotation)
                fov        = $cam.fov
                resolution = @($size[0], $size[1])
                sha256     = (Get-FileHash -LiteralPath $dst -Algorithm SHA256).Hash.ToLowerInvariant()
            })
        }
    }
    if (Test-Path -LiteralPath $runsDir) { Remove-Item -LiteralPath $runsDir -Recurse -Force }

    $commit = (git -C $RepoRoot rev-parse HEAD).Trim()
    if ($LASTEXITCODE -ne 0 -or $commit -notmatch '^[0-9a-f]{40}$') { throw "Could not read the commit SHA (got '$commit')." }
    $meta = [ordered]@{
        commit    = $commit
        dirty     = [bool](git -C $RepoRoot status --porcelain)
        date      = (Get-Date).ToString('yyyy-MM-ddTHH:mm:sszzz')
        ueVersion = Get-UeVersion
        cameras   = @($captured)
        skipped   = @($skipped)
    }
    $meta | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $OutDir 'capture.json') -Encoding utf8

    Write-Host "Captured $($captured.Count) camera(s), skipped $($skipped.Count), into $OutDir"
}
catch {
    Write-Error $_.Exception.Message -ErrorAction Continue
    exit 1
}
