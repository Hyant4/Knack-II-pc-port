/* SPDX-License-Identifier: GPL-2.0-or-later
 * Independent Synthetic Test Suite for NGS2 ATRAC9 Stream Completion and Lifecycle.
 *
 * Implements strict contracts per Tech Lead Sol directive (DECISAO_EOF_E_TESTES_CANDIDATO.md):
 *  1. Initial PLAY without FIFO: ACTIVE/PLAYING without FINISHED; queries idempotent.
 *     Tested with 0x30 on Sampler rack and 0x10 on SubMixer rack.
 *  2. Waiting between segments: inter-segment buffering without null command maintains ACTIVE=1.
 *     Subsequent segment decoded without false EOF.
 *  3. Explicit finite EOF: two segments + null command (0x10000001 with 0 bytes/desc).
 *     Remains ACTIVE during playback; transitions to ACTIVE=0, FINISHED=1 only after complete drain.
 *  4. Pause/Resume with pending EOF: PAUSE preserves FIFO and generation under render (ACTIVE=1, PLAYING=0).
 *     RESUME restores playback; completion occurs only after final drain.
 *  5. EOS during copy barrier: blocking snapshot barrier in separate thread while null command is issued.
 *     Voice cannot terminate while reservation/copy is inflight.
 *  6. Cancellation during decode: blocking decode barrier; Cmd 6 Action 3 advances generation,
 *     discards in-flight decode without leaking into FIFO, and stops voice without natural FINISHED.
 *     Voice B demonstrates strict voice isolation.
 *  7. STOP and reuse: Cmd 6 Action 2 cancellation; new PLAY does not inherit prior FINISHED.
 *     Equivalent waveform reconfig preserves open stream; changed waveform advances generation.
 *
 * Two explicit execution modes:
 *  - Baseline diagnostic mode (default): confirms exact RED predicates on missing contracts.
 *  - Positive contract mode (--assert-positive-contract): requires all assertions to succeed (exit 0).
 */

#include <assert.h>
#include <math.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#include "../port/src/runtime_tlg.c"

/* Proven bit masks from game.elf instructions (0x00b6f008..0x00b6f045) */
#define SCE_NGS2_STATE_ACTIVE    0x00000001u /* Bit 0: mask 0x01 tested at 0x00b6f040 */
#define SCE_NGS2_STATE_PLAYING   0x00000002u /* Bit 1: mask 0x22 tested at 0x00b6f013 */
#define SCE_NGS2_STATE_FINISHED  0x00000020u /* Bit 5: mask 0x20 tested at 0x00b6f026 */

/* Mock synchronization state for deterministic concurrency testing */
struct RuntimeAtrac9 { unsigned frame, channels; };
static pthread_mutex_t mock_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t mock_cond = PTHREAD_COND_INITIALIZER;
static int snapshot_block, snapshot_entered, decode_block, decode_entered;
static unsigned atrac9_init_calls __attribute__((unused));
static unsigned snapshot_calls __attribute__((unused));
static int fail_snapshot,fail_decode;

RuntimeAtrac9 *runtime_atrac9_create(void) { return calloc(1, sizeof(RuntimeAtrac9)); }
void runtime_atrac9_release(RuntimeAtrac9 *decoder) { free(decoder); }

int runtime_atrac9_init(RuntimeAtrac9 *decoder, const unsigned char config[4], Atrac9CodecInfo *info) {
    ++atrac9_init_calls;
    assert(decoder);
    memset(info, 0, sizeof(*info));
    if (!memcmp(config, "\xfe\x7a\x17\xf0", 4)) {
        decoder->channels = 4; info->channels = 4; info->superframeSize = 768;
    } else {
        assert(!memcmp(config, "\xfe\x78\x29\xf0", 4));
        decoder->channels = 8; info->channels = 8; info->superframeSize = 1344;
    }
    info->samplingRate = 48000;
    info->framesInSuperframe = 4;
    info->frameSamples = 256;
    return 0;
}

int runtime_atrac9_decode(RuntimeAtrac9 *decoder, const unsigned char *input, int16_t *pcm, int *used) {
    (void)input;
    pthread_mutex_lock(&mock_lock);
    decode_entered = 1;
    pthread_cond_broadcast(&mock_cond);
    while (decode_block) pthread_cond_wait(&mock_cond, &mock_lock);
    pthread_mutex_unlock(&mock_lock);

    if (fail_decode) return -1;
    if (decoder->channels == 8) {
        static const int padded_used[4] = {300, 300, 289, 280};
        *used = padded_used[decoder->frame % 4u];
    } else {
        *used = decoder->frame % 4u ? 191 : 195;
    }
    for (unsigned i = 0; i < 256u * decoder->channels; ++i) {
        pcm[i] = (int16_t)(decoder->frame + i + 1);
    }
    ++decoder->frame;
    return 0;
}

int runtime_memory_copy_snapshot(void *dst, uintptr_t address, uint64_t size) {
    ++snapshot_calls;
    pthread_mutex_lock(&mock_lock);
    snapshot_entered = 1;
    pthread_cond_broadcast(&mock_cond);
    while (snapshot_block) pthread_cond_wait(&mock_cond, &mock_lock);
    pthread_mutex_unlock(&mock_lock);

    if (fail_snapshot || !dst || !address || !size || size > SIZE_MAX) return 0;
    memcpy(dst, (const void *)address, (size_t)size);
    return 1;
}

