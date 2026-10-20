/* SPDX-License-Identifier: GPL-2.0-or-later
 * HLE-2.2D-B2-DIAG: byte-budget and overflow telemetry stays passive.
 */
#define main ngs2_overflow_existing_tests_main
#include "test_ngs2_overflow.c"
#undef main

static void b2_rejected_segment(Ngs2Handle *handle,uint64_t voice_held,
                                uint64_t global_held) {
    unsigned char bytes[192]={0};
    unsigned char desc[0x20]={0},cmd[0x20];
    make_cmd(cmd,sizeof(cmd),0x10000001u);
    putptr(cmd,8,(uintptr_t)bytes); putptr(cmd,0x18,(uintptr_t)desc);
    put32(cmd,0x10,1);put32(cmd,0x14,1);put32(desc,4,sizeof(bytes));
    pthread_mutex_lock(&ngs2_lock);
    Ngs2Voice *voice=ngs2_voice_locked(handle);
    assert(voice && voice->atrac9 && voice->atrac9->segment_count==0);
    voice->atrac9->queued_compressed_bytes=voice_held;
    ngs2_compressed_bytes=global_held;
    uint64_t gen=voice->atrac9->generation;
    pthread_mutex_unlock(&ngs2_lock);
    assert(!ngs2_voice_control(handle,cmd));
    pthread_mutex_lock(&ngs2_lock);
    assert(voice->atrac9->generation==gen);
    assert(voice->atrac9->queued_compressed_bytes==voice_held);
    assert(voice->atrac9->segment_count==0 && voice->atrac9->pcm_count==0);
    voice->atrac9->queued_compressed_bytes=0;
    ngs2_compressed_bytes=0;
    pthread_mutex_unlock(&ngs2_lock);
}
static void b2_test_budget_enabled(void) {
    OverflowFixture *f=calloc(1,sizeof(*f)); assert(f);
    fixture_open(f); atomic_store(&ngs2_audio_diag_setting,1);
    const uint64_t near_voice=NGS2_ATRAC9_VOICE_BUDGET-128u;
    const uint64_t near_global=NGS2_ATRAC9_GLOBAL_BUDGET-128u;
    for (int codec=0;codec<2;++codec) {
        Ngs2Handle *h=f->voices[codec];
        b2_rejected_segment(h,near_voice,0);           /* voice-only */
        b2_rejected_segment(h,0,near_global);          /* global-only */
        b2_rejected_segment(h,near_voice,near_global); /* both */
    }
    pthread_mutex_lock(&ngs2_lock);
    Ngs2System *sys=ngs2_system_locked(f->system);
    assert(sys->diag_reject_budget==6);
    assert(sys->diag_b2_voice_only_hevag==1 && sys->diag_b2_voice_only_at9==1);
    assert(sys->diag_b2_global_only_hevag==1 && sys->diag_b2_global_only_at9==1);
    assert(sys->diag_b2_both_hevag==1 && sys->diag_b2_both_at9==1);
    assert(sys->diag_b2_rejected_bytes==6u*192u);
    assert(sys->diag_b2_max_voice_held==near_voice);
    assert(sys->diag_b2_max_global_held==near_global);
    assert(sys->diag_b2_max_request_bytes==192);
    pthread_mutex_unlock(&ngs2_lock);
    fixture_push(f,1); render_once(f);
    pthread_mutex_lock(&ngs2_lock);
    sys=ngs2_system_locked(f->system);
    assert(sys->diag_b2_max_eligible==40);
    assert(sys->diag_b2_eligible_sum==40);
    assert(sys->diag_b2_overflow_33_64==1);
    assert(sys->diag_b2_overflow_65_128==0 && sys->diag_b2_overflow_129_plus==0);
    assert(sys->diag_b2_clipped_samples==0 && sys->diag_b2_nonfinite_samples==0);
    assert(sys->grains_consumed==32 && sys->virtualized_grains==8);
    pthread_mutex_unlock(&ngs2_lock);
    /* Prove that the clip counter observes the existing clamp, not a new gain. */
    fixture_push(f,1);
    pthread_mutex_lock(&ngs2_lock);
    Ngs2Voice *voice=ngs2_voice_locked(f->voices[1]);
    union { float f; uint32_t u; } gain={.f=64.0f};
    voice->descriptors[0]=gain.u;
    pthread_mutex_unlock(&ngs2_lock);
    render_once(f);
    pthread_mutex_lock(&ngs2_lock);
    sys=ngs2_system_locked(f->system);
    assert(sys->diag_b2_clipped_samples>0);
    assert(sys->diag_b2_nonfinite_samples==0);
    pthread_mutex_unlock(&ngs2_lock);
    assert(f->master[0]<=1.0f && f->master[0]>=-1.0f);
    fixture_close(f); free(f);
    puts("PASS: B2-DIAG classifies per-voice/global/both and clipping without gain changes");
}
static void b2_test_disabled(void) {
    OverflowFixture *f=calloc(1,sizeof(*f)); assert(f);fixture_open(f);
    b2_rejected_segment(f->voices[1],NGS2_ATRAC9_VOICE_BUDGET-128u,0);
    fixture_push(f,1); render_once(f);
    pthread_mutex_lock(&ngs2_lock);
    Ngs2System *sys=ngs2_system_locked(f->system);
    assert(sys->diag_reject_budget==0 && sys->diag_b2_voice_only_hevag==0);
    assert(sys->diag_b2_max_eligible==0 && sys->diag_b2_eligible_sum==0);
    assert(sys->grains_consumed==32 && sys->virtualized_grains==8);
    pthread_mutex_unlock(&ngs2_lock);
    fixture_close(f); free(f);
    puts("PASS: B2-DIAG disabled keeps counters zero and render behavior unchanged");
}
int main(void) {
    b2_test_budget_enabled();b2_test_disabled();
    puts("ALL HLE-2.2D-B2 DIAGNOSTIC TESTS PASSED");
    return 0;
}
