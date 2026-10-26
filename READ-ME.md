# Knack II PC — V0.17.6


Adds the manual **GTX 1060 black texture (experimental)** preset in
**Settings > Video > Compatibility presets**.

- Applies `gpu_readbacks=2` (Precise) and `eager_readback=0` (early readback off).
- Preserves resolution, FSR, controls and other settings. Save settings and restart the game to compare the same scene.
- No automatic GPU detection. To revert, select the previous readback values and save again.
- Preserves `Play Knack II.exe` startup, mandatory dump validation and the V0.17.5 shader-preloading preference.
- Launcher rebuilt with version 0.17.6 and PT/EN/RU interface. Native runtime and `Play Knack II.exe` are byte-identical to the V0.17.5 package.

**Limitation:** this is an experimental configuration for investigating issue #12. A fix for black surfaces on the GTX 1060 has not been confirmed. This release does not add a shader/driver patch or expand the supported dump profiles.

Download `Knack-II-Windows-x64-V0.17.6.zip`, extract the entire folder and open `Knack II.exe`.
No game, firmware or saves are included. V0.17.5 remains available for rollback.
Matching sources and SHA-256 checksums are included. Validation for this publication is synthetic and package-focused; no real game was started.
