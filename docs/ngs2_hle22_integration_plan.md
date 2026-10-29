# HLE-2.2 — plano de integração ATRAC9/NGS2

> **Gate:** HLE-2.2-P
>
> **Autor:** GPT-5.6 Sol
>
> **Data:** 2026-10-18
>
> **Branch inspecionada:** `knack2-windows`
>
> **HEAD na inspeção inicial:** `276570c`
>
> **HEAD na revisão final:** `6856cce` (avanço externo durante o Gate; somente `.gitignore` nos commits intermediários)
>
> **Natureza:** planejamento; nenhuma alteração de runtime autorizada neste Gate

## 1. Decisão executiva

Recomendação: **GO condicional para HLE-2.2A; NO-GO para HLE-2.2B e HLE-2.2C até seus pré-requisitos específicos serem medidos**.

Primeira integração deve manter um decoder ATRAC9 persistente por voz, copiar cada segmento aceito para armazenamento host antes de retornar de `VoiceControl`, decodificar fora de `SystemRender` por meio de worker limitado e entregar PCM por FIFO SPSC. `SystemRender` não deve inicializar decoder, alocar memória, copiar bytes guest nem esperar produção de PCM. Falta de PCM resulta em silêncio e contador de underrun, nunca em espera.

Dois requisitos são arquiteturalmente obrigatórios:

1. uma operação de snapshot guest atômica em relação a map/unmap, pois `runtime_memory_is_mapped()` valida o intervalo, mas não o mantém mapeado durante `memcpy`;
2. uma barreira LibAtrac9 comum ao AJM e ao NGS2, pois `Atrac9InitDecoder()` reescreve tabelas globais lidas por `Atrac9Decode()`.

Um warm-up único não corrige o segundo problema. Qualquer inicialização posterior, inclusive com outra configuração, reabre a corrida.

HLE-2.2B deve ser um bypass diagnóstico opt-in, não uma alegação de reprodução fiel do grafo NGS2. O grafo completo Sampler → DynamicSubmixer → PreMaster → Master ainda não está implementado. HLE-2.2C continua experimental e não habilita áudio NGS2 por padrão.

## 2. Baseline e evidências

### 2.1 Estado inicial do repositório

Na inspeção inicial deste Gate:

- branch: `knack2-windows`;
- HEAD: `276570c`;
- `HANDOFF_ASTRA.md`, `port/src/runtime_tlg.c`, documentos e testes HLE/ATRAC9 já estavam modificados ou não rastreados;
- submódulo `port/gpu/third_party/fsr-vulkan` já estava sujo;
- `port/third_party/LibAtrac9` estava em `efca2e3` sem alteração local.

Durante a revisão final, HEAD avançou externamente de `276570c` para `6856cce` por três commits `Update .gitignore` (`f10d513`, `62e0b57`, `6856cce`). Este Gate não criou esses commits nem alterou `.gitignore`.

Essas mudanças são preexistentes e devem permanecer intactas. Este Gate cria somente este documento.

### 2.2 Evidência comprovada

- `runtime_tlg.c` mantém handles e estado HLE-1, mas `ngs2_render()` ainda preenche todos os buffers com zero.
- `VoiceGetState` retorna buffers zerados de `0x10` ou `0x30`; esse comportamento legado deve permanecer em HLE-2.2A/B.
- comando `0x10000001` observado possui `0x20` bytes:
  - `+0x08`: endereço dos bytes comprimidos;
  - `+0x10`: flag de 32 bits;
  - `+0x14`: flag de 32 bits;
  - `+0x18`: ponteiro para descritor temporário de `0x20` bytes;
  - descritor `+0x04`: tamanho comprimido em bytes.
- descritor foi observado em stack; todos os campos necessários devem ser copiados durante `VoiceControl`.
- mesmo endereço e tamanho reapareceram com marcador diferente. Dedupe por `(ponteiro, tamanho)` é incorreto.
- segmentos observados incluem `1.447.680`, `624.960`, `3.937.536` e `6.890.688` bytes. Limite de 256 KiB é incompatível.
- `FE7A17F0` foi validado como ATRAC9, 4 canais, 48 kHz, 4 frames por superframe, 768 bytes por superframe e 256 amostras por canal por frame.
- oito superframes consecutivos foram decodificados com um handle persistente: 32 frames, 6.144 bytes, 8.192 amostras por canal, RMS S16 `1287,705937`, pico `4116` e hash `a79abed856a64db5`.
- consumo real dentro de cada superframe foi `195, 191, 191, 191`; 192 é apenas média e não pode ser stride.
- reinicializar a cada superframe alterou PCM e elevou a maior descontinuidade de fronteira de 79 para 2.812. Estado do decoder deve sobreviver entre segmentos contíguos.
- dois handles produziram PCM idêntico quando usados alternadamente em uma thread. Concorrência simultânea não foi validada.
- sessão HLE-2.1A abriu portas AudioOut 0, 1 e 8 como float, 8 canais, 256 frames, 48 kHz, com downmix estéreo no host.
- `run.py` remove variáveis herdadas `TLG_*`, mas transmite `TLG_CONFIG=<out>/knack2.ini`. `port/run.py` transmite `TLG_CONFIG=<data>/tlg.ini`. O trace HLE-2.1A provou leitura da chave `ngs2_segment_trace=1` por esse caminho.

