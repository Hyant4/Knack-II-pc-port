# Knack II para PC

Port independente e experimental de Knack II para Windows e Linux x86-64.

**[Downloads publicados](https://github.com/Hyant4/Knack-II-pc-port/releases/latest)** ·
[Notas da V0.18](RELEASE-NOTES.md) · [Créditos e licenças](docs/CREDITS.md)

O jogo não acompanha o pacote. Use seu próprio dump completo de **CUSA08014
01.00 ou 01.01**, ou **CUSA07670 01.01**, e driver com **Vulkan 1.3+**.
A base americana 01.00 não exige update. A validação confere região, versão,
executáveis e módulos; Europa 01.00 e outras versões não comprovadas continuam
sem suporte. [Compatibilidade e evidências](docs/COMPATIBILIDADE_REGIONAL.md).

## Windows

1. Baixe o ZIP Windows e extraia a pasta inteira em um diretório novo.
2. Abra **`Knack II.exe`**.
3. Selecione a pasta que contém `eboot.bin`, `game.elf`, `sce_sys` e `sce_module`.
4. Clique em **Validar**, configure suas preferências e clique em **Jogar**.

Python, Qt, runtime e dependências acompanham o pacote. Não é preciso instalar
Python ou MSYS2. Não execute de dentro do ZIP.

A aba **Instalação** oferece **Extrair jogo-base (.pkg)** e **Instalar update
(.pkg)**. A extração preserva o PKG original; a atualização mantém backup e
restauração. Confira região, versão e espaço disponível antes de confirmar.
[Guia de extração](docs/EXTRACAO_PKG_BASE.md).

**`Play Knack II.exe`** usa as preferências salvas e mantém a validação da
instalação. Faça a primeira configuração pelo launcher normal.

## Linux — experimental

O AppImage inclui launcher, Python, Qt e bibliotecas nativas. Este binário usa
base Ubuntu 26.04 e requer **glibc 2.43+**, Linux x86-64 e Vulkan 1.3+.
Drivers gráficos e glibc ficam no sistema. AppImage não reduz esse requisito;
distribuições com glibc anterior não são atendidas por este pacote. Compatibilidade
com Mint não foi certificada.

```bash
chmod +x Knack-II-x86_64-0.18.0.AppImage
./Knack-II-x86_64-0.18.0.AppImage
```

Sem FUSE, use `--appimage-extract-and-run`. Abrir o AppImage mostra o launcher;
não inicia o jogo automaticamente. No Linux, selecione um dump já extraído:
o extrator PKG Windows não acompanha esse pacote.

[Guia Linux](docs/DESENVOLVIMENTO_LINUX.md) · [AppImage](docs/APPIMAGE_LINUX.md).

## Configurações e progresso

Resolução da janela e resolução interna são independentes. QHD/4K internos,
FSR, ultrawide experimental, guia visual de controles e cooperativo local
continuam disponíveis. A V0.18 acrescenta controle separado do antialiasing
nativo e reúne as novidades descritas nas [notas da versão](RELEASE-NOTES.md).

As predefinições GTX 1060 e RX 580 descrevem seus ajustes. O perfil RX 580 é
experimental e a junção de barreiras fica desligada por padrão; não há
confirmação de que resolva todos os casos de perda de dispositivo.

Saves e preferências ficam fora da pasta do pacote: `%LOCALAPPDATA%\KnackII-PC`
no Windows; `$XDG_DATA_HOME/knack-ii-pc` ou `~/.local/share/knack-ii-pc` no Linux.
Para atualizar o port, extraia a versão nova em outra pasta, preserve os dados
de usuário e valide novamente a instalação. Não é necessário apagar caches.

## Capturas de tela

<p align="center"><img src="docs/screenshots/knack-ii-title-screen.png" alt="Tela inicial de Knack II no port para PC" width="49%" /> <img src="docs/screenshots/knack-ii-combat.png" alt="Combate de Knack II no port para PC" width="49%" /></p>

## Estado e ajuda

Projeto **experimental**. Há evidências de gameplay inicial em configurações
específicas; campanha completa, todas as GPUs e todos os modos não foram
certificados. Build e testes automatizados não substituem teste de jogo.

Use **Logs e diagnóstico** e [abra uma issue](https://github.com/Hyant4/Knack-II-pc-port/issues)
com erro, etapa e hardware. Revise caminhos pessoais antes de anexar logs.
Não envie jogo, firmware, saves ou dumps de memória.

## Créditos

Baseado em [HayatoG/iss_pc](https://github.com/HayatoG/iss_pc), com backend derivado
do [shadPS4](https://github.com/shadps4-emu/shadPS4). Inclui contribuições Linux de
[SkippySteve no PR #19](https://github.com/Hyant4/Knack-II-pc-port/pull/19), cujos
commits originais foram preservados. [Créditos completos](docs/CREDITS.md).

Projeto comunitário não oficial, sem afiliação com a Sony. Knack II pertence
aos respectivos titulares. Para compilar: [Windows](docs/DESENVOLVIMENTO_WINDOWS.md)
e [Linux](docs/DESENVOLVIMENTO_LINUX.md).
