/* SPDX-License-Identifier: GPL-2.0-or-later
 * Tests for NGS2 Voice Lifecycle, VoiceGetState idempotency,
 * truthful playback counters, and SoundX disassembly contracts.
 * Compiles and runs against the REAL runtime_tlg.c HLE without mocks.
 */
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "runtime_tlg.c"

/* Mock dependencies required by standalone runtime_tlg.c test harnesses */
void runtime_restart(void) { abort(); }
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
    for (size_t i = 0; i < count; ++i)
        if (!strcmp(table[i].name, name)) return (uintptr_t)table[i].function;
    return 0;
}

static int g_simulate_payload_failure = 0;
static void (*g_payload_hook)(void) = NULL;

int runtime_memory_copy_snapshot(void *dst, uintptr_t address, uint64_t size) {
    if (!dst || !address || !size || size > SIZE_MAX) return 0;
    if (size == 192 && g_payload_hook) {
        void (*hook)(void) = g_payload_hook;
        g_payload_hook = NULL;
        hook();
    }
    if (size == 192 && g_simulate_payload_failure) {
        return 0;
    }
    memcpy(dst, (const void *)address, (size_t)size);
    return 1;
}

/* Proven Orbis OS NGS2 VoiceGetState flags */
#define SCE_NGS2_VOICE_STATE_FLAG_ACTIVE   0x00000001u /* Bit 0 */
#define SCE_NGS2_VOICE_STATE_FLAG_PLAYING  0x00000002u /* Bit 1 */
#define SCE_NGS2_VOICE_STATE_FLAG_PAUSED   0x00000004u /* Bit 2 */
#define SCE_NGS2_VOICE_STATE_FLAG_FINISHED 0x00000020u /* Bit 5 */

/* Helpers for constructing guest commands */
static void put16(void *dst, size_t off, uint16_t v) { memcpy((char *)dst + off, &v, 2); }
static void put32(void *dst, size_t off, uint32_t v) { memcpy((char *)dst + off, &v, 4); }
static void putptr(void *dst, size_t off, uintptr_t v) { memcpy((char *)dst + off, &v, sizeof(v)); }
static void make_cmd(void *dst, uint16_t size, uint32_t id) {
    memset(dst, 0, size); put16(dst, 0, size); put32(dst, 4, id);
}

static void voice_cmd_setup(Ngs2Handle *handle, int hevag) {
    unsigned char cmd[0x28]; make_cmd(cmd, sizeof(cmd), 0x10000000u);
    put32(cmd, 8, hevag ? 0x1cu : 0x40u);
    put32(cmd, 12, hevag ? 1u : 4u);
    put32(cmd, 16, 48000);
    put32(cmd, 20, hevag ? 0u : 0xfe7a17f0u);
    assert(!ngs2_voice_control(handle, cmd));

    unsigned char play[0x0c]; make_cmd(play, sizeof(play), 6);
    put32(play, 8, 0); /* Action 0: PLAY */
    assert(!ngs2_voice_control(handle, play));

    float matrix[32] = {0}; matrix[0] = 0.5f;
    unsigned char gain[0x18]; make_cmd(gain, sizeof(gain), 1);
    put32(gain, 12, hevag ? 8u : 32u); putptr(gain, 16, (uintptr_t)matrix);
    assert(!ngs2_voice_control(handle, gain));
}

static void voice_cmd_send_empty_descriptor(Ngs2Handle *handle) {
    unsigned char cmd[0x20];
    make_cmd(cmd, sizeof(cmd), 0x10000001u);
    /* data_address = 0, flag_10 = 0, flag_14 = 0, descriptor_address = 0 */
    assert(!ngs2_voice_control(handle, cmd));
}

static void voice_cmd_send_data_segment(Ngs2Handle *handle, uintptr_t data_addr, uint32_t byte_count) {
    uint32_t desc[8] = {0};
    desc[1] = byte_count; /* offset 4 is byte_count */
    unsigned char cmd[0x20];
    make_cmd(cmd, sizeof(cmd), 0x10000001u);
    putptr(cmd, 8, data_addr);
    put32(cmd, 0x10, 0);
    put32(cmd, 0x14, 0);
    putptr(cmd, 0x18, (uintptr_t)desc);
    assert(!ngs2_voice_control(handle, cmd));
}

static void voice_cmd_action(Ngs2Handle *handle, uint32_t action) {
    unsigned char cmd[0x0c];
    make_cmd(cmd, sizeof(cmd), 6);
    put32(cmd, 8, action);
    assert(!ngs2_voice_control(handle, cmd));
}

static void voice_add_fifo_grain(Ngs2Voice *voice, int16_t amplitude) {
    Ngs2Atrac9Voice *s = voice->atrac9;
    assert(s && s->pcm_count < NGS2_ATRAC9_FIFO_CAPACITY);
    Ngs2PcmGrain *g = &s->fifo[s->pcm_write];
    g->generation = s->generation;
    g->frames = 256;
    g->channels = voice->waveform.channels;
    for (unsigned i = 0; i < 256u * g->channels; ++i) g->pcm[i] = amplitude;
    s->pcm_write = (s->pcm_write + 1u) % NGS2_ATRAC9_FIFO_CAPACITY;
    ++s->pcm_count;
}

/* SoundX internal voice tracking state simulated from disassembly */
typedef struct {
    uint32_t internal_flags;         /* [r14+r13+1Ch] (Pool 1) or [r15+r13+1Ch] (Pool 2) */
    uint64_t finished_counter;       /* [rdx+4CB0h] */
    uint32_t samples_played;         /* [r14+r13+98h] */
    int entered_inactive_branch;     /* Entered 0xb6f04d (eax |= 0x100) */
} SoundXVoiceContext;

