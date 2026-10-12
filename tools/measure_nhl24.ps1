# Starts NHL 24 through tools\run_nhl24.ps1, fires the capture triggers at fixed offsets from the
# first presented frame, closes the window gracefully (WM_CLOSE) and copies the draw log beside the
# other logs. Usage: powershell -NoProfile -ExecutionPolicy Bypass -File tools/measure_nhl24.ps1 -Label before
param(
    [string]$Label = 'run',
    [int]$TraceFrames = 150,
    [string]$TraceAt = '6',      # seconds after the first presented frame (or anchored, e.g. G22)
    [double]$Shot1At = 2.5,      # intro screenshots
    [int]$Shot1Frames = 30,
    [double]$Shot2At = 37.0,     # menu screenshots
    [int]$Shot2Frames = 30,
    [double]$Shot3At = 8.0,      # intro -> menu transition (image churn window)
    [int]$Shot3Frames = 60,
    [string]$PressAt = '40',     # press cross at these offsets, comma-separated (title screen,
                                 # then main menu -> Play Now, ...); '0' = never
    [string]$Shot4At = '44',     # screenshot burst after each press, comma-separated
    [int]$Shot4Frames = 60,
    # Free-form timeline: "<offset>:<key>" presses (key = keyboard letter from default.ini, e.g.
    # n = cross, c = triangle, b = circle, v = square) and "<offset>" screenshot bursts of
    # $ShotFrames; both lists are merged and run in time order after the PressAt presses.
    [string]$Keys = '',
    [string]$ShotsAt = '',
    [int]$ShotFrames = 30,
    [string]$CloseAt = '66',     # seconds after the first frame (or anchored, e.g. G50)
    [int]$FirstFrameTimeout = 420,
    [double]$AnchorGrace = 20,   # see the timeline below
    [double]$AnchorTimeout = 600
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$user = Join-Path $root 'nhlport\build\user'
$captures = Join-Path $user 'captures'
$diag = Join-Path $user 'log\nhl24_diag.log'
$frames = Join-Path $user 'log\nhl24_frames.csv'
$shotsDir = Join-Path $user 'screenshots'
$logs = Join-Path $root 'logs'

foreach ($t in 'frametrace', 'screenshot') {
    $p = Join-Path $captures $t
    if (Test-Path $p) { Remove-Item $p -Force }
}
# The driver names the file drawlog_<frames-1>.txt (counter of draw-log frames at write time).
$drawName = 'drawlog_' + ($TraceFrames - 1) + '.txt'
$oldDraw = Join-Path $captures $drawName
if (Test-Path $oldDraw) { Remove-Item $oldDraw -Force }
$shotsBefore = @(Get-ChildItem $shotsDir -Filter '*.png' -ErrorAction SilentlyContinue | ForEach-Object { $_.Name })
if (Test-Path $frames) { Remove-Item $frames -Force }
if (Test-Path $diag) { Remove-Item $diag -Force }

$started = Get-Date
$runner = Start-Process -FilePath 'powershell' -ArgumentList @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', (Join-Path $root 'tools\run_nhl24.ps1')) -WorkingDirectory $root -PassThru -WindowStyle Hidden
Write-Host "[$Label] runner pid $($runner.Id) started $($started.ToString('HH:mm:ss'))"

# Wait for the first presented frame (frames CSV gets its first data row).
$t0 = $null
while (-not $t0) {
    Start-Sleep -Milliseconds 250
    if ($runner.HasExited) { throw "runner exited before the first frame (exit $($runner.ExitCode))" }
    if (((Get-Date) - $started).TotalSeconds -gt $FirstFrameTimeout) { throw 'no frame within timeout' }
    if (Test-Path $frames) {
        $n = (Get-Content $frames -ErrorAction SilentlyContinue | Measure-Object -Line).Lines
        if ($n -ge 2) { $t0 = Get-Date }
    }
}
Write-Host "[$Label] first frame after $([int](($t0 - $started).TotalSeconds)) s"

function Fire([string]$name, [int]$value, [double]$at) {
    $now = ((Get-Date) - $t0).TotalSeconds
    if ($at -gt $now) { Start-Sleep -Milliseconds ([int](($at - $now) * 1000)) }
    Set-Content -Path (Join-Path $captures $name) -Value "$value" -Encoding ascii
    Write-Host "[$Label] trigger $name=$value at +$([math]::Round(((Get-Date) - $t0).TotalSeconds, 1)) s"
}
$inv = [System.Globalization.CultureInfo]::InvariantCulture
$presses = @($PressAt -split '[,;]' | Where-Object { $_.Trim() } | ForEach-Object { [double]::Parse($_.Trim(), $inv) } | Where-Object { $_ -gt 0 })
# A trace that falls after the first key press is fired from the timeline below (in time order
# with the presses) instead of blocking here before the presses.
$traceAnchored = $TraceAt.Trim() -match '^[A-Z]'
$traceLate = ($TraceFrames -gt 0) -and ($traceAnchored -or (($presses.Count -gt 0) -and ([double]::Parse($TraceAt, $inv) -gt ($presses | Measure-Object -Minimum).Minimum)))
if ($Shot1Frames -gt 0) { Fire 'screenshot' $Shot1Frames $Shot1At }
if ($TraceFrames -gt 0 -and -not $traceLate) { Fire 'frametrace' $TraceFrames ([double]::Parse($TraceAt, $inv)) }
if ($Shot3Frames -gt 0) { Fire 'screenshot' $Shot3Frames $Shot3At }
if ($Shot2Frames -gt 0) { Fire 'screenshot' $Shot2Frames $Shot2At }

# Cross is bound to the N key in user\input_config\default.ini: the first press leaves the
# "PRESS X" title screen, the next one confirms the focused main-menu tile (Play Now).
$sig = '[DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd); [DllImport("user32.dll")] public static extern void keybd_event(byte bVk, byte bScan, uint dwFlags, UIntPtr dwExtraInfo);'
$u32 = Add-Type -MemberDefinition $sig -Name U32 -Namespace Win -PassThru
function PressKey([double]$at, [char]$key) {
    $now = ((Get-Date) - $t0).TotalSeconds
    if ($at -gt $now) { Start-Sleep -Milliseconds ([int](($at - $now) * 1000)) }
    $game = Get-Process -Name 'nhl24' -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $game) { return }
    [void]$u32::SetForegroundWindow($game.MainWindowHandle)
    Start-Sleep -Milliseconds 300
    $vk = [byte][char]::ToUpper($key)   # letters: virtual-key code == upper-case ASCII
    $u32::keybd_event($vk, 0, 0, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 150
    $u32::keybd_event($vk, 0, 2, [UIntPtr]::Zero)
    Write-Host "[$Label] pressed $key at +$([math]::Round(((Get-Date) - $t0).TotalSeconds, 1)) s"
}
function PressCross([double]$at) { PressKey $at 'n' }
$pressShots = @($Shot4At -split '[,;]' | Where-Object { $_.Trim() } | ForEach-Object { [double]::Parse($_.Trim(), $inv) })
for ($i = 0; $i -lt $presses.Count; $i++) {
    if ($presses[$i] -le 0) { continue }
    PressCross $presses[$i]
    # Bursts longer than ~100 frames stall and block later triggers; keep them short (offset 0 = none).
    if ($Shot4Frames -gt 0 -and $i -lt $pressShots.Count -and $pressShots[$i] -gt 0) { Fire 'screenshot' $Shot4Frames $pressShots[$i] }
}

# Free-form timeline (-Keys "66:c,80:n" / -ShotsAt "70,80,90"), executed in time order. An entry
# may be anchored to an event of the diagnostic log instead of the first frame: prefix T = the
# first "resolution ... -> 2112x2112" (3D arena background = team select), prefix G = the first
# "resolution ... -> 2048x1024" (two-layer shadow map = match loaded), e.g. -Keys "T3:c" (triangle
# 3 s after the team select appears), -ShotsAt "G6,G20", -TraceAt G22, -CloseAt G50. The anchor
# event must appear after the previous timeline entry fired; when only an older match exists,
# the entry fires after $AnchorGrace seconds. Keys: letters = keyboard presses; "shot" = burst of
# $ShotFrames screenshots; "trace" = frametrace of $TraceFrames; "dump=<hex>[+<hex>...]" =
# captures/dumpimage of those guest addresses.
$anchorPatterns = @{ 'T' = 'resolution: scene .*-> 2112x2112'; 'G' = 'resolution: scene .*-> 2048x1024' }
$anchorTimes = @{}
$lastFired = 0.0
function DiagNow { return ((Get-Date) - $started).TotalSeconds }
function ParseAt([string]$text) {
    $text = $text.Trim()
    if ($text -match '^([A-Z])(\d+(\.\d+)?)$') { return [pscustomobject]@{ Anchor = $Matches[1]; Offset = [double]::Parse($Matches[2], $inv) } }
    return [pscustomobject]@{ Anchor = ''; Offset = [double]::Parse($text, $inv) }
}
function WaitAnchor([string]$anchor) {
    if ($anchorTimes.ContainsKey($anchor)) { return $anchorTimes[$anchor] }
    $pattern = $anchorPatterns[$anchor]
    $since = [math]::Max(0.0, $script:lastFired - 1.0)
    $graceStart = $null
    while ($true) {
        $older = $null
        foreach ($m in @(Select-String -Path $diag -Pattern $pattern -ErrorAction SilentlyContinue)) {
            if ($m.Line -match '^\[\s*([0-9.]+)\]') {
                $t = [double]::Parse($Matches[1], $inv)
                if ($t -ge $since) {
                    $wall = ((Get-Date) - $t0).TotalSeconds   # the event is in the past; fire relative to now
                    $anchorTimes[$anchor] = $wall
                    Write-Host "[$Label] anchor $anchor`: '$($m.Line.Trim())' (at +$([math]::Round($wall, 1)) s)"
                    return $wall
                }
                $older = $t
            }
        }
        if ($older -ne $null) {
            if (-not $graceStart) { $graceStart = Get-Date; Write-Host "[$Label] anchor $anchor`: only an older event (t=$older); waiting $AnchorGrace s for a new one" }
            elseif (((Get-Date) - $graceStart).TotalSeconds -ge $AnchorGrace) {
                $wall = ((Get-Date) - $t0).TotalSeconds
                $anchorTimes[$anchor] = $wall
                Write-Host "[$Label] anchor $anchor`: using the older event (at +$([math]::Round($wall, 1)) s)"
                return $wall
            }
        }
        if (((Get-Date) - $started).TotalSeconds -gt $AnchorTimeout) { throw "anchor $anchor ($pattern) not seen within $AnchorTimeout s" }
        if ((Get-Process -Name 'nhl24' -ErrorAction SilentlyContinue) -eq $null) { throw "game exited while waiting for anchor $anchor" }
        Start-Sleep -Milliseconds 250
    }
}
function ResolveAt($at) {
    if ($at.Anchor) { return (WaitAnchor $at.Anchor) + $at.Offset }
    return $at.Offset
}
$timeline = @()
foreach ($item in ($Keys -split '[,;]')) {
    if (-not $item.Trim()) { continue }
    $parts = $item.Trim() -split ':', 2
    $timeline += [pscustomobject]@{ At = (ParseAt $parts[0]); Key = $parts[1] }
}
foreach ($item in ($ShotsAt -split '[,;]')) {
    if (-not $item.Trim()) { continue }
    $timeline += [pscustomobject]@{ At = (ParseAt $item); Key = 'shot' }
}
if ($traceLate) { $timeline += [pscustomobject]@{ At = (ParseAt $TraceAt); Key = 'trace' } }
# order: plain entries by offset first, then T-anchored, then G-anchored (each by offset)
$anchorRank = @{ '' = 0; 'T' = 1; 'G' = 2 }
foreach ($ev in ($timeline | Sort-Object @{ Expression = { $anchorRank[$_.At.Anchor] } }, @{ Expression = { $_.At.Offset } })) {
    $at = ResolveAt $ev.At
    if ($ev.Key -eq 'trace') { Fire 'frametrace' $TraceFrames $at }
    elseif ($ev.Key -eq 'shot') { Fire 'screenshot' $ShotFrames $at }
    elseif ($ev.Key -like 'dump=*') {
        $now = ((Get-Date) - $t0).TotalSeconds
        if ($at -gt $now) { Start-Sleep -Milliseconds ([int](($at - $now) * 1000)) }
        $addrs = ($ev.Key.Substring(5) -split '\+') -join "`n"
        Set-Content -Path (Join-Path $captures 'dumpimage') -Value $addrs -Encoding ascii
        Write-Host "[$Label] trigger dumpimage ($($ev.Key.Substring(5))) at +$([math]::Round(((Get-Date) - $t0).TotalSeconds, 1)) s"
    }
    else { PressKey $at ([char]$ev.Key) }
    $script:lastFired = DiagNow
}

$closeAtSec = ResolveAt (ParseAt $CloseAt)
$now = ((Get-Date) - $t0).TotalSeconds
if ($closeAtSec -gt $now) { Start-Sleep -Milliseconds ([int](($closeAtSec - $now) * 1000)) }
$game = Get-Process -Name 'nhl24' -ErrorAction SilentlyContinue | Select-Object -First 1
if ($game) {
    $ok = $game.CloseMainWindow()
    Write-Host "[$Label] WM_CLOSE sent at +$([math]::Round(((Get-Date) - $t0).TotalSeconds, 1)) s (ok=$ok)"
    if (-not $game.WaitForExit(90000)) { Write-Host "[$Label] game did not exit within 90 s" }
} else {
    Write-Host "[$Label] game process not found"
}
if (-not $runner.WaitForExit(60000)) { Write-Host "[$Label] runner still running" }

$latestDiag = Get-ChildItem $logs -Filter 'nhl24-*.nhl24_diag.log' | Sort-Object LastWriteTime -Descending | Select-Object -First 1
$stamp = $latestDiag.Name -replace '^nhl24-(.*)\.nhl24_diag\.log$', '$1'
$draw = Join-Path $captures $drawName
# The draw log is written on a helper thread; give it a moment.
for ($i = 0; $i -lt 40 -and -not (Test-Path $draw); $i++) { Start-Sleep -Milliseconds 500 }
Start-Sleep -Seconds 2
foreach ($d in @(Get-ChildItem $captures -Filter 'drawlog_*.txt' -ErrorAction SilentlyContinue | Where-Object { $_.LastWriteTime -gt $started })) {
    $suffix = if ($d.Name -eq $drawName) { '' } else { '_' + ($d.BaseName -replace '^drawlog_', 'n') }
    $dst = Join-Path $logs "nhl24-$stamp.drawlog_$Label$suffix.txt"
    Copy-Item $d.FullName $dst -Force
    Write-Host "[$Label] draw log -> $dst ($($d.Length) bytes)"
}
if (-not (Test-Path $draw)) { Write-Host "[$Label] no draw log $drawName written" }
foreach ($d in @(Get-ChildItem $captures -Filter 'img_*.raw' -ErrorAction SilentlyContinue | Where-Object { $_.LastWriteTime -gt $started })) {
    $dst = Join-Path $logs "nhl24-$stamp.$Label.$($d.Name)"
    Copy-Item $d.FullName $dst -Force
    Write-Host "[$Label] image dump -> $dst ($($d.Length) bytes)"
}
$shotsAfter = @(Get-ChildItem $shotsDir -Filter '*.png' -ErrorAction SilentlyContinue | Where-Object { $shotsBefore -notcontains $_.Name })
$shotDir = Join-Path $logs "shots-$stamp-$Label"
New-Item -ItemType Directory -Force $shotDir | Out-Null
foreach ($s in $shotsAfter) { Move-Item $s.FullName (Join-Path $shotDir $s.Name) -Force }
Write-Host "[$Label] diag log: $($latestDiag.FullName)"
Write-Host "[$Label] screenshots: $($shotsAfter.Count) -> $shotDir"
Write-Host "[$Label] done"
