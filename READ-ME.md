# Knack II PC — V0.17.3


Runtime update fixing reuse of the shader and pipeline cache. The launcher keeps its PT / EN / RU languages and displays the updated version.

- **Experimental shader preloading:** enabled by default. In **Settings → Video**, disable the toggle and save settings to return to on-demand loading. Your preference applies next session.
- Compatible variants at different indices no longer cause false cache rejections; actual conflicts remain rejected.
- On-demand mode now reads previously saved shaders when the cache is compatible.
- Fixed a temporary reference in loaded shader metadata.

**Limitations:** preloading may increase the wait before entering the game. Stutter improvements are partial: a cold cache still needs compilation; pauses and frame-rate variation may remain. This update does not guarantee stable 60 FPS.

**Validation:** native build, 22 passing synthetic cache checks, a local replay of 1,219 records, tests of the toggle/persistence/startup modes, and short sessions with the European 01.01 edition, including skipping the opening and movement. The full campaign, other GPUs, and co-op were not revalidated in this update.

**Installation:** download `Knack-II-Windows-x64-V0.17.3.zip`, extract the entire folder, and open `Knack II.exe`. Use your own CUSA08014 or CUSA07670 version 01.01 dump. Saves and preferences remain in `%LOCALAPPDATA%\KnackII-PC`. You do not need to clear your existing cache. V0.17.2 remains available as a rollback.

Matching sources: `Knack-II-sources-V0.17.3.zip`. Check `SHA256SUMS.txt`. No game, dump, or saves are included.
