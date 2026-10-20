# HANDOFF_ASTRA.md

## Identificação visual do Knack pequeno e revisão de texturas — 20/10/2026 — GPT-5.6 Sol

### Escopo e baseline

Investigação autorizada para verificar se o probe KNACK-PX examinava o passe correto e identificar
os draws do Knack pequeno. Branch `feature/ngs2-hle22b-mixer`, HEAD inicial `5ca34c0`. O único
arquivo protegido alterado nesta etapa foi `vk_rasterizer.cpp`; também foram adicionados
`scripts/run_knack2_visual_capture.py`, `scripts/knack_visual_report.py` e
`tests/test_knack_visual_report.py`. Alterações preexistentes, saves, áudio B2-F2D e submódulo
`fsr-vulkan` foram preservados. Nenhum commit, push, merge ou operação destrutiva foi executado.

### Passe correto

A crítica da auditoria foi confirmada. GS `8943ac2c` aparece somente em draws `PointList`
(`prim=1`) com um render target; a captura anterior continha 37/39 desses draws. O Knack pequeno e
as peças vermelhas/douradas visíveis foram localizados no G-buffer: `TriangleList` (`prim=4`), cinco
render targets, VS `6e383eb8`, sem GS, PS `b1f064c3`. Portanto, KNACK-PX examinava outro passe,
provavelmente partículas/billboards, e não serve para concluir a causa do flickering das malhas.

O probe `KNACK-VIS` repete draws candidatos dentro de uma ROI de tela, com todas as escritas de cor,
depth e stencil desligadas. O draw original e os cinco attachments permanecem intactos. Na captura
manual `control/visual-live2/`, ROI do corpo (`1100,690,30,50`) encontrou 20 draws do grupo
VS/PS acima; ROI das peças desprendidas (`980,640,40,40`) encontrou 13. O piso também aparece como
camada anterior em ambas as ROIs, mas o único grupo de malhas que cobre corpo e peças é
`6e383eb8/b1f064c3`. Evidência visual: `output_000.png` até `output_007.png`.

### Texturas, buffers e compute

Nos dois command streams consecutivos, todos os draws do grupo usam exatamente:

- `t0`: BC1 1024x1024, endereço guest `0x10d972a400`;
- `t1`: BC7 1024x1024, endereço guest `0x10d95d3c00`;
- sampler `d9193d31`.

Foram 31 draws/380 instâncias no primeiro stream e 39 draws/564 instâncias no segundo. Trinta
conjuntos de vertex/index buffers persistiram e sete conjuntos adicionais apareceram no segundo.
As identidades das texturas não mudaram. Isto não prova igualdade de texels; nova instrumentação
compilada registra `Image::hash`, UID, alias generation, flags e tamanho na próxima captura.

O PS amostra as duas texturas e escreve cinco G-buffer targets. IR e SPIR-V não contêm `Discard`,
`OpKill`, `Demote` ou alpha-test. O VS possui quatro vertex attributes e também SSBOs somente-leitura;
`InstanceId` indexa `vs.b0`, que participa diretamente de `Position0`. Logo, ausência de attributes
não foi usada como inferência de SSBO. A instrumentação de upload exato foi ampliada para VS
`6e383eb8`, cobrindo `vs.b0`/`vs.b2` na próxima captura.

Nos streams analisados havia 203 e 47 regiões escritas por dispatches. Nenhuma sobrepôs as 341 e
429 leituras do grupo-alvo, comparando mesmo frame e frame adjacente. Draws consumidores usam fila
`gfx`; não foi identificado produtor compute para suas texturas, SSBOs, vertex ou index buffers.
Assim, nenhuma dependência Compute→Graphics específica pode ser apontada nesta captura.

### Único A/B escolhido

Flags LDS/lane foram rejeitadas: os shaders identificados não usam LDS, workgroup barriers ou wave
ops. Foi implementado `TLG_KNACK_SMALL_SYNC=1`, exposto por `--small-sync`. O modo faz um único
`Scheduler::Finish()` por `SubmitDone`, imediatamente antes do primeiro draw com VS `6e383eb8`.
Não usa `ALL_COMMANDS`, não é padrão e não é correção. Resultado positivo implicará visibilidade de
uploads anteriores, mas não provará qual dependência falta; resultado neutro enfraquecerá a hipótese
de sincronização CPU/upload→draw.

### Validação e limites

Build incremental de `tlg-probe.exe` passou. `test_knack_visual_report.py`: 6/6; conjunto focado
KNACK-VIS/KNACK-PX/launcher: 22/22; `py_compile` e `git diff --check` passaram. Smoke real do probe
anterior consultou 744 draws e coletou 744/744 queries; captura manual do Knack pequeno consultou
895 e 903 draws nas duas ROIs. Não houve benchmark. O probe ativo repete draws e pode reduzir
desempenho; desativado, não cria query pool nem repete draws. A extensão de hash de textura e o A/B
de sync foram compilados, mas ainda não executados em gameplay. O processo manual atual foi mantido
aberto a pedido do proprietário.

---

## KNACK-PX vazio — causa-raiz, correção e prova das queries — 20/10/2026 — GPT-5.6 Sol

### Baseline e causa-raiz

Branch `feature/ngs2-hle22b-mixer`, HEAD inicial `5ca34c0`. Escopo crítico permaneceu limitado
a `vk_rasterizer.cpp`; ferramentas e teste alterados: `scripts/knack_contribution_report.py`,
`scripts/run_knack2_contribution_capture.py` e `tests/test_knack_contribution_report.py`. Trabalho
preexistente, saves, áudio B2-F2D e submódulo `fsr-vulkan` foram preservados. Nenhum commit,
push, merge ou operação destrutiva foi executado.