void runtime_restart(void) { assert(!"Unexpected restart in NGS2 test"); }
int64_t runtime_file_getdents(int fd, char *buffer, uint64_t size, int64_t *base) {
    (void)fd; (void)buffer; (void)size; (void)base; return -1;
}
void runtime_wait_begin(const char *kind, const void *object, const char *name,
                        const void *const *owner, uintptr_t guest_frame) {
    (void)kind; (void)object; (void)name; (void)owner; (void)guest_frame;
}
void runtime_wait_end(void) {}
const char *runtime_symbol(const char *name) { return name; }
uintptr_t runtime_lookup(const RuntimeExport *table, size_t count, const char *name) {
    for (size_t i = 0; i < count; ++i) {
        if (!strcmp(table[i].name, name)) return (uintptr_t)table[i].function;
    }
    return 0;
}

/* Command and option builder helpers */
static void put_u16(void *data, size_t offset, uint16_t value) {
    memcpy((char *)data + offset, &value, sizeof(value));
}
static void put_u32(void *data, size_t offset, uint32_t value) {
    memcpy((char *)data + offset, &value, sizeof(value));
}
static void put_uptr(void *data, size_t offset, uintptr_t value) {
    memcpy((char *)data + offset, &value, sizeof(value));
}
static void command(void *data, uint16_t size, uint32_t id) {
    memset(data, 0, size);
    put_u16(data, 0, size);
    put_u32(data, 4, id);
}
static void system_option(unsigned char sys[0x40]) {
    memset(sys, 0, 0x40);
    put_u32(sys, 0, 0x40);
    put_u32(sys, 0x20, 256);
    put_u32(sys, 0x24, 48000);
}
static void rack_option(unsigned char rack[0xa8], uint32_t capacity, const char *name) {
    memset(rack, 0, 0xa8);
    put_u32(rack, 0, 0xa8);
    put_u32(rack, 0x20, capacity);
    snprintf((char *)rack + 8, 16, "%s", name);
}
static void setup_waveform(Ngs2Handle *voice, uint32_t channels, uint32_t codec_config) {
    unsigned char cmd[0x28];
    command(cmd, sizeof(cmd), UINT32_C(0x10000000));
    put_u32(cmd, 8, NGS2_WAVEFORM_ATRAC9);
    put_u32(cmd, 0x0c, channels);
    put_u32(cmd, 0x10, 48000);
    put_u32(cmd, 0x14, codec_config);
    assert(!ngs2_voice_control(voice, cmd));
}
static void set_voice_state(Ngs2Handle *voice, uint32_t event) {
    unsigned char cmd[0x0c];
    command(cmd, sizeof(cmd), 6);
    put_u32(cmd, 8, event);
    assert(!ngs2_voice_control(voice, cmd));
}
static void enqueue_segment(Ngs2Handle *voice, const void *bytes, uint32_t size) {
    unsigned char descriptor[0x20], cmd[0x20];
    memset(descriptor, 0, sizeof(descriptor));
    put_u32(descriptor, 4, size); /* descriptor.byte_count */
    /* descriptor+0x08 is loop_count = 0 */
    command(cmd, sizeof(cmd), UINT32_C(0x10000001));
    put_uptr(cmd, 8, (uintptr_t)bytes);
    put_u32(cmd, 16, 1); /* flag_10 */
    put_u32(cmd, 20, 1); /* flag_14 */
    put_uptr(cmd, 24, (uintptr_t)descriptor);
    assert(!ngs2_voice_control(voice, cmd));
}
static void enqueue_null_command(Ngs2Handle *voice) {
    unsigned char cmd[0x20];
    command(cmd, sizeof(cmd), UINT32_C(0x10000001));
    /* data_address = 0, flag_10 = 0, flag_14 = 0, descriptor_address = 0 */
    assert(!ngs2_voice_control(voice, cmd));
}
static void push_synthetic_grain(Ngs2Voice *voice, int16_t sample_value) {
    Ngs2Atrac9Voice *state = voice->atrac9;
    if (!state) {
        state = calloc(1, sizeof(*state));
        state->generation = 1;
        voice->atrac9 = state;
    }
    assert(state->pcm_count < NGS2_ATRAC9_FIFO_CAPACITY);
    Ngs2PcmGrain *grain = &state->fifo[state->pcm_write];
    grain->generation = state->generation;
    grain->frames = 256;
    grain->channels = voice->waveform.channels;
    for (unsigned i = 0; i < 256u * grain->channels; ++i) grain->pcm[i] = sample_value;
    state->pcm_write = (state->pcm_write + 1u) % NGS2_ATRAC9_FIFO_CAPACITY;
    ++state->pcm_count;
}

/* Retry wrapper for non-blocking trylock mixer in unit tests */
static int test_render_retry(Ngs2Handle *sys, const Ngs2RenderBuffer *renders, uint32_t count) {
    for (int retry = 0; retry < 100; ++retry) {
        pthread_mutex_lock(&ngs2_lock);
        Ngs2System *s = ngs2_system_locked(sys);
        uint64_t r0 = s ? (s->main_renders + s->ambiguous_renders_ignored) : 0;
        pthread_mutex_unlock(&ngs2_lock);

        int rc = ngs2_render(sys, renders, count);
        if (rc != 0) return rc;

        pthread_mutex_lock(&ngs2_lock);
        s = ngs2_system_locked(sys);
        uint64_t r1 = s ? (s->main_renders + s->ambiguous_renders_ignored) : 0;
        pthread_mutex_unlock(&ngs2_lock);
        if (r1 > r0) return 0;
        Sleep(1);
    }
    return -1; /* A contended render is not a successful quantum. */
}

