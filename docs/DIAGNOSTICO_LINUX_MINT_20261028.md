# Diagnóstico Linux Mint — 28/10/2026

Retomada por Codex a partir de `CONTINUAR_LINUX_MINT.md`. Esta etapa avaliou
ambiente e viabilidade sem configurar ou compilar o projeto. Evidências locais
persistentes: `scratch/mint-diagnostico-20261028/`, ignorado pelo Git. Não contém
dump, assets ou saves. Resultados abaixo são desta sessão Mint, salvo indicação
expressa de histórico WSL.

## Ambiente e armazenamento

| Item | Observação atual |
| --- | --- |
| Sistema | Linux Mint 22.3 Zena, base Ubuntu noble, x86-64 |
| Kernel / sessão | 6.14.0-37-generic / X11 |
| glibc | 2.39 |
| CPU | Ryzen 7 5700G, 8 núcleos / 16 threads |
| RAM / swap | 31 GiB totais; aproximadamente 21 GiB disponíveis; sem swap |
| Checkout | Diretório original no volume Windows, montado em NTFS/fuseblk, leitura/escrita |
| SSD persistente | Volume do checkout: 953 GiB, aproximadamente 245 GiB livres |
| HOME e raiz Live | Overlay `/cow`, 16 GiB, aproximadamente 12 GiB livres |
| Outros volumes | Ventoy exFAT de 7,2 GiB; nenhuma área ext4 persistente montada identificada |

Não foi encontrada confirmação de persistência do HOME Live. A linha de boot
contém `boot=casper` sem opção de persistência; tratar HOME, instalações e venvs
da sessão como temporários. O checkout e estes registros ficam no SSD NTFS.
Nenhuma montagem, alteração de partição ou cópia completa do checkout foi feita.
Não usar os 12 GiB temporários como destino padrão de um build completo.

## Baseline Git e preservação

- Branch: `experiment/gpu-small-sync-prebind-20261020`.
- HEAD: `3d23fb71a48e290e0387d2d80e4c04bf6babdb17`.
- Status inicial bruto: **1.098 entradas** (132 exclusões, 828 modificações,
  24 adições no índice, 4 submódulos sujos, 1 adição também modificada e
  109 entradas não rastreadas). Total de caminhos adicionados ao índice: 25.
- A consulta temporária `git -c core.autocrlf=true status --short`, sem mudar
  configuração, apresentou **367 entradas**. A referência Windows tinha 366
  antes da criação do documento de retomada. A proximidade é consistente com
  CRLF, mas não certifica identidade integral com um snapshot externo ausente.
- `core.filemode=false`, `core.symlinks=false`, `core.ignorecase=true` já estavam
  configurados; `core.autocrlf` estava ausente. Não alterei essas opções.
- Exemplos `.gitmodules`, `port/src/cpu_check.c`, `imgui/imgui.h` e a licença
  FSR-Vulkan são idênticos ao HEAD após normalizar apenas CRLF em memória.
  Não normalizei nem regravei nenhum fonte.
- Na verificação final, o hash binário de `.git/index` diferiu do inicial.
  Não foi executado add/stage/restore nem alteração de configuração; a lista
  dos 25 caminhos e tipos de adição estagiados permaneceu igual. A causa da
  regravação do índice não foi isolada; não afirmar preservação byte a byte
  do índice ou atribuir automaticamente o evento a um refresh do VS Code.
  Nenhuma restauração do índice foi tentada. O status final bruto foi 371,
  enquanto a consulta com CRLF passou de 367 para 368, acrescentando somente
  este relatório. O conteúdo dos 1.127 arquivos anteriores foi reconferido:
  só o handoff mudou, pela inclusão desta seção de diagnóstico.
- O JSON de baseline preserva os status bruto e com CRLF, nomes no índice,
  SHA-256 do índice e hashes de 1.127 arquivos existentes rastreados/não
  ignorados do repositório principal. Não é backup integral do checkout;
  não inclui conteúdos ignorados nem hashes de todos os arquivos dos submódulos.

Todos os sete submódulos estão inicializados e coincidem com as revisões do
documento de retomada:

