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
