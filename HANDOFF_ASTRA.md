# HANDOFF_ASTRA.md

> Atualização de 17/10/2026: **cenário inicial do Chapter 1-1 restaurado**, com
> iluminação, geometria, movimento e pausa verificados. A seção 6 registra as duas
> causas gráficas corrigidas, testes e capturas em execução normal.
> Marcos anteriores `09e6ccc` e `1e13acc` preservados em `knack2-windows`.
> Seções 1–5 preservam o histórico; restrições e diagnósticos antigos não descrevem
> o estado atual. Alterações gráficas desta etapa foram expressamente autorizadas.

## 7. Validação durante ação — 17/10/2026

Teste encerrado a pedido do usuário, após combate inicial, transição cinematográfica
e retorno ao gameplay na área interna do Chapter 1-1. Correções gráficas continuam
salvas em `44bf1e6`; esta etapa não alterou código nem recompilou o runtime.

### Arquivos e interfaces

Único arquivo rastreado alterado nesta etapa: `HANDOFF_ASTRA.md`. Nenhuma interface
ou header novo. Scripts locais ignorados `out/action-play.py` e
`out/launch-action-stats.py` exercitaram o pad e a captura já existentes. Capturas
e sequência de entradas: `out/knack2/action-check/`, incluindo `actions.jsonl`.
Preservados `.vscode/` e patches preexistentes do submódulo `fsr-vulkan`.

### Evidência observada

Sessão principal com telemetria: `out/knack2/session-20261017-195151/01-game.elf.log`.
Launcher local adicionou `TLG_FRAME_STATS=1`, `TLG_PERF_DIAG=1` e
`TLG_AUDIO_STATS=1`, mantendo as otimizações normais. Capturas individuais do display,
sem trace de passes. Teste usou pausas entre ações; estatísticas da sessão inteira
não são um benchmark contínuo de combate.

- `r-giant-strike.png`: combate com blindado, dano e fragmentos de relíquias visíveis.
- `r-giant-kick.png`: explosão/fumaça após combate e recuperação do tamanho do personagem.
- `r-after-giant.png`: sequência cinematográfica com robô sobre veículo.
- `r-cutscene-exit.png` e `r-small-exit.png`: retorno ao controle com Knack pequeno,
  passagem pelo duto e tutorial Size Up na área interna.
- `r-interior-walk.png`: tamanho restaurado a 13 pés e cenário interno iluminado.
- `r-interior-fight.png`, `r-interior-combo.png` e `r-kick-close.png`: novo combate,
  destroços, partículas, dano e recuperação/retomada. Capturas PNG inspecionadas.

Não houve novo import `STOP`, assertion ou device lost identificado no trecho
observado. Processo de teste PID 28976 encerrado manualmente após o pedido de parar;
eventual exit de término forçado não representa crash espontâneo.

### Desempenho e anomalias para investigação

- Amostras durante a sequência de ação, linhas 7308 e 7381 do log: 59,8 e 59,2 FPS,
  pior frame de 33,5 e 66,7 ms, respectivamente. Houve 1/2 compilações, somando
  23,7/50,6 ms. Não certificar 60 FPS estáveis em toda a fase.
- Nessas amostras, `buffer_cache.cpp:350` concentrou 2030,020/1759,789 ms de espera
  acumulada por janela de aproximadamente 5 s, em 430/432 chamadas; máximos por
  chamada de 13,061/13,871 ms. Leitura do código confirma `scheduler.Finish()` em
  `BufferCache::DownloadMemory`. É candidato concreto a investigação de readback;
  esses totais não equivalem diretamente a tempo extra por frame. Nenhuma mudança
  em sincronização, ABI, memória ou hooks foi feita nesta etapa.
- Transição cinematográfica teve amostras a 30 FPS e uma janela a 27,3 FPS,
  pior frame de 366,5 ms, com 15 compilações somando 580,5 ms (linha 8050).
  Correlação registrada; causa exclusiva do pico não foi isolada.
- AudioOut manteve buffers nas portas 0, 1 e 17, com `starved 0` nas amostras
  examinadas. Não foi feita escuta para certificar conteúdo ou sincronismo sonoro.
  Nenhum evento de inicialização/erro de Ajm foi identificado; sua cobertura
  permanece não demonstrada, mesmo com AudioOut ativo.
- AvPlayer abriu vídeos de UI e `st01/mv_01a_020a_mv_02a_010a.mp4` durante o boot;
  há saídas normais dos decoders do primeiro vídeo. O aviso SetLogCallback STUBBED
  permanece. A cutscene posterior foi vista, mas não foi atribuída a uma nova
  abertura do AvPlayer; não extrapolar sua validação para todos os vídeos.
