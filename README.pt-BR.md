> 🇧🇷 **Documentação em português.** [English / international README](README.md).

# Knack II para PC

Port independente e experimental de Knack II para Windows.

**[Baixar para Windows — ZIP, 238 MB](https://github.com/Hyant4/Knack-II-pc-port/releases/tag/knack2-alpha-Version-0.10)**

[Notas da versão alpha](https://github.com/Hyant4/Knack-II-pc-port/releases/tag/knack2-alpha-Version-0.10)

## O que você precisa

- Windows de 64 bits recente.
- Placa de vídeo com suporte a Vulkan e driver atualizado.
- Seu próprio dump do **Knack II — CUSA08014, versão 01.01**.
- Dois controles, caso queira jogar em cooperativo local.

O jogo não acompanha o download. O pacote já inclui as dependências para jogar;
não é necessário instalar Python, Qt ou MSYS2.

## Como jogar

1. Baixe o **ZIP para Windows** no link acima.
2. Extraia a pasta inteira. Não abra o programa de dentro do ZIP.
3. Abra **`Knack II.exe`**.
4. Selecione a pasta do jogo e clique em **Validar**.
5. Em **Configurações**, escolha idioma, jogadores e ajustes gráficos. Clique em **Jogar**.

Selecione a pasta que contém `eboot.bin`, `game.elf`, `sce_sys` e `sce_module`.
Os idiomas disponíveis dependem dos arquivos presentes no seu dump.

Saves e preferências ficam em `%LOCALAPPDATA%\KnackII-PC`.

## Estado da versão

Esta é uma **alpha experimental**. Partida individual e cooperativo local foram
testados no início do jogo; a campanha completa ainda não foi validada.
Falhas gráficas e encerramentos inesperados ainda podem ocorrer.

## Capturas de tela

<p align="center"><img src="docs/screenshots/knack-ii-title-screen.png" alt="Tela inicial de Knack II no port para PC" width="49%" /> <img src="docs/screenshots/knack-ii-combat.png" alt="Combate de Knack II no port para PC" width="49%" /></p>

## Precisa de ajuda?

- **Validação falhou:** confira a pasta selecionada, o código **CUSA08014** e a versão **01.01** do jogo.
- **Erro ao abrir:** confira se extraiu o ZIP inteiro e manteve todos os arquivos juntos.
- **Outro problema:** use **Ver logs** no launcher e [abra uma issue](https://github.com/Hyant4/Knack-II-pc-port/issues), informando o erro, a etapa em que aconteceu e seu hardware.

Mais opções de controles, idiomas e logs: [guia do launcher](docs/LAUNCHER_WINDOWS.md).

## Créditos

Baseado em [HayatoG/iss_pc](https://github.com/HayatoG/iss_pc), com backend gráfico
derivado do [shadPS4](https://github.com/shadps4-emu/shadPS4) e contribuições de
outros ports e bibliotecas. Veja os [créditos e licenças](docs/CREDITS.md).

Projeto comunitário não oficial, sem afiliação com a Sony. Knack II pertence aos respectivos titulares.

Para contribuir ou compilar: [guia de desenvolvimento](docs/DESENVOLVIMENTO_WINDOWS.md).
