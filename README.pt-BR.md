<h1 align="center">Knack II — Port Nativo para PC</h1>
<p align="center"><strong>Port independente e experimental de Knack II para Windows e Linux (x86-64).</strong></p>
<p align="center">
  <a href="https://github.com/Hyant4/Knack-II-pc-port/releases/latest"><img src="https://img.shields.io/badge/vers%C3%A3o-v0.18-0969da?style=flat-square" alt="Versão v0.18" /></a>
  <img src="https://img.shields.io/badge/Windows-x64-555?style=flat-square" alt="Windows x64" />
  <img src="https://img.shields.io/badge/Linux-AppImage%20experimental-555?style=flat-square" alt="Linux experimental" />
  <img src="https://img.shields.io/badge/Vulkan-1.3%2B-555?style=flat-square" alt="Vulkan 1.3+" />
</p>
<p align="center"><strong><a href="https://github.com/Hyant4/Knack-II-pc-port/releases/latest">Baixar a última versão</a></strong> · <a href="RELEASE-NOTES.md">Novidades da v0.18</a> · <a href="https://github.com/Hyant4/Knack-II-pc-port/issues">Reportar um problema</a></p>
<p align="center"><a href="README.md">English</a> · <strong>Português (Brasil)</strong></p>

<p align="center">
  <img src="docs/screenshots/knack-ii-title-screen.png" alt="Tela inicial do Knack II no port para PC" width="48%" />
  <img src="docs/screenshots/knack-ii-combat.png" alt="Gameplay do Knack II no port para PC" width="48%" />
</p>

> [!IMPORTANT]
> Este é um projeto comunitário **não oficial e experimental**, não um download do jogo. Você precisa fornecer seu próprio dump completo e obtido legalmente de Knack II. Os pacotes **não incluem jogo, firmware ou saves**. A compatibilidade com toda a campanha e todos os hardwares **não foi certificada**.

## Novidades da v0.18

- **Suporte a Linux:** launcher e runtime nativos, com AppImage experimental para x86-64.
- **Instalação de PKGs no Windows:** extração do jogo-base e instalação de atualizações na aba **Instalação**, com validação, backup e restauração do update.
- **Versão americana 01.00:** funciona pelo launcher normal sem exigir a atualização 01.01.
- **Melhorias de compatibilidade gráfica:** caminho alternativo de interpolação para GPUs sem suporte baricêntrico nativo; novos diagnósticos; predefinição de desempenho GTX 1060 e perfil experimental RX 580.
- **Mais opções gráficas:** antialiasing nativo independente de FSR e da resolução interna.
- **Erros em vários idiomas:** mais mensagens e erros do launcher seguem o idioma selecionado na interface (português, inglês ou russo). Algumas mensagens ainda podem não estar traduzidas.
- **Áudio opcional:** NGS2 com módulo de firmware fornecido pelo usuário via `--firmware-ngs2`; HLE continua sendo o padrão.

Os recursos anteriores continuam: resolução de janela independente da resolução interna, QHD/4K nativos, FSR, ultrawide experimental, cooperativo local para dois jogadores, guia de controles, pré-carregamento de shaders e início direto com validação.

Consulte as [notas completas da v0.18](RELEASE-NOTES.md) para detalhes e limitações.

## Download e instalação

### Windows (x64)

