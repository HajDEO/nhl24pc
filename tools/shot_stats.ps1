# Per-frame brightness of a screenshot sequence and the difference to the neighbouring frames:
# a frame that differs strongly from both neighbours while they match each other is a flicker.
param([Parameter(Mandatory)][string]$Dir, [double]$Threshold = 6.0)
Add-Type -AssemblyName System.Drawing
$files = Get-ChildItem $Dir -Filter '*.png' | Sort-Object Name
$w = 96; $h = 54
$frames = @()
foreach ($f in $files) {
    $img = [System.Drawing.Image]::FromFile($f.FullName)
    $bmp = New-Object System.Drawing.Bitmap $w, $h
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBilinear
    $g.DrawImage($img, 0, 0, $w, $h)
    $g.Dispose(); $img.Dispose()
    $px = New-Object 'double[]' ($w * $h)
    $sum = 0.0
    for ($y = 0; $y -lt $h; $y++) {
        for ($x = 0; $x -lt $w; $x++) {
            $c = $bmp.GetPixel($x, $y)
            $l = 0.299 * $c.R + 0.587 * $c.G + 0.114 * $c.B
            $px[$y * $w + $x] = $l
            $sum += $l
        }
    }
    $bmp.Dispose()
    $frames += [pscustomobject]@{ Name = $f.Name; Mean = $sum / ($w * $h); Px = $px }
}
function PixDiff($a, $b) {
    $d = 0.0
    for ($i = 0; $i -lt $a.Length; $i++) { $d += [math]::Abs($a[$i] - $b[$i]) }
    return $d / $a.Length
}
$flicker = 0
for ($i = 0; $i -lt $frames.Count; $i++) {
    $prev = if ($i -gt 0) { PixDiff $frames[$i - 1].Px $frames[$i].Px } else { 0.0 }
    $next = if ($i -lt $frames.Count - 1) { PixDiff $frames[$i].Px $frames[$i + 1].Px } else { 0.0 }
    $around = if ($i -gt 0 -and $i -lt $frames.Count - 1) { PixDiff $frames[$i - 1].Px $frames[$i + 1].Px } else { 0.0 }
    $flag = ''
    if ($i -gt 0 -and $i -lt $frames.Count - 1 -and $prev -gt $Threshold -and $next -gt $Threshold -and $around -lt ($Threshold / 2)) { $flag = ' FLICKER'; $flicker++ }
    '{0,3} {1} mean {2,6:F1} dprev {3,5:F1} dnext {4,5:F1} dnbrs {5,5:F1}{6}' -f $i, $frames[$i].Name, $frames[$i].Mean, $prev, $next, $around, $flag
}
"frames $($frames.Count) flicker $flicker"