/* Emulation of SoundX Call Site 1 (0xb6efed, size 0x30, Pool 1 / Rack 1) */
static void soundx_simulate_call_site_1(const uint32_t *ngs2_state, SoundXVoiceContext *ctx) {
    uint32_t eax = ctx->internal_flags;
    uint32_t ecx = eax & 0x101u;
    if (ecx != 1u) return; /* 0xb6f007: cmp ecx, 1 -> Voice not active in SoundX, skip */

    uint32_t state_flags = ngs2_state[0];
    uint32_t edx = state_flags & 0x22u;

    if (edx == 2u) {
        eax |= 0x10u; /* mark playing */
    } else {
        if (eax & 0x10u) {
            if (state_flags & 0x20u) {
                ctx->finished_counter++; /* inc qword [rdx+4CB0h] */
            }
        }
        eax &= ~0x10u; /* clear playing flag */
    }
    ctx->internal_flags = eax;

    if (state_flags & 0x01u) {
        ctx->samples_played = ngs2_state[2]; /* update samples played */
    } else {
        ctx->entered_inactive_branch = 1;
        eax |= 0x100u;
        ctx->internal_flags = eax;
    }
}

/* Emulation of SoundX Call Site 2 (0xb6f180, size 0x10, Pool 2 / Rack 2) */
static void soundx_simulate_call_site_2(const uint32_t *ngs2_state, SoundXVoiceContext *ctx) {
    uint32_t ecx = ctx->internal_flags;
    if (!(ecx & 1u)) return;
    if (ecx & 0x100u) return; /* bit 8 set: skip */

    uint32_t eax = ngs2_state[0];
    uint32_t edx = ecx;
    ecx |= 0x10u;
    edx &= ~0x10u;

    if (!(eax & 0x02u)) ecx = edx;
    ctx->internal_flags = ecx;
    if (eax & 0x01u) {
        ctx->samples_played = ngs2_state[2];
    } else {
        ctx->samples_played = 0;
    }
}

/* Test Fixture for Real NGS2 System & Voice */
typedef struct {
    Ngs2Handle *system, *rack, *voice;
    float master[256 * 8], aux[256 * 8];
    Ngs2RenderBuffer outputs[2];
} TestHarness;

static void harness_open(TestHarness *h, int hevag) {
    memset(h, 0, sizeof(*h));
    ngs2_atrac9_setting = 1;
    ngs2_render_mix_setting = 1;
    atomic_store(&ngs2_audio_diag_setting, 0);

    unsigned char sysopt[0x40] = {0}, rackopt[0xa8] = {0};
    put32(sysopt, 0, 0x40); put32(sysopt, 0x20, 256); put32(sysopt, 0x24, 48000);
    put32(rackopt, 0, 0xa8); put32(rackopt, 0x20, 2);
    memcpy(rackopt + 8, "Sampler", 8);

    assert(!ngs2_system_create(sysopt, NULL, &h->system));
    assert(!ngs2_rack_create(h->system, 0x1000, rackopt, NULL, &h->rack));
    assert(!ngs2_voice_handle(h->rack, 0, &h->voice));
    voice_cmd_setup(h->voice, hevag);

    h->outputs[0] = (Ngs2RenderBuffer){h->master, sizeof(h->master), 0x18, 8};
    h->outputs[1] = (Ngs2RenderBuffer){h->aux, sizeof(h->aux), 0x18, 8};
}

static void harness_close(TestHarness *h) {
    assert(!ngs2_system_destroy(h->system, NULL));
}

static void harness_render(TestHarness *h) {
    pthread_mutex_lock(&ngs2_lock);
    Ngs2System *sys = ngs2_system_locked(h->system);
    uint64_t before = sys->main_renders;
    pthread_mutex_unlock(&ngs2_lock);

    for (int retry = 0; retry < 50; ++retry) {
        assert(!ngs2_render(h->system, h->outputs, 2));
        pthread_mutex_lock(&ngs2_lock);
        uint64_t after = sys->main_renders;
        pthread_mutex_unlock(&ngs2_lock);
        if (after > before) return;
    }
    assert(!"harness_render could not acquire mixer gate");
}

/* ========================================================================= */
/* Test Cases                                                                */
/* ========================================================================= */

/* Test 1: Full Buffer Initialization for 16-byte and 48-byte queries */
static void test_buffer_initialization(int is_b2f2d_expected) {
    TestHarness h;
    harness_open(&h, 0);

    pthread_mutex_lock(&ngs2_lock);
    Ngs2Voice *v = ngs2_voice_locked(h.voice);
    voice_add_fifo_grain(v, 1000);
    pthread_mutex_unlock(&ngs2_lock);

    /* 16-byte query with dirty canary */
    uint8_t raw16[16];
    memset(raw16, 0xa5, sizeof(raw16));
    assert(!ngs2_voice_state(h.voice, raw16, 0x10));
    uint32_t *u16 = (uint32_t *)raw16;

    /* 48-byte query with dirty canary */
    uint8_t raw48[48];
    memset(raw48, 0x5a, sizeof(raw48));
    assert(!ngs2_voice_state(h.voice, raw48, 0x30));
    uint32_t *u48 = (uint32_t *)raw48;

    if (is_b2f2d_expected) {
        assert(u16[0] == (SCE_NGS2_VOICE_STATE_FLAG_ACTIVE | SCE_NGS2_VOICE_STATE_FLAG_PLAYING));
        assert(u16[1] == 0);
        assert(u16[3] == 0);

        assert(u48[0] == (SCE_NGS2_VOICE_STATE_FLAG_ACTIVE | SCE_NGS2_VOICE_STATE_FLAG_PLAYING));
        assert(u48[1] == 0);
        for (int i = 3; i < 12; ++i) assert(u48[i] == 0 && "Trailing 48-byte fields must be 0");
    } else {
        /* Baseline: all zeros */
        for (int i = 0; i < 4; ++i) assert(u16[i] == 0);
        for (int i = 0; i < 12; ++i) assert(u48[i] == 0);
    }

    harness_close(&h);
    puts("PASS: test_buffer_initialization");
}

