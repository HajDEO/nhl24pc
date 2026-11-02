# NHL24 PC on Linux (native build)

Status: **works** — the native Linux build of this fork runs NHL 24 (menus, matches, Russian
text). Installing, patching, settings and launching are done by two Python scripts, with no
Wine, PowerShell or .NET. The Windows build under Wine does **not** work (see "Wine" below).

## Tested setup

| | |
|---|---|
| Distro | Gentoo Linux (systemd) |
| CPU / GPU | Ryzen 7 9700X (iGPU present) / Radeon RX 9060 XT |
| Driver | Mesa RADV 26.1.8, Vulkan 1.4 |
| Compiler | clang 23.1, CMake 4.3, Ninja 1.13 |
| Python | 3.14 with Tk 8.6 |
| Game | CUSA37934, base 1.00 + update 1.60 |
| Source | `main` as of 2026-11-01 |

## 1. Build the emulator

Dependencies (Gentoo names; other distros: same as upstream shadPS4):
`llvm-core/clang dev-build/cmake dev-build/ninja dev-vcs/git dev-util/vulkan-headers
media-libs/vulkan-loader media-libs/alsa-lib media-video/pipewire dev-libs/openssl
dev-libs/libevdev virtual/libudev x11-libs/libXext x11-libs/libXrandr media-libs/libpng
sys-apps/util-linux`

```bash
git clone --recursive https://github.com/HajDEO/nhl24pc.git
cd nhl24pc
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
cmake --build build -j$(nproc)
```

Builds without changes; the result is `build/nhl24`.

**Recommended: apply the repack's emulator changes.** The NHL24PC V3.2 repack ships its
emulator changes as sources (`source_changes/`, 41 files on top of commit `bef79997`).
`nhl24pc-v3.2-repack.patch` in this folder is the same changes as one patch. Without it the
launcher's `NHLPORT_*` switches (anisotropy, LOD bias, menu frame rate, image pool, GTX 10xx fix,
GPU checkpoints) do nothing, and the texture-cache overlap that crashes PS4 Pro mode is not handled.

```bash
git checkout bef7999707db27884cabe5780f301c4a5d2abdc8
git apply /path/to/nhl24pc-v3.2-repack.patch
cmake --build build -j$(nproc)
```

Not built on Linux yet: the patch keeps Windows-only parts under `#ifdef _WIN32`, but compile
errors are possible.

## 2. Install the game

Put the scripts from this folder and the emulator into the game folder:

```
NHL24/
  nhl24-linux          <- build/nhl24, copied/renamed
  nhl24-linux.py       <- installer, patches, text-mode settings
  nhl24-launcher.py    <- launcher window (optional)
```

```bash
cd NHL24
./nhl24-linux.py install /path/to/base.pkg /path/to/update.pkg
```

This does what the Windows installer does:

* extracts both PKGs with the emulator's own extractor (`nhl24-linux --extract-pkg`)
  into `game/` and `game-UPDATE/`, checking PKG type and region;
* patches `eboot.bin` by byte signature (backup in `eboot.bin.orig`): freeze before the first
  puck drop, dynamic resolution off, render resolution for your screen and VRAM;
* writes `user/config.json` (from `tools/config_default.json` if present) with
  `neo_mode: false` (see section 4);
* adds "NHL 24" to the application menu and to the desktop.