A sessão vazia `session-20261020-133641/01-game.elf.log` recebeu corretamente o caminho absoluto
do trigger e armou o query pool. O arquivo `control/contribution-01/contribution.request` continuou
existindo, enquanto `capture.request` foi consumido e gerou 32 arquivos RAW. O mesmo processo
compilou sete permutações do GS `8943ac2c`. Portanto, launcher, processo e shader estavam corretos;
o trigger KNACK-PX nunca foi consultado.

Causa: `KnackContributionProbe::BeginFrame()` era chamado somente no caminho de display pass
condicionado por `camera_motion->Enabled()`. A sessão usa `TLG_UPSCALER=off`; sem debug overlay,
`CameraMotion::Enabled()` é falso. Assim, o log parava em `Armed`, sem detectar ou remover o
arquivo. Um primeiro desacoplamento ainda dependente de `IsDisplayBuffer()` também não disparou no
smoke de boot. A fronteira confiável encontrada foi `Rasterizer::OnSubmit()`, chamada após
`sceGnmSubmitDone`.

### Correção diagnóstica

Polling e rotação das slices de query agora ocorrem em `Rasterizer::OnSubmit()`, depois de
`DrainDrawPipe()` e antes de `runtime.TickFrame()`. Caminho normal mantém somente checagem de
ponteiro nulo por submissão. Captura continua opt-in. Logs explícitos cobrem `Armed`, `Trigger
detected`, `Capture started`, `Matching draws found`, `Queries submitted`, `Results collected`,
`Capture completed` e `Capture failed reason=...`.

`TLG_KNACK_CONTRIB_GS=any`, `TLG_KNACK_CONTRIB_FRAMES` e
`TLG_KNACK_CONTRIB_MAX_DRAWS` existem somente para smoke controlado; padrão continua filtrando
GS `8943ac2c`, 16 janelas e até 256 draws. Campo `frame` do log KNACK-PX identifica sequência de
`SubmitDone`, não frame apresentado. Correlação visual continua pelos RAW disparados junto; dois
contadores não devem ser igualados.

Analisador agora falha com código 2 e mensagem específica quando não há frames/resultados, valida
número declarado de queries e pares O/R, reconhece todos os estados e analisa tentativa mais
recente após rearme. Wrapper também informa `trigger_not_consumed` se processo termina com arquivo
ainda presente.

### Prova executada

Build incremental de `tlg-probe.exe` passou. Smoke automático, sem gameplay manual, usou
`GS=any`, quatro janelas e oito draws por janela. Primeira tentativa, criada cedo demais no boot,
falhou explicitamente com `no_matching_draws`. Rearme durante renderização produziu:

- `Trigger detected frame=1816` e consumo do arquivo;
- 32 matching draws;
- quatro lotes de 16 queries submetidos;
- quatro lotes de 16 resultados coletados com `eWait` e `eSuccess`;
- 64/64 resultados, pares O/R completos;
- `Capture completed frames=4 matching_draws=32 queries=64`.

Analisador resumiu quatro lotes com sucesso; todos 32 draws sintéticos tiveram `O>0` e `R>0`.
Testes: `test_knack_contribution_report.py` 7/7, `test_knack2.py` 11/11, `py_compile`, build
incremental e `git diff --check` passaram. Analisador aplicado à captura vazia antiga agora retorna:
`no KNACK-PX result frames; states=Armed; trigger was never detected on render thread`.

Não houve benchmark de FPS. Desativado, não há pool, queries, draws repetidos ou espera GPU; apenas
checagem nula por `SubmitDone`. Durante captura, há repetição diagnóstica de draws e espera por
resultados, portanto impacto pode ser relevante e não representa desempenho normal. MSAA permaneceu
desligado e nenhuma correção gráfica foi aplicada.

---

## Flickering dinâmico — vídeo, shaders candidatos e probe de contribuição — 20/10/2026 — GPT-5.6 Sol

### Evidência visual

Foi analisado `C:\Users\dartv\Videos\2026-10-20 11-13-58.mp4`, com duração de
25,533 segundos. Nos quadros pedidos, o agrupamento vermelho/dourado aparece em 13,6 s,
desaparece quase todo em 14,0 s, reaparece em 14,2 s e desaparece novamente em 14,6 s.
Quadros intermediários acrescentam informação importante: em 13,4 s e 14,3 s permanecem
silhuetas/fragmentos cinza e sombras aproximadamente no mesmo volume ocupado pelas peças
coloridas. O Knack pequeno, HUD, asfalto e cenário continuam estáveis. Isso indica perda seletiva
de uma passagem/material; não um desaparecimento do frame ou do personagem inteiro.

O corrimão metálico direito permanece visualmente estável entre 13,2 e 14,9 s e nas amostras de
2 fps da gravação completa. O vídeo não mostra um evento de destruição do poste de Sunstone que
permita associá-lo ao mesmo ciclo. Portanto, corrimão e poste não foram atribuídos à causa das
peças do Knack. Cada um ainda exige captura própria quando reproduzir o defeito.

### Associação disponível com draws

A captura anterior em `control/geometry/` contém quatro frames apresentados com as peças
vermelhas/douradas visíveis. Os dois `frame_record` dessa captura registram exatamente 52 draws
com GS `8943ac2c`, as mesmas quantidades em ambos os frames, distribuídas entre dez pixel shaders.
As 1.070 comparações guest/Vulkan não tiveram mismatch. Porém, essa captura ocorreu às 11:05 e o
vídeo foi gravado às 11:13; ela comprova o conjunto candidato no estado visível, mas não contém um
par visível/invisível sincronizado. Não é usada como prova de que todos os 52 draws pertencem às
peças defeituosas.

Os dez pixel shaders candidatos (`00c2a70c`, `1483ac5f`, `20d1e774`, `2dc4270a`, `4ebc508c`,
`613b2261`, `9b440dd0`, `ba32a360`, `c99fd0c1`, `e8209784`) não contêm `Discard`, `Demote` ou
`Kill` no IR capturado. O GS `8943ac2c` contém quatro `EmitVertex`, mas possui caminho condicional
do bloco 138 diretamente ao bloco 152 que salta todas as emissões. Assim, draw packet e instance
count presentes não garantem primitivas produzidas. Nos 52 draws, `pc=0x240` deixa os bits de cull
front/back desligados, stencil é zero e depth é LessEqual sem escrita. A hipótese mais forte passa
a ser a decisão de emissão/posição de clip do GS; depth ou composição continuam possíveis até
medição de contribuição.