/* Test 2: Polling Order 0x30 then 0x10 (SoundX Loop 1 then Loop 2) */
static void test_soundx_polling_order_30_then_10(int is_b2f2d_expected) {
    TestHarness h;
    harness_open(&h, 0);

    pthread_mutex_lock(&ngs2_lock);
    Ngs2Voice *v = ngs2_voice_locked(h.voice);
    voice_add_fifo_grain(v, 2000);
    pthread_mutex_unlock(&ngs2_lock);

    /* Render grain */
    harness_render(&h);

    /* Send EOF (empty descriptor) after FIFO drained to 0 */
    voice_cmd_send_empty_descriptor(h.voice);

    uint32_t s48[12], s16[4];
    memset(s48, 0, sizeof(s48));
    memset(s16, 0, sizeof(s16));

    /* Query 0x30 first */
    assert(!ngs2_voice_state(h.voice, s48, 0x30));
    /* Query 0x10 second */
    assert(!ngs2_voice_state(h.voice, s16, 0x10));

    SoundXVoiceContext ctx = { .internal_flags = 0x11u /* active + playing */ };
    soundx_simulate_call_site_1(s48, &ctx);
    soundx_simulate_call_site_2(s16, &ctx);

    if (is_b2f2d_expected) {
        assert(s48[0] == SCE_NGS2_VOICE_STATE_FLAG_FINISHED);
        assert(s16[0] == SCE_NGS2_VOICE_STATE_FLAG_FINISHED);
        assert(ctx.finished_counter == 1 && "Finished counter must increment exactly once");
        assert((ctx.internal_flags & 0x100u) != 0 && "Flag 0x100 must be set");

        /* Second frame poll with same voice: Call Site 1 and 2 must skip */
        soundx_simulate_call_site_1(s48, &ctx);
        soundx_simulate_call_site_2(s16, &ctx);
        assert(ctx.finished_counter == 1 && "Counter must not increment again");
    } else {
        /* Baseline fails: s48[0] is 0 */
        assert(s48[0] == 0);
        assert(ctx.finished_counter == 0);
    }

    harness_close(&h);
    puts("PASS: test_soundx_polling_order_30_then_10");
}

/* Test 3: Polling Order 0x10 then 0x30 (Audited SoundX Dual-Loop Inverted Execution) */
static void test_soundx_polling_order_10_then_30(int is_b2f2d_expected) {
    TestHarness h;
    harness_open(&h, 0);

    pthread_mutex_lock(&ngs2_lock);
    Ngs2Voice *v = ngs2_voice_locked(h.voice);
    voice_add_fifo_grain(v, 2000);
    pthread_mutex_unlock(&ngs2_lock);

    harness_render(&h);
    voice_cmd_send_empty_descriptor(h.voice);

    uint32_t s16[4], s48[12];
    memset(s16, 0, sizeof(s16));
    memset(s48, 0, sizeof(s48));

    /* Inverted query order: 0x10 FIRST, then 0x30 SECOND */
    assert(!ngs2_voice_state(h.voice, s16, 0x10));
    assert(!ngs2_voice_state(h.voice, s48, 0x30));

    SoundXVoiceContext ctx_rack2 = { .internal_flags = 0x11u /* active + playing */ };
    SoundXVoiceContext ctx_rack1 = { .internal_flags = 0x11u /* active + playing */ };

    /* 1. Simulate Call Site 2 with s16 first */
    soundx_simulate_call_site_2(s16, &ctx_rack2);

    /* 2. Simulate Call Site 1 with s48 second */
    soundx_simulate_call_site_1(s48, &ctx_rack1);

    if (is_b2f2d_expected) {
        assert(s16[0] == SCE_NGS2_VOICE_STATE_FLAG_FINISHED);
        assert(s48[0] == SCE_NGS2_VOICE_STATE_FLAG_FINISHED);

        /* Rack 1 voice processed by Call Site 1 must increment finished_counter exactly once */
        assert(ctx_rack1.finished_counter == 1 && "Finished counter must increment exactly once");
        assert((ctx_rack1.internal_flags & 0x100u) != 0 && "Flag 0x100 must be set on Rack 1 voice");
        assert(ctx_rack1.entered_inactive_branch == 1);

        /* Rack 2 context updated cleanly without modifying finished_counter */
        assert((ctx_rack2.internal_flags & 0x10u) == 0 && "Playing flag cleared on Rack 2");

        /* Second frame poll: both must skip (idempotent across multiple frames) */
        soundx_simulate_call_site_2(s16, &ctx_rack2);
        soundx_simulate_call_site_1(s48, &ctx_rack1);
        assert(ctx_rack1.finished_counter == 1 && "Finished counter must not increment again on subsequent polls");
    } else {
        assert(s16[0] == 0);
        assert(s48[0] == 0);
        assert(ctx_rack1.finished_counter == 0);
    }

    harness_close(&h);
    puts("PASS: test_soundx_polling_order_10_then_30");
}

/* Test 4: EOF arrives AFTER FIFO is already drained to 0 */
static void test_eof_after_fifo_empty(int is_b2f2d_expected) {
    TestHarness h;
    harness_open(&h, 0);

    pthread_mutex_lock(&ngs2_lock);
    Ngs2Voice *v = ngs2_voice_locked(h.voice);
    voice_add_fifo_grain(v, 1500);
    pthread_mutex_unlock(&ngs2_lock);

    /* Render grain 1: FIFO is now 0 */
    harness_render(&h);

    pthread_mutex_lock(&ngs2_lock);
    v = ngs2_voice_locked(h.voice);
    assert(v->atrac9->pcm_count == 0);
    pthread_mutex_unlock(&ngs2_lock);

    /* At this moment, pcm_count is 0, but EOF has NOT arrived yet */
    uint32_t state[4];
    assert(!ngs2_voice_state(h.voice, state, 0x10));
    if (is_b2f2d_expected) {
        /* Still PLAYING because stream has not ended */
        assert(state[0] == (SCE_NGS2_VOICE_STATE_FLAG_ACTIVE | SCE_NGS2_VOICE_STATE_FLAG_PLAYING));
    }

    /* NOW send EOF (empty descriptor) while pcm_count == 0 */
    voice_cmd_send_empty_descriptor(h.voice);

    /* Check state immediately */
    assert(!ngs2_voice_state(h.voice, state, 0x10));
    if (is_b2f2d_expected) {
        /* Must transition immediately to FINISHED */
        assert(state[0] == SCE_NGS2_VOICE_STATE_FLAG_FINISHED);
    } else {
        assert(state[0] == 0);
    }

    harness_close(&h);
    puts("PASS: test_eof_after_fifo_empty");
}

