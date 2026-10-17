# Knack 2 para Windows — primeiro boot visual

Runtime experimental para **CUSA08014 / 01.01**, derivado de `iss_pc`: executa o código x86-64 original e traduz gráficos para Vulkan. O perfil confere os hashes dos dois executáveis em [profiles/CUSA08014-01.01.json](profiles/CUSA08014-01.01.json).

**Marco local de 17/10/2026:** tela de título de Knack II confirmada em dois boots, com mapa, logotipo e copyright. Capturas separadas por 63,243 segundos no primeiro e 34,500 segundos no segundo. Continuação: glifos corrigidos; seleção Single Player / Normal carregou **Chapter 1-1 — The Invasion**, em Newhaven, com HUD, tutorial de movimento e menu de pausa respondendo a Options. Correção gráfica posterior restaurou o cenário inicial: ruas, prédios, personagem, iluminação e fumaça visíveis, com duas capturas separadas por 57,162 segundos, movimento e pausa verificados em execução normal. Jogabilidade da fase inteira ainda não está validada. Máquina: Windows, Ryzen 7 5700G, RX 6650 XT, 32 GB RAM. Áudio completo, desempenho e Linux permanecem sem validação.

| Camada | Evidência local |
| --- | --- |
| Build | MSYS2 CLANG64, RelWithDebInfo, LTO desligado; `build.ps1` concluído |
| Imports | `eboot.bin`: zero ausentes; `game.elf`: 61 ainda ausentes na análise completa |
| CPU | `eboot.bin` solicita `game.elf`; supervisor inicia processo limpo preservando quatro argumentos |
| SaveData | GetParam lê 1328 bytes; UmountWithBackup copia arquivos e publica evento de conclusão |
| Apresentação | Duas inicializações com conteúdo real observado por mais de 30 segundos; encerramento previsto por timeout |
| Texto | Prompt completo, menus e tutorial legíveis; âncora acumulada corrigida em `FontWritingStep+0x20` |
| Primeira fase | Cenário inicial do Chapter 1-1 restaurado; movimento e pausa funcionais; duas capturas separadas por 57,162 segundos |

Nenhum dos 61 imports ausentes interrompeu a transição observada até a primeira fase. Isso não comprova compatibilidade do jogo inteiro. Os testes partiram do `SAVEDATA00` já existente; primeiro boot sem save ainda requer validação no jogo.

A falha gráfica tinha duas causas: limpeza do buffer MSAA de transparências não aplicada à imagem Vulkan (alpha zero apagava a composição) e descritores de vértices antigos reutilizados após atualização do guest. A correção propaga o padrão real de limpeza e verifica os descritores antes de reutilizar a preparação. Evidências locais em `out/knack2/gpu-verified/`, com hashes em `visual-verification.json`; diagnóstico detalhado na seção 6 de [HANDOFF_ASTRA.md](HANDOFF_ASTRA.md). Capturas, dump e saves ficam fora do Git.

## Compilar

Instale MSYS2 CLANG64. O caminho padrão usado aqui é `%LOCALAPPDATA%\knack2-toolchain\msys64`; para outra instalação, defina `$env:MSYS2_ROOT = 'C:\msys64'` no PowerShell.

No shell **MSYS2 CLANG64**, instale as dependências:

```bash
pacman -S --needed git patch \
  mingw-w64-clang-x86_64-{toolchain,cmake,ninja,pkgconf,python} \
  mingw-w64-clang-x86_64-{vulkan-headers,vulkan-loader,vulkan-memory-allocator,glslang} \
  mingw-w64-clang-x86_64-{sdl3,fmt,boost,robin-map,xxhash,ffmpeg,zydis,freetype} \
  mingw-w64-clang-x86_64-{spirv-tools,spirv-headers,spirv-cross}
```

Na raiz do repositório, em PowerShell:

```powershell
git submodule update --init --recursive
py -3 -m venv .venv
.\.venv\Scripts\python.exe -m pip install iced-x86==1.21.0
.\build.ps1 -Jobs 6
```