### 2.3 Hipóteses ainda não promovidas a contrato

- código SoundX `0x42` seleciona rota ATRAC9, enquanto comando waveform observado no NGS2 usa formato `0x40`. Não filtrar apenas por `0x42` dentro do NGS2.
- quatro canais da configuração 5 são dois blocos estéreo na LibAtrac9. Isso não prova semântica espacial FL/FR/BL/BR.
- comando 1 parece fornecer `numChannels × 8` floats de roteamento, mas orientação da matriz e grafo de destino precisam de confirmação antes de áudio real.
- significado final das flags de segmento, campos restantes do descritor, marcador zerado, loop, streaming e fim de voz não está fechado.
- `Ngs2RenderBuffer.waveform`, multiplicidade de buffers e todos os layouts aceitos ainda precisam de captura controlada.

Documentação anterior que chama `+0x08` de handle de bloco ou dá semântica fechada aos campos restantes do descritor deve ser tratada como hipótese histórica. Captura HLE-2.1A prevalece para este plano.

## 3. Contratos existentes que não podem regredir

- handles continuam opacos, estáveis e distintos por sistema/rack/voz;
- modo padrão continua silencioso;
- `VoiceGetState` continua zerado em HLE-2.2A/B;
- eventos Play/Pause/Resume/Stop/Release preservam semântica HLE-1;
- AudioOut, SDL3, formato da porta, volume e pacing não mudam;
- AJM continua funcional e usa a mesma LibAtrac9;
- nenhum bit `FINISHED` será publicado por duração estimada;
- nenhum acesso a bytes guest ocorre após snapshot;
- erro em uma voz não impede renderização silenciosa das demais;
- configuração ausente ou inválida equivale a HLE-1 atual.

## 4. Arquitetura proposta

### 4.1 Fluxo de dados

```text
SoundX VoiceControl(VOICE_ALLOC / VOICE_SEGMENT / estado / matriz)
        |
        | validar tamanho, configuração, limites e geração
        v
snapshot guest protegido contra map/unmap
        |
        v
fila host de Ngs2SegmentEvent --(worker limitado)--> handle ATRAC9 persistente
                                                    |
                                                    v
                                      FIFO S16 intercalado por voz
                                                    |
SystemRender: snapshot leve de vozes + dreno não bloqueante + matriz diagnóstica
        |
        v
PCM F32 PS4 de 8 canais e 256 frames
        |
        v
sceAudioOutOutputs -> volume/remapeamento/downmix de runtime_audio.c -> SDL3/WASAPI
```

Controle e cópia ocorrem fora de `SystemRender`. Decoder worker produz antecipadamente. Render apenas consome PCM pronto, converte S16 para F32, acumula, mede e limita saída experimental.

### 4.2 Estruturas propostas

Estruturas abaixo são internas ao host. Não alteram ABI guest.

```c
typedef enum {
    NGS2_AT9_UNINITIALIZED,
    NGS2_AT9_READY,
    NGS2_AT9_DRAINING,
    NGS2_AT9_FAULTED,
    NGS2_AT9_CLOSING
} Ngs2Atrac9InitState;

typedef enum {
    NGS2_SEGMENT_UNKNOWN,
    NGS2_SEGMENT_RESIDENT,
    NGS2_SEGMENT_STREAMING
} Ngs2SegmentKind;

typedef struct {
    uint64_t sequence;          /* nunca derivado só do ponteiro */
    uint64_t generation;
    uintptr_t guest_address;    /* diagnóstico; nunca desreferenciar após enqueue */
    uint32_t byte_count;
    uint32_t flag_10;
    uint32_t flag_14;
    uint32_t descriptor_words[6];
    uint64_t descriptor_18;
    uint8_t *host_bytes;
    size_t cursor;              /* sempre no início de um superframe */
    Ngs2SegmentKind kind;
} Ngs2SegmentEvent;

typedef struct {
    uint64_t generation;
    uint64_t first_sample;
    uint32_t frames;
    uint8_t channels;
    int16_t pcm[256 * 8];
} Ngs2PcmGrain;

typedef struct {
    void *decoder;
    Atrac9CodecInfo info;
    uint8_t config[4];
    Ngs2Atrac9InitState init_state;

    Ngs2SegmentEvent segments[8];
    uint32_t segment_read;
    uint32_t segment_write;
    uint64_t queued_compressed_bytes;

    Ngs2PcmGrain fifo[8];
    _Atomic uint32_t pcm_read;
    _Atomic uint32_t pcm_write;
    uint32_t pcm_read_offset;

    uint64_t compressed_cursor;
    uint64_t decoded_sample_cursor;
    uint64_t rendered_sample_cursor;
    uint64_t decoded_frames;
    uint64_t decode_errors;
    uint64_t underruns;

    _Atomic uint64_t generation;
    _Atomic uint32_t worker_refs;
    _Atomic uint32_t render_refs;
    _Atomic int cancel_requested;
} Ngs2VoiceAtrac9;
```

