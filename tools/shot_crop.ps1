# Crops the bottom band (and right edge) of a screenshot and scales it up for inspection of the
# black band / deformed strip at the image border. Usage: shot_crop.ps1 -File <png> -Out <png>
param(
    [Parameter(Mandatory)][string]$File,
    [Parameter(Mandatory)][string]$Out,
    [int]$Rows = 72,      # bottom rows to keep
    [int]$Scale = 4
)
Add-Type -AssemblyName System.Drawing
$img = [System.Drawing.Image]::FromFile($File)
$w = $img.Width; $h = $img.Height
$crop = New-Object System.Drawing.Bitmap ($w * $Scale / 4), ($Rows * $Scale)
$g = [System.Drawing.Graphics]::FromImage($crop)
$g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::NearestNeighbor
$g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::Half
$src = New-Object System.Drawing.Rectangle 0, ($h - $Rows), $w, $Rows
$dst = New-Object System.Drawing.Rectangle 0, 0, $crop.Width, $crop.Height
$g.DrawImage($img, $dst, $src, [System.Drawing.GraphicsUnit]::Pixel)
$g.Dispose()
$crop.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
$crop.Dispose(); $img.Dispose()
"saved $Out ($($w)x$($h) -> bottom $Rows rows, x-scale $($Scale/4), y-scale $Scale)"
