/* SPDX-License-Identifier: GPL-2.0-or-later
 * HLE-2.2D-B1: scoped HEVAG one-shot reuse; no ATRAC9 or state ABI changes.
 */
#define main ngs2_overflow_existing_tests_main
#include "test_ngs2_overflow.c"
#undef main

static void b1_wave(Ngs2Handle *h,int hevag) {
    unsigned char cmd[0x28]; make_cmd(cmd,sizeof(cmd),0x10000000u);
    put32(cmd,8,hevag ? 0x1cu : 0x40u);
    put32(cmd,12,hevag ? 1u : 4u);
    put32(cmd,16,48000);
    put32(cmd,20,hevag ? 0u : 0xfe7a17f0u);
    assert(!ngs2_voice_control(h,cmd));
}
static void b1_end(Ngs2Handle *h) {
    unsigned char cmd[0x20]; make_cmd(cmd,sizeof(cmd),0x10000001u);
    assert(!ngs2_voice_control(h,cmd));
}
static void b1_data(Ngs2Handle *h,const unsigned char *data,uint32_t bytes) {
    unsigned char cmd[0x20],desc[0x20]={0};
    make_cmd(cmd,sizeof(cmd),0x10000001u);
    putptr(cmd,8,(uintptr_t)data); putptr(cmd,0x18,(uintptr_t)desc);
    put32(cmd,0x10,1); put32(cmd,0x14,1); put32(desc,4,bytes);
    assert(!ngs2_voice_control(h,cmd));
}
static void b1_pause(Ngs2Handle *h) {
    unsigned char cmd[0x0c];make_cmd(cmd,sizeof(cmd),6); put32(cmd,8,1);
    assert(!ngs2_voice_control(h,cmd));
}
static void b1_resume(Ngs2Handle *h) {
    unsigned char cmd[0x0c];make_cmd(cmd,sizeof(cmd),6); put32(cmd,8,2);
    assert(!ngs2_voice_control(h,cmd));
}
static void b1_fill_hevag(unsigned char *data,size_t n) {
    assert(n>=160 && n%16==0);
    for (size_t i=0;i<n;i+=16) {
        memset(data+i,0x77,16);
        data[i]=0; data[i+1]=0;
    }
    data[n-15]=1; /* VAG terminal flag on final block. */
}
static void b1_test_reuse_after_terminal(void) {
    OverflowFixture *f=calloc(1,sizeof(*f)); assert(f);fixture_open(f);
    atomic_store(&ngs2_audio_diag_setting,1);
    Ngs2Handle *h=f->voices[1];
    pthread_mutex_lock(&ngs2_lock);
    Ngs2Voice *v=ngs2_voice_locked(h);
    Ngs2Atrac9Voice *s=v->atrac9; assert(s);
    s->hevag.terminal_seen=1; s->hevag.end_requested=1;
    s->hevag.history[0]=938; uint64_t old_gen=s->generation;
    uint64_t old_cancels=s->cancellations;
    pthread_mutex_unlock(&ngs2_lock);
    b1_wave(h,1);
    pthread_mutex_lock(&ngs2_lock);
    assert(s->generation==old_gen+1);
    assert(!s->hevag.terminal_seen && !s->hevag.end_requested);
    assert(!s->hevag.history[0]);
    assert(s->cancellations==old_cancels);
    pthread_mutex_unlock(&ngs2_lock);
    unsigned char frames[16*12];b1_fill_hevag(frames,sizeof(frames));
    b1_data(h,frames,sizeof(frames)); b1_end(h);
    pthread_mutex_lock(&ngs2_lock);
    Ngs2System *sys=ngs2_system_locked(f->system);
    assert(sys->diag_segment_accepted>=1 && sys->diag_reject_terminal==0);
    pthread_mutex_unlock(&ngs2_lock);
    /* The worker must decode the new generation; the mixer consumes it. */
    int heard=0;
    for (int tries=0;tries<600 && !heard;++tries) {
        struct timespec delay={0,1000000}; nanosleep(&delay,NULL);
        pthread_mutex_lock(&ngs2_lock);
        int ready=s->pcm_count>0;
        pthread_mutex_unlock(&ngs2_lock);
        if (ready) {
            render_once(f);
            for (size_t j=0;j<sizeof(f->master)/sizeof(f->master[0]);++j)
                if (fabsf(f->master[j])>0.00001f) {heard=1;break;}
        }
    }
    assert(heard && "new HEVAG generation must reach the master output");
    fixture_close(f);free(f);
    puts("PASS: same-waveform HEVAG after terminal decodes/mixes without cancellation");
}
static void b1_test_identical_open(void) {
    OverflowFixture *f=calloc(1,sizeof(*f)); assert(f);fixture_open(f);
    pthread_mutex_lock(&ngs2_lock);
    Ngs2Voice *v=ngs2_voice_locked(f->voices[1]);
    Ngs2Atrac9Voice *s=v->atrac9;
    s->hevag.history[0]=789;
    add_grain(v,8192);
    uint64_t gen=s->generation;
    uint64_t cancellations=s->cancellations;
    pthread_mutex_unlock(&ngs2_lock);
    b1_wave(f->voices[1],1);
    pthread_mutex_lock(&ngs2_lock);
    assert(s->generation==gen && s->cancellations==cancellations);
    assert(s->pcm_count==1 && s->hevag.history[0]==789);
    pthread_mutex_unlock(&ngs2_lock);
    /* Old ATRAC9 behavior is preserved even for an idle identical configuration. */
    pthread_mutex_lock(&ngs2_lock);
    v=ngs2_voice_locked(f->voices[0]); s=v->atrac9;
    gen=s->generation;
    pthread_mutex_unlock(&ngs2_lock);
    b1_wave(f->voices[0],0);
    pthread_mutex_lock(&ngs2_lock);
    assert(s->generation==gen);
    pthread_mutex_unlock(&ngs2_lock);
    fixture_close(f);free(f);
    puts("PASS: identical open HEVAG and unchanged ATRAC9 generation");
}
static void b1_test_retrigger_during_pending(void) {
    OverflowFixture *f=calloc(1,sizeof(*f)); assert(f);fixture_open(f);
    atomic_store(&ngs2_audio_diag_setting,1);
    Ngs2Handle *h=f->voices[3];
    b1_pause(h);
    unsigned char frames[16*12];b1_fill_hevag(frames,sizeof(frames));
    b1_data(h,frames,sizeof(frames));b1_end(h);
    pthread_mutex_lock(&ngs2_lock);
    Ngs2Voice *v=ngs2_voice_locked(h);Ngs2Atrac9Voice *s=v->atrac9;
    uint64_t gen=s->generation,cancels=s->cancellations;
    assert(s->segment_count==1 && s->hevag.end_requested);
    pthread_mutex_unlock(&ngs2_lock);
    b1_wave(h,1);
    pthread_mutex_lock(&ngs2_lock);
    assert(s->generation==gen+1 && s->cancellations==cancels+1);
    assert(s->segment_count==0 && s->queued_compressed_bytes==0);
    assert(!s->hevag.terminal_seen && !s->hevag.end_requested);
    pthread_mutex_unlock(&ngs2_lock);
    b1_data(h,frames,sizeof(frames));b1_end(h);b1_resume(h);
    pthread_mutex_lock(&ngs2_lock);
    Ngs2System *sys=ngs2_system_locked(f->system);
    assert(sys->diag_reject_terminal==0 && sys->diag_segment_accepted==2);
    pthread_mutex_unlock(&ngs2_lock);
    fixture_close(f);free(f);
    puts("PASS: re-trigger after end_requested cancels old generation and accepts new data");
}
static void b1_test_tail_reservation(void) {
    OverflowFixture *f=calloc(1,sizeof(*f)); assert(f);fixture_open(f);
    pthread_mutex_lock(&ngs2_lock);
    Ngs2Voice *v=ngs2_voice_locked(f->voices[5]);
    Ngs2Atrac9Voice *s=v->atrac9;
    s->hevag.end_requested=1;
    s->segment_reservations=1;
    ngs2_hevag_flush_tail_locked(v->rack->system,s);
    assert(!s->hevag.terminal_seen);
    s->segment_reservations=0;
    ngs2_hevag_flush_tail_locked(v->rack->system,s);
    assert(s->hevag.terminal_seen);
    pthread_mutex_unlock(&ngs2_lock);
    fixture_close(f);free(f);
    puts("PASS: a reserved segment prevents premature HEVAG EOF");
}

