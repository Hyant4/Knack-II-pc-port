# Knack II PC — 0.18.1 — Pré-release experimental (RX 580)

## Português

A versão **0.18.1** é uma pré-release focada no teste de compatibilidade gráfica para a **Radeon RX 580** (issue #20), mantendo a **base atual da V0.18 consolidada e intacta**.

### O que há de novo na 0.18.1

- **Guard de formatos estáticos de vértices (exclusivo do preset RX 580):**
  - Implementado no atalho de preparação de pipelines (`TryPreparedPipeline`).
  - Quando o preset **RX 580: compatibilidade experimental** está ativo e o driver não expõe entrada de vértice dinâmica, os formatos Vulkan atuais dos descritores V# são verificados contra a chave preparada.
  - Se houver divergência de formato, a chave é recalculada pelo caminho normal, evitando polígonos/faces deformadas ou pretas no cenário do terraço.
  - Mudanças isoladas de endereço/stride continuam aceitas sem rejeição desnecessária.
- **Ativação estritamente opt-in pelo preset:**
  - O guard, a junção de barreiras e o diagnóstico são condicionados estritamente por `rx580_preset=1` e `TLG_RX580_PRESET=1`.
  - **Não há ativação automática por detecção de GPU AMD.**
  - Trocar para qualquer outro preset remove completamente as flags e o comportamento RX na sessão seguinte.
  - Preferências antigas só migram se houver seleção explícita anterior do preset RX.
- **Caráter experimental:**
  - O carregamento do jogo passou a funcionar na sessão enviada pelo relator, mas permanecem polígonos/faces incorretos.
  - **Este novo guard ainda não possui confirmação visual no hardware do relator (RX 580).**
  - **É obrigatório clicar em "Salvar configurações" e reiniciar o launcher** para que os novos ajustes entrem em vigor no processo.
  - **Nenhum firmware de console é necessário para este teste.**

### Pacotes

- **Windows:** `Knack-II-Windows-x64-V0.18.1.zip`
  - Extraia tudo em uma pasta nova.
  - Para teste isolado sem migrar saves ou preferências habituais, utilize **`Test RX 580.cmd`**.
- **Linux (AppImage experimental):** `Knack-II-x86_64-0.18.1.AppImage`
  - Compilado na base Ubuntu 26.04; requer **glibc 2.43+** e **Vulkan 1.3+** no sistema host. O AppImage não reduz esse requisito de glibc; distribuições com glibc anterior não são compatíveis. Não certificado no Linux Mint.
  - Sem FUSE, execute com `./Knack-II-x86_64-0.18.1.AppImage --appimage-extract-and-run`.
  - Abrir o launcher não inicia o jogo automaticamente. Selecione um dump próprio já extraído.
- **Fontes:** `Knack-II-sources-V0.18.1.zip`, incluindo todas as dependências, licenças e bundle FSR.

Dumps aceitos: **CUSA08014 01.00/01.01** e **CUSA07670 01.01**.
Nenhum jogo, dump proprietário, firmware ou save acompanha os pacotes.

---

## English

Release **0.18.1** is an experimental pre-release dedicated to graphics compatibility testing on the **Radeon RX 580** (issue #20), while preserving the entire **consolidated V0.18 feature base intact**.

### What's new in 0.18.1

- **Static vertex format guard (exclusive to the RX 580 preset):**
  - Implemented in the pipeline preparation fast-path (`TryPreparedPipeline`).
  - When the **RX 580: experimental compatibility** preset is selected and the driver does not expose dynamic vertex input, current Vulkan formats from V# descriptors are verified against the prepared pipeline key.
  - If a format mismatch is detected, the key is recalculated through the normal path, preventing black or corrupt geometry in scenes such as the terrace.
  - Address or stride changes alone without format differences do not trigger unnecessary rejection.
- **Strictly opt-in preset activation:**
  - The guard, barrier merging, and barrier diagnostics are conditioned strictly on `rx580_preset=1` and `TLG_RX580_PRESET=1`.
  - **There is no automatic activation based on AMD GPU detection.**
  - Switching to any other preset completely clears RX flags and behaviors for the subsequent session.
  - Older preferences only migrate if an explicit RX preset was previously saved.
- **Experimental status & testing instructions:**
  - Game loading succeeded in the reporter's logs, but corrupt faces/polygons remained visible.
  - **This new format guard has not yet been visually confirmed on the reporter's RX 580 hardware.**
  - **You must click "Save settings" and restart the launcher/game** for the C++ runtime flags to take effect in a new process.
  - **No console firmware is required for this test.**

### Packages

- **Windows:** `Knack-II-Windows-x64-V0.18.1.zip`
  - Extract the entire archive into a clean folder.
  - To test without touching your normal preferences or saves, run **`Test RX 580.cmd`**.
- **Linux (Experimental AppImage):** `Knack-II-x86_64-0.18.1.AppImage`
  - Built on Ubuntu 26.04; requires host **glibc 2.43+** and **Vulkan 1.3+**. The AppImage does not lower the glibc requirement; older distributions are not supported. Compatibility with Linux Mint has not been certified.
  - If FUSE is unavailable, run with `./Knack-II-x86_64-0.18.1.AppImage --appimage-extract-and-run`.
  - Opening the AppImage opens the launcher and never launches the game automatically. Select an already-extracted dump.
- **Sources:** `Knack-II-sources-V0.18.1.zip`, including dependencies, licenses, and the FSR bundle.

Supported dumps: **CUSA08014 01.00/01.01** and **CUSA07670 01.01**.
No game files, firmware, saves, or proprietary assets are included.