`Ngs2Voice` recebe ponteiro opcional para `Ngs2VoiceAtrac9`, alocado somente após configuração ATRAC9 aceita e somente no modo opt-in. Vocações não ATRAC9 não pagam custo de handle/FIFO.

`Atrac9Handle` mede aproximadamente 92.352 bytes no ABI/toolchain atual. Alocação preguiçosa é obrigatória: 256 handles antecipados custariam cerca de 22,6 MiB, sem contar filas e segmentos.

FIFO de oito grains custa até 32 KiB por voz de oito canais e 16 KiB por voz de quatro canais. O tamanho cobre aproximadamente 42,7 ms a 48 kHz; será ajustado por medida, não por intuição.

### 4.3 Decoder e cursores

- configuração recebida como inteiro deve ser convertida para ordem canônica dos quatro bytes: `FE 7A 17 F0`; não passar representação little-endian `F0 17 7A FE`;
- após `Atrac9InitDecoder`, validar `Atrac9GetCodecInfo`: canais 1..8, `frameSamples` 1..256, `framesInSuperframe` positivo, `superframeSize` positivo e coerente;
- decoder permanece associado à mesma voz e geração enquanto configuração e continuidade não mudarem;
- novo waveform com configuração diferente, Stop/Release ou destruição incrementa geração e cancela trabalho antigo;
- Pause impede consumo do FIFO, mas não destrói decoder; worker pode parar ao atingir high-water mark;
- Resume reutiliza estado e PCM já preparado;
- Reset confirmado limpa fila, FIFO e decoder. Marcador de segmento zerado não será tratado como Reset sem contrato adicional.

Para cada superframe:

1. exigir `remaining >= info.superframeSize` antes da primeira chamada;
2. manter `superframe_offset = 0`;
3. chamar `Atrac9Decode` `framesInSuperframe` vezes;
4. validar retorno zero, `bytesUsed > 0` e `bytesUsed <= superframeSize - superframe_offset`;
5. produzir exatamente `frameSamples × channels` S16 por chamada;
6. exigir fechamento exato `superframe_offset == superframeSize`;
7. avançar cursor do segmento pelo tamanho validado do superframe.

`bytesUsed` governa o avanço entre frames. `superframeSize` governa o limite externo. Nenhum caminho usa 192 ou outro stride médio.

Quando grain do sistema difere de 256, FIFO mantém `pcm_read_offset` e permite consumo parcial/concatenado. Decodificação continua em blocos naturais da biblioteca.

### 4.4 Thread de decode

HLE-2.2A começa com um worker NGS2 por sistema, fila de jobs limitada e no máximo um job em voo por voz. Motivos:

- elimina decode e init de `SystemRender`;
- mantém ordem determinística por voz;
- limita concorrência antes de teste de thread safety;
- permite cancelar por geração sem tocar ponteiros guest.

Se throughput de um worker for insuficiente, expansão para pool só recebe GO após teste concorrente da LibAtrac9 e uso da barreira compartilhada descrita abaixo. Jobs de mesma voz nunca executam simultaneamente.

Worker preenche FIFO até high-water mark e cede para outras vozes. Não decodifica segmento inteiro antecipadamente. Fila cheia suspende aquela voz; não descarta PCM nem bloqueia render.

`SystemRender` usa dreno SPSC sem mutex. Estado de roteamento é copiado para snapshot host curto; objeto da voz ganha `render_refs` antes de liberar lock global. Render nunca conserva ponteiro de voz sem referência.

### 4.5 Locks, cancelamento e destruição

Ordem de locks:

```text
ngs2 registry lock -> voice control lock -> LibAtrac9 gate
```

Código não pode adquirir `ngs2_lock` enquanto segura barreira LibAtrac9. Render não adquire barreira LibAtrac9.

Destruição:

1. remover handle da tabela visível sob `ngs2_lock`;
2. marcar voz `CLOSING`, incrementar geração e solicitar cancelamento;
3. remover jobs ainda não iniciados;
4. mover voz para lista de aposentadoria;
5. retornar do `RackDestroy` sem esperar sob `ngs2_lock`;
6. worker libera decoder, snapshots e voz somente quando `worker_refs == 0` e `render_refs == 0`;
7. `SystemDestroy` sinaliza término, aguarda worker fora de locks de registro, drena lista aposentada e encerra thread.

Isso preserva invalidação imediata de handles e evita use-after-free. Timeout de shutdown deve gerar erro explícito e relatório; nunca liberar objeto ainda referenciado.

## 5. Concorrência da LibAtrac9