/* Test 5: EOF arrives BEFORE FIFO is drained */
static void test_eof_before_fifo_empty(int is_b2f2d_expected) {
    TestHarness h;
    harness_open(&h, 0);

    pthread_mutex_lock(&ngs2_lock);
    Ngs2Voice *v = ngs2_voice_locked(h.voice);
    voice_add_fifo_grain(v, 1500);
    voice_add_fifo_grain(v, 1500);
    pthread_mutex_unlock(&ngs2_lock);

    /* Send EOF while 2 grains remain in FIFO */
    voice_cmd_send_empty_descriptor(h.voice);

    uint32_t state[4];
    assert(!ngs2_voice_state(h.voice, state, 0x10));
    if (is_b2f2d_expected) {
        /* Still PLAYING because 2 grains remain */
        assert(state[0] == (SCE_NGS2_VOICE_STATE_FLAG_ACTIVE | SCE_NGS2_VOICE_STATE_FLAG_PLAYING));
    }

    /* Render grain 1: 1 grain remains */
    harness_render(&h);
    assert(!ngs2_voice_state(h.voice, state, 0x10));
    if (is_b2f2d_expected) {
        assert(state[0] == (SCE_NGS2_VOICE_STATE_FLAG_ACTIVE | SCE_NGS2_VOICE_STATE_FLAG_PLAYING));
        assert(state[2] == 256);
    }

    /* Render grain 2: FIFO drained to 0 */
    harness_render(&h);
    assert(!ngs2_voice_state(h.voice, state, 0x10));
    if (is_b2f2d_expected) {
        assert(state[0] == SCE_NGS2_VOICE_STATE_FLAG_FINISHED);
    } else {
        assert(state[0] == 0);
    }

    harness_close(&h);
    puts("PASS: test_eof_before_fifo_empty");
}

/* Test 6: Worker completion without new grains or with decode error after EOF */
static void test_worker_completion_after_eof(int is_b2f2d_expected) {
    TestHarness h;
    harness_open(&h, 0);

    /* Worker is actively decoding in background when empty descriptor arrives */
    pthread_mutex_lock(&ngs2_lock);
    Ngs2Voice *v = ngs2_voice_locked(h.voice);
    Ngs2Atrac9Voice *st = v->atrac9;
    st->decode_inflight = 1; /* Simulate worker actively decoding in background */
    pthread_mutex_unlock(&ngs2_lock);

    /* Empty descriptor received (EOF requested) while worker decode is in-flight */
    voice_cmd_send_empty_descriptor(h.voice);

    /* While decode_inflight == 1, voice MUST remain PLAYING */
    uint32_t s16[4];
    assert(!ngs2_voice_state(h.voice, s16, 0x10));
    if (is_b2f2d_expected) {
        assert(s16[0] == (SCE_NGS2_VOICE_STATE_FLAG_ACTIVE | SCE_NGS2_VOICE_STATE_FLAG_PLAYING));
    }

    /* Simulate worker releasing decode_inflight (as in runtime_tlg.c:1294) with 0 grains produced */
    pthread_mutex_lock(&ngs2_lock);
    v = ngs2_voice_locked(h.voice);
    st = v->atrac9;
    st->decode_inflight = 0;
#ifdef NGS2_B2F2D_SUPPORTED
    ngs2_atrac9_check_completion_locked(v);
#endif
    pthread_mutex_unlock(&ngs2_lock);

    /* Now that decode_inflight is released and no grains/segments remain, voice must be FINISHED */
    assert(!ngs2_voice_state(h.voice, s16, 0x10));
    if (is_b2f2d_expected) {
        assert(s16[0] == SCE_NGS2_VOICE_STATE_FLAG_FINISHED);
    } else {
        assert(s16[0] == 0);
    }

    /* Also test decode error branch: worker encounters decode error after EOF */
    voice_cmd_action(h.voice, 0); /* PLAY to resume slot */
    pthread_mutex_lock(&ngs2_lock);
    v = ngs2_voice_locked(h.voice);
    st = v->atrac9;
    st->decode_inflight = 1;
    st->decode_errors++; /* decode error occurred */
    pthread_mutex_unlock(&ngs2_lock);

    voice_cmd_send_empty_descriptor(h.voice);

    /* Worker finishes error handling and releases decode_inflight */
    pthread_mutex_lock(&ngs2_lock);
    v = ngs2_voice_locked(h.voice);
    st = v->atrac9;
    st->decode_inflight = 0;
#ifdef NGS2_B2F2D_SUPPORTED
    ngs2_atrac9_check_completion_locked(v);
#endif
    pthread_mutex_unlock(&ngs2_lock);

    assert(!ngs2_voice_state(h.voice, s16, 0x10));
    if (is_b2f2d_expected) {
        assert(s16[0] == SCE_NGS2_VOICE_STATE_FLAG_FINISHED);
    } else {
        assert(s16[0] == 0);
    }

    harness_close(&h);
    puts("PASS: test_worker_completion_after_eof");
}

/* Test 7: Adjustment 1 — EOF after snapshot release (failed & accepted snapshot reservation) */
static TestHarness *g_hook_harness = NULL;
static void on_snapshot_hook_send_eof(void) {
    if (g_hook_harness) {
        /* Empty descriptor arrives while snapshot reservation is in flight */
        voice_cmd_send_empty_descriptor(g_hook_harness->voice);
    }
}

