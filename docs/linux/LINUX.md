# NHL24 PC on Linux (native build)

Status: **works** — the native Linux build of this fork boots NHL 24 to the language
screen and beyond. The Windows build under Wine does **not** work (see "Wine" below).

## Tested setup

| | |
|---|---|
| Distro | Gentoo Linux (systemd) |
| CPU / GPU | Ryzen 7 9700X (iGPU present) / Radeon RX 9060 XT |
| Driver | Mesa RADV 26.1.8, Vulkan 1.4 |
| Compiler | clang 23.1, CMake 4.3, Ninja 1.13 |
| Game | CUSA37934, base 1.00 + update 1.60 |
| Source | `main` as of 2026-11-01 |

## 1. Build

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

## 2. Game files

The emulator expects the same layout as on Windows, next to the binary:

```
NHL24/
  nhl24-linux        <- build/nhl24, copied/renamed
  game/              <- extracted base PKG (eboot.bin inside)
  game-UPDATE/       <- extracted update 1.60 (optional)
  user/              <- config.json, patches/, saves, shader cache
```

I extracted and patched the game with the Windows installer running under Wine
(that part works). `nhl24 --extract-pkg` from the README may also work natively —
not tested.

## 3. Required config change: PS4 Pro mode off

In `user/config.json` set:

```json
"neo_mode": false,
```

With `neo_mode: true` the game crashes on the first frames:

```
[Debug] <Critical> (shadPS4:GpuComm) texture_cache.cpp:585 ResolveOverlap: Unreachable code!
Encountered unresolvable image overlap with equal memory address.
```

The overlap dump right before it shows two identical 2560-pitch images (tile mode 0xa,
32 bpp) that differ only in `Alt tile` (`Same alt tile: false`). With Neo mode off the
game runs.

## 4. Run

```bash
cd NHL24
./nhl24-linux
```

The binary finds `game/eboot.bin` by itself. First start: black screen for several
minutes while shaders compile.

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

## Launcher under Wine (repack-specific)

I used the playground.ru repack "NHL24PC 3.2", which ships a PowerShell launcher
(`tools/launcher.ps1`) instead of `launcher.exe`. To use it on Linux:

* Wine runs it in PowerShell 7 (.NET 8), not Windows PowerShell 5.1, so its `Add-Type`
  block needs extra references: `System.ComponentModel.Primitives`,
  `System.ComponentModel.TypeConverter`, `System.Windows.Forms.Primitives`,
  `System.Drawing.Primitives`, `System.Collections`, `System.Drawing.Common`.
* The PLAY button is patched to write the `NHLPORT_*` environment variables to
  `linux_env.sh` and exit instead of starting the Windows exe; `nhl24.sh` then starts
  the native binary with them.

`patch_launcher.sh` and `nhl24.sh` in this folder do both. Russian text (the launcher's
"Russian game text" option) works this way.