/* Assertion macros for conformance vs contract gates */
#define CHECK_CONFORMING(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "CONFORMANCE_FAILED case=%s line=%d: %s\n", case_name, __LINE__, #expr); \
        return 1; \
    } \
} while (0)

#define CHECK_GATE(expr, predicate_str) do { \
    if (assert_positive_contract) { \
        if (!(expr)) { \
            fprintf(stderr, "COMPLETION_CONTRACT_FAILED case=%s line=%d: %s\n", case_name, __LINE__, predicate_str); \
            return 1; \
        } \
    } else { \
        if (!(expr)) { \
            printf("BASELINE_RED_CONFIRMED case=%s line=%d predicate=%s\n", case_name, __LINE__, predicate_str); \
            return 0; /* Expected RED confirmed on baseline; subsequent assertions are blocked post-RED */ \
        } \
    } \
} while (0)

/* Test fixture */
typedef struct {
    Ngs2Handle *sys;
    Ngs2Handle *sampler_rack;
    Ngs2Handle *submixer_rack;
    Ngs2Handle *voice_a;
    Ngs2Handle *voice_b;
    Ngs2Handle *sub_voice;
    float master[256 * 8], aux[256 * 8];
    Ngs2RenderBuffer outputs[2];
} CompletionFixture;

static void open_fixture(CompletionFixture *f) {
    memset(f, 0, sizeof(*f));
    unsigned char so[0x40], ro[0xa8];
    system_option(so);
    assert(!ngs2_system_create(so, NULL, &f->sys));

    rack_option(ro, 4, "Sampler");
    assert(!ngs2_rack_create(f->sys, 0x1000, ro, NULL, &f->sampler_rack));
    assert(!ngs2_voice_handle(f->sampler_rack, 0, &f->voice_a));
    assert(!ngs2_voice_handle(f->sampler_rack, 1, &f->voice_b));

    rack_option(ro, 2, "SubMixer");
    assert(!ngs2_rack_create(f->sys, 0x2000, ro, NULL, &f->submixer_rack));
    assert(!ngs2_voice_handle(f->submixer_rack, 0, &f->sub_voice));

    setup_waveform(f->voice_a, 4, UINT32_C(0xfe7a17f0));
    setup_waveform(f->voice_b, 4, UINT32_C(0xfe7a17f0));
    setup_waveform(f->sub_voice, 2, UINT32_C(0xfe7a17f0));

    /* Explicit synthetic route, never a production routing fallback. */
    float matrix[32] = {0}; matrix[0] = 1.0f;
    unsigned char matrix_cmd[0x18];
    command(matrix_cmd, sizeof(matrix_cmd), 1);
    put_u32(matrix_cmd, 12, 32); put_uptr(matrix_cmd, 16, (uintptr_t)matrix);
    assert(!ngs2_voice_control(f->voice_a, matrix_cmd));
    assert(!ngs2_voice_control(f->voice_b, matrix_cmd));
    f->outputs[0] = (Ngs2RenderBuffer){f->master, sizeof(f->master), 0x18, 8};
    f->outputs[1] = (Ngs2RenderBuffer){f->aux, sizeof(f->aux), 0x18, 8};
}

static void close_fixture(CompletionFixture *f) {
    if (f->sys) assert(!ngs2_system_destroy(f->sys, NULL));
}

/* Wait for actual worker progress, with the enclosing process timeout as
 * deadlock guard. No fixed render count is interpreted as completed drain. */
static void drain_voice(CompletionFixture *f,Ngs2Voice *voice) {
    for (;;) {
        pthread_mutex_lock(&ngs2_lock);
        Ngs2Atrac9Voice *st=voice->atrac9;
        int work=st && (st->pcm_count || st->active_valid || st->segment_count ||
                        st->segment_reservations || st->snapshot_inflight || st->decode_inflight);
        if (st) assert(!st->faulted);
        if (!work) { pthread_mutex_unlock(&ngs2_lock); break; }
        if (!st->pcm_count) {
            pthread_cond_wait(&ngs2_cond,&ngs2_lock);
            pthread_mutex_unlock(&ngs2_lock);
            continue;
        }
        pthread_mutex_unlock(&ngs2_lock);
        assert(!test_render_retry(f->sys,f->outputs,2));
    }
}

/* =========================================================================
 * CASE 1: Início — PLAY sem FIFO retorna ACTIVE/PLAYING sem FINISHED.
 *         Testa 0x30 em Sampler e 0x10 em SubMixer real. Idempotência.
 * ========================================================================= */