- Avisos de pressão de memória da texture cache persistem (por exemplo,
  2806 MiB sobre orçamento de 2582 MiB, limiar crítico 5165 MiB). Sem device lost
  observado. Vulkan validation layers não estavam habilitadas.

Quebra específica de Sunstones **não confirmada**. Nomes antigos de capturas com
`sunstone` não comprovam a ação. Persistência de um novo checkpoint após reiniciar
também não foi retestada. Próxima etapa deve partir destes limites, sem repetir
longas tentativas de combate automaticamente. Usuário considerou suficiente a
quantidade de testes deste marco.

## 6. Correção gráfica do Chapter 1-1 — 17/10/2026

### Diagnóstico confirmado

Branch `knack2-windows` sincronizada com `git merge --ff-only flash-boot-debug` (já atualizada, HEAD inicial `1e13acc`). `09e6ccc` e `1e13acc` preservados. Escopo gráfico e runtime GPU autorizados expressamente nesta etapa; ABI, memória virtual, hooks, fontes e SaveData não foram editados.

Correções e testes salvos em `44bf1e6`: `fix(gpu): restore Chapter 1-1 transparency and vertex geometry`.
O usuário relatou 60 FPS após essa correção. A validação anterior não mediu frame times;
esse relato não certifica desempenho durante combate, destruição ou transições.

Duas falhas distintas estavam ocultas pela apresentação preta:

1. **Limpeza do buffer de transparências MSAA.** O compute `0299e222` repete 16 bytes brutos em `0x10b4cb0000`, 17.694.720 bytes. Constante capturada: `00000000 3c000000 00000000 3c000000`, dois texels RGBA16F `(0,0,0,1)`. `TextureCache::RefreshImage` ignorava superfícies com `guest_samples > 1`, inclusive com MSAA desligado no host. O compute de composição `3c466c60` multiplica a cena opaca pelo alpha das transparências. Todos os 518.400 pixels chegavam com alpha zero, apagando uma cena já iluminada corretamente.
2. **Descritores de vértices preparados antes da atualização do guest.** `TryPreparedPipeline` compara os registros e user data achatados, mas `VertexAttribute::GetSharp` lê o V# através de um ponteiro para memória do guest. O conteúdo apontado pode mudar sem mudar o ponteiro nem os dados achatados. Reutilizar esses V#s antigos produzia triângulos gigantes e cenário deformado. Comparação durante a mesma execução: máscara de otimizações `0` reproduziu corrupção; `4096` (PreparedResources desligado) restaurou cenário; retornar a `0` reproduziu, retornar a `4096` restaurou novamente. A correção compara os V#s atuais antes de aceitar os inputs preparados e usa o caminho normal quando diferem.

As capturas completas de diagnóstico alteram o caminho de preparação de vértices e sincronizam readbacks; por isso um frame correto durante diagnóstico sozinho não validava estabilidade. A aceitação final usou execução normal, sem trace de passes ou máscaras especiais.

### Alterações e interfaces

- `port/gpu/shadps4/video_core/texture_cache/buffer_clear.h` (novo): registro de preenchimento uniforme, consumo por endereço/tamanho exatos e invalidação por sobreposição.
- `port/gpu/shadps4/video_core/texture_cache/texture_cache.h` e `port/gpu/shadps4/video_core/texture_cache/texture_cache.cpp`: `RecordBufferClear(VAddr, u64, const BufferClears::Pattern&)`; preserva limpeza pendente até criação/atualização da imagem; converte padrão RGBA16F repetido para clear Vulkan; cancela registro em escritas CPU/GPU, escrita em attachment/storage e unmap.
- `port/gpu/shadps4/video_core/renderer_vulkan/vk_rasterizer.cpp`: reconhece shader bruto verificado, valida cobertura total e constantes residentes no CPU; mantém dispatch original para consumidores do buffer; valida descritores de vértices antes de reutilizar preparação; trace agora inclui clear, máscaras e blend de alpha.
- `port/gpu/shadps4/shader_recompiler/frontend/fetch_shader.h`: `FetchShaderData::MatchesBuffers(const Info&, std::span<const AmdGpu::Buffer>) const`. Nenhum campo ou layout de struct modificado.
- `port/gpu/shadps4/video_core/renderer_vulkan/vk_frame_capture.h`, `port/gpu/shadps4/video_core/renderer_vulkan/vk_frame_capture.cpp` e `port/gpu/shadps4/core/libraries/videoout/driver.cpp`: `FrameCapture::OnGpuFlip(VAddr)`, ativado por `TLG_CAPTURE_GPU_FLIP=1`, delimita captura para jogos que copiam/compõem o display via compute; drena fila de draws antes de acessar entradas do trace.
- `port/gpu/shadps4/video_core/renderer_vulkan/vk_pipeline_cache.cpp`: serializa e deduplica dumps de shader por nome. Escritas concorrentes de shaders de cópia causavam sharing violations no Windows.
- `port/tests/test_buffer_clear.cpp` (novo), `port/tests/test_async_fetch_snapshot.cpp`, `port/tests/test_frame_capture.cpp`, `port/gpu/CMakeLists.txt`: regressões de invalidação, V# mutável com ponteiro constante e captura sem draw direto no display.
- `README.md` e `HANDOFF_ASTRA.md`: estado e evidências atualizados.

