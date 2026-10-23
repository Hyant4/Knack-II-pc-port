# Créditos e proveniência

A adaptação para Knack II é mantida neste [repositório](https://github.com/Hyant4/Knack-II-pc-port). Ela reutiliza uma base extensa de outros projetos. Renomear o launcher, adaptar serviços ou corrigir o áudio não transfere a autoria do código herdado.

As categorias abaixo distinguem base direta, linhagem, backports e dependências. Uma referência técnica não significa que seus autores participaram diretamente desta adaptação ou validaram Knack II.

## Base direta e linhagem do runtime

| Projeto e autores | Relação documentada |
| --- | --- |
| [HayatoG/iss_pc](https://github.com/HayatoG/iss_pc) | Base direta do checkout, snapshot `d65fa53a1e7cfe71c8eb402b1e6526fa835ef5df`. O [README original](../README.upstream.md) permanece intacto. |
| [HayatoG/tlg_pc](https://github.com/HayatoG/tlg_pc) | Runtime e infraestrutura da etapa The Last Guardian presentes na linhagem herdada. |
| [HayatoG/gr2_pc](https://github.com/HayatoG/gr2_pc) | Infraestrutura compartilhada da família de ports e serviços específicos herdados. |
| [FelipePrado0/SpartanPort](https://github.com/FelipePrado0/SpartanPort), anteriormente `gow3_pc` | Origem histórica de parte do runtime e referências Windows/diagnóstico. [UPSTREAM.md](../port/UPSTREAM.md) registra os commits herdados `9a6d17c` e `2a0eac6`; a adaptação também documentou a referência `1436e7e`. |
| [Supermedo/bloodborne_pc](https://github.com/Supermedo/bloodborne_pc) | Elo da genealogia registrado por `port/UPSTREAM.md`. |
| [deadinside28/bloodborne_pc](https://github.com/deadinside28/bloodborne_pc) | Base anterior da família de loaders e referência gráfica; snapshot de referência `3c57da4`. |

Nomes internos como `tlg`, `gr2` e `iss` refletem essa história. Eles não indicam que o jogo alvo continue sendo um dos títulos anteriores.

## Backend gráfico e correções herdadas

| Projeto e autores | Contribuição registrada |
| --- | --- |
| [shadPS4 e seus contribuidores](https://github.com/shadps4-emu/shadPS4) | Video core, Vulkan, recompilador de shaders e bibliotecas de suporte. Snapshot principal vendorizado: `b4e7ae776071ceb1dd35004577a8719481e1d258`. |
| [shadPS4-Infamous-FixBuild](https://github.com/rayhlancaner-png/shadPS4-Infamous-FixBuild) — Eye Of The Ruins/rayhlancaner-png, TOXIC-KID, 0xDeftones | Backports de atomics, stencil, memória, pipelines, uploads e readbacks, conforme o inventário herdado. |
| [AjncSystem/ShadowOfTheColossus-PC](https://github.com/AjncSystem/ShadowOfTheColossus-PC) | Barreiras LDS, reconvergência e `S_WQM_B64`, adaptados pela base. |
| [luizgustavs/Pink-shadPS4](https://github.com/luizgustavs/Pink-shadPS4) | Crédito da parte multi-wave/ReadLane herdada por intermédio do port de Shadow of the Colossus. |
| [Mrsuss60/bloodborne_pc_windows_port](https://github.com/Mrsuss60/bloodborne_pc_windows_port) | Referência do backport de cache de pipelines do driver, `b972798`. |
| [0xCydral/bloodborne_pc](https://github.com/0xCydral/bloodborne_pc) | Referências de cache de pipelines, `bdb9769` e `4f16e00`. |
| [T3CHNOLOG1C/CTR](https://github.com/T3CHNOLOG1C/CTR) | Origem histórica registrada do backport BP09, `d26bcb2`, para imagens 1D. O endereço retornou 404 na conferência de 22/10/2026; o crédito histórico é mantido. |
| [FireBurn/FSR-Vulkan](https://github.com/FireBurn/FSR-Vulkan) | Submódulo de suporte FSR do backend, com adaptações versionadas em `port/gpu/patches/fsr-vulkan/`. |

O inventário por commit e o escopo de cada importação estão em [VENDOR.txt](../port/gpu/VENDOR.txt). Esse registro preserva a autoria e o escopo dos backports, incluindo os componentes compartilhados que continuam no runtime.

## Áudio, mídia, fontes e preparação

| Projeto | Uso no port |
| --- | --- |
| [Thealexbarney/LibAtrac9](https://github.com/Thealexbarney/LibAtrac9) | Decodificação ATRAC9. [Licença MIT no commit fixado](https://github.com/Thealexbarney/LibAtrac9/blob/efca2e3af35562a09a9bb6deed90e45b4b824dc4/LICENSE). |
| [FFmpeg](https://github.com/FFmpeg/FFmpeg) | Bibliotecas de áudio/vídeo, demux, conversão e decodificação usadas pelo runtime e pelo player herdado. |
| [SDL / libsdl-org](https://github.com/libsdl-org/SDL) | SDL3 para áudio, janela e entrada. |
| [FreeType](https://github.com/freetype/freetype) | Renderização de fontes no caminho HLE. |
| [iced-x86 / icedland](https://github.com/icedland/iced) | Decodificação de instruções nos scripts de preparação. |
| [DejaVu Fonts](https://dejavu-fonts.github.io/) | Fonte de suporte e testes; [avisos Bitstream/DejaVu/Arev preservados](../port/gpu/third_party/fonts/LICENSE-DejaVu.txt). |

A consulta de contratos SaveData também usou como referência o [shadPS4 no commit `e6c79a5`](https://github.com/shadps4-emu/shadPS4/tree/e6c79a5fe0ea2f3d03fcb9c5c50cf61af063f3f5/src/core/libraries/save_data). Isso não atribui ao upstream a implementação local de backup em diretórios.

## Bibliotecas auxiliares do build gráfico

Estas bibliotecas chegam por submódulos, código vendorizado ou pacotes do sistema. A lista não implica autoria local nem integração nova nesta atualização de documentação.

| Projeto | Papel |
| --- | --- |
| [KhronosGroup/Vulkan-Headers](https://github.com/KhronosGroup/Vulkan-Headers) | Headers e registro Vulkan fixados por submódulo. |
| [ocornut/imgui](https://github.com/ocornut/imgui) | Interface e menu de diagnóstico do backend. |
| [Neargye/magic_enum](https://github.com/Neargye/magic_enum) | Reflexão de enums C++. |
| [richgel999/miniz](https://github.com/richgel999/miniz) | Compressão. |
| [herumi/xbyak](https://github.com/herumi/xbyak) | Geração de código x86/x64. |
| [fmtlib/fmt](https://github.com/fmtlib/fmt) | Formatação. |
| [boostorg/boost](https://github.com/boostorg/boost) | Utilitários C++. |
| [Tessil/robin-map](https://github.com/Tessil/robin-map) | Tabelas hash. |
| [Cyan4973/xxHash](https://github.com/Cyan4973/xxHash) | Hashes. |
| [GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator) | Alocação de memória Vulkan. |
| [zyantific/zydis](https://github.com/zyantific/zydis) | Decodificação de instruções x86/x64 no código nativo. |
| [Sirit vendorizado](../port/gpu/third_party/sirit/README.md) e [SPIRV-Headers](../port/gpu/third_party/sirit/externals/SPIRV-Headers/README.md) | Emissão e definições SPIR-V herdadas. |

Os arquivos [`.gitmodules`](../.gitmodules), [CMakeLists.txt](../port/gpu/CMakeLists.txt) e [lista do ambiente observado](knack2-toolchain-packages.txt) documentam caminhos e dependências. Os commits dos submódulos são registrados pelo Git; versões de pacotes locais não substituem esses pins.

## Launcher e ferramentas

O launcher específico de Knack II utiliza Python, Qt for Python/PySide6 e PyInstaller. Referências: [CPython](https://github.com/python/cpython), [Qt for Python](https://code.qt.io/cgit/pyside/pyside-setup.git/) e [PyInstaller](https://github.com/pyinstaller/pyinstaller).

A interface herdada utilizava as fontes abaixo. Sua interface e assets não utilizados foram removidos; os avisos históricos de autoria permanecem preservados: [Barlow](../port/launcher/qt/fonts/OFL-Barlow.txt) e [Big Shoulders Stencil Display](../port/launcher/qt/fonts/OFL-BigShouldersStencilDisplay.txt). Os arquivos de licença foram preservados nos caminhos originais.

## Licenças e recursos do jogo

Consulte [port/LICENSE](../port/LICENSE), [shadPS4/LICENSE](../port/gpu/shadps4/LICENSE) e os arquivos de licença de cada dependência. Não remova avisos de copyright, atribuição ou SPDX ao reorganizar o projeto. A licença de uma biblioteca não deve ser atribuída automaticamente ao conjunto inteiro.

Os binários, assets, marcas e saves do jogo não passam a ser software livre por serem utilizados localmente. A arte de Knack II usada pela instalação compilada é obtida do dump local do proprietário e não acompanha os fontes deste port.

Knack e Knack II pertencem aos respectivos titulares, incluindo Sony Interactive Entertainment. Nenhum crédito aqui implica endosso ou vínculo oficial.