| Submódulo | SHA atual |
| --- | --- |
| fsr-vulkan | c64f093404125e960813f95f1996c14780f0dd69 |
| imgui | f1cc2ae15e53a861a874c3034aae6798fde194ab |
| magic_enum | e046b69a3736d314fad813e159b1c192eaef92cd |
| miniz | 2948eebafc5b9f1c69bebed80333e403a09bd62d |
| vulkan-headers | 01393c3df0e5285b54ee6527466513f9e614be94 |
| xbyak | cd6bad3cad2e07a3e277c23d2b1f065320024717 |
| LibAtrac9 | efca2e3af35562a09a9bb6deed90e45b4b824dc4 |

FSR-Vulkan tem 756 entradas brutas, reduzidas a **seis patches locais** na
consulta com CRLF: `src/ffx_vk_fsr4_v07.c`, backend `ffx_vk.cpp`, componentes
`ffx_frameinterpolation.cpp`, `ffx_fsr3upscaler.cpp`, `ffx_opticalflow.cpp` e
`ffx_message.cpp` do SDK 1.1.4. Preservados; sem update de submódulos.

`git diff --check` considerando CRLF já encontrava whitespace preexistente em
`run.py:33`. Essa falha não foi corrigida nesta tarefa documental.

## GPU e Vulkan: evidência atual

`lspci -nnk`: Radeon Navi 23, PCI `1002:73ef`, driver kernel **amdgpu**.
`vulkaninfo --summary` concluiu com código 0:

| Campo | Resultado |
| --- | --- |
| Dispositivo físico | AMD Radeon RX 6650 XT (RADV NAVI23), discrete GPU |
| Vulkan anunciado pelo dispositivo | **1.4.305** |
| Driver | RADV, Mesa 25.0.7-0ubuntu0.24.04.2 |
| Loader / instância | 1.3.275 |
| Segundo dispositivo | llvmpipe LLVM 20.1.2, software; inadequado para medir a Radeon |

`VK_DRIVER_FILES`, `VK_ICD_FILENAMES`, `LD_LIBRARY_PATH`, `VK_LAYER_PATH` e
`MESA_LOADER_DRIVER_OVERRIDE` estavam ausentes. Não houve herança do Dozen.

O requisito de versão 1.3 em `vk_platform.h` é atendido. Isso elimina o bloqueio
de versão observado no WSL/Dozen; enumeração não prova criação do renderer,
submissão/readback, boot ou desempenho. Nenhum smoke com envio de comandos nem
inicialização do backend completo foi executado nesta etapa.

## Dependências e viabilidade de build

Instalados: GCC/G++ **13.3.0**, Python **3.12.3**, pkg-config **1.8.1** e
build-essential. Ausentes: CMake, Ninja, iced_x86, PySide6 e pip. O módulo
Python `venv` existe, mas o pacote `python3-venv`/ensurepip não está instalado.
A `.venv` do checkout Windows não deve ser reutilizada como ambiente Linux.

As consultas pkg-config para Vulkan, SDL3, FFmpeg, FreeType, fmt, xxHash,
Zydis e X11 falharam por ausência dos pacotes de desenvolvimento. O loader
Vulkan e Mesa funcionais já estão presentes; isso difere dos headers/dev.

Candidatos do cache APT atual (sem atualizar índices ou instalar):

| Dependência | Candidato / conclusão |
| --- | --- |
| CMake / Ninja | 3.28.3 / 1.11.1, disponíveis |
| GCC/G++ 14 | 14.2.0-4ubuntu2~24.04.1, disponíveis em noble-updates |
| SDL3 dev | `libsdl3-dev` sem pacote/candidato no cache consultado |
| glslang-tools | 15.1.0 |
| Vulkan dev | 1.3.275; headers fixados 1.4.354 também presentes no submódulo |
| FFmpeg dev | 6.1.1 |
| FreeType / fmt / Boost | 2.13.2 / 9.1.0 / 1.83 |
| robin-map / xxHash | 1.2.1 / 0.8.2 |
| VMA / Zydis | 3.0.1 + snapshot / 4.0.0 |
| X11 dev / python3-venv | disponíveis |

