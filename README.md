# Knack II PC Port — Unofficial Windows Alpha

**Knack II (also known as Knack 2) on Windows — an independent, experimental PC port with Vulkan graphics, a Windows launcher, and local two-player co-op.**

[**Download the Windows x64 alpha (v0.10)**](https://github.com/Hyant4/Knack-II-pc-port/releases/tag/knack2-alpha-Version-0.10) · [Português (Brasil)](README.pt-BR.md) · [Screenshots](#screenshots) · [Credits and licenses](docs/CREDITS.md)

> **Alpha status:** This is an unofficial community project, not a finished or fully validated PC release. The project owner reports successfully reaching **Chapter 2 in local co-op**, without problems observed in that session. The rest of the campaign has **not** been verified. Graphical bugs, Vulkan device-loss errors, and crashes may occur.

## What is this project?

Knack II PC Port is an independent effort to run the PlayStation 4 game **Knack II** natively on Windows. The downloadable Windows package contains the port, launcher, supervisor, runtime, and required bundled dependencies. **It does not contain the game.** You must supply your own game dump.

**Current public release:** [Knack II PC — Alpha Windows x64 v0.10](https://github.com/Hyant4/Knack-II-pc-port/releases/tag/knack2-alpha-Version-0.10). The release page includes the Windows ZIP, the matching source snapshot, checksums, and detailed test limitations.

## Requirements

- Recent 64-bit Windows installation.
- A graphics card and updated driver with Vulkan support.
- Your **own Knack II PS4 game dump**, title ID **CUSA08014**, version **01.01**.
- Two controllers if you want to try local co-op.

The Windows ZIP already includes the runtime dependencies needed to launch the packaged port; installing Python, Qt, or MSYS2 separately is not required.

## Download and setup

1. Open the [official GitHub release](https://github.com/Hyant4/Knack-II-pc-port/releases/tag/knack2-alpha-Version-0.10) and download the **Windows x64 ZIP**, not the source ZIP.
2. Extract the **entire** archive to a folder. Do not run it from inside the ZIP.
3. Launch `Knack II.exe`.
4. Select the folder containing your own game files (`eboot.bin`, `game.elf`, `sce_sys`, and `sce_module`) and click **Validate / Validar**.
5. Choose language, graphics options, and the number of players in **Settings / Configurações**, then select **Play / Jogar**.

Available in-game languages depend on the files present in your dump. Settings and saves are stored under `%LOCALAPPDATA%\KnackII-PC`.

## Compatibility and known limitations

| Area | Current public evidence |
| --- | --- |
| Boot, menus, movement, early combat | Tested; the owner also reports progressing to Chapter 2 in a cooperative session |
| Local co-op | **Playable in the owner's test through reaching Chapter 2**, with no issues reported in that session; select two players and connect two controllers |
| Audio | Dialogue, music, and sound effects were confirmed in an initial section after an audio pause/resume fix |
| Full campaign | **Not validated** |
| Stability and graphics | Experimental; graphical issues and Vulkan device-loss may occur |
| Later chapters and broader PC configurations | Not certified |

The original alpha v0.10 release notes record earlier limited testing; the Chapter 2 co-op milestone is a **subsequent owner-reported gameplay result**, not an independent full-campaign certification. See the [full release notes](https://github.com/Hyant4/Knack-II-pc-port/releases/tag/knack2-alpha-Version-0.10) for the exact scope of each test.

## Screenshots

<p align="center">
  <img src="docs/screenshots/knack-ii-title-screen.png" alt="Knack II title screen running in the experimental Windows PC port" width="49%" />
  <img src="docs/screenshots/knack-ii-combat.png" alt="Early combat in the experimental Knack II PC port" width="49%" />
</p>

## Troubleshooting and feedback

- **Game folder rejected?** Check that your dump is CUSA08014, version 01.01, and that the required files exist.
- **Launcher fails to start?** Extract the full ZIP and keep all included files together.
- **Other issue?** Open the launcher's logs and [report a reproducible issue](https://github.com/Hyant4/Knack-II-pc-port/issues) with your hardware, driver, and the point where it failed.

Additional documentation: [Windows launcher guide (Portuguese)](docs/LAUNCHER_WINDOWS.md) · [Development guide](docs/DESENVOLVIMENTO_WINDOWS.md) · [Publishing and local-data notes](docs/PUBLICACAO.md).

## Credits and legal notice

This project draws on [HayatoG/iss_pc](https://github.com/HayatoG/iss_pc), graphics work derived from [shadPS4](https://github.com/shadps4-emu/shadPS4), and other open-source projects. Consult the [full credits and third-party license notices](docs/CREDITS.md), also included with the release archives.

**Independent, unofficial fan project. Not affiliated with or endorsed by Sony Interactive Entertainment. Knack II belongs to its respective rights holders.** No game assets, firmware, or game dump are included.
