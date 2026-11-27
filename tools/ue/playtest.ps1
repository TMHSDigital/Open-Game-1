# Scripted play-test: the game plays itself through scenarios while this script records it, then
# cuts one clip and one contact sheet per segment and writes a summary with the game's own numbers.
#
# The game side is UCSPlaytestRunner (Game/Source/CombatSimulator/Playtest): started with
# -CSPlaytest=<scenarios> it drives the player (movement, punches, knockdowns), frames the whole
# body with a review camera, draws the segment's label on screen and writes timed events to a
# JSON-lines log, then quits. This script only launches, records and cuts; it sends no keys, so the
# result does not depend on window focus or on where the mouse is.
#
# Needs: ffmpeg on PATH, an unlocked desktop with the game window left uncovered (its rectangle is
# captured from the screen), and for
# -Target Editor editor binaries built from the current source (tools/ue/build.ps1).
#
# Usage:
#   pwsh tools/ue/playtest.ps1                          # every scenario, editor binaries
#   pwsh tools/ue/playtest.ps1 -Scenario punch,stance   # some
#   pwsh tools/ue/playtest.ps1 -Target Packaged         # the last package (tools/ue/package.ps1)
#   pwsh tools/ue/playtest.ps1 -List                    # the scenario names
#
# Output (default _scratch/playtest/<time>-<scenario>/, gitignored):
#   recording.mp4      the whole run
#   events.jsonl       the game's log
#   NN-<label>.mp4     one clip per segment
#   NN-<label>.png     its contact sheet (8 frames)
#   overview.png       24 frames of the whole run
#   summary.md         segments, times, the game's numbers, clip and sheet names
#
# Exit code: 0 when the game played to the end; 1 otherwise (the summary says how far it got).