1. Baixe `Knack-II-Windows-x64-V0.18.zip` em [Releases](https://github.com/Hyant4/Knack-II-pc-port/releases/latest) e extraia **todo o conteúdo para uma pasta nova**.
2. Abra **`Knack II.exe`** (não execute dentro do ZIP). Python, Qt e runtime já estão incluídos; não é necessário instalar Python ou MSYS2.
3. Selecione a pasta do jogo extraído que contém `eboot.bin`, `game.elf`, `sce_sys` e `sce_module`.
4. Clique em **Validar**, ajuste suas preferências e clique em **Jogar**.

**Precisa extrair seu PKG?** Use **Instalação → Extrair jogo-base (.pkg)**. Os arquivos são extraídos **na mesma pasta do PKG selecionado**, sem sobrescrever uma instalação existente. A opção **Instalar update (.pkg)** verifica região/versão e oferece backup e restauração. Veja o [guia de extração](docs/EXTRACAO_PKG_BASE.md).

Após configurar o jogo, **`Play Knack II.exe`** pode iniciar com as preferências salvas, mantendo a validação da instalação.

### Linux (experimental, x86-64)

Baixe `Knack-II-x86_64-0.18.0.AppImage` em [Releases](https://github.com/Hyant4/Knack-II-pc-port/releases/latest) e execute:

```bash
chmod +x Knack-II-x86_64-0.18.0.AppImage
./Knack-II-x86_64-0.18.0.AppImage
```

Se não houver FUSE, use `./Knack-II-x86_64-0.18.0.AppImage --appimage-extract-and-run`.

**Requisitos Linux:** Vulkan 1.3+ e **glibc 2.43+**. O AppImage foi compilado em Ubuntu 26.04; não inclui glibc ou drivers gráficos, nem funciona em sistemas com glibc anterior. A compatibilidade com Linux Mint não foi certificada. O pacote abre o launcher, mas **não inclui o extrator PKG do Windows**: selecione um dump próprio já extraído.

Consulte o [guia de desenvolvimento Linux](docs/DESENVOLVIMENTO_LINUX.md) e as [notas do AppImage](docs/APPIMAGE_LINUX.md).

## Versões do jogo compatíveis

| Região | Title ID | Versão | Situação |
| --- | --- | --- | --- |
| EUA | `CUSA08014` | `01.00` | Compatível; atualização dispensável |
| EUA | `CUSA08014` | `01.01` | Compatível |
| Europa | `CUSA07670` | `01.01` | Compatível |

**Europa 01.00 e versões ainda não verificadas não são compatíveis.** O launcher valida região, versão, executáveis e módulos obrigatórios. Não misture atualizações de regiões diferentes. É necessário driver com suporte a **Vulkan 1.3+**.

Veja os [detalhes e evidências de compatibilidade regional](docs/COMPATIBILIDADE_REGIONAL.md).

## Configurações, saves e solução de problemas

- Configure resolução interna, FSR, antialiasing, ultrawide experimental e cooperativo local pelo launcher. O perfil GTX 1060 prioriza desempenho; o perfil RX 580 é **experimental**, com junção opcional de barreiras **desligada por padrão**. Não há correção universal confirmada para `Device lost`.
- Escolha o idioma do launcher na interface. Mais mensagens de erro e avisos de instalação foram traduzidos, inclusive para quem utiliza inglês.
- Saves e preferências ficam **fora** da pasta do pacote:
  - **Windows:** `%LOCALAPPDATA%\KnackII-PC`
  - **Linux:** `$XDG_DATA_HOME/knack-ii-pc` ou `~/.local/share/knack-ii-pc`
- Para atualizar o port, extraia a nova versão **em outra pasta**, preserve os dados do usuário e valide a instalação do jogo novamente. **Não é necessário apagar os caches.**
- Se houver falha, abra **Logs e diagnóstico** e [registre uma issue](https://github.com/Hyant4/Knack-II-pc-port/issues) com GPU, sistema, região/versão, passos para reproduzir e logs relevantes. Remova caminhos pessoais dos logs e não envie jogo, firmware ou dumps de memória.

**Limitações:** testes automatizados e abertura do launcher não comprovam funcionamento da campanha inteira em todas as GPUs. Linux e correções específicas para hardware continuam experimentais.

## Créditos e desenvolvimento

O port usa como base [HayatoG/iss_pc](https://github.com/HayatoG/iss_pc), com backend gráfico derivado do [shadPS4](https://github.com/shadps4-emu/shadPS4). Obrigado a **SkippySteve** pelas contribuições Linux do [PR #19](https://github.com/Hyant4/Knack-II-pc-port/pull/19), com os commits originais preservados.

[Créditos, proveniência e licenças](docs/CREDITS.md) · [Compilar no Windows](docs/DESENVOLVIMENTO_WINDOWS.md) · [Compilar no Linux](docs/DESENVOLVIMENTO_LINUX.md)

Projeto sem afiliação com a Sony. Knack II e seus conteúdos pertencem aos respectivos titulares.

Para apoiar o desenvolvimento: [Buy Me a Coffee](https://buymeacoffee.com/hyant4).
