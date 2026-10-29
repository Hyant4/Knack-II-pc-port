<div align="center">

# Knack II — Native PC Port

**An independent, experimental native port of Knack II for Windows and Linux (x86-64).**

[![Release](https://img.shields.io/badge/release-v0.18-0969da?style=flat-square)](https://github.com/Hyant4/Knack-II-pc-port/releases/latest)
![Windows](https://img.shields.io/badge/Windows-x64-555?style=flat-square)
![Linux](https://img.shields.io/badge/Linux-AppImage%20experimental-555?style=flat-square)
![Vulkan](https://img.shields.io/badge/Vulkan-1.3%2B-555?style=flat-square)

**[Download the latest release](https://github.com/Hyant4/Knack-II-pc-port/releases/latest)** · **[What's new in v0.18](RELEASE-NOTES.md)** · **[Report an issue](https://github.com/Hyant4/Knack-II-pc-port/issues)**

**English** · [Português (Brasil)](README.pt-BR.md)

</div>

<p align="center">
  <img src="docs/screenshots/knack-ii-title-screen.png" alt="Knack II title screen running on the PC port" width="48%" />
  <img src="docs/screenshots/knack-ii-combat.png" alt="Knack II gameplay on the PC port" width="48%" />
</p>

> [!IMPORTANT]
> This is an **unofficial, experimental community project**, not a game download. You must provide your own complete, legally obtained Knack II dump. No game, firmware or save files are included. Hardware and campaign-wide compatibility have **not** been fully certified.

## What's new in v0.18

- **Linux support:** native launcher and runtime, plus an experimental x86-64 AppImage.
- **Windows installation tools:** extract a base-game `.pkg` and install an update `.pkg` from the launcher's **Installation** tab, with validation, update backup and restore.
- **US base version 01.00:** runs through the normal launcher workflow without requiring the 01.01 update.
- **More GPU compatibility work:** interpolation fallback for GPUs without native barycentric support; additional graphics diagnostics; GTX 1060 performance and experimental RX 580 presets.
- **More graphics control:** toggle native antialiasing independently of FSR and internal rendering resolution.
- **Better multilingual errors:** more launcher errors and messages now follow the selected interface language (Portuguese, English or Russian). Some messages may still be untranslated.
- **Optional audio path:** firmware-backed NGS2 via `--firmware-ngs2` with a module supplied by the user; HLE remains the default.

Existing features include independent window/internal resolutions, native QHD/4K rendering, FSR, experimental ultrawide, local two-player co-op, controller guide, shader preloading and validated direct launch.

See the [complete v0.18 release notes](RELEASE-NOTES.md) for details and limitations.

## Download & set up

### Windows (x64)

1. Download `Knack-II-Windows-x64-V0.18.zip` from [Releases](https://github.com/Hyant4/Knack-II-pc-port/releases/latest) and extract the **entire ZIP into a new folder**.
2. Launch **`Knack II.exe`** (do not run it from inside the ZIP). Python, Qt and the required runtime are bundled; you do not need to install Python or MSYS2.
3. Select the folder of your extracted game containing `eboot.bin`, `game.elf`, `sce_sys` and `sce_module`.
4. Click **Validate**, configure your preferences and click **Play**.

**Need to extract your own PKG?** Use **Installation → Extract base game (.pkg)**. The files are extracted **beside the selected PKG**, without overwriting an existing installation. **Installation → Install update (.pkg)** includes version/region checks and update backup/restore. See the [PKG extraction guide](docs/EXTRACAO_PKG_BASE.md) (Portuguese).

After the first setup, **`Play Knack II.exe`** can launch using saved preferences while retaining installation validation.

### Linux (experimental, x86-64)

Download `Knack-II-x86_64-0.18.0.AppImage` from [Releases](https://github.com/Hyant4/Knack-II-pc-port/releases/latest), then run:

```bash
chmod +x Knack-II-x86_64-0.18.0.AppImage
./Knack-II-x86_64-0.18.0.AppImage
```

Without FUSE, use `./Knack-II-x86_64-0.18.0.AppImage --appimage-extract-and-run`.

**Linux requirements:** Vulkan 1.3+ and **glibc 2.43+**. This AppImage is built on Ubuntu 26.04; it does **not** bundle glibc or graphics drivers and does not support older-glibc systems. Linux Mint compatibility has not been certified. The Linux package opens the launcher but **does not include the Windows PKG extractor**: select your own already-extracted game dump.

See the [Linux development guide](docs/DESENVOLVIMENTO_LINUX.md) and [AppImage notes](docs/APPIMAGE_LINUX.md).

## Supported game versions

| Region | Title ID | Game version | Status |
| --- | --- | --- | --- |
| US | `CUSA08014` | `01.00` | Supported; update not required |
| US | `CUSA08014` | `01.01` | Supported |
| Europe | `CUSA07670` | `01.01` | Supported |

**Europe 01.00 and unverified versions are not supported.** The launcher validates the region, version, executables and required modules. Do not mix updates from different regions. All builds require a GPU driver supporting **Vulkan 1.3+**.

For details, see [regional compatibility and validation evidence](docs/COMPATIBILIDADE_REGIONAL.md).

## Settings, saves & troubleshooting

- The launcher lets you configure rendering resolution, FSR, antialiasing, experimental ultrawide and local co-op. The GTX 1060 preset targets performance; the RX 580 compatibility preset is **experimental**, with optional barrier merging **disabled by default**. It is **not** a confirmed universal fix for `Device lost`.
- Choose your preferred launcher language in the interface. Additional error messages and installation prompts are localized, helping English-speaking users avoid Portuguese-only errors.
- Saves and preferences live **outside** the extracted package:
  - **Windows:** `%LOCALAPPDATA%\KnackII-PC`
  - **Linux:** `$XDG_DATA_HOME/knack-ii-pc` or `~/.local/share/knack-ii-pc`
- To update the port, extract the new release to **another folder**, keep your user data and validate the game installation again. **You do not need to delete caches.**
- If something fails, open **Logs & Diagnostics**, then [submit an issue](https://github.com/Hyant4/Knack-II-pc-port/issues) with your GPU, OS, game region/version, steps to reproduce and relevant sanitized logs. Remove personal paths before sharing; do not upload game files, firmware or memory dumps.

**Compatibility note:** automated tests and successful launcher startup are not proof of full-campaign support on every GPU. Linux and hardware-specific fixes remain experimental.

## Credits & development

This port builds on [HayatoG/iss_pc](https://github.com/HayatoG/iss_pc) and includes a graphics backend derived from [shadPS4](https://github.com/shadps4-emu/shadPS4). Thanks to **SkippySteve** for the Linux contributions in [PR #19](https://github.com/Hyant4/Knack-II-pc-port/pull/19), with original commits preserved.

[Full credits, provenance & licenses](docs/CREDITS.md) · [Build on Windows](docs/DESENVOLVIMENTO_WINDOWS.md) · [Build on Linux](docs/DESENVOLVIMENTO_LINUX.md)

This project is not affiliated with Sony. Knack II and its assets belong to their respective owners.

If you would like to support continued development: [Buy Me a Coffee](https://buymeacoffee.com/hyant4).