static int test_case_1_initial_play_no_fifo(int assert_positive_contract) {
    const char *case_name = "initial_play_no_fifo";
    CompletionFixture f;
    open_fixture(&f);

    /* 1. SubMixer query 0x10 (not a decodable stream, does not require EOF) */
    uint8_t sub_buf[0x10];
    memset(sub_buf, 0xa5, sizeof(sub_buf));
    CHECK_CONFORMING(ngs2_voice_state(f.sub_voice, sub_buf, sizeof(sub_buf)) == 0);

    /* 2. Issue PLAY on Sampler Voice A without any queued FIFO */
    set_voice_state(f.voice_a, 0);

    /* 3. Query Sampler Voice A with size 0x30 */
    uint8_t query1[0x30];
    memset(query1, 0xa5, sizeof(query1));
    CHECK_CONFORMING(ngs2_voice_state(f.voice_a, query1, sizeof(query1)) == 0);
    uint32_t flags = 0;
    memcpy(&flags, query1, sizeof(flags));

    /* 4. Idempotence: 10 repeated queries must produce identical memory without mutating voice */
    pthread_mutex_lock(&ngs2_lock);
    Ngs2Voice *va = ngs2_voice_locked(f.voice_a);
    uint64_t gen_before = va->atrac9 ? va->atrac9->generation : 0;
    uint32_t pcm_before = va->atrac9 ? va->atrac9->pcm_count : 0;
    pthread_mutex_unlock(&ngs2_lock);

    for (int i = 0; i < 10; ++i) {
        uint8_t query_rep[0x30];
        memset(query_rep, 0x5a, sizeof(query_rep));
        CHECK_CONFORMING(ngs2_voice_state(f.voice_a, query_rep, sizeof(query_rep)) == 0);
        CHECK_CONFORMING(!memcmp(query1, query_rep, sizeof(query1)));
    }

    pthread_mutex_lock(&ngs2_lock);
    CHECK_CONFORMING(va->internal_state == NGS2_VOICE_PLAYING);
    if (va->atrac9) {
        CHECK_CONFORMING(va->atrac9->generation == gen_before);
        CHECK_CONFORMING(va->atrac9->pcm_count == pcm_before);
    }
    pthread_mutex_unlock(&ngs2_lock);

    /* 5. Positive Contract Gate: ACTIVE=1, PLAYING=1, FINISHED=0 */
    CHECK_GATE((flags & SCE_NGS2_STATE_ACTIVE) != 0, "(flags & SCE_NGS2_STATE_ACTIVE) != 0");
    CHECK_GATE((flags & SCE_NGS2_STATE_PLAYING) != 0, "(flags & SCE_NGS2_STATE_PLAYING) != 0");
    CHECK_GATE((flags & SCE_NGS2_STATE_FINISHED) == 0, "(flags & SCE_NGS2_STATE_FINISHED) == 0");
    set_voice_state(f.sub_voice,0);
    assert(!ngs2_voice_state(f.sub_voice,sub_buf,sizeof(sub_buf)));
    uint32_t sub_flags; memcpy(&sub_flags,sub_buf,sizeof(sub_flags));
    CHECK_GATE(sub_flags==3,"sub_flags == 3");
    set_voice_state(f.sub_voice,4);
    assert(!ngs2_voice_state(f.sub_voice,sub_buf,sizeof(sub_buf)));
    memcpy(&sub_flags,sub_buf,sizeof(sub_flags));
    CHECK_GATE(sub_flags==1,"sub_flags == 1");


    close_fixture(&f);
    printf("PASS_COMPLETION_CASE %s\n", case_name);
    return 0;
}

/* =========================================================================
 * CASE 2: Espera — submissão finita termina de decodificar e drenar sem
 *         comando nulo; voz continua ativa. Submissão seguinte prova espera sem falso EOF.
 * ========================================================================= */
static int test_case_2_inter_segment_wait(int assert_positive_contract) {
    const char *case_name = "inter_segment_wait";
    CompletionFixture f;
    open_fixture(&f);

    set_voice_state(f.voice_a, 0); /* PLAY */

    /* Enqueue segment 1 (768 bytes ATRAC9 superframe) */
    unsigned char seg1[768];
    memset(seg1, 0x33, sizeof(seg1));
    enqueue_segment(f.voice_a, seg1, sizeof(seg1));

    /* Wait for worker to decode segment 1 into FIFO */
    pthread_mutex_lock(&ngs2_lock);
    Ngs2Voice *va = ngs2_voice_locked(f.voice_a);
    while (!va->atrac9 || (!va->atrac9->pcm_count && !va->atrac9->faulted)) {
        pthread_cond_wait(&ngs2_cond, &ngs2_lock);
    }
    CHECK_CONFORMING(!va->atrac9->faulted && va->atrac9->pcm_count > 0);
    pthread_mutex_unlock(&ngs2_lock);

    drain_voice(&f,va);

    /* Voice is now in inter-segment wait (FIFO empty, no null command sent).
     * Must remain ACTIVE (ACTIVE=1, FINISHED=0). */
    uint8_t state_buf[0x30];
    CHECK_CONFORMING(ngs2_voice_state(f.voice_a, state_buf, sizeof(state_buf)) == 0);
    uint32_t flags = 0;
    memcpy(&flags, state_buf, sizeof(flags));

    /* Gate: Must not drop ACTIVE or report FINISHED prematurely */
    CHECK_GATE((flags & SCE_NGS2_STATE_ACTIVE) != 0, "(flags & SCE_NGS2_STATE_ACTIVE) != 0");
    CHECK_GATE((flags & SCE_NGS2_STATE_FINISHED) == 0, "(flags & SCE_NGS2_STATE_FINISHED) == 0");

    /* Submit segment 2 to prove voice is still fully active */
    unsigned char seg2[768];
    memset(seg2, 0x44, sizeof(seg2));
    enqueue_segment(f.voice_a, seg2, sizeof(seg2));

    pthread_mutex_lock(&ngs2_lock);
    while (va->atrac9->pcm_count == 0 && !va->atrac9->faulted) {
        pthread_cond_wait(&ngs2_cond, &ngs2_lock);
    }
    CHECK_CONFORMING(!va->atrac9->faulted && va->atrac9->pcm_count > 0);
    pthread_mutex_unlock(&ngs2_lock);

    assert(!test_render_retry(f.sys, f.outputs, 2));

    close_fixture(&f);
    printf("PASS_COMPLETION_CASE %s\n", case_name);
    return 0;
}