Interfaces já conectadas, sem trabalho pendente de integração. Sem alterações nos executáveis originais ou distribuição de dados do jogo. `.vscode/` e patches preexistentes do submódulo `fsr-vulkan` preservados.

### Hipóteses revistas

- Avisos de formatos A2B10G10R10 Snorm/Sint e R32G32B32 vêm da enumeração geral de capacidades. Esses formatos não aparecem nos attachments do frame examinado. G-Buffer usa RGBA8, RG16F, RGBA16F e A2B10G10R10 **Unorm**. Não aplicado remapeamento cego de 32 para 64 bits, que exigiria conversão de dados/strides.
- `RemoveAppendBufferLaneOffset`: caminho existente de offset zero encontra IAdd/ISub encadeado com mbcnt e substitui lane offset por zero. Revisão concluiu que a simplificação já ocorre; nenhuma alteração adicional. Dumps do caminho Continue examinado contêm DataAppend, sem DataConsume encontrado. Não alegar cobertura de todos os shaders/fases.
- Experimentos com barreiras globais, espera GPU antes de EOP/EOS e rastreamento extra de GDS não eliminaram o defeito de descritores. Alterações experimentais removidas; nenhum finish por frame ou bloqueio global foi mantido como correção.

### Evidência intermediária

- Frame trace baseline: `out/knack2/gpu-trace/trace/frame_20261017-184050_1.txt`; inputs/outputs brutos em `gpu-trace/passes/`.
- Antes da composição: `gpu-trace/hdr-081.png`; depois: `gpu-trace/hdr-082.png`. Essas imagens HDR são conversões diagnósticas, não prova do display.
- Primeiro frame apresentado após corrigir alpha: `out/knack2/gpu-fixed/stage-01.png`; sessão `session-20261017-185435`.
- `out/knack2/gpu-fixed/alpha-verification.json`: antes 518.400/518.400 alphas zero; depois 429.262 alphas 1, apenas 8 zero, média 0,926076. Valores intermediários preservam fumaça/partículas.
- A/B dos inputs preparados: sessão `session-20261017-190422`, imagens `gpu-gds/gds-only.png`, `no-prepared.png`, `prepared-again.png`, `no-prepared-02.png`. A alteração experimental GDS presente nessa sessão foi removida posteriormente.

### Validação final e reprodução

Build Windows `build.ps1 -Jobs 6` concluído; log `out/build-graphics-final.log`.
Três testes nativos passaram, registrados em `out/graphics-tests.log`:

- `buffer-clear-test`: correspondência exata, consumo único, atualização mais recente,
  invalidação por sobreposição parcial, intervalos adjacentes e padrão uniforme.
- `async-fetch-snapshot-test`: descritor muda com ponteiro constante, troca de endereço
  e stride, restauração e contagem; preserva snapshot imutável do compilador.
- `frame-capture-test`: frame sem draw direto no display, fronteiras de flip,
  buffer nulo ignorado e arquivo de captura produzido.

Execução final com padrões de `run.py`, sem máscara de otimizações, barreiras extras,
fences globais, captura de passes ou desativação da preparação assíncrona:

```powershell
.\.venv\Scripts\python.exe run.py --game-dir 'C:\Users\dartv\Downloads\knack 2\CUSA08014' --timeout 420 --pad-file out/knack2/gpu-verified/pad.txt --capture-dir out/knack2/gpu-verified/frames
```

O pad começa neutro (`lx=128 ly=128 rx=128 ry=128`). Após título estabilizado,
Options por 700 ms; após menu, Cross por 700 ms seleciona **Continue** no save local
existente. Um pulso `ly=0` por dois segundos move o personagem; Options abre pausa.
Solicitações individuais `capture.request` usam o readback de apresentação existente.
PNGs foram decodificados dos RAWs RGBA, sem redimensionamento, e inspecionados:

