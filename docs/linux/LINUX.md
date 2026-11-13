# NHL24 PC on Linux

NHL24 PC runs as a native Linux application. Wine, PowerShell and the Windows launcher are not
required. The Linux package contains the emulator, a Tk launcher, a command-line installer, a clean
portable configuration and the current NHL 24 memory patch. You must supply your own legally dumped
NHL 24 PKG.

Supported game packages:

- CUSA37933 (US) and CUSA37934 (EU)
- base game 01.00
- update 01.60

## Requirements

- x86-64 Linux on an x86-64-v3 CPU (Intel Haswell or AMD Excavator and newer)
- a Vulkan 1.3-capable GPU with current drivers
- 8 GB RAM minimum; 16 GB recommended
- about 40 GB free for the extracted game
- Python 3.8 or newer
- Tk 8.6 for the graphical launcher (`python3-tk` on Debian/Ubuntu, `tk` on Arch)
- CMake 3.24 or newer when building from source

The emulator build dependencies are the same as upstream shadPS4. Common packages are shown below.

Debian/Ubuntu:

```bash
sudo apt install build-essential clang clang-tools git cmake ninja-build libasound2-dev libpulse-dev \
  libopenal-dev libssl-dev zlib1g-dev libedit-dev libudev-dev libevdev-dev libsdl2-dev \
  libjack-dev libsndio-dev libvulkan-dev vulkan-validationlayers libpng-dev uuid-dev python3-tk
```

Ubuntu 22.04's repository contains CMake 3.22, which is too old. Install a current CMake release or
build in a newer distro/container.

Fedora:

```bash
sudo dnf install clang git cmake ninja-build alsa-lib-devel pipewire-jack-audio-connection-kit-devel \
  openal-soft-devel openssl-devel libevdev-devel systemd-devel libXext-devel libXcursor-devel \
  libXi-devel libXrandr-devel vulkan-devel vulkan-validation-layers libpng-devel libuuid-devel \
  python3-tkinter
```

Arch Linux:

```bash
sudo pacman -S base-devel clang git cmake ninja sdl2 vulkan-validation-layers libpng util-linux python tk
```

## Build

Clone all submodules, configure and build:

```bash
git clone --recursive https://github.com/HajDEO/nhl24pc.git
cd nhl24pc
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
cmake --build build --parallel "$(nproc)"
```

The native executable is `build/nhl24`. No additional patch is required: build the current branch
directly.

## Create a portable Linux package

From the repository root, after building:

```bash
./docs/linux/make-linux-repack.sh
```

The script creates `NHL24-PC-Linux-v<version>.tar.gz` in the current directory using only files from
this repository. To use another binary or output path:

```bash
./docs/linux/make-linux-repack.sh /path/to/nhl24 /path/to/NHL24-PC-Linux.tar.gz
```

Extract the archive somewhere writable. Keep its `nhl24`, Python scripts and `user/` directory
together.

The workflow's prebuilt artifact is named `nhl24-pc-ubuntu-24.04-x64-...` because Ubuntu 24.04 is its
binary compatibility baseline. Build from source for older distributions. `BUILD-INFO.txt` inside the
archive records the exact source commit and required glibc/libstdc++ symbol versions.

## Install and play

Graphical launcher:

```bash
./nhl24-launcher.py
```

Click **Install PKG** and select the base-game PKG. After it finishes, use the same button to install
the 01.60 update. The installer checks the package type and region and extracts updates into the
`game-UPDATE/` overlay. Click **PLAY** to start.

Command-line equivalent:

```bash
./nhl24-linux.py install /path/to/base.pkg
./nhl24-linux.py update /path/to/update.pkg
./nhl24-linux.py play
```

The command-line installer can also install both packages in one operation:

```bash
./nhl24-linux.py install /path/to/base.pkg /path/to/update.pkg
```

Other commands:

- `status` shows the installed version and patch state.
- `settings` opens the text settings menu.
- `remove-update` returns to the base game.
- `desktop` recreates application-menu and desktop shortcuts.
- `desktop --remove-wine` also removes obsolete Wine shortcuts created by older packages.

The graphical launcher exposes settings implemented by the current source: render resolution,
fullscreen/windowed mode, HDR, VSync, frame limit, FSR, safe readbacks, diagnostics, Russian game text
and keyboard bindings. PS4 Pro mode remains disabled because it is not stable for this title.

The installer places the game under `game/`, the update under `game-UPDATE/`, and saves/configuration
under `user/`. The first launch can stutter or remain black for several minutes while shaders compile.

Logs are stored in `user/log/`:

- `shad_log.txt` is the emulator log.
- `linux_run.log` captures launcher output.
- `extract.log` and `install-gui.log` cover package installation.

## Running from a source checkout

The scripts can be run directly from `docs/linux/`. They discover `build/nhl24` automatically and
seed a clean configuration from `release/user/config.json`:

```bash
./docs/linux/nhl24-launcher.py
```

In this development layout, game data and portable settings are kept beside the executable as
`build/game/`, `build/game-UPDATE/` and `build/user/`. This matches the emulator's executable-relative
lookup. For normal play, the portable archive is recommended.

## Notes

- On Wayland, install XWayland/`xrandr` if automatic display-size detection falls back to 1920x1080.
- A binary built on a newer distribution may require newer glibc/libstdc++ versions. The packaging
  script records both detected symbol-version requirements in `BUILD-INFO.txt` and prints its dynamic
  libraries.
- Do not upload or redistribute game files, PKGs, saves or shader caches.