### Probe temporário de contribuição

`vk_rasterizer.cpp` ganhou instrumentação opt-in acionada por `TLG_KNACK_CONTRIB_TRIGGER`.
Somente draws com GS `8943ac2c` recebem duas occlusion queries durante 16 frames:

1. `O`: draw original, contando amostras que sobrevivem shader e depth/stencil.
2. `R`: repetição sem escrita de cor, com depth, stencil, cull e rasterizer discard desligados.

A repetição não altera color/depth attachments. Interpretação: `O>0` localiza a perda após os
testes, em alpha/blend/composição; `O=0,R>0` localiza em depth/stencil/cull; `O=0,R=0`, somado à
ausência de discard nos PS, localiza em emissão GS, clipping ou geometria degenerada.

Ferramentas adicionadas: `scripts/run_knack2_contribution_capture.py` injeta o trigger e captura
16 frames; `scripts/knack_contribution_report.py` resume resultados por frame e pixel shader.

### Validação e limite

`build.ps1 -Jobs 6` passou. `py_compile` dos scripts passou e teste sintético do relatório
classificou corretamente casos após depth e eliminados por depth/cull. Smoke de 50 s criou o query
pool (`relaxed=1`) sem assertion, validation error ou device loss. Replay de 100 s não alcançou a
cena alvo nem consumiu o trigger; logo, queries sobre `8943ac2c` ainda dependem de reprodução
manual. Nenhuma correção definitiva foi aplicada.

---

## Flickering dinâmico — MSAA NO-GO e verificação de geometria versus visibilidade — 20/10/2026 — GPT-5.6 Sol

### Objetivo, autorização e baseline

Investigação autorizada para diferenciar dados de geometria incorretos de falhas de
visibilidade/composição no Chapter 1-1. Escopo protegido limitado a
`vk_rasterizer.cpp`, `vk_pipeline_cache.cpp`, `vk_draw_prep.cpp` e `buffer_cache.cpp`, além de
testes e ferramentas de diagnóstico. Branch `feature/ngs2-hle22b-mixer`, HEAD inicial `5ca34c0`.
Os quatro arquivos protegidos estavam limpos; alterações preexistentes de áudio B2-F2D,
`HANDOFF_ASTRA.md`, `run.py`, testes, arquivos não rastreados e submódulo `fsr-vulkan` foram
preservadas. Nenhum commit, merge, push, reset ou clean foi executado.

### Decisão sobre MSAA

O proprietário validou MSAA real durante gameplay: peças do Knack, poste de Sunstone e
guarda-corpo mantiveram o mesmo flickering; desempenho piorou; nenhum artefato novo apareceu.
Resultado: NO-GO. O experimento usou `TLG_NO_MSAA=0`; o log da sessão
`session-20261020-100442/01-game.elf.log` confirma `GPU: guest multisampling on` e execução dos
shaders do passe dinâmico (`ES 69abf99d`, `GS 8943ac2c` e fragment shaders associados). O caminho
de código usa `ColorBuffer::NumSamples()`/`DepthBuffer::NumSamples()` na chave do pipeline e
`LiverpoolToVK::NumSamples()` na criação Vulkan; com MSAA ligado, o alvo conhecido do passe pede
4 amostras. Não houve captura direta do `VkPipelineMultisampleStateCreateInfo` nessa sessão;
portanto, a evidência disponível confirma seleção do caminho 4x no código, não uma inspeção
RenderDoc do pipeline vivo. O padrão AMD voltou a 1x: não há `TLG_NO_MSAA=0` em `run.py`, nenhum
processo do jogo ficou ativo e os quatro arquivos autorizados estavam limpos antes desta etapa.

### Evidência de dois frames consecutivos

`control/record/frame_record_0.txt` e `frame_record_1.txt` registram o passe com GS `8943ac2c`:
37 e 39 draws, respectivamente; 30 draws foram alinhados pelo comparador. Cinco grupos de
fragment shader mantiveram a mesma quantidade; dois ganharam um draw no segundo frame. O total de
instâncias variou de 2033 para 2045. O alvo `0x10b4cb0000` R16G16B16A16, depth
`0x10c04d8000` D32, blend `0x65000501`, depth `0x7007b2`, stencil desligado e PointList ficaram
iguais. O depth test é LessEqual, sem depth write; o blend é premultiplicado. Buffers GPU de
16 MiB e 2 MiB alternaram endereços ping-pong e mudaram hash, enquanto o índice GPU de 4 bytes
permaneceu estável. Isso enfraquece ausência de draw, pipeline alternante e buffer GPU totalmente
estagnado. Ainda não exclui upload incorreto dos buffers CPU de 240 bytes usados por
transformações: a captura anterior comparava apenas memória guest para esses buffers.

### Instrumentação implementada

- `vk_rasterizer.cpp`: durante `TLG_FRAME_RECORD_TRIGGER`, registra amostras de rasterização,
  color/depth, `ImageInfo` e backing real. Com `TLG_FRAME_RECORD_VERIFY_UPLOAD=1`, somente draws
  com GS `8943ac2c` comparam bytes guest com o `VkBuffer` e offset realmente ligados ao draw.
  Buffers até 1 KiB usam palavras completas; maiores usam hash de 64 KiB. Instrumentação é
  opt-in e não altera renderização normal.
- `scripts/frame_diff.py`: filtro `--contains`, resumo de amostras e detecção explícita de mismatch
  guest/GPU.
- `scripts/run_knack2_geometry_capture.py`: wrapper diagnóstico que injeta os gatilhos sem tornar
  MSAA real padrão.