/* =========================================================================
 * CASE 3: EOF Finito — 2 segmentos + comando nulo. Não termina entre segmentos
 *         nem com FIFO pendente. Após drain, flags tornam-se inativo/FINISHED.
 * ========================================================================= */
static int test_case_3_finite_stream_eof_and_drain(int assert_positive_contract) {
    const char *case_name = "finite_stream_eof_and_drain";
    CompletionFixture f;
    open_fixture(&f);

    set_voice_state(f.voice_a, 0); /* PLAY */

    unsigned char seg[768];
    memset(seg, 0x55, sizeof(seg));
    enqueue_segment(f.voice_a, seg, sizeof(seg));
    enqueue_segment(f.voice_a, seg, sizeof(seg));

    /* Send explicit null command (EOF marker) */
    enqueue_null_command(f.voice_a);

    /* Wait until decode is in progress or FIFO has grains */
    pthread_mutex_lock(&ngs2_lock);
    Ngs2Voice *va = ngs2_voice_locked(f.voice_a);
    while (!va->atrac9 || (!va->atrac9->pcm_count && !va->atrac9->faulted)) {
        pthread_cond_wait(&ngs2_cond, &ngs2_lock);
    }
    CHECK_CONFORMING(!va->atrac9->faulted);
    pthread_mutex_unlock(&ngs2_lock);

    /* Query during playback / drain: must remain ACTIVE, not FINISHED */
    uint8_t query_during[0x30];
    CHECK_CONFORMING(ngs2_voice_state(f.voice_a, query_during, sizeof(query_during)) == 0);
    uint32_t flags_during = 0;
    memcpy(&flags_during, query_during, sizeof(flags_during));

    CHECK_GATE((flags_during & SCE_NGS2_STATE_ACTIVE) != 0, "(flags_during & SCE_NGS2_STATE_ACTIVE) != 0");
    CHECK_GATE((flags_during & SCE_NGS2_STATE_FINISHED) == 0, "(flags_during & SCE_NGS2_STATE_FINISHED) == 0");

    /* Render all grains until complete drain */
    drain_voice(&f,va);

    /* Query post-drain completion */
    uint8_t query_after1[0x30], query_after2[0x30];
    CHECK_CONFORMING(ngs2_voice_state(f.voice_a, query_after1, sizeof(query_after1)) == 0);
    CHECK_CONFORMING(ngs2_voice_state(f.voice_a, query_after2, sizeof(query_after2)) == 0);
    CHECK_CONFORMING(!memcmp(query_after1, query_after2, sizeof(query_after1)));

    uint32_t flags_after = 0;
    memcpy(&flags_after, query_after1, sizeof(flags_after));

    /* Gate: Natural completion: ACTIVE=0, PLAYING=0, FINISHED=1 */
    CHECK_GATE((flags_after & SCE_NGS2_STATE_ACTIVE) == 0, "(flags_after & SCE_NGS2_STATE_ACTIVE) == 0");
    CHECK_GATE((flags_after & SCE_NGS2_STATE_PLAYING) == 0, "(flags_after & SCE_NGS2_STATE_PLAYING) == 0");
    CHECK_GATE((flags_after & SCE_NGS2_STATE_FINISHED) != 0, "(flags_after & SCE_NGS2_STATE_FINISHED) != 0");

    close_fixture(&f);
    printf("PASS_COMPLETION_CASE %s\n", case_name);
    return 0;
}

/* =========================================================================
 * CASE 4: Pausa — PCM e marcador de fim pendentes; PAUSE preserva FIFO e
 *         geração sob render (ACTIVE=1, PLAYING=0). RESUME e drain completo.
 * ========================================================================= */
