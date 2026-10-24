<p align="center">
  <img src="docs/screenshots/knack-ii-title-screen.png" alt="Knack II title screen running in the Windows port" width="76%" />
</p>

<h1 align="center">Knack II PC Port</h1>

<p align="center">
  <strong>Independent Windows port of Knack II — native guest execution, Vulkan graphics, local co-op, and a bundled launcher.</strong>
</p>

<p align="center">
  <a href="https://github.com/Hyant4/Knack-II-pc-port/releases/tag/knack2-Version-0.14"><strong>Download Windows x64 V0.14</strong></a>
  · <a href="README.pt-BR.md">Português (Brasil)</a>
  · <a href="https://hyant4.github.io/Knack-II-pc-port/">Project website</a>
  · <a href="https://github.com/Hyant4/Knack-II-pc-port/issues">Report an issue</a>
</p>

> **Experimental project.** Early solo play and local co-op have been exercised; the project owner has reported reaching Chapter 2 in local co-op. The full campaign, every hardware configuration, and later chapters are still not validated.

## Play it

1. Download [**Knack-II-Windows-x64-V0.14.zip**](https://github.com/Hyant4/Knack-II-pc-port/releases/download/knack2-Version-0.14/Knack-II-Windows-x64-V0.14.zip).
2. Extract the entire ZIP. Do not run it from inside the archive.
3. Open `Knack II.exe`.
4. Select your own **Knack II CUSA08014, version 01.01** dump and choose **Validate**.
5. Set language, graphics, and one or two local players, then select **Play**.

Your selected folder must contain `eboot.bin`, `game.elf`, `sce_sys`, and `sce_module`.

The package includes its Windows runtime dependencies. It does **not** include the game, firmware, saves, or game assets.

## What V0.14 adds

| Area | Current release behavior |
| --- | --- |
| Dialogue | The game's explicit Voice category receives an additional 50% gain. Music and effects keep their own category gains. |
| Overall audio | SDL output gain is 30% higher. |
| Simultaneous sounds | Normal mixer path combines up to 256 sources per block, avoiding prior cuts caused by rotating 32-source selection. |
| Pause and resume | Synthetic contracts cover voice queues and sample clocks across pause/resume and cancelled renders. |
| Graphics | FSR 3.1/4 options and corrected Windows FSR 4 shaders remain available. |

The dialogue gain and continuity changes passed native and synthetic validation. The V0.14 dialogue change has not yet had an independent listening test in gameplay. Initial audio-feed gaps observed in an earlier manual session remain a separate investigation.

## What works today

| Area | Evidence and limit |
| --- | --- |
| Launcher | Windows launcher validates a selected dump, remembers its location, and starts the supervised runtime. |
| Early game | Boot, title, menus, Chapter 1-1 movement, combat, and transition work have been exercised locally. |
| Local co-op | Two-player local co-op has been exercised by the project owner through Chapter 2. This is not a full-campaign certification. |
| Saves | Saves and preferences live in `%LOCALAPPDATA%\KnackII-PC`; solo and co-op share the selected profile. |
| Graphics | Vulkan support and a current graphics driver are required. Visual defects, device loss, and crashes can still occur. |
| Campaign | Not fully validated. Treat every new area, cutscene, and hardware combination as experimental. |

<p align="center">
  <img src="docs/screenshots/knack-ii-combat.png" alt="Early Knack II combat in the experimental Windows port" width="76%" />
</p>

## Requirements

- Recent 64-bit Windows.
- Vulkan-capable GPU with an updated driver.
- Your own Knack II dump: **CUSA08014 / 01.01**.
- Two controllers for local co-op.

No Python, Qt, or MSYS2 installation is needed for the released Windows ZIP.

## Saves, settings, and logs

Saves, preferences, caches, and launcher logs are stored under `%LOCALAPPDATA%\KnackII-PC`.

Keep the game closed before changing or copying save files. The launcher keeps solo and co-op on the shared profile selected by the project configuration. See the [Windows launcher guide](docs/LAUNCHER_WINDOWS.md) for controls, language, profiles, and logs.

If a problem occurs, use **View logs** in the launcher and open an [issue](https://github.com/Hyant4/Knack-II-pc-port/issues) with your GPU, driver, selected game stage, and a short reproduction path. Do not attach game files, saves, or private paths.

## Build from source

The release source snapshot is attached to each release. For V0.14, download [**Knack-II-sources-V0.14.zip**](https://github.com/Hyant4/Knack-II-pc-port/releases/download/knack2-Version-0.14/Knack-II-sources-V0.14.zip); it matches the distributed binaries more closely than GitHub's automatic source archives.

Development requires Windows, MSYS2 CLANG64, Python tooling, a Vulkan stack, and your own supported dump. Start with the [Windows development guide](docs/DESENVOLVIMENTO_WINDOWS.md). The build process never distributes game files.

## How it works

| Layer | Role |
| --- | --- |
| Guest runtime | Runs the original x86-64 game code and reimplements required PS4-facing services. |
| Graphics | Translates the game's Gnm/GCN graphics work through a Vulkan backend derived from shadPS4 work. |
| Audio | Provides AudioOut, codec paths, source mixing, and Windows device delivery. |
| Launcher | Validates the dump, keeps local settings, manages players, and starts the supervised session. |

## Credits and legal notice

This project builds on [HayatoG/iss_pc](https://github.com/HayatoG/iss_pc), shadPS4-derived graphics work, and other open-source projects. See [credits and third-party licenses](docs/CREDITS.md).

**Independent, unofficial community project.** It is not affiliated with or endorsed by Sony Interactive Entertainment. Knack II belongs to its respective rights holders. No game assets, firmware, or game dump are included.

For the exact V0.14 download hashes, use [SHA256SUMS.txt](https://github.com/Hyant4/Knack-II-pc-port/releases/download/knack2-Version-0.14/SHA256SUMS.txt).

