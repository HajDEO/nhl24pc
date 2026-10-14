# NHL24 PC

**NHL 24 (PS4, CUSA37933) running natively on Windows** – a single-game fork of the
[shadPS4](https://github.com/shadps4-emu/shadPS4) emulator, tuned and fixed for one title, with a launcher
that extracts the game from your own PKG and starts it. No console, no CPU emulation: the game's x86-64
code runs directly on your processor, the PS4 system libraries are replaced by native Windows
implementations, and the GPU work goes through Vulkan.

> Slovenská verzia je nižšie: [Slovensky](#slovensky). Video: https://youtu.be/eGo1-W3FaYw

| | |
|---|---|
| Video | [NHL 24 running on PC – YouTube](https://youtu.be/eGo1-W3FaYw) |
| Ready-to-run package | [`release/`](release) – `launcher.exe`, `nhl24.exe`, runtime DLLs, clean `user\` template |
| Current build | **v0.8.3** (2026-10-14), Windows 10/11 x64 |
| Needs | your own NHL 24 PKG (base v01.00, optionally the update), a Vulkan 1.3 GPU |
| License | GPL-2.0-or-later (same as shadPS4) |

## How it works

The PS4 is an x86-64 machine. NHL 24's executable (`eboot.bin`) and its modules are ordinary AMD64 code,
so this port **does not emulate a CPU**: the game code is loaded into the process and executed by your
CPU directly, at native speed. What the emulator provides is everything around it:

- **HLE of the PS4 operating system** – kernel calls, threads, memory, files, audio (AJM), pads, video
  output, trophies, saves… are implemented as native Windows code (shadPS4's "libraries"). NHL 24 needs
  about 91 % of its imports resolved by HLE; the rest comes from the game's own modules.
- **GPU** – the PS4's GCN command streams are interpreted by a command-processor thread, the GCN
  shaders are recompiled to SPIR-V and executed through Vulkan, with a texture/buffer cache keeping guest
  memory and GPU memory coherent.
- **Game-specific work in this fork** (the full diary is `docs/DEVLOG.sk.md`): fence writes through a
  backing view instead of page faults (no GPU drain per fence), readbacks that wait only for the
  submission that wrote the data, full mip-chain depth/colour copies, CMASK/HTILE metadata with reference
  counting and a UI-coverage emulation (the game builds its HUD mask from CMASK), libSceJson2 and AJM
  fixes, wave64 lowering fixes, a GPU-thread idle backoff and pending-ops polling that halve the idle CPU
  load, a diagnostics suite (frame trace, draw log, image/buffer dumps, hotkey capture) compiled in but
  off by default, and memory patches (`game_patches/nhl24.xml`, loaded automatically from
  `user\patches\`).
- **Launcher** (`launcher/`, C# WinForms, needs only the .NET Framework 4 that ships with Windows):
  extracts the base/update PKG with shadPS4's PKG/PFS code built into `nhl24.exe`
  (`nhl24.exe --extract-pkg`), shows the installed version, exposes the useful settings (resolution,
  fullscreen, vsync, sharpening, language, controls, the Windows red-zone protection) and starts the game.

## Support / Podpora

If this project is useful to you, you can support its development:

[![ko-fi](https://ko-fi.com/img/githubbutton_sm.svg)](https://ko-fi.com/E1E61VGHII)

## Playing

1. Download the repository (green **Code → Download ZIP**), unpack the ZIP and open its `release`
   folder – everything you need is there (`launcher.exe`, `nhl24.exe`, runtime DLLs, `user\`). Keep the
   unpacked folder somewhere you can write to (not Program Files).
2. Start `launcher.exe`. Click **Install PKG** and pick your NHL 24 **base game** package (v01.00).
   **Do not install the update PKG**: the port needs game version 1.00; with the 1.6 update the game
   freezes at the first puck drop (issue #1). The launcher checks the title ID (CUSA37933) and the kind
   of package, shows size and version, and extracts into `game\` next to the launcher, with progress.
3. Press **PLAY**. Simple view = the settings most people touch; Advanced = everything, including the
   keyboard mapping and the game folder.

First run: saves, the profile and the shader cache are created under `user\`. The first minutes stutter
while the shaders compile; afterwards the game runs at 50–60 fps on a mid-range GPU (Ryzen 5 4500 +
RTX 5060: 51 fps in play, 30 fps locked in menus and cutscenes, like the console).

**Requirements**: Windows 10/11 x64; a Vulkan 1.3 GPU with current drivers (NVIDIA GTX 10xx/RTX,
AMD RDNA; integrated GPUs are too slow); 8 GB RAM (16 GB recommended); about 40 GB free for the
extracted game; a gamepad (Xbox, DualShock 4, DualSense) is detected automatically, the keyboard works
too.

**Known issues** (v0.8.3): the 1.6 update freezes at the first puck drop (use the base game v01.00 only);
occasional black tile columns in cutscenes that run at a dynamic resolution
(analysis and next steps in `docs/DEVLOG.sk.md`, section "Finálny build v0.8.1"); rare "device lost" crashes. If the game
crashes while a match loads (right after team selection, error 0xc0000005, issues #1/#3), turn on **Red zone
protection (Windows)** in the launcher's quick settings (off by default, experimental; see `docs/DEVLOG.sk.md`,
section "v0.8.3: ochrana red zone"). Windows SmartScreen may warn about the unsigned exe
(More info → Run anyway); some antivirus products dislike emulators that map large memory regions – add
an exception if needed.

## Repository layout

```
release/        ready-to-run package (binaries + clean user template), rebuilt by tools\make_release.ps1
launcher/       launcher sources (Launcher.cs), pictures, icon, build script (csc from .NET Framework)
src/            emulator sources (shadPS4 fork; NHL-specific changes are marked "nhlport")
externals/      third-party libraries as git submodules (clone with --recursive)
game_patches/   nhl24.xml – memory patches applied by the emulator at start
tools/          build, run, measurement and diagnostics scripts (PowerShell / Python)
docs/           DEVLOG.sk.md (development diary, Slovak), bbport_analysis.md, patches/ (early fixes)
documents/      shadPS4's own documentation and README
```

## Building

Only needed to change the emulator or the launcher; players use `release/`.

- Windows 10/11, [Visual Studio 2022 Build Tools](https://visualstudio.microsoft.com/visual-cpp-build-tools/)
  (MSVC v143 + Windows SDK), [LLVM](https://github.com/llvm/llvm-project/releases) (clang-cl, lld),
  [CMake](https://cmake.org/) 3.24+, [Ninja](https://ninja-build.org/), [Vulkan SDK](https://vulkan.lunarg.com/).
- `git clone --recursive https://github.com/HajDEO/nhl24pc.git`
- `powershell -ExecutionPolicy Bypass -File tools\build_nhlport.ps1 -DebugInfo` → `build\nhl24.exe` (+ `.pdb`)
- `powershell -ExecutionPolicy Bypass -File launcher\build_launcher.ps1` → `build\launcher.exe`
- `powershell -ExecutionPolicy Bypass -File tools\make_release.ps1` → refreshes `release\`

Development notes: `tools\run_nhl24.ps1` runs the build with the diagnostics log enabled
(`NHLPORT_DIAG=1`; F1 captures a frame: draw log, image dumps, screenshot), `tools\measure_nhl24.ps1`
scripts measurement runs, `tools\capture_report.py` summarises a capture. The version lives in
`src/common/port_version.h` (also used for the exe resource and the launcher title).

## Contributing

Issues and pull requests are welcome – performance, the open rendering issues above, DualSense
features, Linux. Keep changes focused; NHL-specific code is marked `nhlport` in comments so it can be
told apart from upstream shadPS4. Please do not upload game files, PKGs, saves or shader caches.

## Legal

This project does not contain any game data. You need your own copy of NHL 24 for PlayStation 4,
dumped as a PKG from a console you own. "NHL" and "EA SPORTS" are trademarks of their respective
owners; this project is not affiliated with EA, the NHL or Sony. The emulator is derived from
[shadPS4](https://github.com/shadps4-emu/shadPS4) (GPL-2.0-or-later); this repository is distributed
under the same license, see `LICENSE` and `LICENSES/`.

---

## Slovensky

**NHL 24 (PS4) bežiace natívne na Windows.** Fork emulátora shadPS4 doladený pre jednu hru plus
launcher, ktorý hru rozbalí z vášho PKG a spustí. Kód hry (x86-64) beží priamo na procesore bez emulácie
CPU; systémové knižnice PS4 sú nahradené natívnou implementáciou (HLE) a grafika ide cez Vulkan
s prekladom shaderov GCN → SPIR-V.

**Video:** https://youtu.be/eGo1-W3FaYw

**Hranie:** stiahnite repozitár (Code → Download ZIP), rozbaľte ho a prejdite do priečinka `release`
(všetko potrebné je tam); spustite `launcher.exe`, cez **Install PKG** vyberte PKG **základnej hry** (v01.00)
– hra sa rozbalí do `game\` vedľa launchera – a stlačte **PLAY**. **Update (patch) neinštalujte**: port potrebuje
verziu hry 1.00, s update 1.6 hra zamrzne pri prvom vhadzovaní (issue #1).
Uložené pozície, profil a shader cache vznikajú v `user\`; prvé minúty sa hra zasekáva (kompilácia
shaderov), potom beží plynulo.

**Požiadavky:** Windows 10/11 x64, GPU s Vulkan 1.3 (NVIDIA GTX 10xx/RTX, AMD RDNA), 8–16 GB RAM,
asi 40 GB miesta na rozbalenú hru. Gamepad funguje automaticky, klávesnica tiež.

**Známe problémy (v0.8.3):** update 1.6 zamrzne pri prvom vhadzovaní (používajte len základnú hru v01.00);
občasné čierne stĺpce dlaždíc v prestrihoch s dynamickým rozlíšením
(rozbor v `docs/DEVLOG.sk.md`), zriedkavé pády „device lost“. Ak hra padá pri načítaní zápasu (hneď po
výbere tímov, chyba 0xc0000005), zapnite v launcheri **Ochrana red zone (Windows)** (predvolene vypnuté,
experimentálne; rozbor v `docs/DEVLOG.sk.md`, sekcia „v0.8.3: ochrana red zone“).

**Vývoj:** zdrojáky emulátora v `src/` (zmeny pre NHL sú označené `nhlport`), launcher v `launcher/`,
skripty v `tools/`, denník vývoja v `docs/DEVLOG.sk.md`. Build: `tools\build_nhlport.ps1`,
`launcher\build_launcher.ps1`, balík: `tools\make_release.ps1`.