static int test_case_4_pause_resume_with_pending_eof(int assert_positive_contract) {
    const char *case_name = "pause_resume_with_pending_eof";
    CompletionFixture f;
    open_fixture(&f);

    set_voice_state(f.voice_a, 0); /* PLAY */

    unsigned char seg[768];
    memset(seg, 0x66, sizeof(seg));
    enqueue_segment(f.voice_a, seg, sizeof(seg));
    enqueue_null_command(f.voice_a);

    pthread_mutex_lock(&ngs2_lock);
    Ngs2Voice *va = ngs2_voice_locked(f.voice_a);
    while (!va->atrac9 || (!va->atrac9->pcm_count && !va->atrac9->faulted)) {
        pthread_cond_wait(&ngs2_cond, &ngs2_lock);
    }
    CHECK_CONFORMING(!va->atrac9->faulted && va->atrac9->pcm_count > 0);
    pthread_mutex_unlock(&ngs2_lock);

    pthread_mutex_lock(&ngs2_lock);
    while (va->atrac9->decode_inflight || va->atrac9->active_valid || va->atrac9->segment_count)
        pthread_cond_wait(&ngs2_cond,&ngs2_lock);
    pthread_mutex_unlock(&ngs2_lock);
    /* Issue Cmd 6 Action 4 (PAUSE) */
    set_voice_state(f.voice_a, 4);

    pthread_mutex_lock(&ngs2_lock);
    uint32_t pcm_paused = va->atrac9->pcm_count;
    uint64_t gen_paused = va->atrac9->generation;
    pthread_mutex_unlock(&ngs2_lock);

    /* Render while paused: must skip voice and preserve FIFO/generation */
    assert(!test_render_retry(f.sys, f.outputs, 2));

    pthread_mutex_lock(&ngs2_lock);
    CHECK_CONFORMING(va->internal_state == NGS2_VOICE_PAUSED);
    CHECK_CONFORMING(va->atrac9->pcm_count == pcm_paused);
    CHECK_CONFORMING(va->atrac9->generation == gen_paused);
    pthread_mutex_unlock(&ngs2_lock);

    /* Query while paused */
    uint8_t state_paused[0x30];
    CHECK_CONFORMING(ngs2_voice_state(f.voice_a, state_paused, sizeof(state_paused)) == 0);
    uint32_t flags_paused = 0;
    memcpy(&flags_paused, state_paused, sizeof(flags_paused));

    /* Gate: ACTIVE=1, PLAYING=0, FINISHED=0 */
    CHECK_GATE((flags_paused & SCE_NGS2_STATE_ACTIVE) != 0, "(flags_paused & SCE_NGS2_STATE_ACTIVE) != 0");
    CHECK_GATE((flags_paused & SCE_NGS2_STATE_PLAYING) == 0, "(flags_paused & SCE_NGS2_STATE_PLAYING) == 0");
    CHECK_GATE((flags_paused & SCE_NGS2_STATE_FINISHED) == 0, "(flags_paused & SCE_NGS2_STATE_FINISHED) == 0");

    /* Issue Cmd 6 Action 5 (RESUME) */
    set_voice_state(f.voice_a, 5);

    /* Render to drain remaining audio */
    drain_voice(&f,va);

    uint8_t state_drained[0x30];
    CHECK_CONFORMING(ngs2_voice_state(f.voice_a, state_drained, sizeof(state_drained)) == 0);
    uint32_t flags_drained = 0;
    memcpy(&flags_drained, state_drained, sizeof(flags_drained));

    CHECK_GATE((flags_drained & SCE_NGS2_STATE_ACTIVE) == 0, "(flags_drained & SCE_NGS2_STATE_ACTIVE) == 0");
    CHECK_GATE((flags_drained & SCE_NGS2_STATE_FINISHED) != 0, "(flags_drained & SCE_NGS2_STATE_FINISHED) != 0");

    close_fixture(&f);
    printf("PASS_COMPLETION_CASE %s\n", case_name);
    return 0;
}

/* =========================================================================
 * CASE 5: EOS durante Cópia — barreira de snapshot em thread separada.
 *         Comando nulo enviado; voz não pode terminar com cópia/reserva pendente.
 * ========================================================================= */
typedef struct {
    Ngs2Handle *voice;
    unsigned char *bytes;
    uint32_t size;
    int done;
} EnqueueThreadParam;

static void *thread_enqueue_worker(void *arg) {
    EnqueueThreadParam *p = (EnqueueThreadParam *)arg;
    enqueue_segment(p->voice, p->bytes, p->size);
    pthread_mutex_lock(&mock_lock);
    p->done = 1;
    pthread_cond_broadcast(&mock_cond);
    pthread_mutex_unlock(&mock_lock);
    return NULL;
}

static int test_case_5_eos_during_copy_barrier(int assert_positive_contract) {
    const char *case_name = "eos_during_copy_barrier";
    CompletionFixture f;
    open_fixture(&f);

    set_voice_state(f.voice_a, 0); /* PLAY */

    unsigned char seg_bytes[768];
    memset(seg_bytes, 0x77, sizeof(seg_bytes));

    pthread_mutex_lock(&mock_lock);
    snapshot_block = 1;
    snapshot_entered = 0;
    pthread_mutex_unlock(&mock_lock);

    EnqueueThreadParam param = {f.voice_a, seg_bytes, sizeof(seg_bytes), 0};
    pthread_t th;
    CHECK_CONFORMING(!pthread_create(&th, NULL, thread_enqueue_worker, &param));

    /* Wait until thread enters snapshot copy */
    pthread_mutex_lock(&mock_lock);
    while (!snapshot_entered) pthread_cond_wait(&mock_cond, &mock_lock);
    pthread_mutex_unlock(&mock_lock);

    /* While snapshot copy is blocked/inflight, send null command */
    enqueue_null_command(f.voice_a);

    assert(!test_render_retry(f.sys,f.outputs,2));
    pthread_mutex_lock(&ngs2_lock);
    Ngs2Voice *copy_voice=ngs2_voice_locked(f.voice_a);
    assert(copy_voice->atrac9->snapshot_inflight && copy_voice->atrac9->segment_reservations);
    pthread_mutex_unlock(&ngs2_lock);
    /* Query state during inflight copy */
    uint8_t state_copy[0x30];
    CHECK_CONFORMING(ngs2_voice_state(f.voice_a, state_copy, sizeof(state_copy)) == 0);
    uint32_t flags_copy = 0;
    memcpy(&flags_copy, state_copy, sizeof(flags_copy));

    /* Gate: Cannot terminate while reservation/copy is inflight! */
    CHECK_GATE((flags_copy & SCE_NGS2_STATE_ACTIVE) != 0, "(flags_copy & SCE_NGS2_STATE_ACTIVE) != 0");
    CHECK_GATE((flags_copy & SCE_NGS2_STATE_FINISHED) == 0, "(flags_copy & SCE_NGS2_STATE_FINISHED) == 0");

    /* Unblock snapshot barrier and join thread */
    pthread_mutex_lock(&mock_lock);
    snapshot_block = 0;
    pthread_cond_broadcast(&mock_cond);
    pthread_mutex_unlock(&mock_lock);

    CHECK_CONFORMING(!pthread_join(th, NULL));
    CHECK_CONFORMING(param.done == 1);

    /* Drain all audio */
    pthread_mutex_lock(&ngs2_lock);
    Ngs2Voice *va = ngs2_voice_locked(f.voice_a);
    pthread_mutex_unlock(&ngs2_lock);

    drain_voice(&f,va);

    uint8_t state_final[0x30];
    CHECK_CONFORMING(ngs2_voice_state(f.voice_a, state_final, sizeof(state_final)) == 0);
    uint32_t flags_final = 0;
    memcpy(&flags_final, state_final, sizeof(flags_final));

    CHECK_GATE((flags_final & SCE_NGS2_STATE_ACTIVE) == 0, "(flags_final & SCE_NGS2_STATE_ACTIVE) == 0");
    CHECK_GATE((flags_final & SCE_NGS2_STATE_FINISHED) != 0, "(flags_final & SCE_NGS2_STATE_FINISHED) != 0");

    close_fixture(&f);
    printf("PASS_COMPLETION_CASE %s\n", case_name);
    return 0;
}