### Validação e limite atual

`build.ps1 -Jobs 6` passou. `no-msaa-test.exe` passou. `test_knack2.py`: 11/11 passaram.
`py_compile` dos dois scripts passou; teste sintético do comparador detectou 1 mismatch em 2
comparações. `git diff --check` passou. Nenhuma sessão de gameplay foi executada após a nova
instrumentação; impacto real fica limitado à janela manual de dois frames e ainda não foi medido.

Próxima ação: capturar exatamente a transição visível/invisível. Zero mismatches guest/GPU
desloca a investigação para saída do GS, depth e alpha/composição. Qualquer mismatch identifica o
buffer e draw exatos para rastrear invalidação/upload em `buffer_cache.cpp`.

---

## B2-F2D — Resolução dos 3 ajustes finais confirmados pelo Argon e GPT-6 — 20/10/2026 — Gemini Antigravity (Auditoria Argon & GPT-6)

### 1. Objetivo e governança
- **Objetivo**: Fechar os três últimos ajustes específicos requeridos por Argon e GPT-6: (1) verificação de término após liberação de snapshot em `ngs2_segment_control`; (2) término imediato após EOF em `PAUSED` seguido de `RESUME` ou `PLAY`; (3) teste de robustez sintética na reconfiguração de voz `STOPPED` com trabalho residual.
- **Governança (`AGENTS.md`)**: `port/src/runtime_tlg.c` e núcleo permaneceram **100% READ-ONLY** no working tree. Patch unificado candidato mantido não aplicado em `knack2_B2F2D_candidate.patch` (validação `git apply --check` com retorno 0). Worktree dirty preservado, zero commits, sem resets ou cleans.

### 2. Implementação dos 3 ajustes no candidato B2-F2D
1. **Ajuste 1 — EOF após liberação de snapshot (`ngs2_segment_control`)**:
   - No caminho comum após a decisão de aceitação/rejeição (onde `segment_reservations` e `snapshot_inflight` já foram decrementados sob `ngs2_lock`), foi inserida a chamada `ngs2_atrac9_check_completion_locked(voice);`.
   - Funciona deterministicamente tanto para snapshots aceitos quanto rejeitados, sem finalizar prematuramente streams que ainda possuam segmentos válidos pendentes.
   - Adicionado o teste `test_eof_during_failed_snapshot_reservation`: sincronizado com o fluxo real de `ngs2_segment_control` via hook de cópia em `runtime_memory_copy_snapshot`. O EOF chega enquanto a reserva está em voo; ao falhar a cópia, a rejeição do snapshot encerra as reservas e o helper transita a voz síncronamente para `FINISHED`.
2. **Ajuste 2 — EOF em PAUSED seguido de RESUME ou PLAY (`ngs2_control_locked`)**:
   - No `ngs2_control_locked`, inserida a chamada `ngs2_atrac9_check_completion_locked(voice)` imediatamente após a voz mudar efetivamente para `NGS2_VOICE_PLAYING` nas ações `0` (PLAY) e `2` (RESUME).
   - Preservadas as regras de não reinicializar contadores nem apagar `end_requested` em chamadas `PLAY` repetidas ou para vozes em pausa.
   - Adicionado o teste `test_eof_while_paused_then_resume`, validando a sequência completa:
     - `PAUSE` → EOF com FIFO vazio → permanece `PAUSED` (`0x05`) → `RESUME` → transita imediatamente para `FINISHED` (`0x20`).
     - E a variante com `PLAY`: `PAUSE` → EOF com FIFO vazio → `PAUSED` (`0x05`) → `PLAY` → transita imediatamente para `FINISHED` (`0x20`).
3. **Ajuste 3 — Teste de reconfiguração com trabalho pendente**:
   - Atualizado `test_stopped_voice_reconfig_with_pending_work`:
     - Insere 2 grains (512 amostras) e emite `STOP`.
     - Valida `dropped_samples == 512` imediatamente após `STOP`, antes do reset.
     - Com a voz `STOPPED`, injeta grãos residuais sintéticos no fixture sob lock.
     - Reconfigura a waveform com a voz parada.
     - Comprova FIFO limpa (`pcm_count == 0`), geração avançada e contadores reinicializados em 0.
   - Documentado no teste que a injeção residual é um teste sintético de robustez de reconfiguração de borda, e não uma sequência normal esperada do guest Orbis.

### 3. Evidências de testes
- **Runner Real (`tests/test_ngs2_voice_state.py`)**:
  - `test_baseline_real_runtime`: OK (Baseline zeros passam; modo B2-F2D ativo acusa RED).
  - `test_candidate_real_runtime_green`: 100% GREEN nos 17 testes contratuais em 1.98 s (tanto com rollback `TLG_NGS2_B2F2D=0` quanto ativo `TLG_NGS2_B2F2D=1`).
- **Suíte de Regressão do Repositório (`tests/`)**:
  - 41 testes executados, 38 aprovados e 3 testes ATRAC9 com fixture externa pulados (`skipped`). Zero falhas, zero erros.
- **Testes `AGENTS.md` (prepare/link)**:
  - `test_prepare.py`: 8/8 OK.
  - `test_link_modules.py`: 1/1 OK.
  - `test_link_libc.py`: 8/8 OK.

---

## B2-F2D — Resolução dos 4 bloqueadores da auditoria GPT-6 e validação final da suíte real — 20/10/2026 — Gemini Antigravity (Auditoria GPT-6 & Argon)

### 1. Objetivo, governança e conformidade
- **Objetivo**: Fechar os 4 bloqueadores específicos identificados pelo GPT-6 na auditoria do candidato B2-F2D, fortalecer o teste B2-F2C e validar a reconfiguração de vozes `STOPPED` com trabalho pendente.
- **Governança (`AGENTS.md`)**: `port/src/runtime_tlg.c` e arquivos protegidos permaneceram **100% READ-ONLY** (sem edições diretas no working tree). Entrega realizada estritamente por meio de patch unificado não aplicado (`knack2_B2F2D_candidate.patch`), validado com `git apply --check` (código de saída 0).
- **Git status**: Branch `feature/ngs2-hle22b-mixer`, HEAD `5ca34c0`. Alterações preexistentes preservadas intactas. Zero commits, zero pushes, sem `git reset` ou `git clean`.

