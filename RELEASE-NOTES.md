# Knack II — 0.17.7-rc1 — issue #12

The tested executable UI remains identified as `0.17.7-candidate`; RC1 distributes those same checked binaries.

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

## Português

Pré-release experimental 0.17.7-rc1. A correção das superfícies pretas não está confirmada.

Inclui seleção/persistência corrigida do preset GTX e suporte por hash exato à variante relatada do executável US 01.01. Região, identidade do título e saves permanecem americanos. Hash desconhecido continua recusado. Launcher, supervisor e runtime foram recompilados juntos. Não inclui protótipo AA nem suporte USA 01.00 de outros trabalhos locais.

Extraia a pasta inteira em local novo e abra **Test GTX 1060.cmd**. O perfil de teste fica em `%LOCALAPPDATA%\KnackII-PC-Issue12-Test`, separado da configuração/progresso habituais. O atalho não inicia o jogo automaticamente. Selecione o dump atualizado intacto, valide, aplique o preset, salve e faça a mesma comparação inicial com FSR/RCAS desligados. Não troque executáveis nem edite metadados.

Após o teste, envie captura, `01-game.elf.log` e `session.json`, removendo caminhos pessoais. Logs devem registrar perfil US, layout compartilhado, readbacks precisos e leitura antecipada desligada. Dados do jogo e saves não devem ser enviados.

As verificações locais foram sintéticas, estáticas e do pacote. Não houve gameplay ou teste na GTX 1060. O fixture não comprova todos os assets da instalação do relator.
