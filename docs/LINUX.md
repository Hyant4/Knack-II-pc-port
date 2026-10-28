# Linux source build (experimental)

Knack II's supervisor and Qt launcher can use the native Linux x86-64 runtime.
Wine and MSYS2 are not needed. The Windows release ZIP is still Windows-only;
Linux currently requires a source build. Building and synthetic tests do not
certify a game boot, a complete campaign, audio, co-op, or every GPU/driver.

You need an x86-64 Linux system, a Vulkan-capable GPU with its working Linux
driver, Python 3.11 or newer, and your own extracted compatible game dump.
The primary supervisor profile is CUSA08014 version 01.01. Point the launcher
at the directory containing `eboot.bin`, `game.elf`, `sce_sys`, and `sce_module`.
A `.pkg` archive is not an extracted dump and cannot be launched directly.

## Build dependencies

The native build uses CMake 3.24+, Ninja, a C++23 compiler, pkg-config, Vulkan,
SDL3, FFmpeg development libraries (avformat, avcodec, avutil, swscale,
swresample), FreeType, fmt, Boost, robin-map, xxHash, VulkanMemoryAllocator,
Zydis, glslang, and X11 headers/libraries. Git submodules supply the remaining
libraries and newer Vulkan headers.

On Fedora 44:

```sh
sudo dnf install git gcc gcc-c++ cmake ninja-build pkgconf-pkg-config \
  python3 python3-pip vulkan-loader-devel SDL3-devel ffmpeg-free-devel \
  freetype-devel fmt-devel boost-devel robin-map-devel xxhash-devel \
  VulkanMemoryAllocator-devel glslang zydis-devel libX11-devel \
  libxkbcommon-x11 xcb-util-cursor xcb-util-wm xcb-util-image \
  xcb-util-keysyms xcb-util-renderutil vulkan-tools
sudo dnf install https://mirrors.rpmfusion.org/free/fedora/rpmfusion-free-release-44.noarch.rpm
sudo dnf install libavcodec-freeworld
```

