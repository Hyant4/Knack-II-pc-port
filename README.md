# Knack II para PC

Port independente e experimental de Knack II para Windows, com suporte Linux nos fontes atuais.

Para Linux e AppImage, consulte [desenvolvimento Linux](docs/DESENVOLVIMENTO_LINUX.md),
[AppImage](docs/APPIMAGE_LINUX.md) e [integração do PR #19](docs/PR19_INTEGRACAO.md).
Os links de download Windows abaixo continuam apontando às versões já publicadas.

**[Baixar para Windows — hotfix 0.17.1](https://github.com/Hyant4/Knack-II-pc-port/releases/tag/knack2-Version-0.17.1)**

[Notas do hotfix 0.17.1](https://github.com/Hyant4/Knack-II-pc-port/releases/tag/knack2-Version-0.17.1)

## O que você precisa

- Windows de 64 bits recente.
- Placa de vídeo com suporte a Vulkan e driver atualizado.
- Seu próprio dump do **Knack II — CUSA08014, versão 01.00 ou 01.01; ou CUSA07670, versão 01.01**.
- Dois controles, caso queira jogar em cooperativo local.

A base americana **CUSA08014 01.00 pode ser usada sem instalar update**.
Selecione o dump extraído no mesmo launcher; a região e a versão são detectadas
pelo `param.sfo`, com conferência integral dos executáveis e módulos obrigatórios.
Veja [compatibilidade regional e limites dos testes](docs/COMPATIBILIDADE_REGIONAL.md).

O jogo não acompanha o download. O pacote já inclui as dependências para jogar;
não é necessário instalar Python, Qt ou MSYS2.

## Como jogar

1. Baixe o **ZIP para Windows** no link acima.
2. Extraia a pasta inteira. Não abra o programa de dentro do ZIP.
3. Abra **`Knack II.exe`**.
4. Selecione a pasta do jogo e clique em **Validar**.
5. Em **Configurações**, escolha idioma, jogadores e ajustes gráficos. Clique em **Jogar**.

Selecione a pasta que contém `eboot.bin`, `game.elf`, `sce_sys` e `sce_module`.

Se você tem o PKG-base, use **Instalação → Extrair jogo-base (.pkg)**.
O launcher extrai diretamente na mesma pasta do PKG, preserva o pacote original
e bloqueia conflitos com arquivos existentes. Veja [extração do jogo-base](docs/EXTRACAO_PKG_BASE.md).
Os idiomas disponíveis dependem dos arquivos presentes no seu dump.

A versão 0.16 reconhece os perfis americano e europeu e oferece idiomas conforme
a região validada: quatro americanos ou quatorze europeus, limitados aos arquivos
instalados. A interface do launcher mantém seus quatro catálogos de tradução.
Veja o [estado da compatibilidade regional](docs/COMPATIBILIDADE_REGIONAL.md).

Nos fontes atuais, o perfil americano 01.00 também é reconhecido com os hashes
exatos documentados na issue #12. A integração foi validada sem executar o jogo;
uma instalação completa 01.00 ainda precisa de teste de gameplay. Downloads já
publicados não recebem essa mudança automaticamente.

O hotfix 0.17.1 corrige proporções do gameplay em 21:9/32:9 e preserva abertura
e vídeos pré-renderizados em área 16:9, com barras pretas. Ao pular, as barras
continuam durante a transição e saem quando gameplay começa.
Veja [evidências e limites do ultrawide](docs/FSR_RESOLUCAO.md#ultrawide-experimental-na-016).

Saves e preferências ficam em `%LOCALAPPDATA%\KnackII-PC`.

## Estado da versão

Esta é uma **alpha experimental**. Partida individual e cooperativo local foram
testados no início do jogo; a campanha completa ainda não foi validada.
Falhas gráficas e encerramentos inesperados ainda podem ocorrer.

## Capturas de tela

<p align="center"><img src="docs/screenshots/knack-ii-title-screen.png" alt="Tela inicial de Knack II no port para PC" width="49%" /> <img src="docs/screenshots/knack-ii-combat.png" alt="Combate de Knack II no port para PC" width="49%" /></p>

## Precisa de ajuda?

- **Validação falhou:** confira a pasta e o perfil: **CUSA08014 / 01.00 ou 01.01**, ou **CUSA07670 / 01.01**. A versão 01.00 requer o launcher atualizado a partir dos fontes atuais.
- **Erro ao abrir:** confira se extraiu o ZIP inteiro e manteve todos os arquivos juntos.
- **Outro problema:** use **Ver logs** no launcher e [abra uma issue](https://github.com/Hyant4/Knack-II-pc-port/issues), informando o erro, a etapa em que aconteceu e seu hardware.

Mais opções de controles, idiomas e logs: [guia do launcher](docs/LAUNCHER_WINDOWS.md).

## Créditos

Baseado em [HayatoG/iss_pc](https://github.com/HayatoG/iss_pc), com backend gráfico
derivado do [shadPS4](https://github.com/shadps4-emu/shadPS4) e contribuições de
outros ports e bibliotecas. Veja os [créditos e licenças](docs/CREDITS.md).

Projeto comunitário não oficial, sem afiliação com a Sony. Knack II pertence aos respectivos titulares.

Para contribuir ou compilar: [guia de desenvolvimento](docs/DESENVOLVIMENTO_WINDOWS.md).

Suporte Linux em desenvolvimento, com pacote local e boot experimental; ainda sem download público:
[instruções e pendências Linux](docs/DESENVOLVIMENTO_LINUX.md).
