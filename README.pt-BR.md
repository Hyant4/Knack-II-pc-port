<p align="center">
  <img src="docs/screenshots/knack-ii-title-screen.png" alt="Knack II rodando no PC" width="250" />
</p>

<h1 align="center">Knack II — Port para PC</h1>

<p align="center">
  <strong>Jogue Knack II no Windows com gráficos Vulkan, launcher próprio e cooperativo local para dois jogadores.</strong>
</p>

<p align="center">
  <a href="https://buymeacoffee.com/hyant4"><img alt="Buy me a coffee — Apoie o projeto" src="https://img.shields.io/badge/Buy%20me%20a%20coffee-Apoie%20o%20projeto-FFDD00?style=flat-square&amp;labelColor=555555&amp;logo=buymeacoffee&amp;logoColor=white" /></a>
  <a href="https://github.com/Hyant4/Knack-II-pc-port/releases/tag/knack2-Version-0.17.3"><img alt="Baixar Windows x64 v0.17.3" src="https://img.shields.io/badge/Baixar-Windows%20x64%20v0.17.3-2ea44f?style=flat-square&amp;logo=windows&amp;logoColor=white" /></a>
</p>

<p align="center">
  <a href="README.md">English</a>
  · <a href="https://github.com/Hyant4/Knack-II-pc-port/releases">Versões</a>
  · <a href="https://hyant4.github.io/Knack-II-pc-port/">Site do projeto</a>
  · <a href="https://github.com/Hyant4/Knack-II-pc-port/issues">Reportar problema</a>
</p>

<p align="center">
  <img alt="Windows" src="https://img.shields.io/badge/Plataforma-Windows-555555?style=flat-square" />
  <img alt="Vulkan" src="https://img.shields.io/badge/Gr%C3%A1ficos-Vulkan-555555?style=flat-square" />
  <img alt="Cooperativo local para 2 jogadores" src="https://img.shields.io/badge/Co--op%20local-2%20jogadores-555555?style=flat-square" />
</p>

---

> **V0.17.3:** pré-carregamento experimental de shaders ativado por padrão; desative em Configurações / Vídeo. A abertura pode demorar mais e engasgos ainda podem ocorrer. Sem garantia de 60 FPS estáveis.

> **V0.17.2:** launcher com interface PT / EN / RU. Escolha fica salva separadamente do idioma do jogo; runtime permanece idêntico à V0.17.1.

> **Hotfix V0.17.1:** gameplay 21:9/32:9 mantém proporções; abertura e vídeos ficam centralizados em 16:9 com barras pretas. Ao pular, barras continuam durante a transição e somem ao iniciar gameplay.

> **Projeto experimental e não oficial.** O responsável pelo projeto relata que o jogo está jogável do início ao fim. O cooperativo local foi exercitado especificamente até o capítulo 2. Isso não equivale a uma certificação independente de toda a campanha ou de todos os computadores: ainda podem ocorrer falhas gráficas, travamentos e problemas de compatibilidade.

## O que mudou na V0.17.2

- Alternância PT / EN / RU no launcher, com preferência independente do idioma do jogo.
- Runtime preservado da V0.17.1.

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

1. Baixe [**Knack-II-Windows-x64-V0.17.3.zip**](https://github.com/Hyant4/Knack-II-pc-port/releases/download/knack2-Version-0.17.3/Knack-II-Windows-x64-V0.17.3.zip).
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

Baixe [**Knack-II-sources-V0.17.3.zip**](https://github.com/Hyant4/Knack-II-pc-port/releases/download/knack2-Version-0.17.3/Knack-II-sources-V0.17.3.zip), anexado à release. Esse é o **pacote de fontes correspondente ao binário Windows publicado**.

**Importante:** os arquivos automáticos *Source code (zip/tar.gz)* gerados pelo GitHub a partir da tag V0.17 representam o commit apontado pela tag, **não** o snapshot completo da compilação validada. Para reproduzir a V0.17 distribuída, utilize o pacote de fontes anexado.

Para compilar ou contribuir, veja o [guia de desenvolvimento](docs/DESENVOLVIMENTO_WINDOWS.md). Confira os hashes no [SHA256SUMS.txt](https://github.com/Hyant4/Knack-II-pc-port/releases/download/knack2-Version-0.17.3/SHA256SUMS.txt).

## Apoie o projeto

Knack II para PC é um projeto comunitário, gratuito e não oficial. Se quiser apoiar a continuidade do desenvolvimento, você pode [**me pagar um café ☕**](https://buymeacoffee.com/hyant4). As doações são totalmente opcionais e não incluem arquivos do jogo nem acesso exclusivo.

## Créditos e avisos legais

Baseado em [HayatoG/iss_pc](https://github.com/HayatoG/iss_pc), com backend gráfico derivado do [shadPS4](https://github.com/shadps4-emu/shadPS4) e contribuições de outros ports e bibliotecas. Veja todos os [créditos, proveniência e licenças](docs/CREDITS.md).

Projeto comunitário **não oficial**, sem afiliação ou endosso da Sony Interactive Entertainment. Knack II pertence aos respectivos titulares. Nenhum arquivo proprietário do jogo acompanha o port.