| Captura em `out/knack2/gpu-verified/` | Conteúdo observado |
| --- | --- |
| `title.png` | Título e prompt legíveis |
| `stage-01.png` | Knack, ruas, prédios, iluminação e fumaça visíveis |
| `stage-02.png` | Mesmo cenário completo, 57,162 segundos depois, sem triângulos esticados |
| `movement.png` | Personagem avançou e deslocou lixeira; geometria preservada |
| `pause.png` | NEWHAVEN / CHAPTER 1-1 / THE INVASION / NORMAL, com cenário visível |

Sessão `out/knack2/session-20261017-191255/`: `00-eboot.bin.log`, `01-game.elf.log`
e `session.json`. Nenhum novo import STOP, assertion, device lost ou crash observado.
`eboot.bin` saiu com 75 na transição esperada. Ao terminar a validação, o processo
do jogo foi encerrado manualmente; `game.elf` exit `4294967295` registra esse término
forçado, não uma falha espontânea. Nenhum processo do teste permanece aberto.

Hashes, intervalo entre RAWs e arquivos de evidência:
`out/knack2/gpu-verified/visual-verification.json`.
SHA-256 do runtime testado:
`a14c22c5eadf55a3a9ceef6aa22b66e903d3d1e44e5575194d68920da19a0fe5`.
O supervisor continua com `visual_verified: false`: não inspeciona pixels.

### Limites e anomalias restantes

Validação cobre o trecho inicial do Chapter 1-1, retomado de save existente,
com movimento curto e pausa. Não certifica a fase inteira, áudio, desempenho,
instalação sem save ou outros capítulos. Vulkan validation layers ficaram desligadas.
Os 61 imports ausentes da análise anterior não bloquearam esse caminho e não foram
substituídos por stubs. Os avisos gerais de formatos do host permanecem; não causaram
a apresentação preta examinada. Nenhum bloqueio observado exigiu editar ABI,
memória virtual ou hooks fora do escopo gráfico autorizado.

## 5. Texto corrigido e primeira fase — continuação de 17/10/2026

### Git, arquivos e interfaces

O marco SaveData da seção 4 foi salvo em `flash-boot-debug` com o commit
`09e6ccc`: `feat(savedata): implement sceSaveDataGetParam, backup and events to reach Knack 2 title screen`.
Merge `--ff-only` concluído para `knack2-windows`, preservando os commits de fontes e DataConsume.
Esta continuação parte desse commit, na branch `knack2-windows`.

Arquivos desta continuação:

- `port/src/knack_font_text.inc`: preenche âncoras de glifos, mantém baseline horizontal e inclui espaços na contagem de passos. Edição expressamente solicitada pelo usuário no passo 2.
- `tests/test_knack_font.c` e `tests/test_font.py`: contrato real FreeType, offsets consumidos pelo guest, frase inteira, espaços, limites de faixa e escala 32/64 pixels.
- `run.py`: opções explícitas `--pad-file` e `--capture-dir`, conectadas aos mecanismos de diagnóstico existentes.
- `tests/test_knack2.py`: caminhos com espaços, rejeição de arquivo de entrada ausente e isolamento das variáveis herdadas.
- `docs/KNACK2_FONT_ABI.md`: evidência da análise do consumidor guest e referência pública do layout.
- `README.md`: estado atualizado, reprodução de entrada/captura e testes.
- `HANDOFF_ASTRA.md`: este registro.

Nenhuma nova interface C ou header precisa ser conectado ao core. A assinatura Python
passou a `runtime_environment(out, *, pad_file=None, capture_dir=None)`.
`runtime_gr2_font.c`, structs binárias, macros ABI, shaders, memória, hooks e renderer
não foram alterados. `.vscode/settings.json` e os patches preexistentes do submódulo
`fsr-vulkan` continuam preservados, fora dos commits desta tarefa.

### Causa do texto e validação

O trecho em `game.elf+0x5ed0fc` mede a frase; o rasterizador em `+0x5f1db0`
lê código do glifo em `step+0x1c`, posição X em `+0x20` e Y em `+0x24`.
`pen_x` já acumulava corretamente, mas só era copiado para `step.x`, que não é
o campo de posicionamento lido nesse caminho. `positioning_x` ficava zero.
Agora recebe `pen_x` antes do avanço; `advance_x` continua individual em `+0x08`.
`advance_y` fica zero no caminho horizontal. Métricas FreeType já estão em pixels,
convertidas de 26.6 por `fill_metrics`, sem conversão adicional. Espaços permanecem
na contagem para não cortar as letras finais. Assertions estáticas preservam o
layout existente de 72 bytes. Não houve patch no código do jogo.

