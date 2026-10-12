# Builds tools/Ps4Analyze.cs with the .NET Framework compiler (no installs needed) and runs it
# over the NHL 24 dump: eboot.bin, sce_module/*.prx and the Frostbite PRX modules in the root.
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\analyze.ps1
#   powershell ... -File tools\analyze.ps1 -GameDir D:\dumps\CUSA37933 -Out analysis
param(
    [string]$GameDir = (Join-Path $PSScriptRoot '..\GAME_FILE\uroot'),
    [string]$Out = (Join-Path $PSScriptRoot '..\analysis'),
    [switch]$Rebuild
)
$ErrorActionPreference = 'Stop'
$src = Join-Path $PSScriptRoot 'Ps4Analyze.cs'
$bin = Join-Path $PSScriptRoot 'bin'
New-Item -ItemType Directory -Force $bin | Out-Null
$exe = Join-Path $bin 'ps4analyze.exe'
if ($Rebuild -or -not (Test-Path $exe) -or ((Get-Item $src).LastWriteTime -gt (Get-Item $exe).LastWriteTime)) {
    Write-Host "compiling $src -> $exe"
    if (Test-Path $exe) { Remove-Item $exe -Force }
    Add-Type -Path $src -OutputAssembly $exe -OutputType ConsoleApplication -ReferencedAssemblies 'System.dll','System.Core.dll'
}
$aerolib = Join-Path $PSScriptRoot '..\third_party\shadps4\aerolib.inl'
$files = @()
$files += (Join-Path $GameDir 'eboot.bin')
$files += Get-ChildItem (Join-Path $GameDir 'sce_module\*.prx') | ForEach-Object { $_.FullName }
$files += Get-ChildItem (Join-Path $GameDir '*.prx') | ForEach-Object { $_.FullName }
Write-Host ("analysing {0} files into {1}" -f $files.Count, $Out)
& $exe --aerolib $aerolib --out $Out @files
exit $LASTEXITCODE
