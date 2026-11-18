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
| Current build | **v0.9.3b** (2026-11-18), Windows 10/11 x64 |
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
  fullscreen, vsync, sharpening, language, controls, all controllers as player 1) and starts the game.

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

1. Download `NHL24-PC-v0.9.3b.zip` from the [latest release](https://github.com/HajDEO/nhl24pc/releases/latest)
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

**Requirements**: Windows 10/11 x64; a Vulkan 1.3 GPU with current drivers (NVIDIA GTX 10xx/RTX,
AMD RDNA; integrated GPUs are too slow); 8 GB RAM (16 GB recommended); about 40 GB free for the
extracted game; a gamepad (Xbox, DualShock 4, DualSense) is detected automatically, the keyboard works
too.

**Known issues** (v0.9.3b): on 8 GB graphics cards the video memory use can exceed the card's budget in
long sessions (the driver then moves part of it to system RAM, which costs performance); rare "device
lost" crashes. The Windows red-zone protection of the emulator is on by default and no longer a
launcher setting: it prevents the 0xc0000005 crashes while a match loads and at random during play
(issues #1/#3/#32, see `docs/DEVLOG.sk.md`) and adds about 20 s to the start of the game. Windows
SmartScreen may warn about the unsigned exe
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

**Hranie:** stiahnite `NHL24-PC-v0.9.3b.zip` z [posledného vydania](https://github.com/HajDEO/nhl24pc/releases/latest)
(alebo repozitár cez Code → Download ZIP a prejdite do priečinka `release`), rozbaľte ho
(všetko potrebné je tam); spustite `launcher.exe`, cez **Install PKG** vyberte PKG **základnej hry** (v01.00)
– hra sa rozbalí do `game\` vedľa launchera – a stlačte **PLAY**. Update 1.60 môžete potom
nainštalovať rovnako; od v0.9.0 port podporuje verziu hry 1.00 aj 1.60. Podporovaná je americká
(CUSA37933) aj európska verzia (CUSA37934, od v0.9.0); update 1.60 je zatiaľ overený len na americkej.
Uložené pozície, profil a shader cache vznikajú v `user\`; prvé minúty sa hra zasekáva (kompilácia
shaderov), potom beží plynulo.

**Požiadavky:** Windows 10/11 x64, GPU s Vulkan 1.3 (NVIDIA GTX 10xx/RTX, AMD RDNA), 8–16 GB RAM,
asi 40 GB miesta na rozbalenú hru. Gamepad funguje automaticky, klávesnica tiež.

**Známe problémy (v0.9.3b):** na 8 GB grafických kartách môže využitie videopamäte pri dlhom hraní
prekročiť rozpočet karty (driver potom časť presunie do systémovej RAM, čo stojí výkon), zriedkavé
pády „device lost“. Ochrana red zone (Windows) je v emulátore predvolene zapnutá a už nie je
nastavením v launcheri: bráni pádom 0xc0000005 pri načítaní zápasu aj náhodne počas hry (issues
#1/#3/#32, rozbor v `docs/DEVLOG.sk.md`) a predlžuje štart hry asi o 20 s.

**Vývoj:** zdrojáky emulátora v `src/` (zmeny pre NHL sú označené `nhlport`), launcher v `launcher/`,
skripty v `tools/`, denník vývoja v `docs/DEVLOG.sk.md`. Build: `tools\build_nhlport.ps1`,
`launcher\build_launcher.ps1`, balík: `tools\make_release.ps1`.
