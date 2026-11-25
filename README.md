# NHL24 PC

**NHL 24 (PS4, CUSA37933 / CUSA37934) running natively on Windows and Linux x86-64** – a single-game fork of the
[shadPS4](https://github.com/shadps4-emu/shadPS4) emulator, tuned and fixed for one title, with a launcher
that extracts the game from your own PKG and starts it. No console, no CPU emulation: the game's x86-64
code runs directly on your processor, the PS4 system libraries are replaced by native host
implementations, and the GPU work goes through Vulkan.

> Slovenská verzia je nižšie: [Slovensky](#slovensky). Video: https://youtu.be/eGo1-W3FaYw

| | |
|---|---|
| Video | [NHL 24 running on PC – YouTube](https://youtu.be/eGo1-W3FaYw) |
| Download | Windows: [**latest release**](https://github.com/HajDEO/nhl24pc/releases/latest) (ZIP) or the [`release/`](release) folder. Linux: the AppImage or Ubuntu 24.04 archive from the latest successful **Build** workflow |
| Ready-to-run package | Windows: [`release/`](release) – `launcher.exe`, `nhl24.exe`, runtime DLLs, clean `user\` template. Linux: one-file AppImage or a portable archive with CLI/Tk launchers |
| Current build | **v0.9.4** (2026-11-25), Windows 10/11 x64; Linux x86-64 builds come from the source after v0.9.4 |
| Needs | your own NHL 24 PKG – US (CUSA37933) or EU (CUSA37934), base v01.00, optionally the 1.60 update – and a Vulkan 1.3 GPU |
| License | GPL-2.0-or-later (same as shadPS4) |

## Support / Podpora

Lately, I've been running into more and more limitations with some of the most popular AI models.
Whether it's reverse engineering, software analysis, security research, or experimenting with my own
projects, mainstream AI services don't always give me the freedom and flexibility I need.

That's why I'd love to build my own powerful AI machine — a dedicated setup where I can run open-source
models locally, experiment without unnecessary restrictions, dive deeper into reverse engineering, and
develop new tools and projects.

The goal is simple: more independence, fewer limitations, and more freedom to explore, learn, build, and
share cool stuff with the community.

Work on the next project is already under way: an **NHL 27 PC port** – see the first look on YouTube:
[NHL 27 running on PC](https://www.youtube.com/watch?v=SU0FeYNAgO8).

If you'd like to support this journey, every contribution brings me one step closer to making it happen.
Even the smallest tip means a lot! ❤️

Thanks for supporting independent development, security research, and open-source projects! 🚀

[![ko-fi](https://ko-fi.com/img/githubbutton_sm.svg)](https://ko-fi.com/hajdeo)

Ko-fi: [ko-fi.com/hajdeo](https://ko-fi.com/hajdeo)

## How it works

The PS4 is an x86-64 machine. NHL 24's executable (`eboot.bin`) and its modules are ordinary AMD64 code,
so this port **does not emulate a CPU**: the game code is loaded into the process and executed by your
CPU directly, at native speed. What the emulator provides is everything around it:

- **HLE of the PS4 operating system** – kernel calls, threads, memory, files, audio (AJM), pads, video
  output, trophies, saves… are implemented as native host code (shadPS4's "libraries"). NHL 24 needs
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
  fullscreen, vsync, sharpening, language, controls, all controllers as player 1) and starts the game.
  Linux uses the Python CLI and optional Tk launcher in `docs/linux/`, which call the same extractor
  of the native `nhl24` executable.

## What's new in v0.9.4

Contributed by [hooman-zamboni](https://github.com/hooman-zamboni) in pull request #33, tested on an
Intel Arc B390 handheld (OneXPlayer 3).

- **Intel Arc: menus and the HUD no longer show vertical stripes.** Every second 64-pixel column of the
  UI was missing, so the 3D scene showed through like through blinds. The game builds its UI mask in a
  compute shader that exchanges values between 32 shader threads, and the Intel Windows driver does not
  exchange them between the two halves of 16. On Intel the exchange now goes through shared memory, and
  compute shaders get a fixed thread group size where the driver could vary it.
- **Smoother play on graphics cards with a single graphics queue (Intel Arc, AMD).** Reading data back
  from the GPU used to wait until the GPU had finished all queued work, about twice per frame. The
  emulator now waits only for the work that wrote the data and runs the copy on the card's separate
  copy engine. On the Arc B390 the wait went from 2.2 ms to 0.8 ms and the game mostly holds 60 fps.
  NVIDIA cards already worked this way and are not affected. `NHLPORT_NO_COPY_ENGINE=1` in the
  environment restores the old behaviour.
- **Errors the graphics driver handles itself no longer stop the emulator.** The Intel driver raises and
  catches C++ exceptions internally; the emulator treated them as a crash and shut its services down,
  which also kept the shader cache from being saved.
- **An outdated shader cache is rebuilt instead of ignored.** When the cache did not match the current
  build or graphics card, the emulator ignored it and also stopped saving, so every session compiled
  all shaders again until `user\cache` was deleted by hand. The old cache is now deleted and a new one
  started. This happens once after updating to v0.9.4, so the first minutes stutter again while the
  shaders compile.

## What's new in v0.9.3c

- **The launcher follows the display scale of Windows** (issue #40). On a display scaled above 100 %
  (for example a 4K screen at 200 %) the text grew while the window and its controls kept their size,
  so labels were cut off and overlapped. The whole window, its texts and controls now scale together.
  On a screen too small for the window (for example 1366x768, or 1920x1080 at 150 %) the launcher
  shrinks to fit instead of running off the screen. `nhl24.exe` is unchanged apart from the version
  number.

## What's new in v0.9.3b

- **Fewer random crashes and flashing black or white squares** (related to issue #19; fix by
  [hooman-zamboni](https://github.com/hooman-zamboni), pull request #38). When the game freed memory,
  the emulator deleted the textures in that memory right away on the game's own thread, while the GPU
  thread could still be using them. The two threads then changed the texture cache at the same time,
  which could crash the game ("Trying to unregister an already unregistered image") or briefly show
  missing textures as black or white squares. Those textures are now deleted on the GPU thread, the
  same way buffer readbacks already work. A full match showed no slowdown.

## What's new in v0.9.3a

- **Menus and cutscenes run at up to 60 fps** (issue #22). The game asks for a 30 fps output in menus
  and cutscenes, like on the console, and 60 fps only during play. The emulator now ignores that
  request and shows every frame the game renders. The speed of the game does not change, the
  animations are just smoother. Menus and team selection hold 60 fps; the heaviest cutscenes are
  limited by the emulator's GPU thread and stay at 20–30 fps for now. The old behaviour can be
  restored with `"ignore_flip_rate": false` in the `GPU` section of `user\config.json`.

## What's new in v0.9.3

- **Random crashes to the desktop during play fixed** (error 0xc0000005, issue #32; the same cause as
  the crash while a match loads, issues #1/#3). Some game functions keep local values in the "red
  zone" below the stack pointer. When such a function touched memory the emulator had protected,
  Windows wrote its exception data over those values and the game crashed a moment later. The
  emulator's red-zone protection is now on by default and was extended to cover every affected
  instruction of the game (246 were left unprotected before). It is no longer a launcher setting. The
  game needs about 20 s longer to start, because its code is analysed first.
- **Custom rosters and teams no longer disappear** (issue #29). If the game was closed or crashed
  while a save was in use, the next start overwrote that save's description file (`param.sfo`) with
  an empty one and the game stopped recognising the save. The file is no longer overwritten, and a
  save damaged this way is repaired on the next start; the roster data itself was never deleted.
- **Swapping controllers keeps your profile** (issue #31). A second controller used to become
  player 2 with a new profile ("shadPS4-2"). The launcher has a new option **All controllers control
  player 1** (off by default, so local multiplayer works as before): the game follows the controller
  you last pressed a button on.

Older changes are listed in the [release notes](https://github.com/HajDEO/nhl24pc/releases).

## Playing

### Windows

1. Download `NHL24-PC-v0.9.4.zip` from the [latest release](https://github.com/HajDEO/nhl24pc/releases/latest)
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
RTX 5060: 51 fps in play, 60 fps in menus; the heaviest cutscenes run at 20–30 fps).

### Linux

Use the `nhl24-pc-appimage-x86_64` artifact from the latest successful **Build** workflow, make the
contained file executable, and run it:

```bash
chmod +x NHL24-PC-v0.9.4-x86_64.AppImage
./NHL24-PC-v0.9.4-x86_64.AppImage
```

The AppImage stores its writable game data under `~/.local/share/nhl24pc`. The portable
`nhl24-pc-ubuntu-24.04-x64` archive remains available; unpack it to a writable directory and run:

```bash
./nhl24-launcher.py
```

The Tk launcher can install your base and update PKGs and start the game. The command-line equivalent
works without Tk:

```bash
./nhl24-linux.py install /path/to/base.pkg /path/to/update.pkg
./nhl24-linux.py play
```

See [the native Linux guide](docs/linux/LINUX.md) for distro dependencies, desktop integration,
Wayland/X11 notes and troubleshooting. Wine is not used.

**Requirements**: Windows 10/11 x64 or a current x86-64 Linux distribution; an x86-64-v3 CPU (Intel
Haswell or AMD Excavator and newer); a Vulkan 1.3 GPU with current drivers (NVIDIA GTX 10xx/RTX, AMD
RDNA; integrated GPUs are too slow); 8 GB RAM (16 GB recommended); about 40 GB free for the extracted
game; a gamepad (Xbox, DualShock 4, DualSense) is detected automatically, the keyboard works too.

**Known issues** (v0.9.4): on 8 GB graphics cards the video memory use can exceed the card's budget in
long sessions (the driver then moves part of it to system RAM, which costs performance); "device
lost" crashes and freezes during a match, reported mostly in NHL Threes (issues #37, #41). The Windows red-zone protection of the emulator is on by default and no longer a
launcher setting: it prevents the 0xc0000005 crashes while a match loads and at random during play
(issues #1/#3/#32, see `docs/DEVLOG.sk.md`) and adds about 20 s to the start of the game. Windows
SmartScreen may warn about the unsigned exe
(More info → Run anyway); some antivirus products dislike emulators that map large memory regions – add
an exception if needed.

## Repository layout

```
release/        ready-to-run package (binaries + clean user template), rebuilt by tools\make_release.ps1
launcher/       launcher sources (Launcher.cs), pictures, icon, build script (csc from .NET Framework)
docs/linux/     native Linux CLI/Tk launchers, packaging script and user guides
src/            emulator sources (shadPS4 fork; NHL-specific changes are marked "nhlport")
externals/      third-party libraries as git submodules (clone with --recursive)
game_patches/   nhl24.xml – memory patches applied by the emulator at start
tools/          build, run, measurement and diagnostics scripts (PowerShell / Python)
docs/           DEVLOG.sk.md (development diary, Slovak), bbport_analysis.md, patches/ (early fixes)
documents/      shadPS4's own documentation and README
```

## Building

Only needed to change the emulator or launcher; players can use a release/workflow artifact.

### Windows

- Windows 10/11, [Visual Studio 2022 Build Tools](https://visualstudio.microsoft.com/visual-cpp-build-tools/)
  (MSVC v143 + Windows SDK), [LLVM](https://github.com/llvm/llvm-project/releases) (clang-cl, lld),
  [CMake](https://cmake.org/) 3.24+, [Ninja](https://ninja-build.org/), [Vulkan SDK](https://vulkan.lunarg.com/).
- `git clone --recursive https://github.com/HajDEO/nhl24pc.git`
- `powershell -ExecutionPolicy Bypass -File tools\build_nhlport.ps1 -DebugInfo` → `build\nhl24.exe` (+ `.pdb`)
- `powershell -ExecutionPolicy Bypass -File launcher\build_launcher.ps1` → `build\launcher.exe`
- `powershell -ExecutionPolicy Bypass -File tools\make_release.ps1` → refreshes `release\`

### Linux

- Install CMake 3.24+ and the compiler/development packages listed in [the Linux build guide](documents/building-linux.md).
- `git clone --recursive https://github.com/HajDEO/nhl24pc.git`
- `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++`
- `cmake --build build --parallel` → `build/nhl24`
- `docs/linux/make-linux-repack.sh build/nhl24` → a redistributable Linux archive
- `docs/linux/make-linux-appimage.sh build/nhl24` → a self-contained x86-64 AppImage

Development notes: `tools\run_nhl24.ps1` runs the build with the diagnostics log enabled
(`NHLPORT_DIAG=1`; F1 captures a frame: draw log, image dumps, screenshot), `tools\measure_nhl24.ps1`
scripts measurement runs, `tools\capture_report.py` summarises a capture. The version lives in
`src/common/port_version.h` (also used for the exe resource and the launcher title).

## Contributing

Issues and pull requests are welcome – performance, the open rendering issues above, DualSense
features and platform support. Keep changes focused; NHL-specific code is marked `nhlport` in comments so it can be
told apart from upstream shadPS4. Please do not upload game files, PKGs, saves or shader caches.

## Legal

This project does not contain any game data. You need your own copy of NHL 24 for PlayStation 4,
dumped as a PKG from a console you own. "NHL" and "EA SPORTS" are trademarks of their respective
owners; this project is not affiliated with EA, the NHL or Sony. The emulator is derived from
[shadPS4](https://github.com/shadps4-emu/shadPS4) (GPL-2.0-or-later); this repository is distributed
under the same license, see `LICENSE` and `LICENSES/`.

---

## Slovensky

**NHL 24 (PS4) bežiace natívne na Windows a Linuxe x86-64.** Fork emulátora shadPS4 doladený pre jednu hru plus
launcher, ktorý hru rozbalí z vášho PKG a spustí. Kód hry (x86-64) beží priamo na procesore bez emulácie
CPU; systémové knižnice PS4 sú nahradené natívnou implementáciou (HLE) a grafika ide cez Vulkan
s prekladom shaderov GCN → SPIR-V.

**Video:** https://youtu.be/eGo1-W3FaYw

**Novinky vo v0.9.4:**

Pripravil [hooman-zamboni](https://github.com/hooman-zamboni) v pull requeste #33, testované na
handhelde s Intel Arc B390 (OneXPlayer 3).

- **Intel Arc: menu a HUD už nemajú zvislé pruhy.** Chýbal každý druhý 64-pixelový stĺpec UI, takže
  cez ne presvitala 3D scéna ako cez žalúzie. Hra si masku UI počíta v compute shaderi, ktorý si
  vymieňa hodnoty medzi 32 vláknami shadera, a ovládač Intel pre Windows ich nevymení medzi dvoma
  polovicami po 16. Na Inteli ide výmena cez zdieľanú pamäť a compute shadery dostanú pevnú veľkosť
  skupiny vlákien tam, kde ju ovládač môže meniť.
- **Plynulejšia hra na kartách s jedným grafickým radom (Intel Arc, AMD).** Čítanie dát späť z GPU
  čakalo, kým GPU dokončí všetku zaradenú prácu, asi dvakrát za snímku. Emulátor teraz čaká len na
  prácu, ktorá dáta zapísala, a kópiu púšťa na samostatnom kopírovacom engine karty. Na Arc B390
  kleslo čakanie z 2,2 ms na 0,8 ms a hra väčšinou drží 60 fps. Karty NVIDIA takto fungovali už
  predtým a nič sa na nich nemení. Pôvodné správanie vráti premenná prostredia
  `NHLPORT_NO_COPY_ENGINE=1`.
- **Chyby, ktoré si grafický ovládač ošetrí sám, už nezastavia emulátor.** Ovládač Intel vnútorne
  vyhadzuje a zachytáva C++ výnimky; emulátor ich bral ako pád a vypol svoje služby, čo zároveň
  bránilo uloženiu shader cache.
- **Zastaraná shader cache sa prebuduje, namiesto toho aby sa ignorovala.** Keď cache nesedela
  s aktuálnou verziou alebo grafickou kartou, emulátor ju ignoroval a prestal aj ukladať, takže sa
  pri každom spustení kompilovali všetky shadery znova, kým niekto ručne nezmazal `user\cache`.
  Stará cache sa teraz zmaže a začne sa nová. Stane sa to raz po prechode na v0.9.4, takže prvé
  minúty sa hra znova zasekáva, kým sa shadery skompilujú.

**Novinky vo v0.9.3c:**

- **Launcher sa riadi mierkou zobrazenia vo Windows** (issue #40). Na displeji s mierkou nad 100 %
  (napríklad 4K pri 200 %) sa zväčšil text, ale okno a ovládacie prvky ostali rovnako veľké, takže
  popisy boli orezané a prekrývali sa. Celé okno, texty aj ovládacie prvky sa teraz škálujú spolu.
  Na obrazovke, na ktorú sa okno nezmestí (napríklad 1366x768 alebo 1920x1080 pri 150 %), sa launcher
  zmenší, namiesto toho aby presahoval mimo obrazovku. `nhl24.exe` sa okrem čísla verzie nezmenil.

**Novinky vo v0.9.3b:**

- **Menej náhodných pádov a blikajúcich čiernych či bielych štvorcov** (súvisí s issue #19; opravu
  pripravil [hooman-zamboni](https://github.com/hooman-zamboni), pull request #38). Keď hra uvoľnila
  pamäť, emulátor textúry v nej zmazal hneď vo vlákne hry, hoci ich GPU vlákno mohlo ešte používať.
  Obe vlákna potom menili texture cache naraz, čo mohlo zhodiť hru („Trying to unregister an already
  unregistered image“) alebo nakrátko ukázať chýbajúce textúry ako čierne či biele štvorce. Tieto
  textúry sa teraz mažú v GPU vlákne, rovnako ako už fungujú readbacky bufferov. Celý odohraný zápas
  nebol pomalší.

**Novinky vo v0.9.3a:**

- **Menu a prestrihy bežia až na 60 fps** (issue #22). Hra si v menu a prestrihoch žiada výstup 30 fps
  ako na konzole a 60 fps len počas zápasu. Emulátor túto požiadavku teraz ignoruje a zobrazí každú
  snímku, ktorú hra vykreslí. Rýchlosť hry sa nemení, animácie sú len plynulejšie. Menu a výber tímov
  držia 60 fps; najťažšie prestrihy brzdí GPU vlákno emulátora a zatiaľ ostávajú na 20–30 fps. Pôvodné
  správanie vráti `"ignore_flip_rate": false` v sekcii `GPU` súboru `user\config.json`.

**Novinky vo v0.9.3:**

- **Opravené náhodné pády na plochu počas hry** (chyba 0xc0000005, issue #32; rovnaká príčina ako pád
  pri načítaní zápasu, issues #1/#3). Niektoré funkcie hry si držia lokálne hodnoty v „red zone“ pod
  ukazovateľom zásobníka. Keď taká funkcia siahla na pamäť, ktorú si emulátor chránil, Windows tieto
  hodnoty prepísal dátami výnimky a hra o chvíľu spadla. Ochrana red zone v emulátore je teraz
  predvolene zapnutá a pokrýva všetky dotknuté inštrukcie hry (predtým ich 246 ostávalo nechránených).
  V launcheri už nie je ako nastavenie. Hra štartuje asi o 20 s dlhšie, lebo sa najprv analyzuje jej kód.
- **Vlastné rostre a tímy už nemiznú** (issue #29). Ak sa hra zavrela alebo spadla, kým sa používala
  uložená pozícia, ďalší štart prepísal jej popisný súbor (`param.sfo`) prázdnym a hra pozíciu prestala
  spoznávať. Súbor sa už neprepisuje a takto poškodená pozícia sa pri ďalšom štarte opraví; samotné
  dáta rostrov sa nikdy nemazali.
- **Striedanie ovládačov zachová profil** (issue #31). Druhý ovládač sa doteraz stal hráčom 2 s novým
  profilom („shadPS4-2“). Launcher má nové nastavenie **Všetky ovládače ovládajú hráča 1** (predvolene
  vypnuté, takže lokálny multiplayer funguje ako doteraz): hru ovláda ten ovládač, na ktorom ste
  naposledy stlačili tlačidlo.

Staršie zmeny sú v [poznámkach k vydaniam](https://github.com/HajDEO/nhl24pc/releases).

**Hranie:** stiahnite `NHL24-PC-v0.9.4.zip` z [posledného vydania](https://github.com/HajDEO/nhl24pc/releases/latest)
(alebo repozitár cez Code → Download ZIP a prejdite do priečinka `release`), rozbaľte ho
(všetko potrebné je tam); spustite `launcher.exe`, cez **Install PKG** vyberte PKG **základnej hry** (v01.00)
– hra sa rozbalí do `game\` vedľa launchera – a stlačte **PLAY**. Update 1.60 môžete potom
nainštalovať rovnako; od v0.9.0 port podporuje verziu hry 1.00 aj 1.60. Podporovaná je americká
(CUSA37933) aj európska verzia (CUSA37934, od v0.9.0); update 1.60 je zatiaľ overený len na americkej.
Uložené pozície, profil a shader cache vznikajú v `user\`; prvé minúty sa hra zasekáva (kompilácia
shaderov), potom beží plynulo. Na Linuxe použite balík `nhl24-pc-ubuntu-24.04-x64` z úspešného
**Build** workflow a `nhl24-launcher.py`; podrobný postup je v
[`docs/linux/LINUX.md`](docs/linux/LINUX.md).

**Požiadavky:** Windows 10/11 x64 alebo aktuálny Linux x86-64, procesor x86-64-v3 (Intel Haswell alebo
AMD Excavator a novší), GPU s Vulkan 1.3 (NVIDIA GTX 10xx/RTX, AMD RDNA), 8–16 GB RAM, asi 40 GB miesta
na rozbalenú hru. Gamepad funguje automaticky, klávesnica tiež.

**Známe problémy (v0.9.4):** na 8 GB grafických kartách môže využitie videopamäte pri dlhom hraní
prekročiť rozpočet karty (driver potom časť presunie do systémovej RAM, čo stojí výkon); pády
„device lost“ a zamrznutia počas zápasu, hlásené najmä v NHL Threes (issues #37, #41). Ochrana red zone (Windows) je v emulátore predvolene zapnutá a už nie je
nastavením v launcheri: bráni pádom 0xc0000005 pri načítaní zápasu aj náhodne počas hry (issues
#1/#3/#32, rozbor v `docs/DEVLOG.sk.md`) a predlžuje štart hry asi o 20 s.

**Vývoj:** zdrojáky emulátora v `src/` (zmeny pre NHL sú označené `nhlport`), launchery v `launcher/`
a `docs/linux/`, skripty v `tools/`, denník vývoja v `docs/DEVLOG.sk.md`. Windows build:
`tools\build_nhlport.ps1`; Linux build je opísaný v `documents/building-linux.md`.