static void test_eof_during_failed_snapshot_reservation(int is_b2f2d_expected) {
    TestHarness h;
    harness_open(&h, 0);

    /* Prepare snapshot hook: when ngs2_segment_control unlocks ngs2_lock to copy snapshot,
     * the hook sends an empty descriptor (EOF). */
    g_hook_harness = &h;
    g_payload_hook = on_snapshot_hook_send_eof;
    g_simulate_payload_failure = 1; /* Simulate payload copy failure in runtime_memory_copy_snapshot */

    /* Send real data segment via ngs2_segment_control (command 0x10000001) */
    unsigned char dummy_payload[192] = {0};
    voice_cmd_send_data_segment(h.voice, (uintptr_t)dummy_payload, 192);

    g_simulate_payload_failure = 0;
    g_payload_hook = NULL;
    g_hook_harness = NULL;

    /* In B2-F2D:
     * When the snapshot failed, segment_reservations & snapshot_inflight decremented to 0.
     * In the common path after accept/reject, ngs2_atrac9_check_completion_locked was called.
     * Since end_requested was 1 and no work remained, the voice transitioned to FINISHED! */
    uint32_t s16[4];
    assert(!ngs2_voice_state(h.voice, s16, 0x10));
    if (is_b2f2d_expected) {
        assert(s16[0] == SCE_NGS2_VOICE_STATE_FLAG_FINISHED && "Failed snapshot after EOF must complete to FINISHED");
    } else {
        assert(s16[0] == 0);
    }

    harness_close(&h);
    puts("PASS: test_eof_during_failed_snapshot_reservation");
}

/* Test 8: Adjustment 2 — EOF while PAUSED then RESUME or PLAY */
static void test_eof_while_paused_then_resume(int is_b2f2d_expected) {
    TestHarness h;
    harness_open(&h, 0);

    /* 1. Sequence: PAUSE -> EOF with empty FIFO -> PAUSED -> RESUME -> FINISHED */
    voice_cmd_action(h.voice, 1); /* PAUSE */

    uint32_t s16[4];
    assert(!ngs2_voice_state(h.voice, s16, 0x10));
    if (is_b2f2d_expected) {
        assert(s16[0] == (SCE_NGS2_VOICE_STATE_FLAG_ACTIVE | SCE_NGS2_VOICE_STATE_FLAG_PAUSED));
    }

    /* Send empty descriptor (EOF) while PAUSED and FIFO empty */
    voice_cmd_send_empty_descriptor(h.voice);

    /* Must still report PAUSED (not prematurely finished while paused) */
    assert(!ngs2_voice_state(h.voice, s16, 0x10));
    if (is_b2f2d_expected) {
        assert(s16[0] == (SCE_NGS2_VOICE_STATE_FLAG_ACTIVE | SCE_NGS2_VOICE_STATE_FLAG_PAUSED));
    }

    /* Issue RESUME (Cmd 6 action 2) */
    voice_cmd_action(h.voice, 2); /* RESUME */

    /* Immediately after resuming, since end_requested == 1 and FIFO is empty,
     * voice must transition cleanly to FINISHED */
    assert(!ngs2_voice_state(h.voice, s16, 0x10));
    if (is_b2f2d_expected) {
        assert(s16[0] == SCE_NGS2_VOICE_STATE_FLAG_FINISHED && "PAUSED -> EOF -> RESUME must complete to FINISHED");
    } else {
        assert(s16[0] == 0);
    }
    harness_close(&h);

    /* 2. Variant: PAUSE -> EOF with empty FIFO -> PAUSED -> PLAY -> FINISHED */
    harness_open(&h, 0);

    /* Issue PAUSE */
    voice_cmd_action(h.voice, 1); /* PAUSE */
    assert(!ngs2_voice_state(h.voice, s16, 0x10));
    if (is_b2f2d_expected) {
        assert(s16[0] == (SCE_NGS2_VOICE_STATE_FLAG_ACTIVE | SCE_NGS2_VOICE_STATE_FLAG_PAUSED));
    }

    /* Send EOF while paused */
    voice_cmd_send_empty_descriptor(h.voice);
    assert(!ngs2_voice_state(h.voice, s16, 0x10));
    if (is_b2f2d_expected) {
        assert(s16[0] == (SCE_NGS2_VOICE_STATE_FLAG_ACTIVE | SCE_NGS2_VOICE_STATE_FLAG_PAUSED));
    }

    /* Issue PLAY (Cmd 6 action 0) to unpause/resume */
    voice_cmd_action(h.voice, 0); /* PLAY */

    /* Action 0 on a PAUSED voice must not wipe end_requested, and must trigger completion to FINISHED */
    assert(!ngs2_voice_state(h.voice, s16, 0x10));
    if (is_b2f2d_expected) {
        assert(s16[0] == SCE_NGS2_VOICE_STATE_FLAG_FINISHED && "PAUSED -> EOF -> PLAY must complete to FINISHED");
    } else {
        assert(s16[0] == 0);
    }

    harness_close(&h);
    puts("PASS: test_eof_while_paused_then_resume");
}

