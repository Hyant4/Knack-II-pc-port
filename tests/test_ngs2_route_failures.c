/* SPDX-License-Identifier: GPL-2.0-or-later
 * Independent synthetic analysis & contract tests for Knack 2 NGS2 HLE.
 *
 * Implements Round 3 Tech Lead Directives (TAREFA_GEMINI_AUDIO_RODADA3.md):
 *  1. Baseline diagnostics vs positive contract gates (explicit RED on baseline).
 *  2. SoundX coupling flow: VoiceGetState zero -> premature Action 3 kill simulation.
 *  3. Positive contract for Cmd5: rack target, voice target, foreign reject, disconnect,
 *     dangling pointer cleanup on rack destroy, self-connection & cycle requirements.
 *  4. Positive contract for Lifecycle: active state during stream/empty FIFO, idempotency.
 *  5. Cmd2 layer separation: source only, bus only, distinct gains.
 *  6. Voice isolation on release: Voice A released/freed, Voice B unaffected.
 */
#define main previous_mixer_tests
#include "test_ngs2_mixer.c"
#undef main

#define FAIL_MSG(fmt, ...) do { fprintf(stderr, "GATE_FAILURE: " fmt "\n", ##__VA_ARGS__); } while (0)
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "CONTRACT ASSERTION FAILED line %d: %s\n", __LINE__, #x); return 1; } } while (0)

/* =========================================================================
 * 1. SoundX Coupling Flow: Static simulation of VoiceGetState zero -> Action 3
 * ========================================================================= */
static int test_soundx_coupling_simulation(int simulate_hard_kill) {
    unsigned char so[0x40], ro[0xa8];
    system_option(so);
    Ngs2Handle *sys = NULL, *rack = NULL, *voice = NULL;

    assert(!ngs2_system_create(so, NULL, &sys));
    rack_option(ro, 4, "Sampler");
    assert(!ngs2_rack_create(sys, 0x1000, ro, NULL, &rack));
    assert(!ngs2_voice_handle(rack, 0, &voice));
    setup_waveform(voice, 4);
    set_voice_state(voice, 0); /* PLAY (Action 0) */

    float matrix[32] = {0}; matrix[0] = 1.0f;
    set_voice_matrix(voice, matrix, 32);

    /* Enqueue 7 grains (as observed in real trace at seq 1201: fifo=7, line 4607) */
    pthread_mutex_lock(&ngs2_lock);
    Ngs2Voice *v = ngs2_voice_locked(voice);
    for (int i = 0; i < 7; ++i) push_synthetic_grain(v, 16384);
    uint32_t fifo_initial = v->atrac9->pcm_count;
    pthread_mutex_unlock(&ngs2_lock);
    CHECK(fifo_initial == 7);

    /* Guest polling thread (0x00b6efed) queries voice state */
    unsigned char state_buf[0x30] = {0};
    int32_t rc_state = ngs2_voice_state(voice, state_buf, sizeof(state_buf));
    CHECK(rc_state == 0);

    /* Actual state prevents the premature guest release. The optional legacy
     * simulation deliberately injects a bad local flag word; it is not the HLE response. */
    CHECK((state_buf[0] & 1) != 0);
    if (simulate_hard_kill) state_buf[0]=0;
    int guest_thinks_finished = ((state_buf[0] & 1) == 0);
    CHECK(guest_thinks_finished == simulate_hard_kill);

    if (simulate_hard_kill) {
        /* Static flow simulation: If Action 3 were treated as hard STOPPED/clear in isolation: */
        pthread_mutex_lock(&ngs2_lock);
        v->internal_state = NGS2_VOICE_STOPPED;
        ngs2_atrac9_clear_pending_locked(v, 1);
        uint32_t fifo_after_kill = v->atrac9->pcm_count;
        pthread_mutex_unlock(&ngs2_lock);

        /* Render */
        float out[256 * 8], aux[256 * 8];
        Ngs2RenderBuffer buffers[2] = {{out, sizeof(out), 0x18, 8}, {aux, sizeof(aux), 0x18, 8}};
        assert(!test_render_retry(sys, buffers, 2));

        /* Proves: All 7 grains discarded; audio is completely MUTED prematurely (out[0] == 0) */
        CHECK(fifo_after_kill == 0);
        CHECK(out[0] == 0.0f);
        printf("STATIC_SIMULATION: Hard kill on Action 3 with VoiceGetState=0 discards %u grains and silences audio!\n", fifo_initial);
    } else {
        /* A legitimate explicit Action 3 must cancel the current generation. */
        unsigned char cmd[0x0c];
        command(cmd, sizeof(cmd), 6);
        put_u32(cmd, 8, 3); /* Action 3 */
        CHECK(!ngs2_voice_control(voice, cmd));

        pthread_mutex_lock(&ngs2_lock);
        v = ngs2_voice_locked(voice);
        CHECK(v->internal_state == NGS2_VOICE_STOPPED);
        CHECK(v->atrac9->pcm_count == 0);
        pthread_mutex_unlock(&ngs2_lock);
    }

    assert(!ngs2_system_destroy(sys, NULL));
    return 0;
}

