# Knack II PC — V0.18

## Português

A V0.18 reúne as novidades desde a última estável, **0.17.5**, incluindo
recursos que apareceram nas pré-releases 0.17.7.

### Novidades

- **Linux nativo e AppImage experimental**, com launcher, supervisor, runtime,
  Python, Qt e bibliotecas nativas.
- **Base americana 01.00** no fluxo normal do launcher, sem exigir update.
  A validação integral da instalação continua obrigatória.
- **PKGs no Windows:** extração do jogo-base e instalação de update pela aba
  Instalação, com verificações, backup e restauração do update.
- **Antialiasing independente do FSR:** escolha entre nativo e desligado,
  mantendo os controles de resolução interna e upscaling.
- **Compatibilidade gráfica:** caminho de interpolação para GPUs sem suporte
  baricêntrico nativo, trazido das RCs, e novos diagnósticos para superfícies
  escuras e falhas de carregamento.
- **Predefinições GTX 1060 e RX 580:** perfil GTX 1060 de desempenho e RX 580 de
  compatibilidade experimental. Junção de barreiras é opcional e desligada por
  padrão; não é uma correção confirmada para toda RX 580.
- **Mensagens e identidade do launcher:** mais erros acompanham o idioma da
  interface; versão exibida e ícones dos executáveis identificam o Knack.
- **Áudio avançado opcional:** NGS2 com módulo de firmware fornecido pelo usuário,
  somente com `--firmware-ngs2`. HLE continua padrão; firmware não é incluído.

QHD/4K internos, ultrawide experimental, cooperativo local, guia visual de
controles e início direto com validação continuam disponíveis; já existiam
na estável anterior.

### Pacotes e atualização

- **Windows:** `Knack-II-Windows-x64-V0.18.zip`. Extraia tudo em uma pasta nova
  e abra `Knack II.exe`. Inclui o extrator PKG Windows.
- **Linux:** `Knack-II-x86_64-0.18.0.AppImage`. Dê permissão de execução e abra.
  Sem FUSE, use `--appimage-extract-and-run`. Use um dump já extraído.
- **Fontes:** `Knack-II-sources-V0.18.zip`, com dependências e licenças.

Os dados de usuário ficam fora do pacote. Preserve saves e preferências;
não é necessário apagar caches. Valide novamente a instalação do jogo.

### Requisitos e limites

**Vulkan 1.3+**. Dumps aceitos: **CUSA08014 01.00/01.01** e **CUSA07670 01.01**.

Linux requer **glibc 2.43+**; o binário usa base Ubuntu 26.04. AppImage não reduz
esse requisito. Drivers gráficos e glibc são externos; distribuições com glibc
anterior não são atendidas por este pacote. Mint não foi certificado.

Projeto, Linux e perfis de diagnóstico continuam experimentais. Não há promessa
de FPS, campanha completa ou correção universal de `Device lost`. A correção
RX 580 ainda depende de confirmação no hardware afetado. Testes automatizados
e abertura do launcher não certificam gameplay completo.

Nenhum jogo, firmware ou save acompanha os pacotes.

### Créditos

Obrigado a **SkippySteve** pelas contribuições Linux do
[PR #19](https://github.com/Hyant4/Knack-II-pc-port/pull/19), preservadas com seus
commits originais, e aos projetos HayatoG/iss_pc e shadPS4.

## English

V0.18 consolidates changes since stable **0.17.5**, including features from
the 0.17.7 prereleases.

- Native Linux launcher/runtime and an experimental AppImage with Python,
  Qt and native libraries included.
- US CUSA08014 01.00 base-game support without requiring an update.
- Windows base-PKG extraction and update installation, with checks and
  update backup/restore.
- Native antialiasing or Off, independent of FSR controls.
- The prereleases' interpolation path for GPUs without native barycentric
  support, plus additional graphics diagnostics.
- GTX 1060 performance and experimental RX 580 presets. Barrier merging is
  optional and off by default; a fix on every affected RX 580 is unconfirmed.
- More failures follow the UI language; corrected executable icons and version.
- Optional user-supplied firmware NGS2 through `--firmware-ngs2`; HLE remains
  the default and no firmware is included.

Native QHD/4K, experimental ultrawide, local co-op, the controller guide and
validated direct Play were already in the previous stable release and remain available.

Extract the Windows ZIP into a new folder and open `Knack II.exe`. On Linux,
make the AppImage executable; use `--appimage-extract-and-run` without FUSE and
select an extracted dump. Preserve existing saves/preferences and validate again.

Requires **Vulkan 1.3+** and **CUSA08014 01.00/01.01** or **CUSA07670 01.01**.
The Linux binary uses Ubuntu 26.04 and requires **glibc 2.43+**. AppImage does
not lower this requirement; graphics drivers and glibc stay external. Older-glibc
distributions are not supported by this binary; Mint has not been certified.

The project remains experimental. No FPS, full-campaign or universal
`Device lost` fix is promised. Automated checks do not certify complete gameplay.
No game, firmware or saves are included. Sources, dependencies and licenses are supplied.

Thanks to **SkippySteve** for [PR #19](https://github.com/Hyant4/Knack-II-pc-port/pull/19),
whose original commits were preserved, and to HayatoG/iss_pc and shadPS4.