### 2. Resolução detalhada dos 4 bloqueadores da auditoria GPT-6
1. **Bloqueador 1 — Dual-Loop SoundX e verificação real de `finished_counter == 1`**:
   - *Análise de Disassembly (`game.elf`)*:
     - Loop 1 (`0xb6efc0 .. 0xb6f0fa`): itera sobre as vozes do Rack 1 (Pool 1, ponteiro em `0x40(%rax)`, stride `0xc8`), consultando `sceNgs2VoiceGetState(size = 0x30)`. É o **único proprietário** do incremento de `finished_counter` (`incq 0x4cb0(%rdx)` em `0xb6f032`) quando a voz transita de playing para `FINISHED (0x20)`. Se `ACTIVE (0x01) == 0`, seta a flag SoundX `0x100` em `0xb6f045`, inativando o slot nas próximas iterações.
     - Loop 2 (`0xb6f140 .. 0xb6f1d8`): itera sobre as vozes do Rack 2 (Pool 2, ponteiro em `0x50(%rax)`, stride dinâmico `0x10(%rcx)`), consultando `sceNgs2VoiceGetState(size = 0x10)`. Não possui `finished_counter` e não altera o Pool 1.
   - *Resolução no Teste (`test_soundx_polling_order_10_then_30`)*:
     - A simulação de SoundX foi corrigida para executar os simuladores na ordem invertida: Call Site 2 (Pool 2) com `s16`, seguido de Call Site 1 (Pool 1) com `s48`.
     - Demonstrado que, devido à estrita idempotência do `ngs2_voice_state`, a consulta prévia de `0x10` não consome o estado; Call Site 1 processa `0x20` e incrementa `ctx_rack1.finished_counter == 1`, marcando `0x100` e demonstrando que pesquisas subsequentes são ignoradas.
2. **Bloqueador 2 — Chamada a `ngs2_atrac9_check_completion_locked` na liberação de `decode_inflight` pelo worker**:
   - No worker ATRAC9 (`port/src/runtime_tlg.c:1294`), a liberação sob lock `state->decode_inflight = 0;` agora chama imediatamente `ngs2_atrac9_check_completion_locked(voice);`.
   - Cobertos deterministicamente na suíte de testes (`test_worker_completion_after_eof`) os cenários de conclusão de decodificação com 0 novos grains gerados e de término após erro de decodificação (`decode_errors++`), garantindo que a voz transite de forma síncrona para `STOPPED` com `finished = 1` sem ficar pendurada em `PLAYING`.
3. **Bloqueador 3 — `Cmd 6 action 0 (PLAY)` condicional e preservação de EOF / cursores**:
   - Em `Cmd 6 action 0`: o reset de `mixed_samples`, `virtualized_samples`, `dropped_samples`, `end_requested` e `finished` ocorre **somente** se a voz estava em `NGS2_VOICE_STOPPED`.
   - Chamadas repetidas de `PLAY` durante reprodução ativa (`PLAYING`) ou em pausa não reiniciam o cursor de amostras nem limpam `end_requested`.
   - Adicionado teste `test_play_command_invariants`:
     - Sub-caso 1: `PLAY` duplicado durante reprodução ativa preserva exatamente `samples_played == 256`.
     - Sub-caso 2: chegada de EOF (descritor vazio) seguida de `PLAY` duplicado mantém `end_requested == 1`; ao drenar o FIFO, a voz transita confiavelmente para `FINISHED`.
4. **Bloqueador 4 — Paridade estrita de HEVAG e vozes sem stream com o baseline**:
   - `ngs2_voice_state` restringe o retorno estendido de flags/amostras estritamente a `if (ngs2_b2f2d_enabled() && voice->atrac9 && ngs2_atrac9_waveform(&voice->waveform))`.
   - Para vozes HEVAG (`format == 0x1c`) ou vozes não configuradas (sem waveform), a estrutura de retorno é preenchida integralmente com zeros via `memset(state, 0, size)`.
   - Adicionado teste `test_hevag_and_unconfigured_voice_baseline_parity`: comprova byte a byte que tanto em 0x10 quanto em 0x30, vozes HEVAG e vozes virgens retornam todos os campos zerados, preservando 100% de paridade com o baseline.
5. **Item Adicional 1 — Fortalecimento da verificação do Bypass B2-F2C**:
   - O teste `test_b2f2c_zero_matrix_bypass_preservation` ativa explicitamente `ngs2_b2f2c_setting = 1`.
   - Assertivas reforçadas para inspecionar as métricas do mixer: `sys->voices_mixed == 0` (voz com ganho zero não é somada aos canais master) e `sys->virtualized_grains == 1` (a drenagem do grão é contabilizada no bypass virtualizado).
6. **Item Adicional 2 — Reconfiguração de voz STOPPED com trabalho pendente**:
   - Adicionado teste `test_stopped_voice_reconfig_with_pending_work`: voz com grãos no FIFO é parada (`STOP`) e reconfigurada com nova waveform. O HLE detecta que a voz está `STOPPED`, limpa o trabalho residual via `ngs2_atrac9_clear_pending_locked`, avança a geração e zera contadores de descartes para a nova geração.

### 3. Evidências de testes
- **Test Runner (`tests/test_ngs2_voice_state.py`)**:
  - `test_baseline_real_runtime`: compila contra o `runtime_tlg.c` real sem modificações. Execução padrão passa (zeros do baseline); execução com `--assert-b2f2d-active` **falha (RED)** conforme esperado.
  - `test_candidate_real_runtime_green`: aplica `knack2_B2F2D_candidate.patch` em repositório isolado temporário. Execução com `TLG_NGS2_B2F2D=0` passa com paridade de baseline (safe rollback); execução com `TLG_NGS2_B2F2D=1` e `--assert-b2f2d-active` é **100% GREEN** em todos os 15 testes contratuais em 1.78 s.
