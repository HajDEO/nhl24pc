# Assembles the ready-to-run package in <repo>\release from the current build:
#   nhl24.exe + launcher.exe (built with tools\build_nhlport.ps1 and launcher\build_launcher.ps1),
#   launcher pictures, the Visual C++ runtime DLLs the exe imports, a clean user\ template
#   (config, keyboard/controller mapping, NHL 24 memory patches) and a short README.
# No game data, saves, shader cache, logs or profile data are copied.
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\make_release.ps1 [-Build ..\build] [-Out ..\release]
param(
    [string]$Build = (Join-Path $PSScriptRoot '..\build'),
    [string]$Out = (Join-Path $PSScriptRoot '..\release')
)
$ErrorActionPreference = 'Stop'
$repo = Resolve-Path (Join-Path $PSScriptRoot '..')
foreach ($f in 'nhl24.exe', 'launcher.exe') {
    if (-not (Test-Path (Join-Path $Build $f))) { throw "$f missing in $Build - build it first" }
}
New-Item -ItemType Directory -Force $Out | Out-Null
# binaries and launcher pictures
Copy-Item -Force (Join-Path $Build 'nhl24.exe') $Out
Copy-Item -Force (Join-Path $Build 'launcher.exe') $Out
foreach ($pic in 'background.png', 'btn.png', 'ps4_pad.png') { Copy-Item -Force (Join-Path $repo "launcher\$pic") $Out }
# Visual C++ runtime (the exe imports these; shipping them avoids the "VCRUNTIME140.dll missing" error)
$redist = Get-ChildItem 'C:\Program Files*\Microsoft Visual Studio\*\*\VC\Redist\MSVC\*\x64\Microsoft.VC143.CRT' -Directory -ErrorAction SilentlyContinue |
    Sort-Object FullName -Descending | Select-Object -First 1
if ($redist) {
    foreach ($dll in 'msvcp140.dll', 'msvcp140_atomic_wait.dll', 'vcruntime140.dll', 'vcruntime140_1.dll') {
        $src = Join-Path $redist.FullName $dll
        if (Test-Path $src) { Copy-Item -Force $src $Out } else { Write-Warning "$dll not found in $($redist.FullName)" }
    }
} else {
    Write-Warning 'Visual C++ redistributable folder not found; users will need the VC++ 2015-2022 x64 runtime installed'
}
# user template: config without machine/user specific sections, controls, memory patches
$user = Join-Path $Out 'user'
New-Item -ItemType Directory -Force $user, (Join-Path $user 'input_config'), (Join-Path $user 'patches') | Out-Null
$cfg = Get-Content (Join-Path $Build 'user\config.json') -Raw | ConvertFrom-Json
foreach ($section in 'Launcher') { if ($cfg.PSObject.Properties[$section]) { $cfg.PSObject.Properties.Remove($section) } }
if ($cfg.General) {
    $cfg.General.install_dirs = @()
    foreach ($k in 'addon_install_dir', 'font_dir', 'home_dir', 'sys_modules_dir') { if ($null -ne $cfg.General.PSObject.Properties[$k]) { $cfg.General.$k = '' } }
}
if ($cfg.GPU -and $null -ne $cfg.GPU.PSObject.Properties['dump_shaders']) { $cfg.GPU.dump_shaders = $false }
if ($cfg.Vulkan -and $null -ne $cfg.Vulkan.PSObject.Properties['gpu_id']) { $cfg.Vulkan.gpu_id = -1 }
$cfg | ConvertTo-Json -Depth 20 | Out-File -Encoding utf8 (Join-Path $user 'config.json')
foreach ($ini in 'default.ini', 'global.ini') { Copy-Item -Force (Join-Path $Build "user\input_config\$ini") (Join-Path $user 'input_config') }
Copy-Item -Force (Join-Path $repo 'game_patches\nhl24.xml') (Join-Path $user 'patches')
# readme
@'
NHL24 PC - ready-to-run package
================================
1. Keep this folder together (nhl24.exe, launcher.exe, *.dll, *.png, user\).
   Put it somewhere you can write to (not Program Files), path without special characters is safest.
2. Start launcher.exe. Choose "Install PKG" and pick your NHL 24 BASE GAME package (v01.00, title ID
   CUSA37933). Do NOT install the update PKG: the port needs game version 1.00; with the 1.6 update the
   game freezes at the first puck drop. The game is extracted into game\ next to the launcher.
3. Press PLAY. Settings (resolution, fullscreen, vsync, sharpening, controls) are in the launcher.
   Saves, profile and the shader cache are created under user\ on first run; the first minutes of
   play stutter while shaders compile, it gets smooth afterwards.
4. If the game crashes while a match loads (right after team selection, error 0xc0000005), turn on
   "Red zone protection (Windows)" in the launcher's quick settings and start the game again.

Requirements: Windows 10/11 x64, a Vulkan 1.3 GPU with current drivers (NVIDIA GTX 10xx+/RTX, AMD RDNA),
8 GB RAM (16 GB recommended), ~40 GB disk for the extracted game. A gamepad (Xbox/DualShock/DualSense)
works out of the box; keyboard mapping is in user\input_config\default.ini.

Source, documentation and issues: https://github.com/HajDEO/nhl24pc

Slovensky: spustite launcher.exe, cez "Install PKG" vyberte PKG základnej hry NHL 24 v01.00 (update
neinštalujte: port potrebuje verziu hry 1.00, s update 1.6 hra zamrzne pri prvom vhadzovaní), hra sa
rozbalí do game\ vedľa launchera; potom PLAY. Uložené pozície a profil
vznikajú v user\ pri prvom spustení. Ak hra padá pri načítaní zápasu (hneď po výbere tímov), zapnite
v launcheri "Ochrana red zone (Windows)" a spustite hru znova.
'@ | Out-File -Encoding utf8 (Join-Path $Out 'README.txt')
Get-ChildItem $Out -Recurse -File | Measure-Object -Property Length -Sum | ForEach-Object { 'release: {0} files, {1:N1} MB -> {2}' -f $_.Count, ($_.Sum / 1MB), $Out }