O shader recompiler compilado usa `std::ranges::contains`
(`lower_wave64_pass.cpp`), ausente nos headers GCC 13 instalados. GCC 14 é o
primeiro candidato a testar com a biblioteca padrão correspondente; não basta
ativar `-std=c++23` no GCC 13. A documentação oficial registra essas adições em
[GCC 14](https://gcc.gnu.org/gcc-14/changes.html). Não houve teste de compilação.

A simulação `apt-get --simulate --no-install-recommends install` para os vinte
pacotes comuns listados em `pacotes-candidatos.txt` (exceto SDL3, indisponível)
resolveu **49 instalações novas e 32 atualizações, zero remoções**. Inclui
atualizações de Python, libstdc++ e X11. Não selecionou Mesa, kernel ou glibc
para atualização. É uma proposta baseada no cache atual, não uma instalação
aprovada nem garantia de disponibilidade dos downloads. Lista completa em
`apt-simulacao.txt`; não reutilizar como autorização de instalação.

Alternativa mínima para SDL3: avaliar fonte oficial com versão e hash fixados,
build em diretório próprio e instalação num prefixo local, apontado só para os
comandos de build por `CMAKE_PREFIX_PATH`/`PKG_CONFIG_PATH`. A seleção da versão
e das dependências X11, áudio e udev ainda precisa ser fechada, conforme a
[documentação SDL para Linux](https://wiki.libsdl.org/SDL3/README-linux).
Não trocar SDL3 por SDL2 nem adicionar repositórios de outra distribuição.
Compatibilidade de FFmpeg 6.1, fmt 9, VMA e Zydis precisa de configure/build real;
a existência do pacote não garante compatibilidade com os fontes atuais.

O pacote histórico Ubuntu 26.04/glibc 2.43 não é candidato validado para este
Mint/glibc 2.39. Não foi executado nem usado para substituir bibliotecas.

## Menor próximo gate

1. Definir uma área Linux persistente para cópia completa dos fontes, build,
   prefixo SDL3 e venv. Um volume ext4 externo existente é uma opção. Se o único
   espaço disponível for NTFS, avaliar separadamente um arquivo-imagem ext4
   de tamanho limitado no SSD, sem alterar partições. Nada disso foi criado.
   Preservar também arquivos novos e metadados/patches dos submódulos; não
   reconstruir a árvore apenas por clone ou pelos arquivos rastreados.
2. Fechar o plano de dependências Mint com o usuário, incluindo custo das
   atualizações propostas e SDL3 local. Revalidar candidatos, versões e tamanho
   antes de instalar. Não há necessidade demonstrada de trocar driver ou glibc.
3. Com espaço e dependências resolvidos, configurar build novo externo via
   `port/gpu/CMakeLists.txt`, C++ com `g++-14`, `TLG_PGO=off`, `TLG_LTO=OFF`,
   `RelWithDebInfo`. `port/out-linux` não existe; `port/out` Windows existe e
   deve ser preservado. Começar conservadoramente com quatro jobs e observar
   memória/espaço. Nenhum comando CMake foi executado nesta etapa.
4. Compilar `tlg-probe`; executar `--cpu-features`; depois rodar testes
   `test_knack2_linux.py`, `test_knack2_runner.py`, `test_knack2_storage.py`
   e de identidade/perfis. Usar Python/venv Linux, dados sintéticos e saídas
   isoladas; não presumir aprovação pelos testes históricos Windows/WSL.
5. Validar dependências ELF, launcher ocioso, smoke Vulkan na Radeon e renderer
   completo sem guest. Só depois propor sessão real finita, com nova autorização,
   dump validado, perfil/saída de teste e sem saves reais.

## Separação das provas

| Gate | Resultado desta sessão |
| --- | --- |
| Ambiente e acesso ao checkout | confirmados |
| Baseline e revisões de submódulos | registradas; identidade integral com Windows não certificada |
| Enumeração física e requisito Vulkan 1.3 | atendidos pela RX 6650 XT/RADV |
| Build atual | não iniciado; dependências e armazenamento por resolver |
| Testes sintéticos | não executados nesta etapa |
| Smoke de comandos Vulkan / renderer | não executados |
| Boot, áudio, controles, gameplay e FPS | não executados / não medidos |

O build, smokes e boot de 24/10 pertencem ao ambiente WSL e aos fontes daquela
data. O sucesso gráfico/áudio/controles documentado no Windows para perfis
regionais não constitui validação Linux. AppImage e publicação permanecem
posteriores ao build e à compatibilidade.