### 5.1 Risco comprovado no código

`InitDecoder()` em `decinit.c` chama:

- `InitMdctTables()`;
- `InitHuffmanCodebooks()`;
- `GenerateGradientCurves()`.

Essas rotinas reescrevem:

- `MdctWindow`, `ImdctWindow`, `SinTables`, `CosTables` e `ShuffleTables` em `tables.c`;
- campos `Lookup` dos codebooks Huffman em `huffCodes.c`;
- `GradientCurves` em `bit_allocation.c`.

`Atrac9Decode()` lê essas estruturas. Init concorrente com Decode constitui data race em C mesmo quando valores calculados são determinísticos. Configurações diferentes podem selecionar janelas diferentes, mas Init reescreve também tabelas comuns; separar por codec config não resolve.

`runtime_ajm.c` serializa suas próprias APIs com mutex recursivo, porém futuro NGS2 não participa desse mutex. Portanto o estado atual não oferece exclusão entre AJM e NGS2.

### 5.2 Serviço compartilhado proposto

Criar adaptador host comum, por exemplo `runtime_atrac9.c/.h`, usado por AJM e NGS2:

- `runtime_atrac9_create()` / `runtime_atrac9_release()` administram handle local;
- `runtime_atrac9_init()` usa write lock global e engloba `Atrac9InitDecoder` + `Atrac9GetCodecInfo`;
- `runtime_atrac9_decode()` usa read lock global;
- lifecycle por instância impede Release concorrente com Decode do mesmo handle.

Read lock permite Decodes simultâneos em handles independentes depois da inicialização. Write lock impede qualquer Decode durante Init. Primeira versão pode configurar um modo de diagnóstico que serializa também Decodes para comparação, mas não deve manter mutex global simples no render — decode ocorre no worker.

AJM deve migrar todas as chamadas diretas para o adaptador no mesmo Gate que introduzir NGS2. Proteger somente NGS2 deixa corrida aberta.

### 5.3 Testes obrigatórios de concorrência

- Init FE7A17F0 em loop enquanto outro handle decodifica fixture autorizada; PCM deve manter hash determinístico com barreira;
- alternar Init FE7A17F0 e FE7829F0 enquanto Decodes de quatro canais continuam;
- duas vozes decodificando simultaneamente, cada uma com handle próprio e sequência idêntica; comparar `bytesUsed` e PCM;
- destruir uma voz enquanto job aguarda e enquanto job está ativo;
- executar caminho AJM sintético e NGS2 simultaneamente;
- stress com pelo menos 10.000 inits/decodes curtos sem crash, corrupção ou divergência;
- ThreadSanitizer em plataforma suportada. Ausência de TSAN no Windows deve ser registrada, não tratada como sucesso.

Um teste serial de dois handles e um warm-up isolado não satisfazem esses critérios.

## 6. Segmentos, ownership e orçamento

### 6.1 Snapshot seguro

`runtime_memory_is_mapped()` sozinho possui janela TOCTOU. Política correta:

1. validar soma `address + size` contra overflow;
2. validar tamanho e orçamento antes de alocar;
3. alocar armazenamento host;
4. copiar por API de memória guest que mantenha read lock da tabela VMA durante validação e cópia;
5. liberar lock antes de qualquer operação NGS2/decoder;
6. publicar evento somente depois de cópia completa;
7. em falha, liberar alocação e manter fila anterior intacta.

API estreita proposta: `runtime_memory_copy_snapshot(void *dst, uintptr_t src, uint64_t size)`. Implementação deve conferir mappings e permissões de leitura sob lock de tabela. Ela requer autorização explícita futura para `runtime_memory.c`/`runtime.h`; não deve ser simulada com `is_mapped()` seguido de `memcpy`.

Após snapshot, decoder acessa somente `host_bytes`. Reciclagem, unmap ou alteração posterior do guest não afeta evento já aceito.

### 6.2 Limites iniciais

Limites experimentais propostos:

| Recurso | Limite inicial | Justificativa |
|---|---:|---|
| segmento individual | 16 MiB | mais de 2× maior segmento observado de 6.890.688 bytes |
| bytes comprimidos por voz | 16 MiB | comporta duas ocorrências do maior segmento observado |
| eventos por voz | 8 | fila limitada; mesma origem ainda pode gerar eventos distintos |
| orçamento comprimido global | 128 MiB | evita multiplicação por 256 vozes sem bloquear casos observados |
| FIFO PCM por voz | 8 grains | até 32 KiB por voz, ~42,7 ms a 48 kHz |

Limites são defesa de memória, não descrição de formato. Métricas reais podem justificar revisão em Gate separado. Não reduzir abaixo dos recursos observados para obter teste artificialmente simples.

Nenhum dedupe automático. Futuro compartilhamento de blobs exigirá identidade de banco comprovada, bytes imutáveis e referência por evento; igualdade de ponteiro/tamanho/fingerprint parcial não basta.

### 6.3 Enqueue, falhas e tempo