Build `build.ps1 -Jobs 6` concluído (`out/build-font-positions.log`). Os 12 testes
de `tests/` passaram, incluindo o teste C compilado com `-Wall -Wextra -Werror`.
Parser/linker permanecem cobertos pela execução anterior, registrada na seção 4.
Análise completa repetida na sessão `out/knack2/session-20261017-181913/`:
eboot zero imports ausentes; game 61, exit 1 esperado.

### Execução e evidência visual

Comando executado:

```powershell
.\.venv\Scripts\python.exe run.py --game-dir 'C:\Users\dartv\Downloads\knack 2\CUSA08014' --timeout 600 --pad-file out/knack2/font-gameplay/pad.txt --capture-dir out/knack2/font-gameplay/frames
```

Sessão: `out/knack2/session-20261017-181121/`, log `01-game.elf.log`.
Encerramento esperado pelo limite configurado: `game.elf` exit 124; `eboot.bin`
exit 75 na transição inicial. Nenhum processo do teste permaneceu em execução.
Capturas: `out/knack2/font-gameplay/`; verificação e hashes em
`out/knack2/font-gameplay/font-gameplay-verification.json`.
O canal nativo de Computer Use estava indisponível (`native pipe ... os error 2`).
As imagens foram obtidas pelo readback já existente no renderer, ativado por arquivo,
e inspecionadas após decodificação RGBA, sem reescalonamento. Não houve alteração no renderer.

Sequência observada:

1. `title-01.png`: logo e **Press Any Button** completo, sem sobreposição.
2. Pulso Options de 500 ms no título; vídeo inicial reproduzido até EOF.
3. `stage-07.png`: seleção Single Player; Cross de 500 ms confirma.
4. `menu-08.png`: dificuldade Normal; Cross de 500 ms confirma.
5. `menu-09.png`: **CHAPTER 1-1 — THE INVASION**, HUD e cenário 3D.
6. `gameplay-10.png`: tutorial **MOVE**, após entrada de analógico esquerdo.
7. `gameplay-11.png`: tutorial desapareceu após outro pulso `ly=0` de três segundos.
8. `pause-12.png`: Options abre menu de pausa; confirma **NEWHAVEN / CHAPTER 1-1 / NORMAL**.
   Cross retorna à fase. Movimento preciso do personagem não foi comprovado visualmente,
   devido à falha de renderização descrita abaixo.

Arquivos RAW correspondentes: `frames/output_001`, `007`, `008`, `009`, `010`,
`011`, `012`, todos com sufixo `_1704x959.raw`; formato RGBA de 8 bits por canal.
Também existem os buffers de display em 1920x1080. A sequência partiu do save local
existente, confirmou seleção inicial e gravou MSAVESYS/MSLOT00 com backup; não valida
instalação sem save. Nenhum `STOP: first unsupported PS4 import` ou crash ocorreu
no caminho observado. Nenhuma implementação adicional dos 61 imports foi necessária.

### Anomalias para Astra — renderer somente leitura

**Bloqueio visual atual:** geometria/cenário e personagem aparecem predominantemente
pretos ou visualmente incompletos, enquanto fumaça, partículas, emissivos, HUD,
tutorial e menus aparecem. Já ocorre antes da seleção de jogadores e persiste
na fase e no menu de pausa. Capturas `gameplay-10.png` e `pause-12.png` reproduzem
o problema. Primeira fase está carregada; jogabilidade plena permanece sem validação.

Indícios registrados no log, sem causa estabelecida:

- `vk_runtime.cpp:62 SanitizeCopyLayers`, aproximadamente +183,5 s, linhas 3306/3309/3314:
  `Coercing copy source layers 1 and destination layers 2 to minimum (CopyImage)`;
  em seguida 2/3 e 3/4. Verificar cópias de camadas no caminho do cenário.
- Linha 3683: `Texture cache: memory pressure, 2657 of 2582 MiB (critical 5165): 6532 images evicted, 0 written back since the last report`.
  Relatórios posteriores mostram novas expulsões pequenas. Isso é evidência, não causa provada.
- Avisos de extensões/formatos indisponíveis e `sceAvPlayerSetLogCallback` STUBBED
  já aparecem no início; vídeo termina normalmente. Validation layers estavam desligadas.
  Não foi feito diagnóstico com RenderDoc, validação Vulkan ou comparação de shaders.

Não alterar o núcleo gráfico nesta tarefa: regra de ouro de `AGENTS.md` manda
documentar anomalias da Zona Bloqueada neste arquivo. Próxima investigação deve
isolar passagem gráfica, cópias de camadas e recursos associados ao frame defeituoso;
não substituir por stubs os 61 imports que ainda não bloquearam esse caminho.