- **Suíte de regressão do repositório (`python -m unittest discover -s tests -v`)**:
  - 41 testes executados, 38 aprovados e 3 testes ATRAC9 com fixture de terceiros pulados (`skipped`). Zero regressões.
- **Testes documentados em `AGENTS.md`**:
  - `test_prepare.py`: 8/8 OK.
  - `test_link_modules.py`: 1/1 OK.
  - `test_link_libc.py`: 8/8 OK.

### 4. Limitações e avaliação para ativação do piloto
- O patch `knack2_B2F2D_candidate.patch` está pronto e validado em nível de HLE, ABI e SoundX, mas permanece **desaplicado** aguardando revisão e autorização de integração do Sol/GPT-6 e Argon.
- A redução do orçamento de 128 MiB depende de o SoundX agora emitir a liberação/reutilização de vozes após observar `FINISHED (0x20)`. Essa hipótese requer validação em sessão de boot real do jogo.

---

## B2-F2D — Candidato corrigido de ciclo de vida NGS2, VoiceGetState idempotente e contadores de playback — 19/10/2026 — Gemini Antigravity (Auditoria Argon & Direção GPT-6)

### Objetivo, escopo e conformidade
Revisão e reformulação do patch experimental B2-F2D após auditoria GPT-6 e Argon. Nenhuma alteração direta foi aplicada em `port/src/runtime_tlg.c` ou em arquivos protegidos do núcleo. O patch candidato foi atualizado e mantido como diff unificado não aplicado em `knack2_B2F2D_candidate.patch` (validação `git apply --check` com retorno 0). Worktree dirty preservado, zero commits, sem resets ou cleans.

### Correções dos 8 bloqueadores da auditoria GPT-6
1. **Detecção de EOF ATRAC9 independente da ordem de chegada**: implementada a função `ngs2_atrac9_check_completion_locked`, chamada tanto no commit do mixer (`ngs2_render`) quanto na chegada do descritor vazio (`ngs2_segment_control`). Cobre deterministicamente o cenário em que o EOF chega após o esvaziamento completo do FIFO (`pcm_count == 0`), bem como EOF antes do render.
2. **Idempotência estrita e consultas invertidas**: `ngs2_voice_state` permanece como leitor puro sem efeitos colaterais. Validadas formalmente as ordens de consulta `0x30 → 0x10` e `0x10 → 0x30`; SoundX incrementa `finished_counter` exatamente uma vez na transição para inatividade e ignora consultas subsequentes via flag `0x100`.
3. **Preservação integral do HEVAG**: escopo do B2-F2D restrito estritamente a waveforms ATRAC9 (`ngs2_atrac9_waveform`). O pipeline HEVAG preserva o processamento de cauda (`ngs2_hevag_flush_tail_locked`), `terminal_seen`, `carry_samples` e semântica existente do Gate HLE-2.2C/B1.
4. **Reconfiguração com isolamento de trabalho em voo**: na reconfiguração com waveform idêntica (`Cmd 0x10000000`), streams com reservas (`segment_reservations`), decodificação (`decode_inflight`) ou snapshots em voo não são abortadas. Streams encerradas ou paradas são reconfiguradas limpando resíduos e avançando a geração.
5. **Contabilidade de `dropped_samples` por geração**: contadores de descartes e amostras renderizadas pertencem à sua respectiva geração; ao iniciar ou reconfigurar nova geração, `mixed_samples`, `virtualized_samples` e `dropped_samples` são reinicializados a 0.
6. **Substituição da validação simulada por execução do runtime real**: removidas todas as funções simuladas em C. O teste `tests/test_ngs2_voice_state.c` inclui e executa diretamente o runtime C real (`runtime_tlg.c`), exercitando `ngs2_voice_control`, `ngs2_render` e `ngs2_voice_state`.
7. **Demonstração de RED no baseline e GREEN no candidato**: o runner `tests/test_ngs2_voice_state.py` executa o mesmo arquivo de teste contra o baseline original (demonstrando falha/RED pelo retorno de zeros) e contra uma cópia isolada com o patch candidato aplicado (demonstrando 100% GREEN em todos os 11 contratos, incluindo modo B2-F2D OFF e preservação do B2-F2C).
8. **Proteção sob lock de todas as chamadas**: auditado que 100% das chamadas a `ngs2_b2f2d_enabled()` e campos estendidos ocorrem sob proteção exclusiva de `pthread_mutex_lock(&ngs2_lock)`.

### Validação e testes
- `tests/test_ngs2_voice_state.py`: execução dos dois modos (Baseline RED + Candidato GREEN) concluída em 1.90 s via Clang C11 (`-Wall -Wextra -Werror`).
- Suíte completa do repositório: 41 testes executados, 38 aprovados e 3 pulados por ausência de fixture externa. Zero regressões.



## HLE-2.2C — HEVAG mono/48 kHz — 18/10/2026 — GPT-5.6 Sol

Implementação autorizada na branch `feature/ngs2-hle22b-mixer`, checkpoint `5ca34c0`, sem commit
ou push. Alterados `port/src/runtime_tlg.c` e `tests/test_ngs2_atrac9_lifecycle.c`; criados
`tests/test_ngs2_hevag.c` e `tests/test_ngs2_hevag.py`. Mudanças locais anteriores em `.gitignore`,
este handoff, `tests/test_ngs2_atrac9_worker.py`, `fsr-vulkan`, `.agents/` e documentos foram
preservadas.