Build validado com Python 3.14, Clang 23.1.3, CMake 4.4.4, Ninja 1.13.2, SDL3 3.4.18, FreeType 2.15.0 e FFmpeg 9.0.2. A lista completa de versões observadas fica em [docs/knack2-toolchain-packages.txt](docs/knack2-toolchain-packages.txt). O comando de instalação usa o catálogo do MSYS2, que pode mudar; a lista registra o ambiente validado e não constitui um espelho dos pacotes.

O binário fica em `port/out/tlg-probe.exe`; o log em `port/out/gpu-build.log`. O build aplica os patches FSR já versionados em `port/gpu/patches/fsr-vulkan/`, deixando esse submódulo modificado por desenho. Os commits dos submódulos permanecem fixados pelo Git.

## Analisar e executar

Use seu próprio dump. Nenhum arquivo do jogo é distribuído pelo projeto.

```powershell
$game = 'C:\Users\dartv\Downloads\knack 2\CUSA08014'
.\.venv\Scripts\python.exe run.py --game-dir $game --analyze
.\.venv\Scripts\python.exe run.py --game-dir $game --check-imports
.\.venv\Scripts\python.exe run.py --game-dir $game --timeout 120
```

`--check-imports` retorna 1 enquanto houver símbolos ausentes, incluindo caminhos ainda não executados. `--timeout` limita a sessão de execução; código 124 indica limite atingido. Código 75 de `eboot.bin` é a transição esperada para `game.elf`. `--out` muda o diretório de dados gerados, padrão `out/knack2`.

O dump é lido diretamente. Imagens preparadas, logs, caches e saves ficam separados, sem duplicar os aproximadamente 52 GB de recursos. Configuração inicial: janela, upscaler desligado e perfil local/offline. A janela se ajusta à área útil do monitor.

- Saves: `out/knack2/user/savedata/1/CUSA08014/`.
- Backups: `out/knack2/user/savedata-backups/1/CUSA08014/SAVEDATA00/`; `current.txt` aponta para a última geração concluída. Gerações anteriores são preservadas.
- Evidências: `out/knack2/session-*/`, com log por executável e `session.json`.
- O supervisor mantém `visual_verified: false`, pois não inspeciona pixels. A confirmação visual fica em `visual-verification.json`, produzido após inspeção das capturas; não é inferida de janela aberta ou compilação de shaders.

A restauração automática de backups e as demais APIs SaveData não chamadas neste trecho ainda não foram implementadas. O backup é síncrono e não declara sucesso antes da cópia completa.

### Entrada e captura para diagnóstico

`--pad-file` habilita explicitamente o arquivo de entrada já suportado por `scePad`. O arquivo deve existir antes de iniciar; tokens como `options`, `cross` e `ly=0` permanecem pressionados até limpar seu conteúdo. `--capture-dir` habilita a captura existente do renderer: criar `capture.request` solicita o próximo frame apresentado.

```powershell
New-Item -ItemType Directory -Force out/knack2/debug | Out-Null
Set-Content -Encoding ASCII out/knack2/debug/pad.txt ''
.\.venv\Scripts\python.exe run.py --game-dir $game --timeout 600 --pad-file out/knack2/debug/pad.txt --capture-dir out/knack2/debug/frames
```

Em outro terminal, na raiz do repositório, após a tela estabilizar:

```powershell
Set-Content -Encoding ASCII out/knack2/debug/pad.txt 'options'
Start-Sleep -Milliseconds 500
Set-Content -Encoding ASCII out/knack2/debug/pad.txt ''
Set-Content -Encoding ASCII out/knack2/debug/frames/capture.request 'capture'
```

Os arquivos `output_NNN_LARGURAxALTURA.raw` capturam a apresentação. Na execução validada, usam RGBA de 8 bits por canal; decodifique com FFmpeg `-f rawvideo -pixel_format rgba -video_size LARGURAxALTURA -i ARQUIVO.raw -frames:v 1 captura.png`. A imagem deve ser inspecionada junto do log. Nenhum comando é injetado por padrão.

