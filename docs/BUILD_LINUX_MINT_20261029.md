# Build e validação no Linux Mint — 29/10/2026

Continuação autorizada pelo usuário após o diagnóstico de 28/10. Trabalho
iniciado na área com sufixo `20261028` e concluído após a mudança de data UTC.
**Build Linux atual aprovado, sem alteração de fontes do port.** Nenhum jogo
foi iniciado; as provas abaixo não constituem boot ou gameplay.

## Área de trabalho e dependências

Área persistente exclusiva no SSD:

```text
/media/mint/0C3CE8FC3CE8E226/knack-linux-mint-20261028
```

A sessão retorna `NoNewPrivs=1` e `sudo -n true` foi negado. A preparação foi
feita em espaço de usuário: **57 pacotes Ubuntu noble baixados e extraídos**,
sem instalação no banco dpkg, sem atualizações do sistema e sem troca de driver,
Mesa, kernel ou glibc. Downloads iniciais de pacotes: aproximadamente 101 MB,
mais os headers complementares de SDL. Arquivos extraídos e seus metadados
pertencem somente à área acima. Nenhum script de manutenção de pacote foi rodado.

A área NTFS passou no teste de criação/leitura de links simbólicos. Foi usada
como saída externa, sem montar imagem, formatar, reparticionar ou ocupar o
HOME temporário do Live com o build completo. Tamanho final aproximado: 1,9 GiB.
Não houve cópia parcial do checkout: o CMake leu os fontes originais completos,
com submódulos e patches existentes, e escreveu em `build/` externo.

Ferramentas disponíveis nesse prefixo: CMake 3.28.3, Ninja 1.11.1, GCC/G++ 14.2.
O C do port foi compilado pelo GCC 13.3 nativo e C++ pelo G++ 14.2 extraído.
Foi mantida a configuração autoritativa `port/gpu/CMakeLists.txt`,
`RelWithDebInfo`, `TLG_PGO=off`, `TLG_LTO=OFF`, quatro tarefas paralelas.