Other commands: `status`, `settings` (text menu), `play`, `update UPDATE.pkg`,
`remove-update`, `mod install|remove|status` (Russia / Belarus national teams, needs the
repack's `tools/mods`), `desktop [--remove-wine]` (re-create shortcuts).

Fresh install from PKG with this script: implemented, not yet tested from scratch
(my copy was first extracted with the Windows installer under Wine).

## 3. Launcher window

`nhl24-launcher.py` is a Tkinter port of the repack's launcher: same layout and settings
(resolution, display mode, FPS cap, FSR, anisotropy, sharpness, Russian text, stable mode,
GTX 10xx fix, diagnostics, keyboard bindings, weak-PC preset, crash report). PLAY applies the
settings to `eboot.bin` / `config.json`, sets the `NHLPORT_*` variables and starts
`nhl24-linux`. "PS4 Pro mode" is shown but locked off.

Needs Tk for Python. Gentoo: `echo "dev-lang/python tk" >> /etc/portage/package.use/python`,
then `emerge -av1 --changed-use dev-lang/python`. Debian/Ubuntu: `python3-tk`. Arch: `tk`.
Pillow is optional (slightly sharper header art).

Russian text: enable it in the launcher, then pick **English** at the game's language screen.

## 4. PS4 Pro mode must be off

With `neo_mode: true` the game crashes on the first frames:

```
[Debug] <Critical> (shadPS4:GpuComm) texture_cache.cpp:585 ResolveOverlap: Unreachable code!
Encountered unresolvable image overlap with equal memory address.
```

The overlap dump right before it shows two identical 2560-pitch images (tile mode 0xa,
32 bpp) that differ only in `Alt tile` (`Same alt tile: false`). With Neo mode off the
game runs. Both scripts force `neo_mode: false` before every start.

## 5. Run

From the application menu or the desktop ("NHL 24"), or:

```bash
./nhl24-launcher.py      # window
./nhl24-linux.py play    # start directly with the saved settings
```

First start: black screen for several minutes while shaders compile.
Log: `user/log/linux_run.log` (plus the emulator's own `user/log/shad_log.txt`).

Desktop shortcut on KDE Plasma: Plasma only starts `.desktop` files from the desktop when
they are executable, otherwise it opens them in a text editor. `desktop` sets the bit; on the
first double click Plasma asks once, answer "Execute". If the Windows installer ran under
Wine, it left its own "NHL 24" shortcuts (they start the old launcher through Wine and no
longer work): `./nhl24-linux.py desktop --remove-wine` removes them.

Multi-GPU note: the emulator picked the discrete card correctly
(`GPU_Model: AMD Radeon RX 9060 XT`, `GPU_Integrated: No`).

## Wine (Windows build) — does not work

`nhl24_new.exe` (v0.8.3, `bef79997-dirty`) under wine-staging 11.20 + DXVK 3.1.1,
prefix set to both Windows 7 and Windows 10, always dies right after the
`Content Mounter` thread starts:

```
[Kernel.Pthread] <Info> (Content Mounter) pthread.cpp:497 posix_pthread_rename_np: name = Content Mounter
[Debug] <Critical> (Game:Main) mutex.cpp:13 lambda: Assertion Failed!
```

The native build does not hit this, so it looks like a Wine threading/sync difference
rather than a bug in the emulator itself.

## Building your own Linux archive

`make-linux-repack.sh` builds `NHL24PC-Linux_<version>.tar.gz` from the Windows repack (takes
`tools/config_default.json`, the national-teams mod `tools/mods/`, `user/input_config/`, the
READMEs) and the built emulator, plus the scripts, guides and sources (patch, SOURCE.txt, LICENSE):

```bash
./make-linux-repack.sh NHL24PC_V3_2.zip ~/src/nhl24pc 3.2-linux1
```

No game files go in. The script prints the glibc version and libraries the emulator needs:
a binary built on a recent distro will not start on an older one.

## Legacy: repack launcher under Wine

Before the native scripts existed, the playground.ru repack's PowerShell launcher
(`tools/launcher.ps1`) was run under Wine for settings, and `nhl24.sh` started the native
binary afterwards. `patch_launcher.sh` adds the assembly references PowerShell 7 needs
(`System.ComponentModel.Primitives`, `System.ComponentModel.TypeConverter`,
`System.Windows.Forms.Primitives`, `System.Drawing.Primitives`, `System.Collections`,
`System.Drawing.Common`) and makes PLAY hand over to the native build. Kept for reference;
`nhl24-launcher.py` replaces it.

## Credits

The patch signatures, settings and launcher design come from the NHL24PC repack by
LEVASTAK; the emulator is HajDEO's nhl24pc fork of shadPS4.
