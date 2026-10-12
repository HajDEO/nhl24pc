# Runs NHL 24 in the nhlport build and keeps the log next to this repo.
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_nhl24.ps1
#   powershell ... -File tools\run_nhl24.ps1 -Fullscreen -ExtraArgs @('--show-fps')
param(
    [string]$Exe = (Join-Path $PSScriptRoot '..\nhlport\build\nhl24.exe'),
    [string]$GameDir = (Join-Path $PSScriptRoot '..\build\game'),
    [string]$LogDir = (Join-Path $PSScriptRoot '..\logs'),
    [switch]$Fullscreen,
    [string[]]$ExtraArgs = @()
)
$ErrorActionPreference = 'Stop'
if (-not (Test-Path $Exe)) { throw "$Exe not found; build first with tools\build_nhlport.ps1" }
$eboot = Join-Path $GameDir 'eboot.bin'
if (-not (Test-Path $eboot)) { throw "$eboot not found" }
New-Item -ItemType Directory -Force $LogDir | Out-Null
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$stdout = Join-Path $LogDir "nhl24-$stamp.stdout.log"
$args = @('-g', $eboot, '-f', ($(if ($Fullscreen) { 'true' } else { 'false' })), '-p', 'nhl24.xml') + $ExtraArgs
Write-Host "running: $Exe $($args -join ' ')"
# crash diagnostic layer (Vulkan SDK) dumps go here when Vulkan.vkcrash_diagnostic_enabled is on
# the release build runs without the diagnostic log and capture triggers; the measurement
# scripts need them
if (-not $env:NHLPORT_DIAG) { $env:NHLPORT_DIAG = '1' }
$env:CDL_OUTPUT_PATH = (Join-Path $LogDir 'cdl')
New-Item -ItemType Directory -Force $env:CDL_OUTPUT_PATH | Out-Null
$p = Start-Process -FilePath $Exe -ArgumentList $args -WorkingDirectory (Split-Path $Exe) -NoNewWindow -PassThru -RedirectStandardOutput $stdout -RedirectStandardError (Join-Path $LogDir "nhl24-$stamp.stderr.log")
$p.WaitForExit()
Write-Host "exit code $($p.ExitCode); stdout/stderr in $LogDir"
# shadPS4 writes its own log under the user directory (user\log\shad_log.txt next to the exe, or
# %APPDATA%\shadPS4\log) - copy the newest one beside our stdout log for convenience.
$userLog = Get-ChildItem -Path (Split-Path $Exe), (Join-Path $env:APPDATA 'shadPS4') -Recurse -Filter 'shad_log.txt' -ErrorAction SilentlyContinue | Sort-Object LastWriteTime -Descending | Select-Object -First 1
if ($userLog) { Copy-Item $userLog.FullName (Join-Path $LogDir "nhl24-$stamp.shad_log.txt"); Write-Host "shadPS4 log: $($userLog.FullName)" }
# nhlport diagnostic log (events + summaries, per-frame CSV, per-thread CPU) written next to shad_log.txt;
# analyse with: python tools\diag_report.py logs\nhl24-<stamp>.nhl24_frames.csv
if ($userLog) {
    foreach ($name in 'nhl24_diag.log', 'nhl24_frames.csv', 'nhl24_threads.csv') {
        $src = Join-Path (Split-Path $userLog.FullName) $name
        if (Test-Path $src) { Copy-Item $src (Join-Path $LogDir "nhl24-$stamp.$name") }
    }
    Write-Host "diag log: $(Join-Path $LogDir "nhl24-$stamp.nhl24_diag.log")"
}
exit $p.ExitCode