/* =========================================================================
 * 2. Cmd 5 Baseline Diagnostic Observations
 * ========================================================================= */
static int test_cmd5_baseline_observations(void) {
    unsigned char so[0x40], ro[0xa8];
    system_option(so);
    Ngs2Handle *sys = NULL, *sys_foreign = NULL;
    Ngs2Handle *sampler_rack = NULL, *premaster_rack = NULL, *voice = NULL, *bus_voice = NULL;

    assert(!ngs2_system_create(so, NULL, &sys));
    assert(!ngs2_system_create(so, NULL, &sys_foreign));

    rack_option(ro, 4, "Sampler");
    assert(!ngs2_rack_create(sys, 0x1000, ro, NULL, &sampler_rack));
    assert(!ngs2_voice_handle(sampler_rack, 0, &voice));

    rack_option(ro, 1, "PreMaster");
    assert(!ngs2_rack_create(sys, 0x2000, ro, NULL, &premaster_rack));
    assert(!ngs2_voice_handle(premaster_rack, 0, &bus_voice));

    /* Target is RACK handle (PreMaster rack from guest 0x00b2c9c7) */
    unsigned char cmd[0x18];
    command(cmd, sizeof(cmd), 5);
    put_uptr(cmd, 16, (uintptr_t)premaster_rack);
    int32_t rc_rack = ngs2_voice_control(voice, cmd);
    printf("BASELINE OBSERVATION: Cmd5 with target=RackHandle returns %d (expected 0 in positive contract)\n", rc_rack);

    assert(!ngs2_system_destroy(sys, NULL));
    assert(!ngs2_system_destroy(sys_foreign, NULL));
    return 0;
}

/* =========================================================================
 * 3. Entrega 1: Positive Contract Gate for Cmd 5 (Strict Assertions)
 * ========================================================================= */