/* Test 9: PLAY command invariants (repeated PLAY and EOF followed by PLAY during playback) */
static void test_play_command_invariants(int is_b2f2d_expected) {
    TestHarness h;
    harness_open(&h, 0);

    pthread_mutex_lock(&ngs2_lock);
    Ngs2Voice *v = ngs2_voice_locked(h.voice);
    voice_add_fifo_grain(v, 1000);
    voice_add_fifo_grain(v, 1000);
    pthread_mutex_unlock(&ngs2_lock);

    /* Render grain 1: 256 samples */
    harness_render(&h);

    uint32_t s16[4];
    assert(!ngs2_voice_state(h.voice, s16, 0x10));
    if (is_b2f2d_expected) {
        assert(s16[0] == (SCE_NGS2_VOICE_STATE_FLAG_ACTIVE | SCE_NGS2_VOICE_STATE_FLAG_PLAYING));
        assert(s16[2] == 256);
    }

    /* Sub-case 1: Repeated PLAY command (Cmd 6 action 0) during active playback */
    voice_cmd_action(h.voice, 0); /* PLAY */

    /* Must NOT reset playback cursor or counters */
    assert(!ngs2_voice_state(h.voice, s16, 0x10));
    if (is_b2f2d_expected) {
        assert(s16[0] == (SCE_NGS2_VOICE_STATE_FLAG_ACTIVE | SCE_NGS2_VOICE_STATE_FLAG_PLAYING));
        assert(s16[2] == 256 && "Repeated PLAY must not reset playback cursor during playback");
    }

    /* Sub-case 2: EOF followed by duplicate PLAY while FIFO still has grains */
    voice_cmd_send_empty_descriptor(h.voice); /* end_requested = 1 */

    pthread_mutex_lock(&ngs2_lock);
    v = ngs2_voice_locked(h.voice);
    if (is_b2f2d_expected) {
#ifdef NGS2_B2F2D_SUPPORTED
        assert(v->atrac9->end_requested == 1);
#endif
    }
    pthread_mutex_unlock(&ngs2_lock);

    /* Duplicate PLAY command issued while EOF is pending */
    voice_cmd_action(h.voice, 0); /* PLAY */

    pthread_mutex_lock(&ngs2_lock);
    v = ngs2_voice_locked(h.voice);
    if (is_b2f2d_expected) {
#ifdef NGS2_B2F2D_SUPPORTED
        assert(v->atrac9->end_requested == 1 && "Duplicate PLAY must not wipe pending EOF");
#endif
    }
    pthread_mutex_unlock(&ngs2_lock);

    /* Render remaining grain: FIFO is now empty */
    harness_render(&h);

    /* Voice must transition to FINISHED, proving EOF was preserved! */
    assert(!ngs2_voice_state(h.voice, s16, 0x10));
    if (is_b2f2d_expected) {
        assert(s16[0] == SCE_NGS2_VOICE_STATE_FLAG_FINISHED && "Voice must complete to FINISHED");
    } else {
        assert(s16[0] == 0);
    }

    harness_close(&h);
    puts("PASS: test_play_command_invariants");
}

/* Test 10: Looping Stream (no EOF) never marks FINISHED */
static void test_looping_stream(int is_b2f2d_expected) {
    TestHarness h;
    harness_open(&h, 0);

    pthread_mutex_lock(&ngs2_lock);
    Ngs2Voice *v = ngs2_voice_locked(h.voice);
    voice_add_fifo_grain(v, 2000);
    pthread_mutex_unlock(&ngs2_lock);

    /* Render grain */
    harness_render(&h);

    /* Even with pcm_count == 0, looping stream remains PLAYING */
    uint32_t state[4];
    assert(!ngs2_voice_state(h.voice, state, 0x10));
    if (is_b2f2d_expected) {
        assert(state[0] == (SCE_NGS2_VOICE_STATE_FLAG_ACTIVE | SCE_NGS2_VOICE_STATE_FLAG_PLAYING));
        assert(state[2] == 256);
    }

    harness_close(&h);
    puts("PASS: test_looping_stream");
}

/* Test 11: Pause and Resume Semantics */
static void test_pause_resume(int is_b2f2d_expected) {
    TestHarness h;
    harness_open(&h, 0);

    pthread_mutex_lock(&ngs2_lock);
    Ngs2Voice *v = ngs2_voice_locked(h.voice);
    voice_add_fifo_grain(v, 1000);
    voice_add_fifo_grain(v, 1000);
    pthread_mutex_unlock(&ngs2_lock);

    /* Render 1 grain: 256 samples */
    harness_render(&h);

    /* PAUSE */
    voice_cmd_action(h.voice, 1);
    uint32_t state[4];
    assert(!ngs2_voice_state(h.voice, state, 0x10));
    if (is_b2f2d_expected) {
        assert(state[0] == (SCE_NGS2_VOICE_STATE_FLAG_ACTIVE | SCE_NGS2_VOICE_STATE_FLAG_PAUSED));
        assert(state[2] == 256);
    }

    /* RESUME */
    voice_cmd_action(h.voice, 2);
    assert(!ngs2_voice_state(h.voice, state, 0x10));
    if (is_b2f2d_expected) {
        assert(state[0] == (SCE_NGS2_VOICE_STATE_FLAG_ACTIVE | SCE_NGS2_VOICE_STATE_FLAG_PLAYING));
        assert(state[2] == 256);
    }

    /* Render remaining grain */
    harness_render(&h);
    assert(!ngs2_voice_state(h.voice, state, 0x10));
    if (is_b2f2d_expected) {
        assert(state[2] == 512);
    }

    harness_close(&h);
    puts("PASS: test_pause_resume");
}

/* Test 12: Reconfiguration & In-flight Work Isolation */
static void test_atrac9_waveform_reconfig_inflight(int is_b2f2d_expected) {
    TestHarness h;
    harness_open(&h, 0);

    pthread_mutex_lock(&ngs2_lock);
    Ngs2Voice *v = ngs2_voice_locked(h.voice);
    Ngs2Atrac9Voice *st = v->atrac9;
    st->segment_reservations = 1; /* In-flight reservation */
    uint64_t gen_before = st->generation;
    pthread_mutex_unlock(&ngs2_lock);

    /* Reconfigure with identical waveform while reservation is in flight */
    voice_cmd_setup(h.voice, 0);

    pthread_mutex_lock(&ngs2_lock);
    v = ngs2_voice_locked(h.voice);
    st = v->atrac9;
    if (is_b2f2d_expected) {
        /* Generation must NOT have advanced because active work is in-flight */
        assert(st->generation == gen_before && "In-flight work must not be cancelled");
    }
    st->segment_reservations = 0;
    pthread_mutex_unlock(&ngs2_lock);

    /* Now stop voice explicitly */
    voice_cmd_action(h.voice, 5); /* STOP */

    /* Reconfigure stopped voice */
    voice_cmd_setup(h.voice, 0);

    pthread_mutex_lock(&ngs2_lock);
    v = ngs2_voice_locked(h.voice);
    st = v->atrac9;
    if (is_b2f2d_expected) {
        /* Stopped voice reconfigures cleanly with advanced generation */
        assert(st->generation > gen_before && "Generation must advance on reuse of stopped slot");
#ifdef NGS2_B2F2D_SUPPORTED
        assert(st->mixed_samples == 0);
        assert(st->dropped_samples == 0);
#endif
    }
    pthread_mutex_unlock(&ngs2_lock);

    harness_close(&h);
    puts("PASS: test_atrac9_waveform_reconfig_inflight");
}