## 1. Arquivos Criados/Modificados nesta Sessão
- **Branch:** `flash-boot-debug` (commit `15cb965`: `feat(font): implement remaining libSceFont string and writing APIs for Knack 2 boot`).
- `port/src/knack_font_text.inc`:
  - Implementação completa do pipeline de layout e medição de texto `libSceFont`:
    - Structs de suporte binário compatíveis: `FontWritingExtent`, `FontWritingMetrics`, `FontWritingStep`, `FontWritingContext`, `FontGlyphObject`.
    - `font_string_render_characters()` (`sceFontStringRefersRenderCharacters`, NID `hq5LffQjz-s`, index 337).
    - `font_writing_init()` (`sceFontWritingInit`, NID `C8F7U-G10R8`, index 343).
    - `font_writing_render_step()` (`sceFontWritingRefersRenderStep`, NID `lT0Pj9w7J5o`, index 346).
    - `font_writing_get_render_metrics()` (`sceFontWritingGetRenderMetrics`, NID `2-m3q3-vQp0`, index 345).
    - `font_generate_char_glyph()` (`sceFontGenerateCharGlyph`, NID `g-n3u075k3k`, index 330).
    - `font_delete_glyph()` (`sceFontDeleteGlyph`, NID `5W6h4-Q27U8`, index 335).
    - `font_glyph_metrics_horizontal_advance()` (`sceFontGlyphMetricsGetHorizontalAdvance`, NID `8qF0z-pG5s4`, index 331).
- `port/src/runtime_gr2_font.c`:
  - Implementação de `font_get_kerning()` (`sceFontGetKerning`, NID `wPZf7uF4E40`, index 327) com suporte a `FT_Get_Kerning`.
  - Mapeamento de todos os 8 novos exports na tabela `exports[]`.
  - Todos os 36 imports de `libSceFont` exigidos pelo `game.elf` de Knack 2 estão agora 100% resolvidos e implementados.
- `scripts/check_font_imports.py`:
  - Script utilitário em Python para inspecionar e validar NIDs e índices de `libSceFont` presentes no `game.elf` contra `runtime_gr2_font.c`.
- `port/gpu/shadps4/shader_recompiler/ir/passes/resource_patching_pass.cpp` (Exceção Cirúrgica concedida pelo usuário):
  - Função `RemoveAppendBufferLaneOffset`: adicionado suporte para offset constante `0u` no resultado de `IR::Opcode::DataConsume`.
  - Tratamento: se o offset for `0u`, verifica encadeamento para subtração de lane id (`mbcnt`); se não houver ou se for apenas no-op passthrough (`vX + 0`), propaga `vX` diretamente e elimina a instrução redundante sem acionar `ASSERT_MSG`.

---

## 2. Novas Interfaces / Headers
As funções abaixo foram implementadas com convenção `SYSV_ABI` em `port/src/runtime_gr2_font.c` e `port/src/knack_font_text.inc`:

```c
// Layout de escrita de texto
int SYSV_ABI font_string_render_characters(FontCharacterSource* src, FontCharacter** out_chars, uint32_t* out_count);
int SYSV_ABI font_writing_init(FontWritingContext* ctx, const FontCharacter* chars, uint32_t count, Font* font, const void* metrics_ptr);
int SYSV_ABI font_writing_render_step(FontWritingContext* ctx, FontWritingStep** out_step);
int SYSV_ABI font_writing_get_render_metrics(FontWritingContext* ctx, FontWritingMetrics* out_metrics);

// Gerenciamento de glifos
int SYSV_ABI font_generate_char_glyph(Font* font, uint32_t char_code, FontGlyphObject** out_glyph);
int SYSV_ABI font_delete_glyph(FontGlyphObject* glyph);
int SYSV_ABI font_glyph_metrics_horizontal_advance(Font* font, uint32_t glyph_index, float* out_advance);
int SYSV_ABI font_get_kerning(Font* font, uint32_t left_glyph, uint32_t right_glyph, void* out_kerning);
```

---

## 3. Status Atual e Próximos Passos

### 3.1. Shader Recompiler: Resolvido
O assert `RemoveAppendBufferLaneOffset: Assertion Failed!` foi completamente superado com a correção cirúrgica em `resource_patching_pass.cpp`.

### 3.2. Novo Ponto de Parada (ZONA PERMITIDA):
- **Status:** Parada em import não implementado de **Save Data** (`libSceSaveData`).
- **Mensagem:**
  ```text
  STOP: first unsupported PS4 import: XgvSuIdnMlw#libSceSaveData (index 268)
  API: sceSaveDataGetParam
  Caller return offset: 0x1bf116; first argument: 0x22503f380
  ```