static int test_cmd5_positive_contract(void) {
    unsigned char so[0x40], ro[0xa8];
    system_option(so);
    Ngs2Handle *sys = NULL, *sys_foreign = NULL;
    Ngs2Handle *sampler_rack = NULL, *premaster_rack = NULL, *voice = NULL, *bus_voice = NULL;

    assert(!ngs2_system_create(so, NULL, &sys));
    assert(!ngs2_system_create(so, NULL, &sys_foreign));

    rack_option(ro, 4, "Sampler");
    assert(!ngs2_rack_create(sys, 0x1000, ro, NULL, &sampler_rack));
    assert(!ngs2_voice_handle(sampler_rack, 0, &voice));

    rack_option(ro, 1, "PreMaster");
    assert(!ngs2_rack_create(sys, 0x2000, ro, NULL, &premaster_rack));
    assert(!ngs2_voice_handle(premaster_rack, 0, &bus_voice));

    unsigned char cmd[0x18];
    command(cmd, sizeof(cmd), 5);
    put_u32(cmd, 8, 0);   /* input_channel = 0 */
    put_u32(cmd, 12, 0);  /* output_channel = 0 */

    /* 1. Target Rack Handle of same system accepted and stored without inventing voice 0 */
    put_uptr(cmd, 16, (uintptr_t)premaster_rack);
    int32_t rc_rack = ngs2_voice_control(voice, cmd);
    if (rc_rack != 0) {
        FAIL_MSG("Item 1 FAILED: Target rack handle rejected with error code %d (expected 0)", rc_rack);
        return 1;
    }
    pthread_mutex_lock(&ngs2_lock);
    Ngs2Voice *v = ngs2_voice_locked(voice);
    if (v->connection != premaster_rack || v->connection_rack == NULL) {
        pthread_mutex_unlock(&ngs2_lock);
        FAIL_MSG("Item 1 FAILED: Target rack handle not stored correctly in voice->connection");
        return 1;
    }
    pthread_mutex_unlock(&ngs2_lock);

    /* 2. Target Voice Handle of same system accepted and stored */
    put_uptr(cmd, 16, (uintptr_t)bus_voice);
    CHECK(ngs2_voice_control(voice, cmd) == 0);
    pthread_mutex_lock(&ngs2_lock);
    CHECK(v->connection == bus_voice);
    CHECK(v->connection_rack != NULL);
    pthread_mutex_unlock(&ngs2_lock);

    /* 3. Foreign System Targets (both Voice and Rack) must be rejected; state preserved */
    Ngs2Handle *foreign_rack = NULL, *foreign_voice = NULL;
    rack_option(ro, 1, "Foreign");
    assert(!ngs2_rack_create(sys_foreign, 0x2000, ro, NULL, &foreign_rack));
    assert(!ngs2_voice_handle(foreign_rack, 0, &foreign_voice));

    /* Foreign Voice */
    put_uptr(cmd, 16, (uintptr_t)foreign_voice);
    int32_t rc_fv = ngs2_voice_control(voice, cmd);
    if (rc_fv == 0) {
        FAIL_MSG("Item 3 FAILED: Foreign voice handle was accepted (expected NGS2_INVALID)");
        return 1;
    }
    pthread_mutex_lock(&ngs2_lock);
    CHECK(v->connection == bus_voice); /* Previous connection preserved */
    pthread_mutex_unlock(&ngs2_lock);

    /* Foreign Rack */
    put_uptr(cmd, 16, (uintptr_t)foreign_rack);
    int32_t rc_fr = ngs2_voice_control(voice, cmd);
    if (rc_fr == 0) {
        FAIL_MSG("Item 3 FAILED: Foreign rack handle was accepted (expected NGS2_INVALID)");
        return 1;
    }
    pthread_mutex_lock(&ngs2_lock);
    CHECK(v->connection == bus_voice); /* Previous connection preserved */
    pthread_mutex_unlock(&ngs2_lock);

    /* 4. Invalid Target rejected; previous connection preserved */
    put_uptr(cmd, 16, (uintptr_t)0xdeadbeefULL);
    CHECK(ngs2_voice_control(voice, cmd) == NGS2_INVALID);
    pthread_mutex_lock(&ngs2_lock);
    CHECK(v->connection == bus_voice);
    pthread_mutex_unlock(&ngs2_lock);

    /* 5. Disconnection with target == NULL zeroes handles and channels */
    put_uptr(cmd, 16, 0);
    CHECK(ngs2_voice_control(voice, cmd) == 0);
    pthread_mutex_lock(&ngs2_lock);
    CHECK(v->connection == NULL);
    CHECK(v->connection_rack == NULL);
    CHECK(v->connection_input == 0);
    CHECK(v->connection_output == 0);
    pthread_mutex_unlock(&ngs2_lock);

    /* 6. Self-Connection rejection & Cycle A->B->A rejection */
    /* 6a. Self-connection (A -> A) */
    put_uptr(cmd, 16, (uintptr_t)voice);
    int32_t rc_self = ngs2_voice_control(voice, cmd);
    if (rc_self == 0) {
        FAIL_MSG("Item 6a FAILED: Self-connection (A->A) was accepted (expected NGS2_INVALID)");
        return 1;
    }

    /* 6b. Cycle A -> B -> A:
     * Connect Voice A -> Voice B. Attempting to connect Voice B -> Voice A forms a 2-node cycle.
     * Simple self-check (target == voice) does NOT detect cycles of length >= 2.
     * Topological traversal or DAG depth bounds are required to avoid infinite render loops. */
    put_uptr(cmd, 16, (uintptr_t)bus_voice);
    CHECK(ngs2_voice_control(voice, cmd) == 0); /* A -> B */

    put_uptr(cmd, 16, (uintptr_t)voice);
    int32_t rc_cycle = ngs2_voice_control(bus_voice, cmd); /* Attempt B -> A */
    if (rc_cycle == 0) {
        FAIL_MSG("Item 6b FAILED: Direct cycle A->B->A was accepted (expected cycle rejection with NGS2_INVALID)");
        return 1;
    }

    /* 7. Target Rack Destruction cleans references in surviving sources */
    /* Reconnect to premaster_rack */
    put_uptr(cmd, 16, (uintptr_t)premaster_rack);
    assert(!ngs2_voice_control(voice, cmd));
    /* Destroy premaster_rack */
    assert(!ngs2_rack_destroy(premaster_rack, NULL));
    premaster_rack = NULL;
    pthread_mutex_lock(&ngs2_lock);
    if (v->connection != NULL || v->connection_rack != NULL) {
        pthread_mutex_unlock(&ngs2_lock);
        FAIL_MSG("Item 7 FAILED: Destroyed rack handle was not cleared in surviving source voice (dangling pointer risk)");
        return 1;
    }
    pthread_mutex_unlock(&ngs2_lock);

    assert(!ngs2_system_destroy(sys, NULL));
    assert(!ngs2_system_destroy(sys_foreign, NULL));
    puts("PASS_CMD5_POSITIVE_CONTRACT");
    return 0;
}