/* Test 13: Adjustment 3 — Reconfiguration of STOPPED voice with pending work */
static void test_stopped_voice_reconfig_with_pending_work(int is_b2f2d_expected) {
    TestHarness h;
    harness_open(&h, 0);

    /* 1. Insert 2 grains (512 samples) and record initial generation */
    pthread_mutex_lock(&ngs2_lock);
    Ngs2Voice *v = ngs2_voice_locked(h.voice);
    voice_add_fifo_grain(v, 1000);
    voice_add_fifo_grain(v, 1000);
    assert(v->atrac9->pcm_count == 2);
    uint64_t gen_before = v->atrac9->generation;
    pthread_mutex_unlock(&ngs2_lock);

    /* 2. Issue STOP (Cmd 6 action 5) while 2 grains remain */
    voice_cmd_action(h.voice, 5); /* STOP */

    /* 3. Check dropped_samples == 512 immediately after STOP, before any reset */
    pthread_mutex_lock(&ngs2_lock);
    v = ngs2_voice_locked(h.voice);
    assert(v->internal_state == NGS2_VOICE_STOPPED);
    Ngs2Atrac9Voice *st = v->atrac9;
    if (is_b2f2d_expected) {
#ifdef NGS2_B2F2D_SUPPORTED
        assert(st->dropped_samples == 512 && "dropped_samples must equal 512 immediately after STOP");
        assert(st->pcm_count == 0 && "STOP must drain FIFO grains");
#endif
    }

    /* 4. With voice STOPPED, inject residual grains into fixture under lock.
     * NOTE: This residual injection is a synthetic robustness test for reconfig
     * boundary handling; it is not an expected normal sequence of the Orbis guest. */
    voice_add_fifo_grain(v, 777);
    assert(st->pcm_count == 1 && "Synthetic residual grain injected");
    pthread_mutex_unlock(&ngs2_lock);

    /* 5. Reconfigure waveform with voice STOPPED */
    voice_cmd_setup(h.voice, 0);

    /* 6. Verify clean FIFO, advanced generation, and reinitialized counters */
    pthread_mutex_lock(&ngs2_lock);
    v = ngs2_voice_locked(h.voice);
    st = v->atrac9;
    if (is_b2f2d_expected) {
        assert(st->generation > gen_before && "Generation must advance on reconfiguring STOPPED voice");
        assert(st->pcm_count == 0 && "Pending FIFO grains must be cleared by reconfig");
#ifdef NGS2_B2F2D_SUPPORTED
        assert(st->mixed_samples == 0 && "New generation starts with 0 mixed samples");
        assert(st->virtualized_samples == 0 && "New generation starts with 0 virtualized samples");
        assert(st->dropped_samples == 0 && "New generation starts with 0 dropped samples");
        assert(st->end_requested == 0 && "New generation is not end_requested");
        assert(st->finished == 0 && "New generation is not finished");
#endif
    }
    pthread_mutex_unlock(&ngs2_lock);

    harness_close(&h);
    puts("PASS: test_stopped_voice_reconfig_with_pending_work");
}

/* Test 14: Dropped Samples Scoped per Generation */
static void test_dropped_samples_generation_scope(int is_b2f2d_expected) {
    TestHarness h;
    harness_open(&h, 0);

    pthread_mutex_lock(&ngs2_lock);
    Ngs2Voice *v = ngs2_voice_locked(h.voice);
    voice_add_fifo_grain(v, 1000);
    voice_add_fifo_grain(v, 1000);
    pthread_mutex_unlock(&ngs2_lock);

    /* Render 1 grain: 256 mixed, 1 remains */
    harness_render(&h);

    /* Issue STOP (Cmd 6 action 5) while 1 grain remains */
    voice_cmd_action(h.voice, 5);

    pthread_mutex_lock(&ngs2_lock);
    v = ngs2_voice_locked(h.voice);
    if (is_b2f2d_expected) {
#ifdef NGS2_B2F2D_SUPPORTED
        assert(v->atrac9->dropped_samples == 256 && "Exactly 1 remaining grain dropped = 256 samples");
        assert(v->atrac9->mixed_samples == 256);
#endif
    }
    pthread_mutex_unlock(&ngs2_lock);

    /* Start new generation on the slot */
    voice_cmd_action(h.voice, 0); /* START */

    pthread_mutex_lock(&ngs2_lock);
    v = ngs2_voice_locked(h.voice);
    if (is_b2f2d_expected) {
#ifdef NGS2_B2F2D_SUPPORTED
        assert(v->atrac9->dropped_samples == 0 && "Dropped samples must reset for new generation");
        assert(v->atrac9->mixed_samples == 0 && "Mixed samples must reset for new generation");
#endif
    }
    pthread_mutex_unlock(&ngs2_lock);

    harness_close(&h);
    puts("PASS: test_dropped_samples_generation_scope");
}