HEVAG `format=0x1c` usa frames de 16 bytes/28 amostras, 128 preditores de quatro coeficientes,
quatro históricos por voz, saturação PCM16 e flags compatíveis com a referência vgmstream. Worker,
snapshot, geração, fila PCM e mixer existentes foram reutilizados. Carry 28→256 atravessa segmentos;
cauda só recebe zero-padding após flag terminal ou comando de reset de segmentos. Roteamento ausente
gera diagnóstico limitado, sem ganho, pan ou matriz fabricados. ATRAC9 permaneceu no caminho anterior.

Validação: 31 testes passaram e 3 testes ATRAC9 com fixture externa ficaram skipped; suíte focada
HEVAG/lifecycle/HLE-1/mixer passou, incluindo os 25 contratos do mixer. Vetor HEVAG sintético de
64 frames produziu 1792 amostras, 7 grains e FNV-1a `4aa4887a67866efa`. Build Windows
`build.ps1 -Jobs 6` passou; SHA-256 de `port/out/tlg-probe.exe`:
`ed4d4449c14081800f6486bad2ce5a4ae5fdebcfb5b315752abf94d7490ba6e6`.

Boot final de 60 s: sessão `out/knack2/boot-hle22c-hevag-20261018/session-20261018-233351/`;
`eboot.bin` saiu com 75 e `game.elf` com 124 por timeout. AudioOut abriu quatro portas, ATRAC9
decodificou e o mixer publicou PCM não zero (`peak_pcm=0.0082`). O trecho não alcançou voz HEVAG;
áudio HEVAG audível permanece não verificado. Nenhum Windows 1450, STOP, assertion ou device lost
foi encontrado no log filtrado. Nenhum processo do teste permaneceu ativo.

> Atualização de 17/10/2026: **cenário inicial do Chapter 1-1 restaurado**, com
> iluminação, geometria, movimento e pausa verificados. A seção 6 registra as duas
> causas gráficas corrigidas, testes e capturas em execução normal.
> Seção 8 registra o diagnóstico técnico da ausência de áudio (causa raiz no stub silencioso de libSceNgs2).
> Marcos anteriores `09e6ccc` e `1e13acc` preservados em `knack2-windows`.
> Seções 1–5 preservam o histórico; restrições e diagnósticos antigos não descrevem
> o estado atual. Alterações gráficas desta etapa foram expressamente autorizadas.

## 9. Gate HLE-1 — núcleo NGS2: handles, racks e estados — 18/10/2026 — GPT-5.6 Sol

### Objetivo, autorização e baseline

Implementação autorizada do núcleo HLE-1 do NGS2, limitada a `port/src/runtime_tlg.c`, testes
sintéticos em `tests/**` e este handoff. Branch inicial `knack2-windows`, HEAD `5664a07`.
O worktree já continha o submódulo `port/gpu/third_party/fsr-vulkan` modificado e o script não
rastreado `scripts/inspect_soundx_ngs2.py`; ambos foram preservados. Nenhum commit, merge, pull ou
push foi executado.

Os relatórios nomeados HLE-0A e HLE-0A.1 não estavam presentes como arquivos no checkout ou nos
anexos disponíveis. A implementação usou os contratos aprovados no Gate HLE-1, o diagnóstico da
seção 8, o script de inspeção preservado e nova confirmação estática dos call sites. Essa ausência
permanece uma limitação documental, não uma alegação de que os relatórios nunca existiram.

### Arquivos modificados e arquitetura

- `port/src/runtime_tlg.c`: substituição do placeholder de handles por `Ngs2System`, `Ngs2Rack` e
  `Ngs2Voice`, mantendo `SystemRender` silencioso.
- `tests/test_ngs2_hle.c`: contrato nativo sintético para lifecycle, comandos, concorrência,
  limites de buffers e render silencioso.
- `tests/test_ngs2_hle.py`: compilação isolada com Clang e execução do contrato nativo.
- `HANDOFF_ASTRA.md`: esta seção, sem alteração do histórico anterior.

Handles são tokens opacos monotônicos e nunca são desreferenciados. A validação procura o token
somente nos registros internos protegidos por mutex. Objetos destruídos são removidos e liberados;
tokens antigos não são reutilizados. A mesma instância de rack e índice retorna o mesmo handle,
enquanto racks diferentes mantêm identidades independentes. Cada rack armazena `rack_id`, número
de instância e capacidade lida do campo confirmado em `option+0x20`, com limite de 4096 vozes.

O sistema registra grain e sample rate confirmados em `option+0x20` e `option+0x24`. O boot
validado criou o sistema com 256 amostras e 48 kHz. Callbacks de alocação guest continuam sem ser
invocados. Destruir rack ou sistema invalida seus handles e limpa conexões de outras vozes antes
de liberar armazenamento.

### Comandos implementados

Todo comando usa o cabeçalho `uint16 size`, `uint16 subTarget`, `uint32 commandId`. Leituras usam
`memcpy`, portanto não dependem de alinhamento do ponteiro guest. Tamanhos menores que o contrato
ou maiores que 4096 são rejeitados.

- `1`: copia imediatamente até 64 dwords do descritor temporário apontado em `+0x10`. O campo em
  `+0x0c` é tratado somente como quantidade confirmada de dwords. Logging limitado registra
  quantidade total e não zeros; nenhum dword é interpretado como ponteiro de áudio.
- `2` e `3`: armazenam `subTarget`, índice de canal e ganho finito para até oito canais.
- `5`: armazena índices da conexão e handle de destino validado; destruição do rack de destino
  remove a referência pendente. Nenhuma mixagem foi implementada.
- `6`: mantém estados internos Play, Pause, Resume, Release e Stop para valores 0, 1, 2, 4 e 5.
- `0x10000000`: armazena formato, canais, sample rate, codec config e campos brutos de `+0x18`,
  `+0x1c` e `+0x20`. Nenhuma semântica de endereço, loop ou streaming foi atribuída.
- `0x10000005`: armazena pitch ratio finito no intervalo `(0, 16]`.
- Outros comandos: retornam compatibilidade sem alterar estado e geram no máximo 32 mensagens
  diagnósticas por processo.