/* =========================================================================
 * 4. Entrega 2: Positive Contract Gate for Voice Lifecycle (VoiceGetState)
 * ========================================================================= */
static int test_lifecycle_positive_contract(void) {
    unsigned char so[0x40], ro[0xa8];
    system_option(so);
    Ngs2Handle *sys = NULL, *rack = NULL, *voice = NULL;

    assert(!ngs2_system_create(so, NULL, &sys));
    rack_option(ro, 4, "Sampler");
    assert(!ngs2_rack_create(sys, 0x1000, ro, NULL, &rack));
    assert(!ngs2_voice_handle(rack, 0, &voice));
    setup_waveform(voice, 4);

    /* Voice is PLAYING, but has temporary empty FIFO (underrun/onset phase before decode completes) */
    set_voice_state(voice, 0); /* PLAY (Action 0) */

    pthread_mutex_lock(&ngs2_lock);
    Ngs2Voice *v = ngs2_voice_locked(voice);
    CHECK(v->internal_state == NGS2_VOICE_PLAYING);
    CHECK(v->atrac9 != NULL);
    CHECK(v->atrac9->pcm_count == 0); /* Temporary underrun / no PCM yet */
    pthread_mutex_unlock(&ngs2_lock);

    /* Positive Contract Requirement:
     * VoiceGetState must report bit 0 = 1 (ACTIVE/PLAYING) while voice is playing,
     * even during temporary underrun/empty FIFO.
     * It must NOT return all zeros, which tricks SoundX into declaring VOICE_FINISHED! */
    unsigned char state_buf[0x30] = {0};
    CHECK(!ngs2_voice_state(voice, state_buf, sizeof(state_buf)));

    if ((state_buf[0] & 1) == 0) {
        FAIL_MSG("LIFECYCLE FAILED: VoiceGetState returned bit 0 = 0 for an active voice with empty FIFO. "
                 "This causes SoundX to mark VOICE_FINISHED and send premature Action 3 kill.");
        assert(!ngs2_system_destroy(sys, NULL));
        return 1;
    }

    /* Idempotency check: repeated queries do not mutate state */
    unsigned char state_buf2[0x30] = {0};
    CHECK(!ngs2_voice_state(voice, state_buf2, sizeof(state_buf2)));
    CHECK(!memcmp(state_buf, state_buf2, sizeof(state_buf)));

    assert(!ngs2_system_destroy(sys, NULL));
    puts("PASS_LIFECYCLE_POSITIVE_CONTRACT");
    return 0;
}

/* =========================================================================
 * 5. Gain Layer Separation: Source Only vs Bus Only vs Distinct Gains
 * ========================================================================= */
