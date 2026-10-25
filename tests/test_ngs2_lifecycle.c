/* SPDX-License-Identifier: GPL-2.0-or-later
 * Independent Positive Contract Tests for NGS2 Voice Lifecycle & VoiceGetState (Revision 3).
 *
 * Implements corrections per Tech Lead Sol directive (DECISAO_ADENDO_E_PROXIMA_TAREFA.md):
 *  1. Proven Masks: Bit 0 (0x01, active), Bit 1 (0x02, playing via 0x22 mask), Bit 5 (0x20, finished).
 *     Removed unproven SCE_NGS2_STATE_PAUSED (0x04) expectation.
 *  2. Gain Vector Sensitivity: Proves guest math at 0x00b2bc6e; positive non-zero expectation labeled
 *     BLOCKED (attack envelope or mute can produce 0.0f).
 *  3. Position Progress: Query observed; freeze under motor progress, unit, width, and monotonicity
 *     labeled BLOCKED.
 *  4. SubMixer Lifecycle: Querying 0x10 on voice_a does not cover SubMixer lifecycle; explicitly
 *     labeled as UNCOVERED (no fake counters fabricated).
 *  5. Drain Transition: Runner halts at first RED predicate; subsequent assertions labeled UNTESTED/BLOCKED.
 *  6. +0x20..+0x2f Scope: Unconsumed bytes scoped strictly to examined game.elf call sites, not global API padding.
 */
#define main previous_mixer_tests_main
#include "test_ngs2_mixer.c"
#undef main

#define REQUIRE(x) do { \
    if (!(x)) { \
        fprintf(stderr, "LIFECYCLE_CONTRACT_FAILED case=%s line=%d: %s\n", name, __LINE__, #x); \
        return 1; \
    } \
} while (0)

/* Proven masks from game.elf instructions. Names are consumer interpretations. */
#define SCE_NGS2_STATE_ACTIVE    0x00000001u /* Bit 0: Mask 0x01 tested at 0xb6f040 (active in SoundX) */
#define SCE_NGS2_STATE_PLAYING   0x00000002u /* Bit 1: Mask 0x22 tested at 0xb6f013 (playing in SoundX) */
#define SCE_NGS2_STATE_FINISHED  0x00000020u /* Bit 5: Mask 0x20 tested at 0xb6f026 (finished in SoundX) */

typedef struct {
    Ngs2Handle *sys;
    Ngs2Handle *sampler_rack;
    Ngs2Handle *submixer_rack;
    Ngs2Handle *voice_a;
    Ngs2Handle *voice_b;
    Ngs2Handle *sub_voice;
    float master[256 * 8], aux[256 * 8];
    Ngs2RenderBuffer outputs[2];
} LifecycleFixture;

static void open_fixture(LifecycleFixture *f) {
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

    setup_waveform(f->voice_a, 4);
    setup_waveform(f->voice_b, 4);
    setup_waveform(f->sub_voice, 2);

    f->outputs[0] = (Ngs2RenderBuffer){f->master, sizeof(f->master), 0x18, 8};
    f->outputs[1] = (Ngs2RenderBuffer){f->aux, sizeof(f->aux), 0x18, 8};
}

static void close_fixture(LifecycleFixture *f) {
    if (f->sys) assert(!ngs2_system_destroy(f->sys, NULL));
}