`VoiceGetState` aceita somente os tamanhos observados `0x10` e `0x30`, limpa exatamente o buffer
fornecido e continua publicando o estado externo legado idle. A máquina interna não avança no
`SystemRender` e nunca publica `FINISHED`. Nenhuma duração foi derivada do tamanho comprimido.
Nenhum decoder, codec, mixer, FX, AudioOut, AJM, ABI ou tabela de NIDs foi alterado.

### Testes e evidências

- Baseline antes da edição: `python -m unittest discover -s tests -v`: 12 testes passaram.
- Teste HLE-1 focado: passou após compilar com `-Wall -Wextra -Werror`.
- Suíte final: 13 testes passaram, incluindo HLE-1.
- Build focado `tlg-probe`: compilou `runtime_tlg.c` e vinculou com sucesso.
- `build.ps1 -Jobs 6`: passou e publicou `port/out/tlg-probe.exe`. Avisos existentes de políticas
  CMake e dois avisos de format string no overlay não pertencem ao HLE-1.
- Sessão válida: `out/knack2-hle1-live/session-20261018-023852/`, com save e artefatos isolados.
  `eboot.bin` terminou com 75 esperado; `game.elf` terminou com 124 por timeout de 30 segundos.
  O log confirmou `NGS2 HLE core initialized (silent, 48000 Hz, 256 samples)`, abriu as portas
  AudioOut 0, 1, 8 e 17, leu SaveData e abriu o vídeo inicial da fase. Nenhum `STOP`, assertion,
  segfault ou device lost foi encontrado no trecho.
- Uma sessão anterior em `out/knack2-hle1/` usou o artefato antigo de `port/out` e mostrou o
  placeholder anterior. Ela não conta como validação do HLE-1.

Não houve captura nem inspeção visual. O teste curto comprova preservação do caminho inicial de
boot até SaveData/vídeo, não título visível, gameplay, áudio audível, sincronismo ou estabilidade
prolongada. Render NGS2 continua produzindo silêncio por desenho deste Gate.

### Pendências e dados necessários para HLE-2

- Confirmar layouts e semântica dos comandos ainda aceitos sem estado, especialmente famílias
  `0x20000000`, `0x20000004`, `0x20010000`, `0x30000000`, `0x30000005` e `0x40020000`.
- Identificar origem e lifetime dos blocos de áudio referenciados pelos descritores do comando 1.
- Confirmar formatos `0x21` e `0x42` por contrato/dados, sem assumir HEVAG ou ATRAC9 apenas pelo ID.
- Determinar duração real, posição de reprodução, loops, streaming, resampling e critério legítimo
  para `FINISHED` antes de publicar estado ao SoundX.
- Definir grafo de sampler, submixers, mastering e FX antes de habilitar mistura entre racks.

HLE-2 não foi iniciado. Próximo passo exige revisão dos dois Tech Leads e nova autorização do usuário.

## 8. Diagnóstico técnico da ausência de áudio no Knack 2 — 17/10/2026

Investigação estritamente somente leitura da ausência de áudio relatada durante o gameplay.
Nenhum arquivo de código do runtime, ABI, memória, Vulkan ou decodificação foi alterado nesta etapa.

### 8.1. Arquivos Criados/Modificados
- `HANDOFF_ASTRA.md`: inclusão desta seção 8 de diagnóstico e handoff.
- `out/inspect_audio_imports.py` (em diretório `out/` ignorado pelo Git): ferramenta temporária para análise quantitativa de imports e relocations.
- Nenhum arquivo da Zona Bloqueada foi tocado.

### 8.2. Novas Interfaces/Headers
Nenhuma nova interface ou header implementado nesta rodada.

### 8.3. Anomalias Detectadas (Read-Only) e Causa Raiz
1. **Causa Raiz Comprovada:** O jogo utiliza a engine de som SoundX (`sndx` da Sony Japan Studio), cujos fluxos de sintetização, carregamento de vozes (`.bsxd`) e mixagem passam integralmente por **`libSceNgs2`** (`libSceNgs2.sprx`). No runtime atual, `libSceNgs2` é um placeholder silencioso em `port/src/runtime_tlg.c:266-320`.
   - Na linha 292 de `port/src/runtime_tlg.c`, `ngs2_render` executa:
     `for (uint32_t i=0;i<count;++i) if (buffers[i].buffer) memset(buffers[i].buffer,0,buffers[i].size);`
   - O thread guest `sndx_out_thread` solicita o render a cada ~5,3 ms via `sceNgs2SystemRender` e entrega os buffers zerados para `sceAudioOutOutputs`.
   - O runtime entrega esses buffers ao SDL3 WASAPI, gerando reprodução contínua de silêncio digital absoluto (`0.0f`).
2. **libSceAjm Não Utilizado:** `game.elf` e `eboot.bin` contêm exatamente 0 imports de `libSceAjm`. O log registra `Runtime: Ajm batches=0, jobs=0, ATRAC9 frames decoded=0`. AJM não faz parte do pipeline de Knack 2.
3. **libSceAudio3d Inativo:** Embora conste nos imports de `game.elf`, `sceAudio3dInitialize` jamais é chamado durante o boot e gameplay (0 chamadas registradas nos logs).
4. **Backend de Áudio e SDL3 Saudáveis:** SDL3 WASAPI abre com sucesso as portas 0, 1, 8 e 17. Telemetria confirma 938 buffers/5s estáveis nas portas 0, 1 e 17 com `starved 0` e fila de ~7–8 KB. Não há falha na entrega ao dispositivo físico.
5. **Classificação da Correção:** **ASTRA**. A implementação de `libSceNgs2` (seja por HLE com sampler/decodificadores de bancos `.bsxd` ou por LLE com `TLG_NGS2=native`) reside na Zona Bloqueada (`port/src/runtime_tlg.c` / núcleo de runtime).

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