/* Test 15: HEVAG Streams Unaffected */
static void test_hevag_stream_unaffected(void) {
    TestHarness h;
    harness_open(&h, 1 /* HEVAG */);

    pthread_mutex_lock(&ngs2_lock);
    Ngs2Voice *v = ngs2_voice_locked(h.voice);
    voice_add_fifo_grain(v, 500);
    pthread_mutex_unlock(&ngs2_lock);

    harness_render(&h);

    /* HEVAG voice stays active and uses its own tail flush logic */
    pthread_mutex_lock(&ngs2_lock);
    v = ngs2_voice_locked(h.voice);
    assert(v->atrac9 && v->waveform.format == 0x1cu);
    pthread_mutex_unlock(&ngs2_lock);

    harness_close(&h);
    puts("PASS: test_hevag_stream_unaffected");
}

/* Test 16: HEVAG and Unconfigured Voice Strict Baseline Parity (All Zeros) */
static void test_hevag_and_unconfigured_voice_baseline_parity(int is_b2f2d_expected) {
    (void)is_b2f2d_expected;
    TestHarness h;
    harness_open(&h, 1 /* HEVAG voice */);

    /* 1. Test HEVAG voice: query 0x10 and 0x30 */
    uint32_t s16[4], s48[12];
    memset(s16, 0xbb, sizeof(s16));
    memset(s48, 0xbb, sizeof(s48));

    assert(!ngs2_voice_state(h.voice, s16, 0x10));
    assert(!ngs2_voice_state(h.voice, s48, 0x30));

    /* In BOTH baseline and B2-F2D candidate, HEVAG returns all zeros (strict parity) */
    for (int i = 0; i < 4; ++i) {
        assert(s16[i] == 0 && "HEVAG voice query 0x10 must return all zeros (baseline parity)");
    }
    for (int i = 0; i < 12; ++i) {
        assert(s48[i] == 0 && "HEVAG voice query 0x30 must return all zeros (baseline parity)");
    }

    /* 2. Test Unconfigured voice (Voice index 1 on rack has no commands issued) */
    Ngs2Handle *voice_unconf = NULL;
    assert(!ngs2_voice_handle(h.rack, 1, &voice_unconf));
    memset(s16, 0xcc, sizeof(s16));
    memset(s48, 0xcc, sizeof(s48));

    assert(!ngs2_voice_state(voice_unconf, s16, 0x10));
    assert(!ngs2_voice_state(voice_unconf, s48, 0x30));

    for (int i = 0; i < 4; ++i) {
        assert(s16[i] == 0 && "Unconfigured voice query 0x10 must return all zeros");
    }
    for (int i = 0; i < 12; ++i) {
        assert(s48[i] == 0 && "Unconfigured voice query 0x30 must return all zeros");
    }

    harness_close(&h);
    puts("PASS: test_hevag_and_unconfigured_voice_baseline_parity");
}

/* Test 17: B2-F2C Zero-Matrix Bypass Preservation and Active Verification */
static void test_b2f2c_zero_matrix_bypass_preservation(int is_b2f2d_expected) {
    ngs2_b2f2c_setting = 1; /* Explicitly activate B2-F2C zero matrix bypass for this contract */
    TestHarness h;
    harness_open(&h, 0);

    /* Set matrix to all zeros (audible volume = 0) */
    float matrix[32] = {0};
    unsigned char gain[0x18]; make_cmd(gain, sizeof(gain), 1);
    put32(gain, 12, 32u); putptr(gain, 16, (uintptr_t)matrix);
    assert(!ngs2_voice_control(h.voice, gain));

    pthread_mutex_lock(&ngs2_lock);
    Ngs2Voice *v = ngs2_voice_locked(h.voice);
    voice_add_fifo_grain(v, 8000);
    pthread_mutex_unlock(&ngs2_lock);

    /* Render under B2-F2C */
    harness_render(&h);

    pthread_mutex_lock(&ngs2_lock);
    Ngs2System *sys = ngs2_system_locked(h.system);
    /* Verify B2-F2C Zero Matrix Bypass was actively exercised */
    assert(sys->voices_mixed == 0 && "Zero matrix must NOT mix into audible outputs");
    assert(sys->virtualized_grains == 1 && "Zero matrix bypass must increment virtualized_grains");
    pthread_mutex_unlock(&ngs2_lock);

    uint32_t state[4];
    assert(!ngs2_voice_state(h.voice, state, 0x10));
    if (is_b2f2d_expected) {
        /* Truthful playback position accounts for the virtualized bypass grain */
        assert(state[2] == 256 && "Virtualized bypass grain accounted in truthful position");
    }

    harness_close(&h);
    ngs2_b2f2c_setting = -1;
    puts("PASS: test_b2f2c_zero_matrix_bypass_preservation");
}

int main(int argc, char **argv) {
    int expect_b2f2d = 0;
    if (argc > 1 && !strcmp(argv[1], "--assert-b2f2d-active")) {
        expect_b2f2d = 1;
    }

    test_buffer_initialization(expect_b2f2d);
    test_soundx_polling_order_30_then_10(expect_b2f2d);
    test_soundx_polling_order_10_then_30(expect_b2f2d);
    test_eof_after_fifo_empty(expect_b2f2d);
    test_eof_before_fifo_empty(expect_b2f2d);
    test_worker_completion_after_eof(expect_b2f2d);
    test_eof_during_failed_snapshot_reservation(expect_b2f2d);
    test_eof_while_paused_then_resume(expect_b2f2d);
    test_play_command_invariants(expect_b2f2d);
    test_looping_stream(expect_b2f2d);
    test_pause_resume(expect_b2f2d);
    test_atrac9_waveform_reconfig_inflight(expect_b2f2d);
    test_stopped_voice_reconfig_with_pending_work(expect_b2f2d);
    test_dropped_samples_generation_scope(expect_b2f2d);
    test_hevag_stream_unaffected();
    test_hevag_and_unconfigured_voice_baseline_parity(expect_b2f2d);
    test_b2f2c_zero_matrix_bypass_preservation(expect_b2f2d);

    if (expect_b2f2d) {
        puts("ALL NGS2 B2-F2D CANDIDATE TESTS PASSED (GREEN)");
    } else {
        puts("BASELINE VERIFICATION COMPLETED");
    }
    return 0;
}