/* =========================================================================
 * CASE 6: Cancelamento durante Decode — barreira de decode; evento 3 avança
 *         geração, descarta PCM em voo sem FINISHED natural; isolamento da Voz B.
 * ========================================================================= */
static int test_case_6_cancellation_during_decode_barrier(int assert_positive_contract) {
    const char *case_name = "cancellation_during_decode_barrier";
    CompletionFixture f;
    open_fixture(&f);

    set_voice_state(f.voice_a, 0); /* PLAY Voice A */
    set_voice_state(f.voice_b, 0); /* PLAY Voice B */

    /* Pre-fill Voice B with synthetic grains to test isolation */
    pthread_mutex_lock(&ngs2_lock);
    Ngs2Voice *va = ngs2_voice_locked(f.voice_a);
    Ngs2Voice *vb = ngs2_voice_locked(f.voice_b);
    push_synthetic_grain(vb, 12000);
    uint64_t gen_b_before = vb->atrac9->generation;
    uint32_t pcm_b_before = vb->atrac9->pcm_count;
    pthread_mutex_unlock(&ngs2_lock);

    /* Arm blocking decode barrier */
    pthread_mutex_lock(&mock_lock);
    decode_block = 1;
    decode_entered = 0;
    pthread_mutex_unlock(&mock_lock);

    /* Enqueue segment into Voice A to trigger decode */
    unsigned char seg_bytes[768];
    memset(seg_bytes, 0x88, sizeof(seg_bytes));
    enqueue_segment(f.voice_a, seg_bytes, sizeof(seg_bytes));

    /* Wait for worker to enter decode for Voice A */
    pthread_mutex_lock(&mock_lock);
    while (!decode_entered) pthread_cond_wait(&mock_cond, &mock_lock);
    pthread_mutex_unlock(&mock_lock);

    pthread_mutex_lock(&ngs2_lock);
    uint64_t gen_a_initial = va->atrac9 ? va->atrac9->generation : 0;
    pthread_mutex_unlock(&ngs2_lock);

    /* Send Cmd 6 Action 3 (Cancel / Immediate Stop) to Voice A */
    set_voice_state(f.voice_a, 3);

    /* Unblock decode worker */
    pthread_mutex_lock(&mock_lock);
    decode_block = 0;
    pthread_cond_broadcast(&mock_cond);
    pthread_mutex_unlock(&mock_lock);

    /* Wait for decode_inflight to clear on Voice A */
    pthread_mutex_lock(&ngs2_lock);
    if (va->atrac9) {
        while (va->atrac9->decode_inflight) pthread_cond_wait(&ngs2_cond, &ngs2_lock);
    }
    uint64_t gen_a_after = va->atrac9 ? va->atrac9->generation : 0;
    uint32_t pcm_a_after = va->atrac9 ? va->atrac9->pcm_count : 0;
    int state_a_after = va->internal_state;
    pthread_mutex_unlock(&ngs2_lock);

    /* Query Voice A flags */
    uint8_t state_a[0x30];
    CHECK_CONFORMING(ngs2_voice_state(f.voice_a, state_a, sizeof(state_a)) == 0);
    uint32_t flags_a = 0;
    memcpy(&flags_a, state_a, sizeof(flags_a));

    /* Gate: Action 3 must advance generation, drop in-flight PCM, set STOPPED, not FINISHED */
    CHECK_GATE(gen_a_after > gen_a_initial, "va->atrac9->generation > gen_a_initial");
    CHECK_GATE(pcm_a_after == 0, "va->atrac9->pcm_count == 0");
    CHECK_GATE(state_a_after == NGS2_VOICE_STOPPED, "va->internal_state == NGS2_VOICE_STOPPED");
    CHECK_GATE((flags_a & SCE_NGS2_STATE_ACTIVE) == 0, "(flags_a & SCE_NGS2_STATE_ACTIVE) == 0");
    CHECK_GATE((flags_a & SCE_NGS2_STATE_FINISHED) == 0, "(flags_a & SCE_NGS2_STATE_FINISHED) == 0");

    /* Conformance: Voice B must be completely isolated and unaffected */
    pthread_mutex_lock(&ngs2_lock);
    CHECK_CONFORMING(vb->internal_state == NGS2_VOICE_PLAYING);
    CHECK_CONFORMING(vb->atrac9->generation == gen_b_before);
    CHECK_CONFORMING(vb->atrac9->pcm_count == pcm_b_before);
    pthread_mutex_unlock(&ngs2_lock);

    /* Render and verify B's actual PCM; A contributes no cancelled samples. */
    assert(!test_render_retry(f.sys, f.outputs, 2));
    CHECK_CONFORMING(fabsf(f.master[0]-(12000.0f/32768.0f*0.25f))<0.0000001f);

    close_fixture(&f);
    printf("PASS_COMPLETION_CASE %s\n", case_name);
    return 0;
}

