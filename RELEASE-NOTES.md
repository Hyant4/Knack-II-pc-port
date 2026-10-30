# Knack II PC — 0.18.2 — Persistência de jogadores e base 0.18.1

## Português

A versão **0.18.2** corrige a persistência da contagem de jogadores no launcher (issue #21) e preserva integralmente todos os recursos consolidados da V0.18 e o preset RX 580 da 0.18.1.

### O que há de novo na 0.18.2

- **Persistência do número de jogadores no launcher (issue #21):**
  - Ao selecionar 1 ou 2 jogadores e clicar em **Salvar configurações**, a preferência é gravada no arquivo `launcher_settings.json`.
  - Reaberturas posteriores do launcher restauram fielmente a quantidade de jogadores selecionada (1 ou 2), inclusive ao iniciar pelo atalho de partida direta com validação de dump.
  - Arquivos de configuração antigos (sem a chave `players`) ou entradas com valores inválidos mantêm com segurança o padrão de 1 jogador.
  - Preserva integralmente o botão **Salvar configurações** (sem salvamento automático prematuro), os IDs guest (P1=1, P2=4099), o perfil de saves compartilhado e as pastas isoladas `coop-UUID` de execução cooperativa.
- **Preservação integral da base da 0.18.1 e preset RX 580:**
  - Mantém o guard estático de formatos de vértices (`TryPreparedPipeline`), a junção de barreiras e o diagnóstico dedicados à **Radeon RX 580**.
  - O relator da issue #20 confirmou a correção do problema gráfico visual no cenário do terraço na 0.18.1 ([comentário #6515510049](https://github.com/Hyant4/Knack-II-pc-port/issues/20#issuecomment-6515510049)). Essa confirmação não certifica a campanha completa nem elimina stutters relatados decorrentes da compilação de shaders/pipelines.
  - Ativação estritamente opt-in: todos os workarounds RX continuam condicionados à seleção explícita do preset (`rx580_preset=1`/`TLG_RX580_PRESET=1`). A troca de preset remove completamente as flags e o comportamento RX na sessão seguinte. Não há ativação automática para GPUs AMD genéricas.

### Pacotes

- **Windows:** `Knack-II-Windows-x64-V0.18.2.zip`
  - Extraia tudo em uma pasta nova.
  - Inclui launcher, runtime, supervisor, executável de jogo direto (`Play Knack II.exe`), dependências completas e `Test RX 580.cmd`.
- **Linux (AppImage):** `Knack-II-x86_64-0.18.2.AppImage`
  - Compilado na base Ubuntu 26.04; requer **glibc 2.43+** e **Vulkan 1.3+** no sistema host. O AppImage não reduz esse requisito de glibc; distribuições com glibc anterior não são compatíveis. Compatibilidade com distribuições baseadas em versões anteriores não é certificada.
  - Sem FUSE, execute com `./Knack-II-x86_64-0.18.2.AppImage --appimage-extract-and-run`.
  - Abrir o launcher não inicia o jogo automaticamente. Selecione um dump próprio já extraído.
- **Fontes:** `Knack-II-sources-V0.18.2.zip`, incluindo todas as dependências, licenças e bundle FSR.

Dumps aceitos: **CUSA08014 01.00/01.01** e **CUSA07670 01.01**.
Nenhum jogo, dump proprietário, firmware ou save acompanha os pacotes.

---

## English

Release **0.18.2** fixes player count persistence in the launcher (issue #21) and preserves the entire consolidated V0.18 feature base along with the RX 580 preset from 0.18.1.

### What's new in 0.18.2

- **Launcher player count persistence (issue #21):**
  - When choosing 1 or 2 players and clicking **Save settings**, the selection is stored in `launcher_settings.json`.
  - Subsequent launcher launches restore the selected player count (1 or 2), including when launching via the direct play shortcut with dump validation.
  - Older configuration files (lacking the `players` key) or invalid values safely default to 1 player.
  - Preserves the **Save settings** button requirement (no premature auto-save), guest user IDs (P1=1, P2=4099), shared save data profile, and isolated `coop-UUID` cooperative execution directories.
- **Full preservation of 0.18.1 base & RX 580 preset:**
  - Preserves the static vertex format guard (`TryPreparedPipeline`), barrier merging, and diagnostics dedicated to the **Radeon RX 580**.
  - The reporter of issue #20 confirmed that the terrace graphics corruption was resolved in 0.18.1 ([comment #6515510049](https://github.com/Hyant4/Knack-II-pc-port/issues/20#issuecomment-6515510049)). Note that this confirmation does not certify a full campaign run or eliminate reported stutters caused by shader/pipeline compilation.
  - Strictly opt-in activation: all RX workarounds remain conditioned on explicit preset selection (`rx580_preset=1`/`TLG_RX580_PRESET=1`). Switching presets clears all RX flags and behaviors for subsequent sessions. No automatic activation for generic AMD GPUs.

### Packages

- **Windows:** `Knack-II-Windows-x64-V0.18.2.zip`
  - Extract the entire archive into a clean folder.
  - Includes launcher, runtime, supervisor, direct play wrapper (`Play Knack II.exe`), all dependent libraries, and `Test RX 580.cmd`.
- **Linux (AppImage):** `Knack-II-x86_64-0.18.2.AppImage`
  - Built on Ubuntu 26.04; requires host **glibc 2.43+** and **Vulkan 1.3+**. The AppImage does not lower the glibc requirement; older distributions are not supported. Compatibility with distributions based on older versions is not certified.
  - If FUSE is unavailable, run with `./Knack-II-x86_64-0.18.2.AppImage --appimage-extract-and-run`.
  - Opening the AppImage opens the launcher and never launches the game automatically. Select an already-extracted dump.
- **Sources:** `Knack-II-sources-V0.18.2.zip`, including dependencies, licenses, and the FSR bundle.

Supported dumps: **CUSA08014 01.00/01.01** and **CUSA07670 01.01**.
No game files, firmware, saves, or proprietary assets are included.