- cópia ocorre em `VoiceControl`, nunca no worker após retorno;
- `ngs2_lock` não permanece tomado durante alocação/cópia grande;
- publicação usa geração: se voz mudou durante cópia, snapshot é descartado com segurança;
- falha de tamanho, mapping, permissão, orçamento ou alocação marca falha diagnóstica da voz e retorna erro NGS2 no modo experimental; modo padrão mantém contrato HLE-1;
- não retornar sucesso depois de aceitar ponteiro inseguro;
- medir tempo de alocação e cópia. Soft warning inicial: 2 ms. Não abortar cópia parcialmente por relógio;
- `VoiceControl` p99 acima de 5 ms exige redesign ou cache comprovado antes de HLE-2.2C.

### 6.4 Bancos residentes versus streaming

Primeira integração ao vivo aceita somente recurso residente previamente identificado. Classificação geral de streaming permanece **não comprovada**: flags e campos restantes do descritor ainda não fornecem contrato suficiente.

Regras:

- `Ngs2SegmentKind` começa `UNKNOWN`;
- HLE-2.2A testa filas e snapshot com dados sintéticos/fixture temporária, sem afirmar streaming;
- HLE-2.2B só mistura caso captura adicional associe evento a banco residente e confirme sequência/marcador;
- evento `UNKNOWN` ou `STREAMING` pode ser observado e contabilizado, mas permanece silencioso;
- marcador zerado é evento/barreira até prova de que significa reset ou fim; não apaga dados automaticamente;
- loops, seek, reposição incremental e espera por próximo bloco ficam fora do primeiro decoder.

Se fonte residente não puder ser reconhecida sem fingerprint proprietário hardcoded, HLE-2.2B é NO-GO e exige instrumentação curta adicional. Runtime não deve incorporar hash de asset como contrato permanente.

## 7. Roteamento e downmix

### 7.1 Comportamento real do AudioOut atual

Para porta PS4 de oito canais, `runtime_audio.c` interpreta entrada como:

```text
L, R, C, LFE, SL, SR, BL, BR
```

Antes de SDL, remapeia para:

```text
FL, FR, C, LFE, BL, BR, SL, SR
```

Quando dispositivo físico é estéreo, downmix atual é:

```text
left  = L + 0,70710678 * (C + SL + BL)
right = R + 0,70710678 * (C + SR + BR)
```

LFE não contribui. HLE-2.2 não modifica esse código.

### 7.2 Fontes ATRAC9 de quatro canais

LibAtrac9 descreve configuração 5 como dois blocos estéreo. Não atribuir FL/FR/BL/BR por posição.

Bypass diagnóstico proposto usa matriz enviada pelo SoundX somente depois de confirmar:

- `descriptor_words == source_channels × 8`;
- orientação fonte/destino por disassembly e captura;
- coeficientes finitos e dentro de limite defensivo;
- correspondência entre matriz, voz e geração.

Fórmula pretendida após confirmação:

```text
out[frame][dst] += (pcm_s16[frame][src] / 32768.0f) * route[src][dst]
```

Isso evita inventar layout dos quatro canais. Se matriz estiver ausente ou ambígua, voz permanece silenciosa e registra razão limitada. Não haverá fallback implícito FL/FR/BL/BR.

Comandos 2/3 e conexões de rack permanecem armazenados sem serem descartados. Aplicação multiplicativa desses ganhos depende de confirmação estática/dinâmica de sua ordem e escopo. Até lá, áudio é denominado **bypass de matriz**, não mixer NGS2.

### 7.3 Ganho e clipping experimental

- coeficientes SoundX não são normalizados nem substituídos;
- usar acumulador F32 de oito canais;
- aplicar ganho mestre diagnóstico fixo de `0,25` (-12,04 dB) somente quando HLE ATRAC9 opt-in estiver ativo;
- registrar ganho no início da sessão;
- limitar amostra final a `[-1, 1]` e contar clamps;
- RMS/pico são medidos antes e depois do ganho/clamp;
- qualquer opção futura de ganho fica no mesmo arquivo INI e continua opt-in.

Esse ganho é proteção auditiva do experimento, não comportamento original do NGS2. HLE-2.2C deve comparar clipping e loudness antes de qualquer ajuste.

## 8. Estados e compatibilidade

### 8.1 Chave opt-in

Chave proposta:

```ini
ngs2_atrac9_hle=1
```

Ausência ou valor falso mantém caminho HLE-1 byte a byte: mesmos handles, estados e render silencioso. Leitura ocorre uma vez por processo por `TLG_CONFIG`.

Transporte comprovado:

- launcher raiz: `<out>/knack2.ini`;
- launcher empacotado: `<data>/tlg.ini`;
- variável herdada `TLG_NGS2_*` não é mecanismo confiável porque `run.py` filtra `TLG_*`.

HLE-2.2A deve adicionar teste do parser e log único `enabled/disabled`, sem expor caminhos privados.

### 8.2 Máquina interna