The native FFmpeg `h264` decoder is required. Fedora's default `ffmpeg-free`
can select `libopenh264`, which fails the runtime's reordered-frame metadata
contract. The RPM Fusion package above supplies the native decoder, following
[Fedora's codec guidance](https://fedoraproject.org/wiki/Hardware_Video_Acceleration).

On Bazzite, use a Fedora 44 Distrobox for these development packages instead
of modifying the immutable host:

```sh
distrobox create --name knack2-dev --image registry.fedoraproject.org/fedora:44
distrobox enter knack2-dev
```

For an NVIDIA Bazzite image, add `--nvidia` to the create command to integrate
the host driver:

```sh
distrobox create --name knack2-dev --image registry.fedoraproject.org/fedora:44 --nvidia
distrobox enter knack2-dev
```

Choose the create command matching your GPU, then enter the container.
Run the Fedora dependency installation and all the following commands inside
that environment. Distrobox shares your home directory and desktop session.
For GPU use, the container must also have access to the appropriate driver;
a plain headless build container does not validate graphics or desktop access.

Check driver visibility inside the Distrobox with `vulkaninfo --summary` after
installing the packages. It should list your real GPU. If Qt reports a Wayland
platform error, try `QT_QPA_PLATFORM=xcb python -m port.launcher.knack2 --real`
through the desktop's XWayland session.

The existing `port/shell.nix` provides an alternative Nix development shell.
The Ubuntu dependency helper in `port/packaging/linux/build_deps.sh` builds
libraries absent from Ubuntu 24.04; it is not a complete system setup script.

## Build and launch

In the Distrobox, enter the checkout shared from the host. From the repository root:

```sh
git submodule update --init --recursive
python3 -m venv .venv
. .venv/bin/activate
python -m pip install iced-x86==1.21.0 -r port/launcher/knack2/requirements.txt
TLG_BUILD_JOBS=2 TLG_LTO=OFF bash port/build.sh
python -m port.launcher.knack2 --real
```

`TLG_BUILD_JOBS` limits native build concurrency (default: 6). Two workers and
LTO disabled reduce peak memory on smaller machines. Omit `TLG_LTO=OFF` to
use the build's default link-time optimization.

The build writes `port/out/tlg-probe`, `port/out/tlg-gpu-capabilities`, and
`port/out/gpu/libtlggpu.so`. Keep the `gpu` directory with the runtime: its
relative runtime library search path is `$ORIGIN/gpu`. The launcher also accepts
a native branded `port/out/knack2-runtime`, preferring it over `tlg-probe`.
Linux does not select Windows `.exe` artifacts left in the same directory.
Do not reuse a Windows CMake build directory for Linux. CMake also records
absolute source paths: if a checkout was built at a different container mount
point and CMake reports a source/cache mismatch, clear its generated metadata
before rebuilding (this preserves the runtime binaries and user data):

```sh
rm -f port/out/gpu/CMakeCache.txt
rm -rf port/out/gpu/CMakeFiles
```

To run through the command-line supervisor:

```sh
python run.py --game-dir /path/to/extracted/dump --timeout 0
```

`--timeout 0` disables the session time limit. Omit it for a bounded 180-second
run. Use `--analyze` for preparation only, or `--check-imports` to check the
prepared executables' imports without gameplay. These modes still require the
dump; passing them is not evidence that gameplay works.

Linux uses the included, licensed DejaVu Sans font for the system-font service.
Set `KNACK2_SYSTEM_FONT=/absolute/path/to/font.ttf` before launching to override
it. Windows continues using its existing Arial fallback. Windows red-zone
rewriting is disabled on Linux, which already preserves the System V red zone.

To launch again later from a Bazzite host terminal, without rebuilding:

```sh
distrobox enter knack2-dev -- bash -lc 'cd "$HOME/Knack-II-pc-port" && .venv/bin/python -m port.launcher.knack2 --real'
```

Select the extracted dump directory, click **Validate**, then **Play**. The
launcher checks the dump before enabling play. For a dump on a separate drive,
make sure its mount point is visible and readable inside the Distrobox.

## Game audio

The game mixes its sound with the console's NGS2 library, which is a firmware
module and not part of the game dump. Without it the runtime renders silence
(`Runtime: NGS2 system (silent) (placeholder)` in the game log). Copy
`libSceNgs2.sprx` from your own console firmware dump (the same file shadPS4
uses from its `sys_modules` folder) into `firmware/` at the repository root:

```sh
mkdir -p firmware && cp /path/to/sys_modules/libSceNgs2.sprx firmware/
```

`run.py` also looks in `$KNACK2_SYSTEM_MODULES` and in shadPS4's
`~/.local/share/shadPS4/sys_modules`. When it finds the module it prints
`Audio: firmware NGS2 ...`, links it into `game.elf` and the game log shows
`Config env: TLG_NGS2=native`. `firmware/` is ignored by git; never commit it.
This applies to Linux only: on Windows `run.py` does not look for the module
and audio is unchanged.

## Language

Start the launcher with `--language en-US` for English launcher text and
English game audio/text:

```sh
python -m port.launcher.knack2 --real --language en-US
```

The language can also be changed in Settings and saved for future launches.

## Data and tests

The Qt launcher stores profiles, settings, and session evidence in
`${XDG_DATA_HOME:-$HOME/.local/share}/knack-ii-pc`. The command-line supervisor
uses `out/knack2` by default; pass `--out` and `--user-data` for explicit output
and persistent profile locations. Back up saves with the game closed.

Synthetic checks require no game files:

```sh
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software \
  python -B -m unittest discover -s tests -p 'test_knack2*.py' -v
bash port/build.sh --test
python -B -m unittest discover -s tests -p test_font.py -v
python -B -m unittest discover -s tests -p test_savedata.py -v
```

For the native Knack II LoadExec roundtrip test:

```sh
mkdir -p out
cc -std=c11 -Wall -Wextra -Werror -pthread -Iport/src \
  tests/test_knack_loadexec.c port/src/runtime_knack.c -o out/knack-loadexec-test
python -B -m unittest discover -s tests -p test_knack2.py -v
```

Report Linux problems with distro, GPU, driver, and reproduction steps. Review
logs for private paths before sharing; do not attach game data or saves.

