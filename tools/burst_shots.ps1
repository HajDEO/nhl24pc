# Burst screenshots of the primary screen with wall-clock timestamps (Unix ms) in the file names,
# so frames can be matched against the emulator's "frame trace" log lines (t=<unix ms>).
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\burst_shots.ps1 -Count 30 -IntervalMs 100 -Out logs\burst1
param([int]$Count = 30, [int]$IntervalMs = 100, [string]$Out = 'logs\burst')
Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.Windows.Forms
New-Item -ItemType Directory -Force $Out | Out-Null
$b = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
$index = @()
for ($i = 1; $i -le $Count; $i++) {
    $bmp = New-Object System.Drawing.Bitmap $b.Width, $b.Height
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $t0 = [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()
    $g.CopyFromScreen($b.Location, [System.Drawing.Point]::Empty, $b.Size)
    $t1 = [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()
    $name = ('s_{0}_{1:d3}.png' -f $t0, $i)
    $bmp.Save((Join-Path $Out $name), [System.Drawing.Imaging.ImageFormat]::Png)
    $g.Dispose(); $bmp.Dispose()
    $index += ('{0},{1},{2}' -f $i, $t0, $t1)
    Start-Sleep -Milliseconds $IntervalMs
}
$index | Set-Content (Join-Path $Out 'index.csv')
Write-Host ("{0} screenshots in {1} ({2}x{3})" -f $Count, $Out, $b.Width, $b.Height)