param(
    [string[]]$Scenario = @('all'),
    [ValidateSet('Editor', 'Packaged')]
    [string]$Target = 'Editor',
    [string]$Map = '/Game/Maps/L_Plane',
    [string]$OutDir,
    [int]$Width = 1280,
    [int]$Height = 720,
    [int]$Fps = 30,
    [int]$TimeoutSeconds = 900,
    [switch]$NoClips,
    [switch]$List
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\env.ps1"

# -Scenario punch,stance arrives as a list; the game takes one comma-separated value.
$Scenario = ($Scenario | ForEach-Object { $_ -split ',' } | Where-Object { $_ }) -join ','
$Scenarios = @('locomotion', 'stance', 'punch', 'knockdown', 'fight', 'moves', 'stamina', 'body')
# Scenarios on their own map, left out of 'all': gym plays on the movement gym.
$MapScenarios = @{ gym = '/Game/Maps/L_MovementGym' }
if (-not $PSBoundParameters.ContainsKey('Map') -and $MapScenarios.ContainsKey([string]$Scenario)) { $Map = $MapScenarios[[string]$Scenario] }
if ($List) {
    $Scenarios | ForEach-Object { $_ }
    $MapScenarios.Keys | ForEach-Object { "$_ (on $($MapScenarios[$_]))" }
    exit 0
}

function Get-Slug([string]$Text) {
    $slug = ($Text.ToLowerInvariant() -replace '[^a-z0-9]+', '-').Trim('-')
    if ($slug.Length -gt 60) { $slug = $slug.Substring(0, 60).Trim('-') }
    return $slug
}

function Invoke-Ffmpeg([string[]]$FfArgs) {
    $out = & $Ffmpeg -hide_banner -loglevel error @FfArgs 2>&1
    if ($LASTEXITCODE -ne 0) { throw "ffmpeg failed: $($FfArgs -join ' ')`n$out" }
}

Add-Type -Namespace CSPlaytest -Name Win -MemberDefinition @'
[DllImport("user32.dll")] public static extern bool SetForegroundWindow(System.IntPtr h);
[DllImport("user32.dll")] public static extern bool ShowWindow(System.IntPtr h, int c);
[DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, System.UIntPtr extra);
[DllImport("user32.dll")] public static extern bool IsIconic(System.IntPtr h);
[DllImport("user32.dll")] public static extern bool SetWindowPos(System.IntPtr h, System.IntPtr after, int x, int y, int cx, int cy, uint flags);
[StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left; public int Top; public int Right; public int Bottom; }
[DllImport("user32.dll")] public static extern bool GetClientRect(System.IntPtr h, out RECT r);
[StructLayout(LayoutKind.Sequential)] public struct POINT { public int X; public int Y; }
[DllImport("user32.dll")] public static extern bool ClientToScreen(System.IntPtr h, ref POINT p);
'@

$Ffmpeg = (Get-Command ffmpeg -ErrorAction SilentlyContinue).Source
if (-not $Ffmpeg) { throw 'ffmpeg is not on PATH (winget install ffmpeg).' }

$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
if (-not $OutDir) { $OutDir = Join-Path $RepoRoot "_scratch\playtest\$stamp-$(Get-Slug $Scenario)" }
New-Item -ItemType Directory -Force $OutDir | Out-Null
$OutDir = (Resolve-Path $OutDir).Path
$EventsPath = Join-Path $OutDir 'events.jsonl'
$Recording = Join-Path $OutDir 'recording.mp4'
$FfLog = Join-Path $OutDir 'ffmpeg.log'

# ---------------------------------------------------------------- launch
$gameArgs = @('-windowed', "-ResX=$Width", "-ResY=$Height", "-CSPlaytest=$Scenario", "-CSPlaytestLog=`"$EventsPath`"", '-NoSplash')
if ($Target -eq 'Editor') {
    Assert-Engine
    Assert-Project
    Assert-BinariesFresh | Out-Null
    $exe = $Editor
    $gameArgs = @("`"$Proj`"", $Map, '-game') + $gameArgs
}
else {
    $exe = Join-Path $PackageDir "$ProjectName.exe"
    Assert-PathExists $exe 'the packaged game (tools/ue/package.ps1)'
    $gameArgs = @($Map) + $gameArgs
}
Write-Host "Starting $Target game: scenario '$Scenario' -> $OutDir"
$game = Start-Process -FilePath $exe -ArgumentList $gameArgs -PassThru

# The game window: the first window of the process (or a child) with a title.
$deadline = (Get-Date).AddSeconds(300)
$window = $null
while (-not $window -and (Get-Date) -lt $deadline) {
    Start-Sleep -Milliseconds 500
    if ($game.HasExited) { throw "The game exited (code $($game.ExitCode)) before showing a window." }
    $window = Get-Process -ErrorAction SilentlyContinue |
        Where-Object { $_.MainWindowHandle -ne 0 -and $_.MainWindowTitle -like "$ProjectName*" -and ($_.Id -eq $game.Id -or $_.ProcessName -like "$ProjectName*") } |
        Select-Object -First 1
}
if (-not $window) { throw 'The game window did not appear in 300 s.' }
$title = $window.MainWindowTitle
$hwnd = $window.MainWindowHandle
Write-Host "Window '$title'"

# In front and not minimised: the recording takes what is on the screen in the window's rectangle.
[CSPlaytest.Win]::keybd_event(0x12, 0, 0, [UIntPtr]::Zero); [CSPlaytest.Win]::keybd_event(0x12, 0, 2, [UIntPtr]::Zero)
[CSPlaytest.Win]::ShowWindow($hwnd, 9) | Out-Null
[CSPlaytest.Win]::SetForegroundWindow($hwnd) | Out-Null
# Topmost while it records: anything that opens over it (a notification, another window) would
# be recorded in its place. The window closes with the game, so nothing is left topmost.
# SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE; HWND_TOPMOST is -1.
[CSPlaytest.Win]::SetWindowPos($hwnd, [IntPtr](-1), 0, 0, 0, 0, 0x0013) | Out-Null

# ---------------------------------------------------------------- record
# The game's client area, taken from the screen: gdigrab's own window capture (title=) gets black
# frames from a DirectX window, so the rectangle the window covers on the desktop is recorded.
Start-Sleep -Milliseconds 800
$rect = New-Object CSPlaytest.Win+RECT
[CSPlaytest.Win]::GetClientRect($hwnd, [ref]$rect) | Out-Null
$origin = New-Object CSPlaytest.Win+POINT
[CSPlaytest.Win]::ClientToScreen($hwnd, [ref]$origin) | Out-Null
$capW = ($rect.Right - $rect.Left) - (($rect.Right - $rect.Left) % 2)
$capH = ($rect.Bottom - $rect.Top) - (($rect.Bottom - $rect.Top) % 2)
if ($capW -le 0 -or $capH -le 0) { throw 'The game window has no client area to record (minimised?).' }
Write-Host "Recording the client area: ${capW}x$capH at $($origin.X),$($origin.Y)"
$psi = [System.Diagnostics.ProcessStartInfo]::new($Ffmpeg)
foreach ($a in @('-hide_banner', '-y', '-f', 'gdigrab', '-framerate', "$Fps", '-draw_mouse', '0',
        '-offset_x', "$($origin.X)", '-offset_y', "$($origin.Y)", '-video_size', "${capW}x$capH", '-i', 'desktop',
        '-c:v', 'libx264', '-preset', 'veryfast', '-crf', '22', '-pix_fmt', 'yuv420p', $Recording)) {
    $psi.ArgumentList.Add($a)
}
$psi.RedirectStandardInput = $true
$psi.RedirectStandardError = $true
$psi.UseShellExecute = $false
$ff = [System.Diagnostics.Process]::Start($psi)
$errTask = $ff.StandardError.ReadToEndAsync()

# gdigrab stamps the first frame with the wall clock ("start: <unix seconds>"), which is what the
# game's events are timed with. It is read from ffmpeg's log once recording has stopped.

# ---------------------------------------------------------------- wait for the game
# The process that owns the window: for a package the launched exe is a stub that starts the real
# game and exits at once, so the launched process is not the one to wait for.
$gameProc = [System.Diagnostics.Process]::GetProcessById($window.Id)
if (-not $gameProc.WaitForExit($TimeoutSeconds * 1000)) {
    Write-Warning "The game did not finish in $TimeoutSeconds s; stopping it."
    if (-not $gameProc.HasExited) { $gameProc.Kill() }
}
Start-Sleep -Milliseconds 500
if (-not $ff.HasExited) {
    $ff.StandardInput.Write('q')
    $ff.StandardInput.Flush()
    if (-not $ff.WaitForExit(15000)) { $ff.Kill() }
}
$ffErr = $errTask.Result
Set-Content -LiteralPath $FfLog -Value $ffErr
if (-not (Test-Path -LiteralPath $Recording)) { throw "No recording; see $FfLog" }
$m = [regex]::Match($ffErr, 'start:\s*([0-9]+\.[0-9]+)')
if (-not $m.Success) { throw "Could not read the recording's start time from $FfLog" }
$videoStart = [double]$m.Groups[1].Value

# ---------------------------------------------------------------- read the events
if (-not (Test-Path -LiteralPath $EventsPath)) { throw "The game wrote no events ($EventsPath)." }
$events = Get-Content -LiteralPath $EventsPath | Where-Object { $_.Trim() } | ForEach-Object { $_ | ConvertFrom-Json }
$ended = [bool]($events | Where-Object { $_.event -eq 'end' })
$segments = [System.Collections.Generic.List[object]]::new()
foreach ($e in $events | Where-Object { $_.event -eq 'segment' }) {
    $result = $events | Where-Object { $_.event -eq 'result' -and $_.index -eq $e.index } | Select-Object -First 1
    $endMs = if ($result) { $result.utc_ms } else { ($events | Select-Object -Last 1).utc_ms }
    $notes = [ordered]@{}
    if ($result -and $result.PSObject.Properties['data']) {
        foreach ($p in $result.data.PSObject.Properties) { $notes[$p.Name] = $p.Value }
    }
    $segments.Add([pscustomobject]@{
        Index  = [int]$e.index
        Label  = $e.label
        Camera = if ($e.PSObject.Properties['data']) { $e.data.camera } else { '' }
        Start  = [math]::Max(0, $e.utc_ms / 1000.0 - $videoStart)
        End    = [math]::Max(0, $endMs / 1000.0 - $videoStart)
        Notes  = $notes
        Clip   = ''
        Sheet  = ''
    })
}

# ---------------------------------------------------------------- the recording ends with the game
# The recording runs on for a moment after the game quits and takes whatever was behind its window
# (a terminal, other apps). It is cut at the game's last event, so only the game is kept.
$lastT = (($events | Select-Object -Last 1).utc_ms / 1000.0) - $videoStart
if ($lastT -gt 1) {
    $trimmed = Join-Path $OutDir 'recording-trimmed.mp4'
    Invoke-Ffmpeg @('-y', '-i', $Recording, '-t', $lastT.ToString('0.###', [Globalization.CultureInfo]::InvariantCulture), '-c', 'copy', $trimmed)
    Move-Item -LiteralPath $trimmed -Destination $Recording -Force
}

# ---------------------------------------------------------------- clips and sheets
if (-not $NoClips) {
    foreach ($s in $segments) {
        $duration = [math]::Max(0.5, $s.End - $s.Start)
        $name = '{0:D2}-{1}' -f $s.Index, (Get-Slug $s.Label)
        $s.Clip = "$name.mp4"
        $s.Sheet = "$name.png"
        $ss = $s.Start.ToString('0.###', [Globalization.CultureInfo]::InvariantCulture)
        $t = $duration.ToString('0.###', [Globalization.CultureInfo]::InvariantCulture)
        $rate = (8.0 / $duration).ToString('0.####', [Globalization.CultureInfo]::InvariantCulture)
        Invoke-Ffmpeg @('-y', '-ss', $ss, '-i', $Recording, '-t', $t, '-c:v', 'libx264', '-preset', 'veryfast', '-crf', '22', '-an', (Join-Path $OutDir $s.Clip))
        Invoke-Ffmpeg @('-y', '-ss', $ss, '-t', $t, '-i', $Recording, '-vf', "fps=$rate,scale=480:-2,tile=4x2", '-frames:v', '1', (Join-Path $OutDir $s.Sheet))
    }
    $total = [double](& (Join-Path (Split-Path $Ffmpeg) 'ffprobe.exe') -v error -show_entries format=duration -of csv=p=0 $Recording)
    $rate = (24.0 / [math]::Max(1.0, $total)).ToString('0.####', [Globalization.CultureInfo]::InvariantCulture)
    Invoke-Ffmpeg @('-y', '-i', $Recording, '-vf', "fps=$rate,scale=320:-2,tile=6x4", '-frames:v', '1', (Join-Path $OutDir 'overview.png'))
}

# ---------------------------------------------------------------- summary
$lines = [System.Collections.Generic.List[string]]::new()
$lines.Add("# Play-test $stamp")
$lines.Add('')
$lines.Add("Scenario ``$Scenario``, $Target build, map ``$Map``. $(if ($ended) { 'Played to the end.' } else { 'DID NOT FINISH: the game stopped early or timed out.' })")
$lines.Add('')
$lines.Add('Recording: `recording.mp4`. Overview: `overview.png`. Times are seconds into the recording.')
$lines.Add('')
$lines.Add('| # | Segment | Camera | Time | Results | Clip | Sheet |')
$lines.Add('|---|---|---|---|---|---|---|')
foreach ($s in $segments) {
    $results = ($s.Notes.GetEnumerator() | ForEach-Object { "$($_.Key): $($_.Value)" }) -join '; '
    $lines.Add(('| {0} | {1} | {2} | {3:0.0}-{4:0.0} | {5} | {6} | {7} |' -f $s.Index, $s.Label, $s.Camera, $s.Start, $s.End, $results, $s.Clip, $s.Sheet))
}
$missed = $segments | Where-Object { $_.Notes.Contains('punches') -and ($_.Notes['punches'] -match '^(\d+) of (\d+)') -and ([int]$Matches[1] -lt [int]$Matches[2]) }
if ($missed) {
    $lines.Add('')
    $lines.Add('Punches that missed:')
    foreach ($s in $missed) { $lines.Add("- $($s.Label): $($s.Notes['punches'])") }
}
$summary = Join-Path $OutDir 'summary.md'
Set-Content -LiteralPath $summary -Value $lines -Encoding utf8

Write-Host "$($segments.Count) segment(s); summary: $summary"
if (-not $ended) { exit 1 }
exit 0
