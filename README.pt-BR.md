> 🇧🇷 **Documentação em português.** [English / international README](README.md).

# Knack II para PC

Port independente e experimental de **Knack II para Windows**, com renderização Vulkan, launcher integrado e cooperativo local para dois jogadores.

**[Baixar Knack II para Windows — Hotfix V0.17.1](https://github.com/Hyant4/Knack-II-pc-port/releases/download/knack2-Version-0.17.1/Knack-II-Windows-x64-V0.17.1.zip)** · [Notas do hotfix](https://github.com/Hyant4/Knack-II-pc-port/releases/tag/knack2-Version-0.17.1)

> **Hotfix V0.17.1:** gameplay 21:9/32:9 mantém proporções; abertura e vídeos ficam centralizados em 16:9 com barras pretas. Ao pular, barras continuam durante a transição e somem ao iniciar gameplay.

> **Projeto experimental e não oficial.** O responsável pelo projeto relata que o jogo está jogável do início ao fim. O cooperativo local foi exercitado especificamente até o capítulo 2. Isso não equivale a uma certificação independente de toda a campanha ou de todos os computadores: ainda podem ocorrer falhas gráficas, travamentos e problemas de compatibilidade.

## O que mudou na V0.17

| Recurso | Situação |
| --- | --- |
| Versão europeia | Reconhecimento/suporte experimental à **CUSA07670**, com boot curto e save temporário validados. Campanha completa, dublagens e legendas ainda não foram certificadas. |
| Ultrawide | Suporte **experimental**; cenas e artes de abertura podem apresentar enquadramento incorreto, laterais azuis, barras incompletas ou imagem estreita. |
| Launcher | Interface em português, inglês, espanhol e francês; outros idiomas selecionados podem utilizar textos em inglês no launcher. |
| Áudio regional | Algumas proteções de endereçamento para ganho de diálogos permanecem específicas da versão americana CUSA08014. |
| Recursos existentes | Vulkan, cooperativo local, opções FSR 3.1/4 e melhorias anteriores de áudio. |

**Atenção:** o problema de enquadramento nas cutscenes **não foi corrigido** na V0.17. Recomenda-se **16:9** para preservar o enquadramento original. As tentativas de correção não validadas ficaram fora dos pacotes publicados.

**Validação da release:** 186 testes aprovados, 4 ignorados e nenhuma falha relatada. Testes automatizados não substituem uma campanha completa jogada e verificada em diferentes configurações.

## Requisitos

- Windows recente de 64 bits.
- Placa de vídeo compatível com Vulkan e driver atualizado.
- Seu próprio dump do jogo: **CUSA08014 (americana, versão 01.01)** ou **CUSA07670 (europeia, suporte experimental)**.
- Dois controles para cooperativo local.

O pacote inclui executáveis e dependências. **Não inclui o jogo, firmware, dumps, saves ou assets proprietários.** Não é necessário instalar Python, Qt ou MSYS2 para jogar.

## Como jogar

1. Baixe [**Knack-II-Windows-x64-V0.17.1.zip**](https://github.com/Hyant4/Knack-II-pc-port/releases/download/knack2-Version-0.17.1/Knack-II-Windows-x64-V0.17.1.zip).
2. Extraia o ZIP completo para uma pasta; não execute diretamente do arquivo compactado.
3. Abra **`Knack II.exe`**.
4. Selecione a pasta do seu jogo contendo `eboot.bin`, `game.elf`, `sce_sys` e `sce_module`. Clique em **Validar**.
5. Em **Configurações**, escolha idioma, opções gráficas e número de jogadores. Clique em **Jogar**.

Os idiomas dentro do jogo dependem dos arquivos presentes no seu dump. Saves, configurações e logs ficam em `%LOCALAPPDATA%\KnackII-PC`. O modo solo e o cooperativo usam o perfil selecionado.

## Compatibilidade e limitações

| Área | Estado atual |
| --- | --- |
| Campanha | O autor relata jogabilidade do início ao fim; não há validação independente de regressão completa para todos os capítulos e sistemas. |
| Cooperativo | Dois jogadores localmente; sessão do autor validada até o capítulo 2. |
| Versão americana | CUSA08014 / 01.01 é a configuração principal testada. |
| Versão europeia | CUSA07670 tem validação curta de inicialização e save; compatibilidade extensa ainda não certificada. |
| Ultrawide e cutscenes | Ultrawide é experimental; use 16:9 para preservar o enquadramento das cenas. |
| Estabilidade | Ainda podem ocorrer erros gráficos, falhas Vulkan ou encerramentos inesperados. |

## Capturas de tela

<p align="center"><img src="docs/screenshots/knack-ii-title-screen.png" alt="Tela inicial de Knack II no port para PC" width="49%" /> <img src="docs/screenshots/knack-ii-combat.png" alt="Combate de Knack II no port para PC" width="49%" /></p>

## Precisa de ajuda?

- **Jogo não é validado:** confira os arquivos e a edição do dump. A CUSA07670 ainda possui suporte experimental.
- **O launcher não abre:** extraia o ZIP inteiro e mantenha todos os arquivos juntos.
- **Falha durante o jogo:** use **Ver logs** no launcher e [abra uma issue](https://github.com/Hyant4/Knack-II-pc-port/issues) com versão do jogo, GPU, driver, resolução, capítulo e passos para reproduzir. Não anexe arquivos do jogo, saves ou caminhos privados.

Mais informações: [guia do launcher](docs/LAUNCHER_WINDOWS.md).

## Código-fonte da versão publicada

Baixe [**Knack-II-sources-V0.17.zip**](https://github.com/Hyant4/Knack-II-pc-port/releases/download/knack2-Version-0.17/Knack-II-sources-V0.17.zip), anexado à release. Esse é o **pacote de fontes correspondente ao binário Windows publicado**.

**Importante:** os arquivos automáticos *Source code (zip/tar.gz)* gerados pelo GitHub a partir da tag V0.17 representam o commit apontado pela tag, **não** o snapshot completo da compilação validada. Para reproduzir a V0.17 distribuída, utilize o pacote de fontes anexado.

Para compilar ou contribuir, veja o [guia de desenvolvimento](docs/DESENVOLVIMENTO_WINDOWS.md). Confira os hashes no [SHA256SUMS.txt](https://github.com/Hyant4/Knack-II-pc-port/releases/download/knack2-Version-0.17/SHA256SUMS.txt).

## Créditos e avisos legais

Baseado em [HayatoG/iss_pc](https://github.com/HayatoG/iss_pc), com backend gráfico derivado do [shadPS4](https://github.com/shadps4-emu/shadPS4) e contribuições de outros ports e bibliotecas. Veja todos os [créditos, proveniência e licenças](docs/CREDITS.md).

Projeto comunitário **não oficial**, sem afiliação ou endosso da Sony Interactive Entertainment. Knack II pertence aos respectivos titulares. Nenhum arquivo proprietário do jogo acompanha o port.
