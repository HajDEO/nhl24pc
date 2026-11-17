NHL24 PC - ready-to-run package
================================
1. Keep this folder together (nhl24.exe, launcher.exe, *.dll, *.png, user\).
   Put it somewhere you can write to (not Program Files), path without special characters is safest.
2. Start launcher.exe. Choose "Install PKG" and pick your NHL 24 BASE GAME package (v01.00, title ID
   CUSA37933 or CUSA37934). The 1.60 update PKG can be installed afterwards the same way; game versions
   1.00 and 1.60 are supported. The game is extracted into game\ next to the launcher.
3. Press PLAY. Settings (resolution, fullscreen, vsync, sharpening, controls) are in the launcher.
   Saves, profile and the shader cache are created under user\ on first run; the first minutes of
   play stutter while shaders compile, it gets smooth afterwards.
4. The game needs about half a minute before its first picture appears: the emulator first prepares
   the game code (this also protects against the 0xc0000005 crashes of older versions).

Requirements: Windows 10/11 x64, a Vulkan 1.3 GPU with current drivers (NVIDIA GTX 10xx+/RTX, AMD RDNA),
8 GB RAM (16 GB recommended), ~40 GB disk for the extracted game. A gamepad (Xbox/DualShock/DualSense)
works out of the box; keyboard mapping is in user\input_config\default.ini.

Source, documentation and issues: https://github.com/HajDEO/nhl24pc

Slovensky: spustite launcher.exe, cez "Install PKG" vyberte PKG základnej hry NHL 24 v01.00 (update 1.60
môžete potom nainštalovať rovnako, podporovaná je verzia hry 1.00 aj 1.60), hra sa
rozbalí do game\ vedľa launchera; potom PLAY. Uložené pozície a profil
vznikajú v user\ pri prvom spustení. Prvý obraz hry sa objaví asi po pol minúte: emulátor si najprv
pripraví kód hry (to zároveň chráni pred pádmi 0xc0000005 zo starších verzií).