static int test_cmd2_layer_separation(void) {
    unsigned char so[0x40], ro[0xa8];
    system_option(so);
    Ngs2Handle *sys = NULL, *sampler_rack = NULL, *bus_rack = NULL;
    Ngs2Handle *voice = NULL, *bus = NULL;

    assert(!ngs2_system_create(so, NULL, &sys));
    rack_option(ro, 4, "Sampler");
    assert(!ngs2_rack_create(sys, 0x1000, ro, NULL, &sampler_rack));
    assert(!ngs2_voice_handle(sampler_rack, 0, &voice));
    setup_waveform(voice, 4);
    set_voice_state(voice, 0);

    float matrix[32] = {0}; matrix[0] = 1.0f;
    set_voice_matrix(voice, matrix, 32);

    rack_option(ro, 1, "Bus");
    assert(!ngs2_rack_create(sys, 0x2000, ro, NULL, &bus_rack));
    assert(!ngs2_voice_handle(bus_rack, 0, &bus));
    set_voice_state(bus, 0);

    /* Connect voice -> bus */
    unsigned char conn[0x18];
    command(conn, sizeof(conn), 5);
    put_uptr(conn, 16, (uintptr_t)bus);
    assert(!ngs2_voice_control(voice, conn));

    float out[256 * 8], aux[256 * 8];
    Ngs2RenderBuffer buffers[2] = {{out, sizeof(out), 0x18, 8}, {aux, sizeof(aux), 0x18, 8}};

    /* Layer 1: Source-only gain attenuation (Cmd 2 ch 0 = 0.5f, Bus unmodified) */
    unsigned char gcmd[0x10];
    command(gcmd, sizeof(gcmd), 2);
    put_u32(gcmd, 8, 0); /* Channel 0 */
    float g_src = 0.5f;
    memcpy(gcmd + 0x0c, &g_src, 4);
    assert(!ngs2_voice_control(voice, gcmd));

    pthread_mutex_lock(&ngs2_lock);
    Ngs2Voice *v = ngs2_voice_locked(voice);
    push_synthetic_grain(v, 16384);
    pthread_mutex_unlock(&ngs2_lock);

    assert(!test_render_retry(sys, buffers, 2));
    printf("LAYER 1 (Source-only 0.5f): master[0]=%.6f (Current runtime publishes %.6f; expected %.6f if applied)\n",
           out[0], 0.125f, 0.125f * g_src);

    /* Layer 2: Bus-only gain attenuation (Bus ch 0 = 0.25f, Source reset to 1.0f) */
    float g_one = 1.0f, g_bus = 0.25f;
    memcpy(gcmd + 0x0c, &g_one, 4);
    assert(!ngs2_voice_control(voice, gcmd));
    memcpy(gcmd + 0x0c, &g_bus, 4);
    assert(!ngs2_voice_control(bus, gcmd));

    pthread_mutex_lock(&ngs2_lock);
    push_synthetic_grain(v, 16384);
    pthread_mutex_unlock(&ngs2_lock);

    assert(!test_render_retry(sys, buffers, 2));
    printf("LAYER 2 (Bus-only 0.25f): master[0]=%.6f (Current runtime publishes %.6f; expected %.6f if applied)\n",
           out[0], 0.125f, 0.125f * g_bus);

    /* Layer 3: Two distinct gains (Source ch 0 = 0.5f, Bus ch 0 = 0.25f) */
    memcpy(gcmd + 0x0c, &g_src, 4);
    assert(!ngs2_voice_control(voice, gcmd));

    pthread_mutex_lock(&ngs2_lock);
    push_synthetic_grain(v, 16384);
    pthread_mutex_unlock(&ngs2_lock);

    assert(!test_render_retry(sys, buffers, 2));
    printf("LAYER 3 (Distinct: Src=0.5f, Bus=0.25f): master[0]=%.6f (Cascade=%.6f, BusOnly=%.6f)\n",
           out[0], 0.125f * g_src * g_bus, 0.125f * g_bus);

    assert(!ngs2_system_destroy(sys, NULL));
    return 0;
}

/* =========================================================================
 * 6. Voice Isolation Contract: Freeing Voice A leaves Voice B untouched
 * ========================================================================= */