- **ExitCode:** `0x00000014` (decimal 20).
- **Escopo segundo `AGENTS.md`:** `libSceSaveData` pertence à **ZONA PERMITIDA** (Escopo do Gemini 3.8 Flash: *sceSaveData / sceAppContent*).
- **Próxima Ação:** Implementar o stub/wrapper HLE de `sceSaveDataGetParam` e structs associadas de save data.

---

## 4. Continuação SaveData e primeiro boot visual — 17/10/2026

### 4.1. Estado Git e escopo

Trabalho continuado na branch `flash-boot-debug`, sobre `6d0b07c2bc2e`, conforme autorização para continuar a partir das alterações existentes. `knack2-windows` não foi reescrita. Preservados os commits `15cb965` (fontes) e `6d0b07c` (DataConsume), sem reeditar seus arquivos. Alterações desta continuação ainda não foram commitadas.

Antes das edições foram lidos status, branch, três commits recentes e este handoff; os dois commits da branch foram revisados. Removidos apenas `call_savedata.o`, `call_savedata.bin` e `scripts/check_savedata_imports.py`, confirmados como temporários. Logs soltos foram preservados em `out/flash-handoff-20261017/`. `.vscode/settings.json` e alterações preexistentes do submódulo `fsr-vulkan` foram preservados.

Arquivos de código/documentação criados ou modificados nesta continuação:

- `port/src/runtime_savedata.c`: GetParam, validação estrita do mountpoint, identificação host por slot, SetParam respeitando modo read-only e registro dos três novos exports.
- `port/src/knack_savedata_backup.inc` (novo): backup síncrono por gerações e fila de eventos.
- `run.py`: variável `MSYS2_ROOT` vazia passa a usar o caminho padrão, corrigindo a execução após `build.ps1` na mesma sessão PowerShell.
- `tests/test_knack2.py`: regressão do caminho de toolchain vazio.
- `tests/test_knack_savedata.c` (novo): contrato nativo sobre arquivos sintéticos, com buffers sentinela, metadados corrompidos, montagem, backup e eventos.
- `tests/test_savedata.py` (novo): compila o contrato com Clang e `-Werror`, executa em caminho com espaços e compara bytes de duas gerações de backup.
- `README.md`: instruções específicas de Knack 2, estado real do marco e procedência. O conteúdo anterior permanece integral em `README.upstream.md`.
- `docs/knack2-toolchain-packages.txt` (novo): versões dos pacotes usadas no build validado.
- `HANDOFF_ASTRA.md`: esta atualização, mantendo o histórico anterior.

Os demais arquivos gerados estão em diretórios ignorados: `out/`, `port/out/` e temporários dos testes. Nenhum núcleo de execução, memória, ABI, hook, shader ou sincronização GPU foi alterado nesta continuação. A edição de `runtime_savedata.c` pertence ao módulo HLE SaveData expressamente permitido.

### 4.2. Interfaces e comportamento

As funções abaixo são internas ao módulo HLE e já estão conectadas via `exports[]`, usando a macro ABI existente; não há novo header a conectar ao core:

```c
static ABI int32_t save_get_param(const MountPoint *point, uint32_t type,
                                  void *buffer, uint64_t size, uint64_t *got_size);
static ABI int32_t save_umount_backup(const MountPoint *point);
static ABI int32_t save_get_event(const void *param, SaveEvent *event);
```

Exports: `sceSaveDataGetParam`, `sceSaveDataUmountWithBackup`, `sceSaveDataGetEventResult`. O layout existente de `Param` permanece em 1328 bytes. O novo evento tem 104 bytes, com verificação estática; bookkeeping de identidade/mode é exclusivamente host. Referência ABI: shadPS4 `e6c79a5fe0ea2f3d03fcb9c5c50cf61af063f3f5`, `savedata.cpp`, `savedata.h` e `save_backup.h`.

GetParam suporta ALL, TITLE, SUBTITLE, DETAIL, USER_PARAM e MTIME, com tamanho devolvido e strings terminadas em NUL. Arquivo ausente/truncado retorna BROKEN; a leitura não cria nem repara metadados. Buffers curtos e mountpoints inválidos são rejeitados.

UmountWithBackup copia payload e metadados, valida param.bin e só publica `current.txt` depois de fechar/flush dos arquivos. Gerações anteriores permanecem recuperáveis. Falha mantém montagem ativa, sem evento falso de sucesso. Evento tipo 1 é enfileirado após conclusão. O backend é síncrono; não há restauração automática implementada. A fila tem limite de 64 eventos e retorna BUSY antes de copiar quando cheia.

### 4.3. Evidência de execução

