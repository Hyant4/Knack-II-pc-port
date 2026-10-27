# Knack II — 0.17.7-rc1 — issue #12

## English

This is the experimental 0.17.7-rc1 prerelease. A fix for black surfaces is not confirmed.

It includes the corrected GTX preset selection/persistence and exact-hash support for the reported US 01.01 executable variant. The game remains CUSA08014; code layout selection does not convert the game region or saves. Unknown hashes are still rejected. Launcher, supervisor and native runtime were rebuilt together. The unrelated native-AA prototype and US 01.00 support are excluded.

### Retest

1. Extract the entire package into a new folder. Keep all files together; do not mix it with an older package.
2. Open **Test GTX 1060.cmd**. It opens the launcher using a separate profile at `%LOCALAPPDATA%\KnackII-PC-Issue12-Test`, leaving the normal profile and progress separate. It does not start gameplay automatically.
3. Select your intact updated US 01.01 game folder, using its original updated `game.elf`. Do not edit `param.sfo` or swap executables. Click **Validate**.
4. Open **Settings > Video** and apply **GTX 1060 black texture**. Its card and current-preset label should now show it as selected. Click **Save settings**.
5. Leave FSR and RCAS off for the first comparison. Start the game and revisit the same opening scene. Initial shader compilation can take time.
6. Report whether validation passed and whether black surfaces remain. Share a screenshot, `01-game.elf.log` and `session.json` from the test profile. The session should record `CUSA08014-01.01`, `elf-0b7843d3`, `gpu_readbacks=2` and `eager_readback=false`. Remove personal paths from anything shared.

If validation still fails, share the exact error. No executables, assets or saves are needed. The ordinary `Knack II.exe` and `Play Knack II.exe` use the normal profile; use the test script for this comparison.

Validation before packaging used synthetic tests, a static fixture with known executable bytes and the reporter's US SFO, and package diagnostics. No real gameplay or GTX 1060 session was run. The static fixture does not certify every asset in the reporter's updated installation.