/* Hold the HEVAG decoder outside ngs2_lock to exercise a real in-flight
 * worker crossing a new waveform generation without a use-after-free. */
static pthread_mutex_t b1_gate_lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t b1_gate_cond=PTHREAD_COND_INITIALIZER;
static int b1_gate_entered,b1_gate_release;
static void b1_block_decode(void *unused) {
    (void)unused;
    pthread_mutex_lock(&b1_gate_lock);
    b1_gate_entered=1; pthread_cond_broadcast(&b1_gate_cond);
    while (!b1_gate_release) pthread_cond_wait(&b1_gate_cond,&b1_gate_lock);
    pthread_mutex_unlock(&b1_gate_lock);
}
static void b1_test_worker_generation_race(void) {
    OverflowFixture *f=calloc(1,sizeof(*f)); assert(f);fixture_open(f);
    Ngs2Handle *h=f->voices[7];
    pthread_mutex_lock(&b1_gate_lock);
    b1_gate_entered=0;b1_gate_release=0;
    pthread_mutex_unlock(&b1_gate_lock);
    ngs2_set_hevag_decode_hook(b1_block_decode,NULL);
    unsigned char frames[16*12];b1_fill_hevag(frames,sizeof(frames));
    b1_data(h,frames,sizeof(frames));b1_end(h);
    pthread_mutex_lock(&b1_gate_lock);
    struct timespec deadline;timespec_get(&deadline,TIME_UTC);
    deadline.tv_sec+=5;
    while (!b1_gate_entered) {
        int err=pthread_cond_timedwait(&b1_gate_cond,&b1_gate_lock,&deadline);
        assert(err==0 && "HEVAG worker did not enter decode hook");
    }
    pthread_mutex_unlock(&b1_gate_lock);
    pthread_mutex_lock(&ngs2_lock);
    Ngs2Voice *v=ngs2_voice_locked(h);Ngs2Atrac9Voice *state=v->atrac9;
    assert(state->decode_inflight && state->hevag.end_requested);
    uint64_t generation=state->generation;
    pthread_mutex_unlock(&ngs2_lock);
    b1_wave(h,1);
    b1_data(h,frames,sizeof(frames));b1_end(h);
    pthread_mutex_lock(&ngs2_lock);
    assert(state->generation==generation+1 && !state->hevag.terminal_seen);
    assert(state->segment_count==1 && state->decode_inflight);
    pthread_mutex_unlock(&ngs2_lock);
    pthread_mutex_lock(&b1_gate_lock);
    b1_gate_release=1;pthread_cond_broadcast(&b1_gate_cond);
    pthread_mutex_unlock(&b1_gate_lock);
    int decoded=0;
    for (int tries=0;tries<600 && !decoded;++tries) {
        struct timespec delay={0,1000000};nanosleep(&delay,NULL);
        pthread_mutex_lock(&ngs2_lock);
        decoded=state->hevag.decoded_frames>=1 && state->generation==generation+1;
        pthread_mutex_unlock(&ngs2_lock);
    }
    assert(decoded);
    ngs2_set_hevag_decode_hook(NULL,NULL);
    fixture_close(f);free(f);
    puts("PASS: in-flight old HEVAG worker cannot corrupt new generation");
}

int main(void) {
    b1_test_reuse_after_terminal();
    b1_test_identical_open();
    b1_test_retrigger_during_pending();
    b1_test_tail_reservation();
    b1_test_worker_generation_race();
    puts("ALL HLE-2.2D-B1 REUSE TESTS PASSED");
    return 0;
}