- Play: permite worker e consumo;
- Pause: preserva decoder, segmentos e FIFO; render não consome voz;
- Resume: retoma sem reset;
- Stop: incrementa geração, cancela fila/FIFO e reinicializa decoder antes de novo Play;
- Release: invalida geração e agenda aposentadoria de recursos;
- waveform/config alterado: nova geração;
- mesmo ponteiro/tamanho com novo marcador: novo evento da mesma geração, salvo reset explícito comprovado.

`VoiceGetState` continua legado. Estados internos `source_exhausted` e `pcm_drained` servem apenas a métricas. FINISHED só poderá ser publicado quando houver prova conjunta de marcador de fim, ausência de loop/streaming pendente, consumo integral e FIFO drenado.

Teste ao vivo deve verificar se estado externo sempre idle faz SoundX reutilizar voz antes da hora. Registrar `(rack instance, voice index, handle, generation)` em cada alloc/segment/play/stop/release com rate limit. Reutilização prematura é condição NO-GO, não motivo para inventar FINISHED.

## 9. Métricas por grain

Sem log por grain em operação normal. Acumular contadores e emitir resumo periódico opt-in:

- número do grain e duração em microssegundos;
- vozes Play, vozes com PCM, vozes silenciosas e vozes em fault;
- frames ATRAC9 decodificados e bytes consumidos;
- profundidade mínima/média/máxima do FIFO;
- underruns e overflows por voz;
- RMS e pico por cada um dos oito canais, antes/depois do ganho;
- samples clampados e NaN/Inf rejeitados;
- tempo de render p50/p95/p99/máximo;
- tempo de decode e espera da barreira LibAtrac9;
- tempo/cópia de enqueue e bytes de snapshot;
- orçamento comprimido atual e máximo;
- alocações dentro de `SystemRender` — obrigatório zero.

Metas iniciais para máquina de teste, com grain de 5,333 ms:

- `SystemRender` p99 ≤ 1,33 ms (25% do grain);
- nenhum render ≥ 5,333 ms;
- nenhuma espera por worker em render;
- nenhum underrun depois do prefill em caso residente de título;
- nenhum NaN/Inf;
- clamps abaixo de 0,01% das amostras; acima disso bloqueia teste audível prolongado.

Metas devem ser medidas com 1, 8 e 32 vozes sintéticas. Falha não autoriza aumentar buffering cegamente; identificar decode, lock ou mix como causa.

## 10. Etapas de implementação

### 10.1 HLE-2.2A — decoder persistente e FIFO, sem mix ativo

**Escopo futuro necessário**

- `port/src/runtime_tlg.c`: estado por voz, parsing de segmento, filas, worker, lifecycle e opt-in;
- novo adaptador compartilhado `port/src/runtime_atrac9.c/.h`;
- `port/src/runtime_ajm.c`: migrar chamadas LibAtrac9 ao adaptador;
- `port/src/runtime_memory.c` e `port/src/runtime.h`: snapshot guest protegido;
- build system: incluir novo adaptador;
- `tests/**`: contratos sintéticos, concorrência, budgets e harness autorizado.

Arquivos protegidos e build exigem autorização expressa nova. Se essa ampliação não for aprovada, HLE-2.2A pode existir apenas como protótipo isolado em testes; não como integração segura ao runtime.

**Mudanças previstas**

- alocar decoder/FIFO por voz apenas em modo opt-in;
- snapshot de `0x10000001` com geração e limites;
- worker preenche FIFO, mas `SystemRender` continua zerando buffers;
- migrar AJM para barreira comum;
- preservar `VoiceGetState` e máquina HLE-1 externa.

**Aceite**

- todos os testes HLE-1 passam com chave ausente;
- mesma sequência real temporária produz hash contínuo conhecido por voz;
- duas vozes mantêm estado independente;
- segmentos repetidos com mesmo ponteiro/tamanho e marcador diferente geram dois eventos;
- truncamento, overflow, unmap simulado, OOM e budgets falham sem UAF/leak;
- Stop/Reset/Destroy concorrentes não causam decode após geração inválida;
- Init/Decode AJM+NGS2 passam stress sem divergência;
- `SystemRender` permanece silencioso e sem alocação;
- tempos de snapshot/decode/lock documentados.

**Rollback**

- remover chave INI restaura caminho HLE-1 sem usar worker ou decoder;
- falha de init de uma voz desativa apenas essa voz;
- nenhum schema de save, ABI ou asset é alterado.

### 10.2 HLE-2.2B — PCM F32 em bypass diagnóstico

**Escopo futuro necessário**

- `port/src/runtime_tlg.c` e testes focados;
- nenhuma alteração esperada em `runtime_audio.c`.

**Pré-requisitos**

- HLE-2.2A aceito;
- layout real de `Ngs2RenderBuffer` capturado;
- matriz comando 1 confirmada;
- banco residente distinguível sem heurística frágil;
- latência dentro das metas.

**Mudanças previstas**

