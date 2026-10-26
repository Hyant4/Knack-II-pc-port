# Knack II PC — Hotfix V0.17.6

## Português

Adiciona a predefinição manual **GTX 1060 black texture (experimental)** em
**Configurações > Vídeo > Predefinições de compatibilidade**.

- Aplica `gpu_readbacks=2` (Preciso) e `eager_readback=0` (leitura antecipada desligada).
- Preserva resolução, FSR, controles e os demais ajustes. Salve as configurações e reinicie o jogo para comparar a mesma cena.
- Não detecta a GPU automaticamente. Para reverter, escolha os valores anteriores nas opções de leitura e salve novamente.
- Mantém o início por `Play Knack II.exe`, a validação obrigatória do dump e a preferência de pré-carregamento da V0.17.5.
- Launcher recompilado com identificação 0.17.6 e interface PT/EN/RU. Runtime nativo e `Play Knack II.exe` preservados byte a byte do pacote V0.17.5.

**Limite:** é uma configuração experimental para investigar o relato da issue #12. A correção das superfícies pretas na GTX 1060 ainda não foi confirmada. Não inclui patch de shader/driver nem ampliação dos perfis de dump suportados.

Baixe `Knack-II-Windows-x64-V0.17.6.zip`, extraia a pasta inteira e abra `Knack II.exe`.
O pacote não inclui jogo, firmware ou saves. A V0.17.5 permanece disponível para retorno.
Fontes correspondentes e SHA-256 acompanham a release. Validações desta publicação são sintéticas e de pacote; nenhum jogo real foi iniciado.

## English

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