static int test_voice_isolation_contract(void) {
    unsigned char so[0x40], ro[0xa8];
    system_option(so);
    Ngs2Handle *sys = NULL, *rack = NULL;
    Ngs2Handle *voice_a = NULL, *voice_b = NULL;

    assert(!ngs2_system_create(so, NULL, &sys));
    rack_option(ro, 8, "Sampler");
    assert(!ngs2_rack_create(sys, 0x1000, ro, NULL, &rack));
    assert(!ngs2_voice_handle(rack, 0, &voice_a));
    assert(!ngs2_voice_handle(rack, 1, &voice_b));

    setup_waveform(voice_a, 4); setup_waveform(voice_b, 4);
    set_voice_state(voice_a, 0); set_voice_state(voice_b, 0);

    float matrix[32] = {0}; matrix[0] = 1.0f;
    set_voice_matrix(voice_a, matrix, 32);
    set_voice_matrix(voice_b, matrix, 32);

    pthread_mutex_lock(&ngs2_lock);
    Ngs2Voice *va = ngs2_voice_locked(voice_a);
    Ngs2Voice *vb = ngs2_voice_locked(voice_b);
    push_synthetic_grain(va, 16384);
    push_synthetic_grain(vb, 8192);
    uint64_t gen_b_before = vb->atrac9->generation;
    uint32_t fifo_b_before = vb->atrac9->pcm_count;
    pthread_mutex_unlock(&ngs2_lock);

    CHECK(va->internal_state == NGS2_VOICE_PLAYING);
    CHECK(vb->internal_state == NGS2_VOICE_PLAYING);
    CHECK(fifo_b_before == 1);

    /* Free Voice A with Action 1 (RELEASE) */
    set_voice_state(voice_a, 1);

    pthread_mutex_lock(&ngs2_lock);
    CHECK(va->internal_state == NGS2_VOICE_RELEASING);
    CHECK(va->atrac9->pcm_count == 0);

    /* Voice B must remain PLAYING, with its generation and FIFO intact */
    CHECK(vb->internal_state == NGS2_VOICE_PLAYING);
    CHECK(vb->atrac9->generation == gen_b_before);
    CHECK(vb->atrac9->pcm_count == fifo_b_before);
    pthread_mutex_unlock(&ngs2_lock);

    /* Render should play Voice B's grain */
    float out[256 * 8], aux[256 * 8];
    Ngs2RenderBuffer buffers[2] = {{out, sizeof(out), 0x18, 8}, {aux, sizeof(aux), 0x18, 8}};
    assert(!test_render_retry(sys, buffers, 2));

    pthread_mutex_lock(&ngs2_lock);
    CHECK(fabsf(out[0] - 0.0625f) < 1e-6f); /* 8192 / 32768 * 0.25 = 0.0625f */
    CHECK(vb->atrac9->pcm_count == 0);
    pthread_mutex_unlock(&ngs2_lock);

    puts("PASS: Voice isolation verified on Voice A release (Voice B playback preserved).");
    assert(!ngs2_system_destroy(sys, NULL));
    return 0;
}

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    ngs2_atrac9_setting = 1;
    ngs2_render_mix_setting = 1;
    ngs2_b2f2a_setting = 0;
    ngs2_b2f2c_setting = 0;
    atomic_store(&ngs2_audio_diag_setting, 0);

    if (argc > 1 && !strcmp(argv[1], "--assert-cmd5-contract")) {
        printf("=== POSITIVE CONTRACT GATE: CMD 5 ===\n");
        return test_cmd5_positive_contract();
    }
    if (argc > 1 && !strcmp(argv[1], "--assert-lifecycle-contract")) {
        printf("=== POSITIVE CONTRACT GATE: LIFECYCLE ===\n");
        return test_lifecycle_positive_contract();
    }

    int sim_kill = (argc > 1 && !strcmp(argv[1], "--simulate-kill")) ? 1 : 0;
    printf("=== NGS2 BASELINE OBSERVATION & ISOLATION SUITE ===\n");

    CHECK(test_soundx_coupling_simulation(sim_kill) == 0);
    CHECK(test_cmd5_baseline_observations() == 0);
    CHECK(test_cmd2_layer_separation() == 0);
    CHECK(test_voice_isolation_contract() == 0);

    puts("PASS ALL REVISION 3 BASELINE EVALUATIONS COMPLETED.");
    return 0;
}