## Testar

```powershell
$tc = if ($env:MSYS2_ROOT) { $env:MSYS2_ROOT } else { "$env:LOCALAPPDATA\knack2-toolchain\msys64" }
$env:PATH = "$tc\clang64\bin;$env:PATH"
& "$tc\clang64\bin\clang.exe" -std=c11 -Iport/src -Iport tests/test_knack_loadexec.c port/src/runtime_knack.c -o out/knack-loadexec-test.exe
.\.venv\Scripts\python.exe -m unittest discover -s tests -v
.\.venv\Scripts\python.exe -m unittest discover -s port/tests -p test_prepare.py -v
.\.venv\Scripts\python.exe -m unittest discover -s port/tests -p test_link_modules.py -v
.\.venv\Scripts\python.exe -m unittest discover -s port/tests -p test_link_libc.py -v
```

Verificam protocolo LoadExec, argumentos e caminhos com espaços, timeout, isolamento de opções, parser SELF, relocações, TLS, metadados SaveData, limites de buffers, erros de corrupção, eventos e preservação de gerações de backup. O contrato de fontes testa os offsets lidos pelo guest, espaços, linha de base e unidades em pixels usando FreeType e a fonte DejaVuSans redistribuída no projeto. Os testes SaveData usam arquivos sintéticos em pasta temporária, sem acessar saves reais. São 29 execuções de teste nesses comandos; uma verificação de módulo é executada pelas duas suítes de link. Nesta continuação, os 12 testes de `tests/` passaram; parser e linker conservam a validação anterior.

Regressões gráficas desta etapa (após o build e com CLANG64 no PATH):

```powershell
cmake --build port/out/gpu --target buffer-clear-test frame-capture-test async-fetch-snapshot-test -j 6
.\port\out\gpu\buffer-clear-test.exe
.\port\out\gpu\frame-capture-test.exe
.\port\out\gpu\async-fetch-snapshot-test.exe
```

Os três testes passaram: invalidação de limpezas por escritas sobrepostas, descritores mutáveis de vértices e captura delimitada pelo flip GPU. A validação visual usou o launcher normal, sem desativar otimizações ou forçar barreiras globais.

## Bases e créditos

- Base principal: [HayatoG/iss_pc — d65fa53](https://github.com/HayatoG/iss_pc/tree/d65fa53a1e7cfe71c8eb402b1e6526fa835ef5df). README original preservado integralmente em [README.upstream.md](README.upstream.md); créditos e histórico mantidos.
- Referência Windows/diagnóstico: [FelipePrado0/gow3_pc — 1436e7e](https://github.com/FelipePrado0/gow3_pc/tree/1436e7e79807de8e832fc1b6da2f8f20a4293172).
- Referência gráfica: [deadinside28/bloodborne_pc — 3c57da4](https://github.com/deadinside28/bloodborne_pc/tree/3c57da4198ad62039f63f33bfd205b9916518acf). Nenhuma correção adicional desta referência foi importada nesta etapa SaveData.
- Contratos ABI SaveData: [shadPS4 — e6c79a5](https://github.com/shadps4-emu/shadPS4/tree/e6c79a5fe0ea2f3d03fcb9c5c50cf61af063f3f5/src/core/libraries/save_data). A implementação de backup em diretórios é local.

Licenças preservadas em [port/LICENSE](port/LICENSE), [port/gpu/shadps4/LICENSE](port/gpu/shadps4/LICENSE) e nas dependências. Os novos arquivos usam SPDX GPL-2.0-or-later quando aplicável. Não incluir dump, saves, imagens preparadas ou recursos proprietários em distribuições.

Estado técnico, capturas locais, avisos e fronteira para a próxima etapa: [HANDOFF_ASTRA.md](HANDOFF_ASTRA.md).
