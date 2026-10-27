# Knack II — 0.17.7-rc2


Experimental prerelease for investigating black surfaces on the GTX 1060. **A rendering fix has not been confirmed.**

The **GTX 1060 black texture** preset now offers optional diagnostics, off by default:

1. **Fresh cache per session:** starts with a separate empty graphics folder, preserving your normal cache.
2. **Fresh cache + synchronous compilation:** also waits for graphics and compute shaders to compile. This may cause pauses.

Extract the entire ZIP into a new folder and open **Test GTX 1060.cmd**, which uses a separate test profile. Validate your intact original updated game. Under Settings → Video, apply the GTX preset, choose the diagnostic mode and save. Compare the same scene with diagnostics off, then option 1, then option 2. Keep all other settings unchanged. Selecting another preset disables diagnostics.

The mode and cache location are recorded in the logs and `session.json`. Share that file, `01-game.elf.log`, and a screenshot of the scene, removing personal paths. There is no need to swap ELF files, edit SFO metadata or resend hashes.

Launcher and supervisor were rebuilt together. The native runtime is byte-identical to RC1 and retains its internal `issue12-candidate-0.17.7` label. Strict executable-variant validation remains enabled. Other presets keep their existing behavior. Unrelated local work on AA, the PKG installer and additional translations is excluded.

Validation: 79 tests passed, one skipped because an optional native harness was unavailable, and two existing GPU-diagnostic contracts were outside this gate. The frozen package was checked in all three modes across two LoadExec stages using a synthetic executable that never loads the game; previous settings and associated icons were checked. No real GTX 1060 gameplay was tested. Stable/latest **0.17.5** remains unchanged.
