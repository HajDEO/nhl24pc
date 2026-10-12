# Post-run evaluation of a measure_nhl24.ps1 run for the black band / edge artefacts: crops the
# bottom rows of one frame per screenshot burst, probes the band rows / right edge of every 5th
# frame, runs the flicker statistics, summarises the UI-target dumps and greps the diagnostic
# and draw logs for the metadata flags. Usage: band_report.ps1 -Label band3
param([Parameter(Mandatory)][string]$Label, [int]$Burst = 30)
$ErrorActionPreference = 'Continue'
$root = Split-Path $PSScriptRoot -Parent
$logs = Join-Path $root 'logs'
$shots = Get-ChildItem $logs -Directory -Filter "shots-*-$Label" | Sort-Object Name | Select-Object -Last 1
$stamp = $shots.Name -replace "^shots-(.*)-$Label$", '$1'
"run $stamp label $Label"
$cropDir = Join-Path $shots.FullName 'crops'
New-Item -ItemType Directory -Force $cropDir | Out-Null
$files = @(Get-ChildItem $shots.FullName -Filter '*.png' | Sort-Object Name)
"screenshots: $($files.Count)"
for ($i = 0; $i -lt $files.Count; $i += $Burst) {
    $f = $files[$i]
    & (Join-Path $PSScriptRoot 'shot_crop.ps1') -File $f.FullName -Out (Join-Path $cropDir "f$($i)_bottom.png") | Out-Null
}
"crops -> $cropDir"
# band / edge probe (python + PIL)
$probe = @'
import sys, glob, os
from PIL import Image
d = sys.argv[1]; step = int(sys.argv[2])
files = sorted(glob.glob(os.path.join(d, '*.png')))
for i, f in enumerate(files):
    if i % step: continue
    try:
        im = Image.open(f).convert('RGB')
    except Exception as e:
        print('%3d unreadable (%s)' % (i, e)); continue
    px = im.load(); w, h = im.size
    cols = range(0, w, 16)
    band = sum(1 for x in cols if max(px[x, 1050]) < 8) / len(cols)          # share of black pixels in row 1050
    bottom5 = sum(1 for x in cols if max(px[x, 1077]) < 8) / len(cols)       # row 1077 black share
    top_bottom_diff = sum(abs(px[x, 1077][c] - px[x, 1070][c]) for x in cols for c in range(3)) / (3 * len(cols))
    right_diff = sum(abs(px[1912, y][c] - px[1896, y][c]) for y in range(8, h, 16) for c in range(3)) / (3 * len(range(8, h, 16)))
    hud = max(px[960, 1010])
    print('%3d %s band1050=%.2f black1077=%.2f d(1077,1070)=%5.1f d(x1912,x1896)=%5.1f px(960,1010)=%s' % (
        i, os.path.basename(f)[-19:-4], band, bottom5, top_bottom_diff, right_diff, hud))
'@
$probePath = Join-Path $env:TEMP 'band_probe.py'
Set-Content -Path $probePath -Value $probe -Encoding ascii
"== band / edge probe (band1050 = black share of row 1050; d(1077,1070) = mean |row1077 - row1070|; d(x1912,x1896) = mean |col1912 - col1896|)"
python -I $probePath $shots.FullName 5
"== flicker"
& (Join-Path $PSScriptRoot 'shot_stats.ps1') -Dir $shots.FullName | Select-Object -Last 1
"== UI target dumps"
foreach ($d in @(Get-ChildItem $logs -Filter "nhl24-$stamp.$Label.img_*.raw")) {
    python -I (Join-Path $PSScriptRoot 'ui_dump_rows.py') $d.FullName --rows 500,1000,1020,1025,1026,1050,1079
}
"== diagnostic log"
$diag = Join-Path $logs "nhl24-$stamp.nhl24_diag.log"
"read by shaders true: $((Select-String -Path $diag -Pattern 'read by shaders true' | Measure-Object).Count) lines; false: $((Select-String -Path $diag -Pattern 'read by shaders false' | Measure-Object).Count)"
Select-String -Path $diag -Pattern 'glitch' | Measure-Object | ForEach-Object { "glitch events: $($_.Count)" }
Select-String -Path $diag -Pattern 'final: scene resolutions|frames: |fps' | Select-Object -First 4 | ForEach-Object { $_.Line.Substring(0, [math]::Min(200, $_.Line.Length)) }
$shad = Join-Path $logs "nhl24-$stamp.shad_log.txt"
if (Test-Path $shad) { Select-String -Path $shad -Pattern 'is read by a shader' | ForEach-Object { $_.Line -replace '^.*SynchronizeMemoryFromImage: ', '' } }
"== draw log"
foreach ($dl in @(Get-ChildItem $logs -Filter "nhl24-$stamp.drawlog_$Label*.txt")) {
    $n = (Select-String -Path $dl.FullName -Pattern '^meta-clear-alpha0' | Measure-Object).Count
    $frames = (Select-String -Path $dl.FullName -Pattern '^dl f(\d+) ' | ForEach-Object { $_.Matches[0].Groups[1].Value } | Sort-Object -Unique | Measure-Object).Count
    "$($dl.Name): $frames frames, meta-clear-alpha0 lines $n"
    Select-String -Path $dl.FullName -Pattern '^meta-clear-alpha0' | Select-Object -First 3 | ForEach-Object { '   ' + $_.Line }
}