static int run_lifecycle_case(const char *name, int assert_positive_contract) {
    LifecycleFixture f;
    open_fixture(&f);

    if (!strcmp(name, "buffer_size_validation")) {
        /* Contract: 0x10 and 0x30 are valid. All other sizes and NULL state must return NGS2_INVALID */
        uint8_t buf48[0x30], buf16[0x10], buf32[0x20];
        REQUIRE(ngs2_voice_state(f.voice_a, buf48, 0x30) == 0);
        REQUIRE(ngs2_voice_state(f.voice_a, buf16, 0x10) == 0);
        REQUIRE(ngs2_voice_state(f.voice_a, NULL, 0x30) == NGS2_INVALID);
        REQUIRE(ngs2_voice_state(f.voice_a, buf32, 0x20) == NGS2_INVALID);
        REQUIRE(ngs2_voice_state(f.voice_a, buf16, 0x08) == NGS2_INVALID);
        REQUIRE(ngs2_voice_state(f.voice_a, buf48, 0x40) == NGS2_INVALID);
        REQUIRE(ngs2_voice_state((Ngs2Handle *)0xdeadbeefULL, buf48, 0x30) == NGS2_INVALID);
    }
    else if (!strcmp(name, "query_idempotence")) {
        /* Contract: Calling VoiceGetState 100 times produces identical output and does not mutate voice */
        set_voice_state(f.voice_a, 0); /* PLAY */
        pthread_mutex_lock(&ngs2_lock);
        Ngs2Voice *va = ngs2_voice_locked(f.voice_a);
        push_synthetic_grain(va, 1000);
        uint32_t fifo_before = va->atrac9->pcm_count;
        uint64_t gen_before = va->atrac9->generation;
        pthread_mutex_unlock(&ngs2_lock);

        uint8_t first_query[0x30];
        memset(first_query, 0x5a, sizeof(first_query));
        REQUIRE(ngs2_voice_state(f.voice_a, first_query, sizeof(first_query)) == 0);

        for (int i = 0; i < 100; ++i) {
            uint8_t current_query[0x30];
            memset(current_query, 0xa5, sizeof(current_query));
            REQUIRE(ngs2_voice_state(f.voice_a, current_query, sizeof(current_query)) == 0);
            REQUIRE(!memcmp(first_query, current_query, sizeof(first_query)));
        }

        pthread_mutex_lock(&ngs2_lock);
        REQUIRE(va->atrac9->pcm_count == fifo_before);
        REQUIRE(va->atrac9->generation == gen_before);
        pthread_mutex_unlock(&ngs2_lock);
    }
    else if (!strcmp(name, "voice_isolation")) {
        /* Contract: Voice A release / clear must not affect Voice B state, generation or FIFO */
        set_voice_state(f.voice_a, 0); /* PLAY A */
        set_voice_state(f.voice_b, 0); /* PLAY B */

        pthread_mutex_lock(&ngs2_lock);
        Ngs2Voice *va = ngs2_voice_locked(f.voice_a);
        Ngs2Voice *vb = ngs2_voice_locked(f.voice_b);
        push_synthetic_grain(va, 16384);
        push_synthetic_grain(vb, 8192);
        uint32_t fifo_b_before = vb->atrac9->pcm_count;
        uint64_t gen_b_before = vb->atrac9->generation;
        pthread_mutex_unlock(&ngs2_lock);

        /* Release Voice A with Action 1 */
        set_voice_state(f.voice_a, 1);

        pthread_mutex_lock(&ngs2_lock);
        REQUIRE(va->internal_state == NGS2_VOICE_RELEASING);
        REQUIRE(va->atrac9->pcm_count == 0);
        REQUIRE(vb->internal_state == NGS2_VOICE_PLAYING);
        REQUIRE(vb->atrac9->pcm_count == fifo_b_before);
        REQUIRE(vb->atrac9->generation == gen_b_before);
        pthread_mutex_unlock(&ngs2_lock);
    }
    else if (!strcmp(name, "reconfig_generation_cancellation")) {
        /* Contract: Waveform reconfig (Cmd 0x10000000) advances generation and clears prior pending */
        set_voice_state(f.voice_a, 0);
        pthread_mutex_lock(&ngs2_lock);
        Ngs2Voice *va = ngs2_voice_locked(f.voice_a);
        push_synthetic_grain(va, 4000);
        uint64_t gen1 = va->atrac9->generation;
        pthread_mutex_unlock(&ngs2_lock);

        /* Reconfigure waveform layout (change channel count from 4 to 2) */
        setup_waveform(f.voice_a, 2);

        pthread_mutex_lock(&ngs2_lock);
        uint64_t gen2 = va->atrac9->generation;
        REQUIRE(gen2 > gen1);
        REQUIRE(va->atrac9->pcm_count == 0);
        pthread_mutex_unlock(&ngs2_lock);
    }
    else if (!strcmp(name, "consumer_gain_vector_impact")) {
        /* P1 Consumer Test: Demonstrates that returning 0 at +0x10 mutates guest gain calculation
         * from 0x00b2bc68..0x00b2bc97 (vmulss 0x38(%r13), %xmm1, %xmm3), forcing effective gain to 0.0f (SILENCE).
         */
        set_voice_state(f.voice_a, 0); /* PLAY */

        uint8_t state[0x30];
        memset(state, 0, sizeof(state));
        REQUIRE(ngs2_voice_state(f.voice_a, state, sizeof(state)) == 0);

        /* Emulate guest 0x00b6f0c0..0x00b6f0c9: vector at +0x10 copied to voice + 0x38 */
        float voice_struct_38 = *(float *)(state + 0x10);

        /* Emulate guest 0x00b2bc68..0x00b2bc97:
         * xmm1 = voice_base_gain (e.g. 1.0f)
         * xmm0 = track_vol (e.g. 0.8f)
         * xmm2 = voice_struct_38 (from VoiceGetState +0x10)
         * xmm3 = xmm2 * xmm1 * xmm0;
         */
        float xmm1 = 1.0f;
        float xmm0 = 0.8f;
        float xmm2 = voice_struct_38;
        float effective_gain = xmm2 * xmm1 * xmm0;

        if (assert_positive_contract) {
            /* Positive Contract Requirement (BLOCKED):
             * effective_gain > 0.0f is NOT a universal contract for PLAY (an attack envelope
             * or legitimate mute could produce 0.0f). Positive expectation is preserved as a
             * sensitivity demonstration and marked BLOCKED pending fixture parameters.
             * Baseline has state[0x10] == 0.0f -> effective_gain == 0.0f -> proven RED gate. */
            REQUIRE(effective_gain > 0.0f);
        } else {
            printf("BASELINE OBSERVATION: consumer_gain_vector_impact produces effective_gain=%.6f (SILENCE)\n",
                   effective_gain);
        }
    }
    else if (!strcmp(name, "streaming_empty_active")) {
        /* Positive Contract: A voice placed in PLAY with an empty FIFO (initial onset or waiting data)
         * MUST report ACTIVE (bit 0 = 1) and PLAYING (bit 1 = 1).
         * Baseline returns 0 -> FALHA RED esperada no baseline. */
        set_voice_state(f.voice_a, 0); /* PLAY */

        uint8_t state[0x30] = {0};
        REQUIRE(ngs2_voice_state(f.voice_a, state, sizeof(state)) == 0);
        uint32_t flags = *(uint32_t *)state;

        if (assert_positive_contract) {
            REQUIRE((flags & SCE_NGS2_STATE_ACTIVE) != 0);
            REQUIRE((flags & SCE_NGS2_STATE_PLAYING) != 0);
        } else {
            printf("BASELINE OBSERVATION: streaming_empty_active returns flags=0x%08x\n", flags);
        }
    }
    else if (!strcmp(name, "inflight_decode_labeled_injection")) {
        /* Labeled Synthetic Injection: While worker decode is in flight, voice must report ACTIVE (bit 0=1).
         * Explicit harness injection labeled per Tech Lead directive. */
        set_voice_state(f.voice_a, 0);
        pthread_mutex_lock(&ngs2_lock);
        Ngs2Voice *va = ngs2_voice_locked(f.voice_a);
        va->atrac9->decode_inflight = 1; /* LABELED SYNTHETIC INJECTION */
        pthread_mutex_unlock(&ngs2_lock);

        uint8_t state[0x30] = {0};
        REQUIRE(ngs2_voice_state(f.voice_a, state, sizeof(state)) == 0);
        uint32_t flags = *(uint32_t *)state;

        pthread_mutex_lock(&ngs2_lock);
        va->atrac9->decode_inflight = 0;
        pthread_mutex_unlock(&ngs2_lock);

        if (assert_positive_contract) {
            REQUIRE((flags & SCE_NGS2_STATE_ACTIVE) != 0);
        } else {
            printf("BASELINE OBSERVATION: inflight_decode_labeled_injection returns flags=0x%08x\n", flags);
        }
    }
    else if (!strcmp(name, "pause_active_state")) {
        /* Positive Contract: Action 4 (PAUSE) sets ACTIVE=1, PLAYING=0.
         * Requirement for SCE_NGS2_STATE_PAUSED=0x04 was removed without primary evidence.
         * Baseline returns 0 -> FALHA RED esperada no baseline. */
        set_voice_state(f.voice_a, 0); /* PLAY */
        set_voice_state(f.voice_a, 4); /* PAUSE (SoundX Action 4) */

        uint8_t state[0x30] = {0};
        REQUIRE(ngs2_voice_state(f.voice_a, state, sizeof(state)) == 0);
        uint32_t flags = *(uint32_t *)state;

        if (assert_positive_contract) {
            REQUIRE((flags & SCE_NGS2_STATE_ACTIVE) != 0);
            REQUIRE((flags & SCE_NGS2_STATE_PLAYING) == 0);
        } else {
            printf("BASELINE OBSERVATION: pause_active_state returns flags=0x%08x\n", flags);
        }
    }
    else if (!strcmp(name, "drain_transition_and_completion")) {
        /* Full Terminal Drain Test with HEVAG explicit codec:
         * 1. Voice in PLAY has 2 grains.
         * 2. Sets hevag.terminal_seen = 1 (HEVAG EOF explicitly labeled).
         * 3. Query 1 (during drain, 2 grains in FIFO): must be ACTIVE (bit 0=1) and NOT FINISHED (bit 5=0).
         * 4. Render grain 1 (1 grain left): must remain ACTIVE (bit 0=1).
         * 5. Render grain 2 (0 grains left, queue drained): must transition to TERMINATED (bit 0=0, bit 5=1).
         * 6. Query 2 (repeat immediately): must remain TERMINATED (idempotent).
         */
        /* Configure Voice A as HEVAG specifically for terminal_seen testing */
        unsigned char cmd[0x28];
        command(cmd, sizeof(cmd), 0x10000000u);
        put_u32(cmd, 8, 0x1cu); /* HEVAG format */
        put_u32(cmd, 12, 1u);   /* 1 channel */
        put_u32(cmd, 16, 48000);
        put_u32(cmd, 20, 0u);
        REQUIRE(ngs2_voice_control(f.voice_a, cmd) == 0);

        set_voice_state(f.voice_a, 0); /* PLAY */

        pthread_mutex_lock(&ngs2_lock);
        Ngs2Voice *va = ngs2_voice_locked(f.voice_a);
        push_synthetic_grain(va, 1000);
        push_synthetic_grain(va, 1000);
        va->atrac9->hevag.terminal_seen = 1; /* Explicit HEVAG EOF marker */
        pthread_mutex_unlock(&ngs2_lock);

        /* Query 1: During drain (2 grains in FIFO) */
        uint8_t state1[0x30] = {0};
        REQUIRE(ngs2_voice_state(f.voice_a, state1, sizeof(state1)) == 0);
        uint32_t flags1 = *(uint32_t *)state1;

        /* Render 1 grain */
        float out[256 * 8], aux[256 * 8];
        Ngs2RenderBuffer buffers[2] = {{out, sizeof(out), 0x18, 8}, {aux, sizeof(aux), 0x18, 8}};
        assert(!test_render_retry(f.sys, buffers, 2));

        /* Query 2: During drain (1 grain in FIFO) */
        uint8_t state2[0x30] = {0};
        REQUIRE(ngs2_voice_state(f.voice_a, state2, sizeof(state2)) == 0);
        uint32_t flags2 = *(uint32_t *)state2;

        /* Render 2nd grain (draining FIFO to 0) */
        assert(!test_render_retry(f.sys, buffers, 2));

        /* Query 3: Post-drain completion */
        uint8_t state3[0x30] = {0};
        REQUIRE(ngs2_voice_state(f.voice_a, state3, sizeof(state3)) == 0);
        uint32_t flags3 = *(uint32_t *)state3;

        /* Query 4: Repeated post-drain query (idempotence) */
        uint8_t state4[0x30] = {0};
        REQUIRE(ngs2_voice_state(f.voice_a, state4, sizeof(state4)) == 0);

        if (assert_positive_contract) {
            /* Drain phase: must remain ACTIVE and NOT fire FINISHED prematurely */
            REQUIRE((flags1 & SCE_NGS2_STATE_ACTIVE) != 0);
            REQUIRE((flags1 & SCE_NGS2_STATE_FINISHED) == 0);
            /* Note: Baseline halts at first failure above (flags1).
             * Subsequent assertions (flags2, flags3, idempotence) are UNTESTED / BLOCKED
             * and not approved as passing. */
            REQUIRE((flags2 & SCE_NGS2_STATE_ACTIVE) != 0);
            REQUIRE((flags2 & SCE_NGS2_STATE_FINISHED) == 0);
            /* Completion phase: transitions to FINISHED, ACTIVE drops to 0 */
            REQUIRE((flags3 & SCE_NGS2_STATE_ACTIVE) == 0);
            REQUIRE((flags3 & SCE_NGS2_STATE_FINISHED) != 0);
            /* Idempotence */
            REQUIRE(!memcmp(state3, state4, sizeof(state3)));
        } else {
            printf("BASELINE OBSERVATION: drain_transition flags1=0x%08x flags2=0x%08x flags3=0x%08x\n",
                   flags1, flags2, flags3);
        }
    }
    else if (!strcmp(name, "position_progress_and_pause_freeze")) {
        /* Position Contract:
         * 1. Query initial position (offset 0x08).
         * 2. Render 1 grain (256 samples). Position must advance by 256 samples.
         * 3. Pause (Action 4). Query twice: position must freeze (remain constant).
         * 4. Isolation: Voice B position must remain 0 while Voice A advances.
         */
        set_voice_state(f.voice_a, 0); /* PLAY A */
        set_voice_state(f.voice_b, 0); /* PLAY B */

        pthread_mutex_lock(&ngs2_lock);
        Ngs2Voice *va = ngs2_voice_locked(f.voice_a);
        push_synthetic_grain(va, 1000);
        push_synthetic_grain(va, 1000);
        pthread_mutex_unlock(&ngs2_lock);

        uint8_t st_a0[0x30] = {0}, st_b0[0x30] = {0};
        REQUIRE(ngs2_voice_state(f.voice_a, st_a0, sizeof(st_a0)) == 0);
        REQUIRE(ngs2_voice_state(f.voice_b, st_b0, sizeof(st_b0)) == 0);
        uint32_t pos_a0 = *(uint32_t *)(st_a0 + 8);
        uint32_t pos_b0 = *(uint32_t *)(st_b0 + 8);

        /* Render 1 grain for Voice A */
        float out[256 * 8], aux[256 * 8];
        Ngs2RenderBuffer buffers[2] = {{out, sizeof(out), 0x18, 8}, {aux, sizeof(aux), 0x18, 8}};
        assert(!test_render_retry(f.sys, buffers, 2));

        uint8_t st_a1[0x30] = {0}, st_b1[0x30] = {0};
        REQUIRE(ngs2_voice_state(f.voice_a, st_a1, sizeof(st_a1)) == 0);
        REQUIRE(ngs2_voice_state(f.voice_b, st_b1, sizeof(st_b1)) == 0);
        uint32_t pos_a1 = *(uint32_t *)(st_a1 + 8);
        uint32_t pos_b1 = *(uint32_t *)(st_b1 + 8);

        /* Pause Voice A */
        set_voice_state(f.voice_a, 4);

        uint8_t st_a_paused1[0x30] = {0}, st_a_paused2[0x30] = {0};
        REQUIRE(ngs2_voice_state(f.voice_a, st_a_paused1, sizeof(st_a_paused1)) == 0);
        REQUIRE(ngs2_voice_state(f.voice_a, st_a_paused2, sizeof(st_a_paused2)) == 0);
        uint32_t pos_a_p1 = *(uint32_t *)(st_a_paused1 + 8);
        uint32_t pos_a_p2 = *(uint32_t *)(st_a_paused2 + 8);

        if (assert_positive_contract) {
            /* Position Progress (BLOCKED):
             * Querying twice without render during pause does NOT verify freeze under engine progress.
             * Unit (samples vs frames), width (32 vs 64-bit), and monotonicity under loop/reconfig
             * remain UNPROVEN / BLOCKED. */
            REQUIRE(pos_a1 > pos_a0);
            /* Voice B was idle: its position must remain unchanged */
            REQUIRE(pos_b1 == pos_b0);
            /* In pause: position must be frozen */
            REQUIRE(pos_a_p1 == pos_a1);
            REQUIRE(pos_a_p2 == pos_a_p1);
        } else {
            printf("BASELINE OBSERVATION: position pos_a0=%u pos_a1=%u pos_paused=%u\n",
                   pos_a0, pos_a1, pos_a_p1);
        }
    }
    else {
        REQUIRE(!"unknown case");
    }

    close_fixture(&f);
    printf("PASS_LIFECYCLE_CASE %s\n", name);
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) return 2;
    setvbuf(stdout, NULL, _IONBF, 0);
    ngs2_atrac9_setting = 1;
    ngs2_render_mix_setting = 1;
    ngs2_b2f2a_setting = 0;
    ngs2_b2f2c_setting = 0;
    atomic_store(&ngs2_audio_diag_setting, 0);
    atomic_store(&ngs2_route_diag_setting, 0);

    int assert_contract = (argc > 2 && !strcmp(argv[2], "--assert-positive-contract")) ? 1 : 0;
    return run_lifecycle_case(argv[1], assert_contract);
}
