/* HLE-2.2D-B0: passive telemetry must never change render/voice behavior.
 * Linux isolated harness: clang -std=gnu11 -D_GNU_SOURCE -Wno-macro-redefined
 *   -include unistd.h -Iport/src -Iport tests/test_ngs2_b0_diag.c -pthread -lm
 */
#define main ngs2_overflow_existing_tests_main
#include "test_ngs2_overflow.c"
#undef main

static void test_b0_enabled(void) {
    OverflowFixture *f=calloc(1,sizeof(*f)); assert(f);
    fixture_open(f);
    atomic_store(&ngs2_audio_diag_setting,1);
    fixture_push(f,2);
    pthread_mutex_lock(&ngs2_lock);
    Ngs2System *sys=ngs2_system_locked(f->system);
    assert(sys);
    for (unsigned i=0;i<40;++i) {
        Ngs2Voice *v=ngs2_voice_locked(f->voices[i]);
        v->diag_onset_pending=1;
        v->diag_onset_sequence=(uint64_t)(1000+i);
    }
    pthread_mutex_unlock(&ngs2_lock);
    /* A zero-matrix first grain must be detected but never amplified. */
    float zeros[32]={0};
    unsigned char gain[0x18];make_cmd(gain,sizeof(gain),1);
    put32(gain,12,32); putptr(gain,16,(uintptr_t)zeros);
    assert(!ngs2_voice_control(f->voices[2],gain));
    render_once(f);
    pthread_mutex_lock(&ngs2_lock);
    sys=ngs2_system_locked(f->system);
    assert(sys->diag_virtual_hevag+sys->diag_virtual_at9==8);
    assert(sys->diag_first_virtual_hevag+sys->diag_first_virtual_at9==8);
    assert(sys->diag_first_zero_at9>=1);
    assert(sys->diag_first_mix_hevag+sys->diag_first_mix_at9>=20);
    assert(sys->grains_consumed==32 && sys->virtualized_grains==8);
    assert(sys->diag_matrix_updates==1);
    pthread_mutex_unlock(&ngs2_lock);
    /* terminal_seen reject must leave the FIFO and state untouched. */
    unsigned char data[16]={0},desc[0x20]={0},cmd[0x20];
    put32(desc,4,16); make_cmd(cmd,sizeof(cmd),0x10000001u);
    putptr(cmd,8,(uintptr_t)data);putptr(cmd,0x18,(uintptr_t)desc);
    pthread_mutex_lock(&ngs2_lock);
    Ngs2Voice *v=ngs2_voice_locked(f->voices[1]);
    v->atrac9->hevag.terminal_seen=1;
    uint32_t count_before=v->atrac9->pcm_count;
    pthread_mutex_unlock(&ngs2_lock);
    assert(!ngs2_voice_control(f->voices[1],cmd));
    uint32_t ext[12];memset(ext,0xa5,sizeof(ext));
    assert(!ngs2_voice_state(f->voices[1],ext,0x30));
    for (unsigned i=0;i<12;++i) assert(ext[i]==0);
    pthread_mutex_lock(&ngs2_lock);
    sys=ngs2_system_locked(f->system);
    assert(sys->diag_reject_terminal==1);
    assert(sys->diag_segment_data==1);
    assert(v->atrac9->pcm_count==count_before);
    pthread_mutex_unlock(&ngs2_lock);
    fixture_close(f); free(f);
    puts("PASS: B0 enabled classifies first quiet, virtual, terminal and retains external state");
}
static void test_b0_disabled(void) {
    OverflowFixture *f=calloc(1,sizeof(*f)); assert(f);
    fixture_open(f); fixture_push(f,1);
    render_once(f);
    pthread_mutex_lock(&ngs2_lock);
    Ngs2System *sys=ngs2_system_locked(f->system);
    assert(sys->diag_first_zero_at9==0 && sys->diag_virtual_hevag==0 &&
           sys->diag_first_virtual_at9==0 && sys->diag_segment_data==0);
    assert(sys->grains_consumed==32 && sys->virtualized_grains==8);
    pthread_mutex_unlock(&ngs2_lock);
    fixture_close(f); free(f);
    puts("PASS: B0 disabled leaves telemetry zero and mixer behavior unchanged");
}
int main(void) {
    test_b0_enabled(); test_b0_disabled();
    puts("ALL HLE-2.2D-B0 DIAGNOSTIC TESTS PASSED");
    return 0;
}
