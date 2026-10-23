# Knack II para PC

Port experimental independente de **Knack II — CUSA08014, versão 01.01**, com foco no Windows x86-64.

O projeto executa o código x86-64 original do jogo por meio de um runtime que implementa serviços do PS4 e traduz gráficos para Vulkan. O backend gráfico deriva do shadPS4; a base direta deste repositório é o port `iss_pc`, de HayatoG.

**Use seu próprio dump do jogo.** Este repositório não distribui jogo, firmware, saves ou assets extraídos. Não é um produto oficial da Sony.

## Estado do projeto

Validação **local de desenvolvimento**, atualizada em **22/10/2026**:

| Área | Evidência disponível |
| --- | --- |
| Boot e menus | Título, seleção de partida e carregamento do início de Chapter 1-1 observados. |
| Gameplay | Movimento, combate e transições no trecho inicial testados; campanha completa ainda não validada. |
| Cooperativo local | Dois personagens com controles independentes confirmados pelo proprietário no início do jogo. |
| Áudio | Falas retornam após pausar e retomar cutscene; música e efeitos normais no trecho testado, conforme confirmação do proprietário. |
| Regressão de áudio | 13 runners focados passaram na etapa `audio-clock09`, incluindo contador de amostras, ciclo de vida, mixer e controles ATRAC9. |
| Gráficos e desempenho | Há correções de transparências e descritores de vértices. Persistem experimentos gráficos e ocorrência registrada de `Device lost`; sem garantia de estabilidade contínua. |
| Outras plataformas | O foco desta adaptação é Windows. Os resultados Linux dos projetos de origem não certificam Knack II no Linux. |

A correção recente de áudio informa ao jogo o contador de amostras efetivamente consumidas por voz. O contador acompanha a reprodução e congela na pausa, permitindo ao sincronizador retomar o som sem impor ganho artificial.

Esses resultados descrevem o build local testado, **não uma certificação de todos os capítulos, imports ou equipamentos**. Builds locais e alterações ainda não publicados podem estar à frente de `main`. Consulte os [créditos e a proveniência](docs/CREDITS.md) e o [guia de publicação](docs/PUBLICACAO.md). Handoffs e instruções de agentes são documentos locais, fora da versão pública atual.

## Usar a instalação compilada

Na instalação já preparada, abra **`Knack II.exe`** na raiz do projeto:

1. Selecione a pasta do seu dump CUSA08014 / 01.01.
2. Clique em **Validar**.
3. Escolha idioma, jogadores e ajustes em **Configurações**.
4. Clique em **Jogar**.

Abrir o launcher não inicia automaticamente o jogo. O caminho é lembrado; a validação é repetida na próxima abertura.

O runtime Windows é `port/out/knack2-runtime.exe`. Builds de diagnóstico também podem usar o nome `tlg-probe.exe`. O launcher compilado utiliza o supervisor auxiliar em `port/out/knack-launcher-supervisor/`.