SDL3 **3.4.18** foi obtido da [release oficial](https://github.com/libsdl-org/SDL/releases/tag/release-3.4.18),
com SHA-256 conferido contra o digest do asset publicado:

```text
9c75cf16330322c217dedd2e0609f1124f1b54b8633e763467b4684d0f4334a3
```

Compilado e instalado somente em `prefix/`: X11/Vulkan, ALSA/PulseAudio dinâmicos,
joystick Linux/HIDAPI. SDL Wayland, KMSDRM e OpenGL/OpenGLES desativados neste
build de desenvolvimento; não é validação de Wayland ou de todas as opções SDL.
O venv Linux contém iced-x86 1.21.0, PySide6-Essentials 6.12.0 e shiboken6 6.12.0.
A `.venv` Windows existente não foi reutilizada.

`run-build.py` na área aplica PATH, includes, bibliotecas e pkg-config apenas ao
processo filho. Os `.pc` extraídos foram ajustados para o prefixo local e links
locais apontam às bibliotecas runtime já presentes no Mint. Isso evita instalar
pacotes no sistema, mas **não produz um pacote autônomo ou redistribuível**.
Não mover esse prefixo nem exportar suas variáveis globalmente sem revalidar.

## Resultados atuais

| Gate | Evidência |
| --- | --- |
| Configure CMake | aprovado, `logs/runtime-configure.log` |
| Build completo | 301 etapas, saída 0, `logs/runtime-build.log` |
| ELF x86-64 | `build/tlg-probe` e `build/libtlggpu.so` gerados |
| CPU atual | `--cpu-features`: `CPU: complete`, saída 0; anuncia os dois layouts aceitos |
| Bibliotecas ELF | `ldd` sob o ambiente local sem dependências ausentes |
| Símbolos glibc dos dois ELF principais | maior versão requerida: GLIBC_2.38; host usa 2.39 |
| Smoke Vulkan do runtime | RX 6650 XT/RADV, envio de comandos + readback de 4.096 bytes PASS |
| Backend Vulkan sem guest | dispositivo lógico e VMA criados por `Vulkan::Instance`, saída 0 |
| Launcher demo | Qt offscreen abriu e encerrou após 1.200 ms, saída 0 |
| Freeze nativo | `freeze-test`, saída 0, `freeze report tests passed` |
| Boot/gameplay/FPS/áudio audível | não executados / não medidos |

O smoke escolheu explicitamente o ICD RADV **somente no processo de teste**:
`/usr/share/vulkan/icd.d/radeon_icd.x86_64.json`. Não foi alterada a seleção global
de driver, e llvmpipe não serviu como evidência de GPU física.

O diagnóstico do backend usa a classe real `Vulkan::Instance` e os stubs
existentes de regressão GPU, que abortam acessos a guest. Está em
`diagnostic-src/backend-device-smoke.cpp`, fora do checkout. Cria dispositivo
lógico e alocador com as verificações do backend, sem janela, presenter,
processador de comandos guest ou dump. **Não é inicialização do renderer completo.**
O backend advertiu sobre extensões opcionais NV/memória e suporte incompleto a
alguns formatos SNORM/SINT/RGB32; avisos foram preservados, sem relaxar validações.

## Testes focados

**104 testes considerados: 100 aprovados, quatro explicitamente não aplicáveis;
zero falhas/erros nos conjuntos finais.** `freeze-test` é um diagnóstico nativo
adicional, separado dessa contagem.

| Conjunto | Total | Resultado |
| --- | ---: | --- |
| test_knack2_linux.py | 11 | aprovados |
| test_knack2_runner.py | 28 | aprovados |
| test_knack2_storage.py | 8 | aprovados |
| test_knack2_regions.py | 12 | aprovados |
| test_knack2_usa100.py | 8 | aprovados |
| test_knack2_identity.py | 8 | quatro aprovados, quatro skips |
| test_savedata.py | 1 | aprovado, harness C e dados sintéticos |
| test_runtime_memory_snapshot.py | 1 | aprovado, harness C |
| test_knack2_dialogue_gain.py | 1 | aprovado, stubs nativos exatos |
| port/tests/test_runtime.py | 7 | aprovados, harness atual compilado |
| port/tests/test_sema.py | 4 | aprovados, harness atual compilado |
| test_knack2.py | 15 | aprovados, inclui LoadExec nativo e rejeição de paths |

Os quatro skips de identidade cobrem identidade shell Windows e três verificações
que exigem um par explícito de artefatos runtime Windows. Não são apresentados
como testes aprovados. Os harnesses `runtime-test`, `sema-test`, `freeze-test` e
`knack-loadexec-test` foram compilados pelo CMake atual (93 etapas adicionais).
Dados de SaveData e processos foram sintéticos; saves reais não foram lidos
nem migrados. A configuração demo Qt usou diretório de dados isolado.

## Ocorrências durante a preparação

As primeiras execuções USA100/identidade falharam por PySide6 ausente. Foram
repetidas após preparar o venv, com os resultados acima. O primeiro build SDL
falhou por um include multiarch D-Bus ainda apontando para `/usr`; o ajuste dos
`.pc` extraídos e um configure SDL novo corrigiram o prefixo, sem editar SDL ou
fontes do port. Os logs de tentativas foram mantidos.

O primeiro diagnóstico externo do backend construiu o dispositivo, mas terminou
em assertion ao consultar diretamente Vulkan-Hpp pelo executável de teste
(`d.getVkHeaderVersion() == VK_HEADER_VERSION`). O harness foi corrigido para
usar somente chamadas exportadas do backend, sem instanciar esse acesso inline
separado. A causa exata do dispatcher externo não foi isolada; não atribuir essa
falha ao driver ou ao jogo. Nenhuma assertion do backend foi retirada. A seleção
real de headers fixados 1.4.354 foi confirmada por preprocessamento em
`backend-header-selection.log`. O segundo diagnóstico concluiu com saída 0.

A invocação inicial de `knack-loadexec-test` sem argumentos retornou 2, conforme
sua checagem de uso. A execução correta ocorreu pelo teste existente
`test_native_loadexec_roundtrip_and_rejection` e passou. Esse retorno de uso não
foi contado como regressão nem como teste aprovado.

## Reprodução e evidências

Na área externa, para reconstruir sem iniciar o jogo:

```bash
python3 run-build.py cmake --build build --target tlg-probe --parallel 4
python3 run-build.py ./build/tlg-probe --cpu-features
```

`logs/runtime-configure-command.json` registra os argumentos exatos do configure;
`logs/backend-device-build-command.json` e `vulkan-smoke-build-command.json`
registram os diagnósticos. Manifestos de dependências, hashes SHA-256 dos ELF,
versões Python, baseline Git e resumo final de testes ficam em `logs/`.
O arquivo `README.md` da área explica os caminhos e as dependências do host.

Branch/HEAD preservados: `experiment/gpu-small-sync-prebind-20261020`,
`3d23fb71a48e290e0387d2d80e4c04bf6babdb17`. Não houve stage, commit, merge, push
ou publicação. A conferência de conteúdo antes da documentação final não
identificou alterações em arquivos anteriores; entradas completas do índice
(paths, modos, estágios e IDs de objetos) iguais à baseline desta etapa.
Mudanças documentais finais: este relatório novo e seção no topo do handoff;
histórico anterior preservado. `run.py:33` mantém whitespace preexistente.

## Próximo gate

Validar inicialização do renderer completo sem guest, distinguindo-a do device
smoke atual. Depois, preparar uma proposta concreta de sessão real finita com
runtime atual, dump local validado, perfil/saída novos e dados de usuário de
teste. Não reutilizar aprovação WSL antiga. Boot, controles, áudio audível,
saves reais, campanha e FPS continuam sem prova Mint; AppImage/publicação
permanecem posteriores. A sessão Live precisa ser revalidada após reboot,
mesmo com fontes e artefatos preservados no SSD.