/* =========================================================================
 * CASE 7: STOP e Reuso — evento 2 tem cancelamento coerente. Nova geração
 *         não herda FINISHED. Reconfiguração equivalente preserva trabalho;
 *         reconfiguração alterada invalida trabalho anterior.
 * ========================================================================= */
static int test_case_7_stop_action2_and_reuse(int assert_positive_contract) {
    const char *case_name = "stop_action2_and_reuse";
    CompletionFixture f;
    open_fixture(&f);

    set_voice_state(f.voice_a, 0); /* PLAY */

    /* Stop with Cmd 6 Action 2 */
    set_voice_state(f.voice_a, 2);

    pthread_mutex_lock(&ngs2_lock);
    Ngs2Voice *va = ngs2_voice_locked(f.voice_a);
    CHECK_CONFORMING(va->internal_state == NGS2_VOICE_STOPPED);
    uint64_t gen_stopped = va->atrac9 ? va->atrac9->generation : 0;
    (void)gen_stopped;
    pthread_mutex_unlock(&ngs2_lock);

    /* Start fresh PLAY on same voice */
    set_voice_state(f.voice_a, 0);

    uint8_t state_reuse[0x30];
    CHECK_CONFORMING(ngs2_voice_state(f.voice_a, state_reuse, sizeof(state_reuse)) == 0);
    uint32_t flags_reuse = 0;
    memcpy(&flags_reuse, state_reuse, sizeof(flags_reuse));

    /* Gate: Must not inherit previous FINISHED; must be ACTIVE/PLAYING */
    CHECK_GATE((flags_reuse & SCE_NGS2_STATE_ACTIVE) != 0, "(flags_reuse & SCE_NGS2_STATE_ACTIVE) != 0");
    CHECK_GATE((flags_reuse & SCE_NGS2_STATE_PLAYING) != 0, "(flags_reuse & SCE_NGS2_STATE_PLAYING) != 0");
    CHECK_GATE((flags_reuse & SCE_NGS2_STATE_FINISHED) == 0, "(flags_reuse & SCE_NGS2_STATE_FINISHED) == 0");

    /* Part B: Equivalent waveform reconfiguration during open stream preserves work */
    pthread_mutex_lock(&ngs2_lock);
    push_synthetic_grain(va, 5000);
    uint64_t gen_open = va->atrac9->generation;
    uint32_t pcm_open = va->atrac9->pcm_count;
    pthread_mutex_unlock(&ngs2_lock);

    /* Send identical waveform (4 channels, 48000 Hz, same codec config) */
    setup_waveform(f.voice_a, 4, UINT32_C(0xfe7a17f0));

    pthread_mutex_lock(&ngs2_lock);
    CHECK_CONFORMING(va->atrac9->generation == gen_open);
    CHECK_CONFORMING(va->atrac9->pcm_count == pcm_open);
    pthread_mutex_unlock(&ngs2_lock);

    /* Part C: Altered waveform reconfiguration (channel change 4 -> 8) drops work and bumps generation */
    setup_waveform(f.voice_a, 8, UINT32_C(0xfe7829f0));

    pthread_mutex_lock(&ngs2_lock);
    CHECK_CONFORMING(va->atrac9->generation > gen_open);
    CHECK_CONFORMING(va->atrac9->pcm_count == 0);
    pthread_mutex_unlock(&ngs2_lock);

    close_fixture(&f);
    printf("PASS_COMPLETION_CASE %s\n", case_name);
    return 0;
}

/* =========================================================================
 * MAIN DISPATCHER
 * ========================================================================= */
int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <case_name> [--assert-positive-contract]\n", argv[0]);
        return 2;
    }
    setvbuf(stdout, NULL, _IONBF, 0);

    /* Standard NGS2 HLE settings for mixer & ATRAC9 */
    ngs2_atrac9_setting = 1;
    ngs2_render_mix_setting = 1;
    ngs2_b2f2a_setting = 0;
    ngs2_b2f2c_setting = 0;
    atomic_store(&ngs2_audio_diag_setting, 0);
    atomic_store(&ngs2_route_diag_setting, 0);

    const char *case_name = argv[1];
    int assert_positive_contract = (argc > 2 && !strcmp(argv[2], "--assert-positive-contract")) ? 1 : 0;

    if (!strcmp(case_name, "initial_play_no_fifo"))
        return test_case_1_initial_play_no_fifo(assert_positive_contract);
    if (!strcmp(case_name, "inter_segment_wait"))
        return test_case_2_inter_segment_wait(assert_positive_contract);
    if (!strcmp(case_name, "finite_stream_eof_and_drain"))
        return test_case_3_finite_stream_eof_and_drain(assert_positive_contract);
    if (!strcmp(case_name, "pause_resume_with_pending_eof"))
        return test_case_4_pause_resume_with_pending_eof(assert_positive_contract);
    if (!strcmp(case_name, "eos_during_copy_barrier"))
        return test_case_5_eos_during_copy_barrier(assert_positive_contract);
    if (!strcmp(case_name, "cancellation_during_decode_barrier"))
        return test_case_6_cancellation_during_decode_barrier(assert_positive_contract);
    if (!strcmp(case_name, "stop_action2_and_reuse"))
        return test_case_7_stop_action2_and_reuse(assert_positive_contract);

    fprintf(stderr, "Unknown test case: %s\n", case_name);
    return 2;
}