**[Baixar alpha Windows x64 — audio-clock09](https://github.com/Hyant4/Knack-II-pc-port/releases/tag/knack2-alpha-2026.10.22-audio-clock09).** Extraia `Knack-II-Windows-x64-audio-clock09.zip` e abra `Knack II.exe`. O pacote inclui as DLLs necessárias; não exige instalar Python, Qt ou MSYS2 para jogar. Forneça seu próprio dump suportado e mantenha o driver Vulkan atualizado.

Essa distribuição preserva o código executável da compilação local testada, remove ícones extraídos do jogo do launcher e informações de debug do runtime. Cinco verificações de empacotamento passaram sem iniciar o jogo. É uma **alpha experimental**, com os limites de gameplay e gráficos descritos acima.

**Clonar os fontes não fornece esses executáveis.** Os fontes correspondentes às alterações locais estão no ZIP `Knack-II-sources-audio-clock09.zip` anexado à mesma release, com os submódulos expandidos. Os arquivos automáticos “Source code” do GitHub refletem a tag de documentação e não substituem esse snapshot. Downloads dos projetos de origem executam seus respectivos jogos, não este perfil.

Detalhes de controles, idiomas, logs e empacotamento: [guia do launcher Windows](docs/LAUNCHER_WINDOWS.md).

## Dump e dados locais

O perfil [CUSA08014-01.01.json](profiles/CUSA08014-01.01.json) verifica a identidade e os hashes suportados. A pasta do jogo precisa conter, entre outros arquivos:

- `eboot.bin` e `game.elf`;
- `sce_sys/param.sfo`;
- os módulos requeridos em `sce_module/`;
- os recursos originais do jogo.

O launcher oferece somente idiomas presentes na instalação validada. Na cópia inspecionada foram encontrados português do Brasil, inglês, espanhol latino-americano e francês.

Saves e preferências do launcher ficam em **`%LOCALAPPDATA%\KnackII-PC`**, separados entre partida individual e cooperativa. Imagens preparadas, logs e caches ficam fora do versionamento. Não coloque dump, saves ou chaves no Git.

## Compilar no Windows

Use Git, Python, MSYS2 **CLANG64**, CMake, Ninja e as dependências nativas do runtime. Em um **checkout novo**, sem alterações locais a preservar:

```powershell
git clone --recurse-submodules https://github.com/Hyant4/Knack-II-pc-port.git
cd Knack-II-pc-port
py -3 -m venv .venv
.\.venv\Scripts\python.exe -m pip install iced-x86==1.21.0
```

No shell **MSYS2 CLANG64**, instale as dependências:

```bash
pacman -S --needed git patch \
  mingw-w64-clang-x86_64-{toolchain,cmake,ninja,pkgconf,python} \
  mingw-w64-clang-x86_64-{vulkan-headers,vulkan-loader,vulkan-memory-allocator,glslang} \
  mingw-w64-clang-x86_64-{sdl3,fmt,boost,robin-map,xxhash,ffmpeg,zydis,freetype} \
  mingw-w64-clang-x86_64-{spirv-tools,spirv-headers,spirv-cross}
```

Depois, na raiz do projeto, em PowerShell:

```powershell
# Ajuste apenas se o MSYS2 estiver em outro local.
# $env:MSYS2_ROOT = 'C:\msys64'
.\build.ps1 -Jobs 6
```

O wrapper usa `%LOCALAPPDATA%\knack2-toolchain\msys64` por padrão. O build gera `port/out/tlg-probe.exe` e registra o log em `port/out/gpu-build.log`. Reconstruir o runtime não substitui automaticamente o executável de nome público que já exista no launcher.

O build aplica os patches FSR versionados e pode deixar esse submódulo modificado por desenho; não descarte essas mudanças para “limpar” o checkout. A lista do ambiente observado está em [knack2-toolchain-packages.txt](docs/knack2-toolchain-packages.txt).

Para construir a interface Qt/Python, siga [Reconstruir somente o launcher](docs/LAUNCHER_WINDOWS.md#reconstruir-somente-o-launcher). Essa etapa é separada da compilação do runtime.

## Diagnóstico e testes

A CLI oficial é o `run.py` da raiz:

```powershell
$game = 'D:\Jogos\KnackII\CUSA08014'
.\.venv\Scripts\python.exe -B run.py --game-dir $game --analyze
.\.venv\Scripts\python.exe -B run.py --game-dir $game --check-imports
```

`--check-imports` pode retornar 1 para símbolos ausentes em caminhos não exercitados. Uma contagem estática de imports não equivale à mesma quantidade de bloqueios de gameplay.

Execuções de diagnóstico usam timeout padrão de 180 segundos. Código 124 indica timeout do supervisor e precisa ser distinguido de crash. O launcher normal usa sessões sem esse limite.

Exemplos de regressões focadas, sem iniciar o jogo:

```powershell
.\.venv\Scripts\python.exe -B -m unittest discover -s port/tests -p test_prepare.py -v
.\.venv\Scripts\python.exe -B -m unittest discover -s port/tests -p test_link_modules.py -v
```

Alguns runners dependem da toolchain ou de fixtures locais. Testes sintéticos, build, validação visual e confirmação auditiva são evidências diferentes.

## Estrutura

| Caminho | Conteúdo |
| --- | --- |
| `port/src/` | Loader, serviços, entrada, codecs e runtime de áudio. |
| `port/gpu/` | Backend Vulkan, compilador de shaders e código gráfico herdado. |
| `port/launcher/knack2/` | Launcher específico de Knack II. |
| `profiles/` | Identidade e hashes do dump suportado. |
| `tests/`, `port/tests/` | Testes e fixtures sintéticas. |
| `docs/` | Guias, créditos e documentação técnica. |
| `docs/archive/` | Artefatos históricos identificados, sem aplicação automática. |
| `out/`, `port/out/` | Saídas locais ignoradas pelo Git. |

Ferramentas e launchers `infamous_*` são herança da base. Permanecem como referência e compatibilidade; não são a entrada de Knack II.

## Bases e créditos

Este port depende do trabalho dos autores dos projetos abaixo:

- **[HayatoG/iss_pc](https://github.com/HayatoG/iss_pc)** — base direta, no snapshot `d65fa53a1e7cfe71c8eb402b1e6526fa835ef5df`.
- **[HayatoG/tlg_pc](https://github.com/HayatoG/tlg_pc)** e **[HayatoG/gr2_pc](https://github.com/HayatoG/gr2_pc)** — linhagem do runtime e da infraestrutura herdada.
- **[shadPS4](https://github.com/shadps4-emu/shadPS4)** — núcleo gráfico Vulkan e recompilação de shaders.
- **[FelipePrado0/SpartanPort](https://github.com/FelipePrado0/SpartanPort)**, anteriormente `gow3_pc`, e **[deadinside28/bloodborne_pc](https://github.com/deadinside28/bloodborne_pc)** — linhagem e referências técnicas do runtime/Windows.
- **[shadPS4-Infamous-FixBuild](https://github.com/rayhlancaner-png/shadPS4-Infamous-FixBuild)** e **[AjncSystem/ShadowOfTheColossus-PC](https://github.com/AjncSystem/ShadowOfTheColossus-PC)** — correções gráficas herdadas e adaptadas pela base.
- **[Thealexbarney/LibAtrac9](https://github.com/Thealexbarney/LibAtrac9)**, **[FFmpeg](https://github.com/FFmpeg/FFmpeg)**, **[SDL](https://github.com/libsdl-org/SDL)** e **[FreeType](https://github.com/freetype/freetype)** — decodificação, mídia, entrada/saída e fontes.

A lista detalhada de forks, bibliotecas, ferramentas e fontes está em **[Créditos e proveniência](docs/CREDITS.md)**. Os hashes e a autoria das importações gráficas permanecem em [VENDOR.txt](port/gpu/VENDOR.txt); a genealogia está em [UPSTREAM.md](port/UPSTREAM.md). O [README original](README.upstream.md) foi preservado integralmente.

## Licenças e titularidade

O código derivado conserva a licença indicada em [port/LICENSE](port/LICENSE) e [shadPS4/LICENSE](port/gpu/shadps4/LICENSE), além dos avisos de autoria por arquivo. Dependências mantêm suas próprias licenças; a adaptação não altera essas condições.

Knack e Knack II pertencem aos respectivos titulares, incluindo Sony Interactive Entertainment. Este projeto comunitário não tem afiliação oficial com a Sony ou os criadores do jogo.