- zerar buffers como hoje;
- para buffer estritamente validado como F32/8 canais/48 kHz, drenar FIFO e acumular matriz;
- preservar silêncio em buffer desconhecido ou voz sem PCM;
- aplicar ganho diagnóstico/clamp e métricas;
- não implementar submixers, efeitos, loops, streaming ou FINISHED.

**Aceite**

- tamanho esperado: `grain × 8 × sizeof(float)` ou contrato capturado equivalente;
- guard pages provam ausência de escrita fora do buffer;
- testes de matriz unitária e matrizes esparsas produzem oito canais exatos;
- 4 canais nunca recebem rótulo espacial implícito;
- RMS/pico não nulos para fixture real; silêncio legítimo continua sucesso de decode;
- modo padrão permanece byte a byte silencioso;
- sessão curta mostra PCM chegando ao AudioOut sem mudança de pacing.

**Rollback**

- chave off restaura `memset` atual;
- erro ou underrun de voz mistura silêncio somente naquele grain;
- formato inesperado bloqueia mix e mantém buffer zerado.

### 10.3 HLE-2.2C — teste audível real

**Escopo**

- configuração INI local e logs isolados;
- somente correções estreitas demonstradas em HLE-2.2A/B;
- nenhuma habilitação padrão.

**Sessões**

1. A: modo padrão, boot/título/menu;
2. B: ATRAC9 opt-in, mesmo cenário e duração;
3. A: retorno ao padrão para detectar regressão;
4. se estável, Chapter 1-1 curto com save/output isolados.

**Aceite**

- boot, menu, cutscene inicial, SaveData e transição já funcional não regridem;
- música/diálogo ATRAC9 audível corresponde ao evento esperado;
- sem estalos sistemáticos em fronteiras, drift progressivo ou voz terminando cedo;
- zero deadlock/UAF/assert;
- underrun, clipping, RMS/pico, render p99 e fila ficam nos limites;
- desligar chave restaura baseline no mesmo binário.

**Rollback**

- parar sessão ao primeiro risco de volume, corrupção, deadlock ou save;
- preservar logs e diretório isolado;
- desativar chave; não reverter mudanças preexistentes nem apagar evidência.

## 11. Matriz de testes

| Camada | Teste | Resultado exigido |
|---|---|---|
| HLE-1 | handles, comandos, estado e render silencioso | sem regressão |
| config | chave ausente, 0, 1 e inválida em `knack2.ini`/`tlg.ini` | default off; log único |
| parser | comandos menores que `0x20`, ponteiros nulos, overflow | rejeição antes de ler campos |
| snapshot | range mapeado, unmap concorrente, reciclagem após retorno | cópia íntegra ou erro; nunca acesso tardio |
| budget | 1.447.680, 6.890.688, 16 MiB e acima do limite | casos observados aceitos; excesso rejeitado |
| eventos | mesmo ponteiro/tamanho com marcadores distintos | duas sequências preservadas |
| decoder | 2/4/8 superframes FE7A17F0 | bytes, PCM e hash conhecidos |
| cursor | `195/191/191/191`, final parcial | fechamento exato; parcial sem Decode |
| estado | Pause/Resume/Stop/Reset/Release | continuidade ou cancelamento conforme contrato |
| lifecycle | Destroy com worker/render ativos | sem UAF, leak ou espera sob lock global |
| concorrência | AJM Init versus NGS2 Decode | sem race/divergência |
| handles | duas vozes e duas configs | estado isolado |
| FIFO | underflow, overflow e wrap | silêncio/controle sem corrupção |
| mixer | matrizes sintéticas 1→8, 4→8 e 8→8 | F32 exato dentro de tolerância |
| buffer | size/layout/count inválidos, guard pages | zero ou erro; nenhuma escrita fora |
| performance | 1/8/32 vozes, 10 min sintético limitado | metas de grain e zero alocação em render |
| jogo | A/B/A curto, título e Chapter 1-1 | áudio experimental sem regressão estrutural |

Fixtures proprietárias permanecem temporárias, fora do Git. Logs guardam apenas origem, offsets, tamanhos, métricas e hashes necessários; nunca bytes brutos completos.

## 12. Matriz de riscos

