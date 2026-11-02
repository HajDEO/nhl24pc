# NHL24 PC

**NHL 24 (PS4, CUSA37933 / CUSA37934) running natively on Windows** – a single-game fork of the
[shadPS4](https://github.com/shadps4-emu/shadPS4) emulator, tuned and fixed for one title, with a launcher
that extracts the game from your own PKG and starts it. No console, no CPU emulation: the game's x86-64
code runs directly on your processor, the PS4 system libraries are replaced by native Windows
implementations, and the GPU work goes through Vulkan.

> Slovenská verzia je nižšie: [Slovensky](#slovensky). Video: https://youtu.be/eGo1-W3FaYw

| | |
|---|---|
| Video | [NHL 24 running on PC – YouTube](https://youtu.be/eGo1-W3FaYw) |
| Download | [**Latest release**](https://github.com/HajDEO/nhl24pc/releases/latest) (ZIP) or the [`release/`](release) folder |
| Ready-to-run package | [`release/`](release) – `launcher.exe`, `nhl24.exe`, runtime DLLs, clean `user\` template |
| Current build | **v0.9.1** (2026-11-02), Windows 10/11 x64 |
| Needs | your own NHL 24 PKG – US (CUSA37933) or EU (CUSA37934), base v01.00, optionally the 1.60 update – and a Vulkan 1.3 GPU |
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

[![ko-fi](https://ko-fi.com/img/githubbutton_sm.svg)](https://ko-fi.com/hajdeo)

## What's new in v0.9.1

- **Black boxes and lines in cutscenes fixed** (issue #17, NVIDIA and AMD). NHL 24 renders cutscenes
  and intros at a dynamic resolution and re-describes the same render targets with a new width, height
  and pitch every few frames. The texture cache served those requests with an image of the previous
  size, which clipped draws and shifted sampling. Such images are now recreated at the requested size
  with their contents carried over.
- **Out-of-video-memory crashes after several matches** (`ErrorOutOfDeviceMemory` on 8 GB cards):
  images left behind when the game reuses memory for another surface are now released once they go idle,
  and the garbage collector no longer stalls on images it cannot release.
- **Be a Pro and NVIDIA fixes** from [torpedatorpeda/NHL24-Be-a-Pro](https://github.com/torpedatorpeda/NHL24-Be-a-Pro),
  reimplemented in the emulator source: no crash when a texel buffer starts inside an image (match
  start), shared-memory ordering for compute shaders on GPUs with 32-wide subgroups (NVIDIA), separate
  descriptors for colour sampling and depth comparison of the same texture, depth-store synchronisation
  when a depth target is also sampled, and 16-bit uniform buffer access enabled where supported.
- The shader cache is rebuilt once on the first start (expect a few minutes of stutter).
- Launcher: **♥ Support the author** button (Ko-fi).

## Playing

1. Download `NHL24-PC-v0.9.1.zip` from the [latest release](https://github.com/HajDEO/nhl24pc/releases/latest)
   (or the repository via **Code → Download ZIP** and open its `release` folder) and unpack it –
   everything you need is there (`launcher.exe`, `nhl24.exe`, runtime DLLs, `user\`). Keep the
   unpacked folder somewhere you can write to (not Program Files).
2. Start `launcher.exe`. Click **Install PKG** and pick your NHL 24 **base game** package (v01.00).
   The 1.60 update PKG can be installed afterwards the same way; since v0.9.0 the port runs game
   version 1.00 and 1.60. Both the US (CUSA37933) and the European (CUSA37934, since v0.9.0) versions
   are supported; the 1.60 update has so far been verified on the US version only. The launcher
   checks the title ID and the kind of package, shows size and version, and extracts into `game\`
   next to the launcher, with progress.
3. Press **PLAY**. Simple view = the settings most people touch; Advanced = everything, including the
   keyboard mapping and the game folder.

First run: saves, the profile and the shader cache are created under `user\`. The first minutes stutter
while the shaders compile; afterwards the game runs at 50–60 fps on a mid-range GPU (Ryzen 5 4500 +
RTX 5060: 51 fps in play, 30 fps locked in menus and cutscenes, like the console).

**Requirements**: Windows 10/11 x64; a Vulkan 1.3 GPU with current drivers (NVIDIA GTX 10xx/RTX,
AMD RDNA; integrated GPUs are too slow); 8 GB RAM (16 GB recommended); about 40 GB free for the
extracted game; a gamepad (Xbox, DualShock 4, DualSense) is detected automatically, the keyboard works
too.

**Known issues** (v0.9.1): on 8 GB graphics cards the video memory use can exceed the card's budget in
long sessions (the driver then moves part of it to system RAM, which costs performance); rare "device
lost" crashes. If the game
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

**Novinky vo v0.9.1:**

- **Opravené čierne obdĺžniky a pruhy v prestrihoch** (issue #17, NVIDIA aj AMD). Hra kreslí prestrihy
  a úvody v dynamickom rozlíšení a tie isté render targety každých pár snímok opisuje s inou šírkou,
  výškou a pitchom. Texture cache takéto požiadavky obsluhovala obrazom predchádzajúcej veľkosti, čím sa
  kreslenie orezalo a vzorkovanie posunulo. Obraz sa teraz vytvorí v požadovanej veľkosti a obsah sa prenesie.
- **Pády na nedostatok videopamäte po niekoľkých zápasoch** (`ErrorOutOfDeviceMemory` na 8 GB kartách):
  obrazy, ktoré ostanú po tom, čo hra použije pamäť pre iný povrch, sa po nečinnosti uvoľnia
  a garbage collector sa už nezasekne na obrazoch, ktoré uvoľniť nemôže.
- **Opravy pre Be a Pro a NVIDIU** z projektu [torpedatorpeda/NHL24-Be-a-Pro](https://github.com/torpedatorpeda/NHL24-Be-a-Pro),
  prepracované priamo v zdrojákoch emulátora: žiadny pád, keď texel buffer začína vnútri obrazu (začiatok
  zápasu), poradie prístupov do shared memory v compute shaderoch na GPU s 32-šírkovým subgroupom
  (NVIDIA), samostatné deskriptory pre vzorkovanie farby a porovnanie hĺbky tej istej textúry,
  synchronizácia zápisu hĺbky, keď sa depth target zároveň vzorkuje, a 16-bitový prístup k uniform
  bufferom tam, kde ho GPU podporuje.
- Shader cache sa pri prvom spustení raz preloží nanovo (pár minút sekania).
- Launcher: tlačidlo **♥ Podporte autora** (Ko-fi).

**Hranie:** stiahnite `NHL24-PC-v0.9.1.zip` z [posledného vydania](https://github.com/HajDEO/nhl24pc/releases/latest)
(alebo repozitár cez Code → Download ZIP a prejdite do priečinka `release`), rozbaľte ho
(všetko potrebné je tam); spustite `launcher.exe`, cez **Install PKG** vyberte PKG **základnej hry** (v01.00)
– hra sa rozbalí do `game\` vedľa launchera – a stlačte **PLAY**. Update 1.60 môžete potom
nainštalovať rovnako; od v0.9.0 port podporuje verziu hry 1.00 aj 1.60. Podporovaná je americká
(CUSA37933) aj európska verzia (CUSA37934, od v0.9.0); update 1.60 je zatiaľ overený len na americkej.
Uložené pozície, profil a shader cache vznikajú v `user\`; prvé minúty sa hra zasekáva (kompilácia
shaderov), potom beží plynulo.

**Požiadavky:** Windows 10/11 x64, GPU s Vulkan 1.3 (NVIDIA GTX 10xx/RTX, AMD RDNA), 8–16 GB RAM,
asi 40 GB miesta na rozbalenú hru. Gamepad funguje automaticky, klávesnica tiež.

**Známe problémy (v0.9.1):** na 8 GB grafických kartách môže využitie videopamäte pri dlhom hraní
prekročiť rozpočet karty (driver potom časť presunie do systémovej RAM, čo stojí výkon), zriedkavé
pády „device lost“. Ak hra padá pri načítaní zápasu (hneď po
výbere tímov, chyba 0xc0000005), zapnite v launcheri **Ochrana red zone (Windows)** (predvolene vypnuté,
experimentálne; rozbor v `docs/DEVLOG.sk.md`, sekcia „v0.8.3: ochrana red zone“).

**Vývoj:** zdrojáky emulátora v `src/` (zmeny pre NHL sú označené `nhlport`), launcher v `launcher/`,
skripty v `tools/`, denník vývoja v `docs/DEVLOG.sk.md`. Build: `tools\build_nhlport.ps1`,
`launcher\build_launcher.ps1`, balík: `tools\make_release.ps1`.
