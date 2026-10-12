# Builds launcher.exe next to nhl24.exe with the C# compiler that ships with Windows
# (.NET Framework 4.x), so no extra toolchain is needed.
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File launcher\build_launcher.ps1 [-Out ..\build]
param(
    [string]$Out = (Join-Path $PSScriptRoot '..\build')
)
$ErrorActionPreference = 'Stop'
$csc = Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319\csc.exe'
if (-not (Test-Path $csc)) { throw "csc.exe not found at $csc" }
$src = Join-Path $PSScriptRoot 'Launcher.cs'
$exe = Join-Path $Out 'launcher.exe'
New-Item -ItemType Directory -Force $Out | Out-Null
$ico = Join-Path $PSScriptRoot 'launcher.ico'   # generated from ico.png in this folder (see README)
$iconArg = if (Test-Path $ico) { "/win32icon:$ico" } else { '' }
& $csc /nologo /target:winexe /optimize+ "/out:$exe" $iconArg `
    /r:System.dll /r:System.Core.dll /r:System.Drawing.dll /r:System.Windows.Forms.dll /r:System.Web.Extensions.dll /r:System.Management.dll `
    $src
if ($LASTEXITCODE -ne 0) { throw "csc failed ($LASTEXITCODE)" }
# the pictures live next to this script and are copied beside the exe
foreach ($pic in 'background.png', 'btn.png', 'ps4_pad.png') {
    $p = Join-Path $PSScriptRoot $pic
    if (Test-Path $p) { Copy-Item -Force $p (Join-Path $Out $pic) }
}
Get-Item $exe | Select-Object FullName, Length, LastWriteTime