| Risco | Probabilidade | Impacto | Mitigação | Condição NO-GO |
|---|---|---|---|---|
| Init/Decode concorrente corrompe tabelas globais | alta sem gate | crítico | adaptador comum RW e migração AJM | qualquer chamada direta restante |
| unmap entre validação e cópia | média | crítico | snapshot sob lock VMA | somente `is_mapped()+memcpy` |
| use-after-free em Rack/SystemDestroy | média | crítico | geração, refs e aposentadoria | free síncrono sob lock atual |
| worker atrasa | média | alto | FIFO, high-water, métricas | render espera decoder |
| memória comprimida explode | média | alto | 16 MiB/voz, 128 MiB global | alocação sem budget |
| dedupe perde evento | alta se usado | alto | sequência/geração; sem dedupe inicial | chave só por ponteiro/tamanho |
| cursor ATRAC9 desalinha | alta com stride médio | alto | bytesUsed + fechamento do superframe | stride 192 |
| 4 canais roteados incorretamente | alta sem matriz | alto | matriz SoundX comprovada | FL/FR/BL/BR presumido |
| volume perigoso/clipping | média | alto | -12 dB opt-in, clamp e métricas | NaN/Inf ou clamp >0,01% |
| idle externo causa reciclagem | desconhecida | alto | telemetria de geração; sem FINISHED | voz reaproveitada cedo |
| marcador zerado interpretado errado | alta | alto | tratar como barreira até prova | flush automático sem evidência |
| streaming confundido com banco | alta | alto | residente apenas no primeiro live gate | fonte UNKNOWN misturada |
| formato de render incorreto | média | crítico | validação estrita + guard pages | layout não capturado |
| regressão AJM | média | alto | wrappers equivalentes + testes AJM | mudança de PCM/status AJM |
| custo de cópia grande | média | médio | medir, copiar fora de locks NGS2 | p99 VoiceControl >5 ms |

## 13. Critérios GO/NO-GO

### GO HLE-2.2A

- nova autorização cobre arquivos protegidos, adaptador compartilhado, snapshot de memória e build;
- revisão aceita worker e budgets;
- fixture temporária autorizada continua disponível para validação real;
- plano de testes de concorrência é executável no ambiente.

### NO-GO HLE-2.2A

- tentativa de proteger LibAtrac9 só no NGS2;
- snapshot baseado apenas em verificação seguida de cópia desprotegida;
- decoder reinicializado por superframe;
- decoder ou alocação dentro de `SystemRender`.

### GO HLE-2.2B

- HLE-2.2A aceito, zero regressão HLE-1/AJM;
- output F32 de oito canais e matriz comando 1 confirmados;
- recurso residente identificado;
- métricas de latência e memória dentro dos limites.

### NO-GO HLE-2.2B

- roteamento de quatro canais depende de palpite;
- streaming/loop é necessário para primeiro cenário;
- estado idle reaproveita voz antes de FIFO drenar;
- render precisa esperar worker ou LibAtrac9.

### GO HLE-2.2C

- sessão muda somente pela chave INI;
- proteção auditiva ativa;
- saves e saída isolados;
- rollback imediato comprovado.

HLE-2.2C ainda não autoriza ativação padrão. Isso exige Gate posterior, cobertura maior de codecs/vozes e revisão GPT-6 + Argon.

## 14. Escopo mínimo e pendências

Escopo mínimo para primeira saída PCM real:

1. ATRAC9 FE7A17F0, 4 canais, 48 kHz;
2. um recurso residente comprovado;
3. snapshot host integral;
4. decoder persistente por voz;
5. worker único e FIFO de oito grains;
6. saída F32 de oito canais por matriz SoundX confirmada;
7. ganho diagnóstico opt-in;
8. `VoiceGetState` legado e sem FINISHED.

Fora do escopo:

- HEVAG/VAG/PCM adicionais;
- streaming, loops, seek e gapless SoundX;
- efeitos, submixers e grafo NGS2 fiel;
- sincronização labial certificada;
- configuração ATRAC9 de oito canais em áudio real;
- ativação padrão.

Pendências que bloqueiam fidelidade, mas não o protótipo HLE-2.2A isolado:

- semântica completa das flags/descritor `0x10000001`;
- marcador zerado e lifetime de lista;
- orientação da matriz comando 1;
- estados externos esperados pelo SoundX;
- classificação robusta de residente/streaming;
- benchmark concorrente da LibAtrac9;
- contrato completo de `Ngs2RenderBuffer`.

Conclusão: arquitetura suporta primeira prova PCM com rollback simples e risco contido, desde que snapshot VMA e barreira LibAtrac9 sejam tratados como requisitos, não otimizações. Implementação não começa neste Gate.

## 15. Validação deste Gate

Testes sintéticos curtos executados antes da entrega:

```text
.\.venv\Scripts\python.exe -m unittest tests.test_ngs2_hle tests.test_atrac9_decode_harness -v

Ran 7 tests in 2.377s
OK
```

Cobertura desta execução: lifecycle/comandos/render silencioso HLE-1, metadados FE7A17F0 e FE7829F0, ordem de bytes da configuração, rejeição de superframe parcial, dois handles serialmente independentes e flags incompatíveis do harness. Não foi executada sessão do jogo, benchmark, teste audível ou decode concorrente.

Estado final esperado do Gate: todo estado preexistente preservado, mais `docs/ngs2_hle22_integration_plan.md` como único arquivo novo desta tarefa. Como o documento permanece não rastreado, `git diff` comum não mostra seu conteúdo; a revisão equivalente é `git diff --no-index -- NUL docs/ngs2_hle22_integration_plan.md`. Nenhum arquivo foi staged, commitado, mesclado ou enviado.