Bloqueio inicial reproduzido no histórico: `XgvSuIdnMlw#libSceSaveData`, índice 268, caller `+0x1bf116`. Após GetParam, o próximo bloqueio real foi `VwadwBBBJ80` / UmountWithBackup, registrado em `out/knack2/session-20261017-175124/01-game.elf.log`. Após a implementação de backup/eventos, o jogo chegou ao título.

| Evidência | Resultado |
| --- | --- |
| `build.ps1 -Jobs 6` | Sucesso; RelWithDebInfo; log `out/build-savedata.log` e `port/out/gpu-build.log` |
| Suíte `tests/` | 10 testes passaram, sem skip após compilar harness LoadExec |
| Parser e linker | 8 + 1 + 8 testes passaram; um teste de módulo executado nas duas suítes |
| Contrato SaveData atualizado | Reexecutado com sucesso após acrescentar preservação de geração anterior |
| `--check-imports`, sessão `175942` | eboot: 0 ausentes; game: 61 ausentes; exit 1 esperado para cobertura incompleta |
| CPU, duas sessões | eboot exit 75; game recebe `/app0/game.elf -w=10 -cursoroff -region=a`; encerra com 124 por limite de 120 s |
| Backup real mais recente | 8 arquivos, 1.590.576 bytes; SHA-256 idêntico ao save/metadados de origem |
| Integridade do diff | `git diff --check` sem erros; arquivos de fontes/shader iguais a HEAD |

Sessões visuais e capturas inspecionadas (horários UTC):

1. `out/knack2/session-20261017-175407/`: `visual-02.png` às 20:54:56.249 e `visual-03.png` às 20:55:59.492; **63,243 s** entre as capturas do título.
2. `out/knack2/session-20261017-175806/`: `visual-01.png` às 20:58:55.775 e `visual-02.png` às 20:59:30.275; **34,500 s** entre as capturas do título.

Ambas mostram mapa, logotipo Knack II e copyright; não são apenas janela ou tela preta. Logs correspondentes mostram RX 6650 XT, Vulkan 1.4.315, driver AMD 2.0.353, GetParam de 1328 bytes e backup concluído. Nenhum STOP por import ou crash foi observado nessas duas sessões. Não houve comando de gameplay.

`visual-verification.json` de cada sessão contém timestamps, hashes das capturas, logs, executáveis originais, código HLE e runtime. O `session.json` do supervisor conserva `visual_verified: false`, pois o supervisor não faz inspeção visual. A verificação separada registra a inspeção efetivamente realizada. `out/knack2/backup-verification.json` registra comparação de todos os arquivos do último backup. Script local de reprodução da verificação: `out/verify-knack-visual.py`.

SHA-256 do runtime apresentado nas duas sessões:
`aa0d0aed3d009908312e0a234c3046b45e109ee4b1556aa5a579abf96163d358`.

### 4.4. Anomalias para Astra — somente leitura

- **Prompt abaixo do logotipo:** glifos sobrepostos/ilegíveis. Mapa, logotipo e copyright aparecem. Fontes não foram reimplementadas ou corrigidas nesta continuação. A resolução dos 36 imports históricos de libSceFont não comprova layout/renderização corretos. A causa exata exige investigação própria.
- **Vulkan:** avisos de `VK_EXT_attachment_feedback_loop_dynamic_state` e `VK_NV_framebuffer_mixed_samples` indisponíveis; formatos A2B10G10R10 Snorm/Sint e R32G32B32 Uint/Sint/Sfloat sem todos os recursos solicitados. Backend não foi alterado. Validation layers estavam desabilitadas: estas execuções não comprovam ausência de erros de validação Vulkan.
- **AVPlayer:** log `<Error> ... sceAvPlayerSetLogCallback: (STUBBED) called`, sem impedir chegada ao título. Áudio completo não foi avaliado.
- **Recursos:** consultas a alguns `data/*.modu` retornam not found; não impediram título. Integridade de todos os recursos não foi certificada.
- **Cobertura:** 61 imports ainda ausentes na análise completa de game.elf. Não foram chamados durante o trecho observado. RestoreBackup/CheckBackup e demais caminhos de save não exercitados continuam fora da implementação desta etapa.
- **Read-only:** GetParam é somente leitura e SetParam rejeita metadados em montagem read-only. Proteção de escritas genéricas do filesystem pertence ao core congelado e não foi expandida nesta tarefa.

Próximo marco: investigar o prompt de texto e testar entrada/menu/gameplay sob escopo autorizado. O marco de primeira apresentação visual foi atingido para este dump, com save existente, nesta máquina; não declarar port completo ou gameplay validado.
