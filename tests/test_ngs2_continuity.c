/* Per-source continuity and pause/clock regression, using the real mixer. */
#define main existing_overflow_main
#include "test_ngs2_overflow.c"
#undef main

typedef struct {
    Ngs2Handle *system,*rack,*voices[257];
    float master[256*8],aux[256*8];
    Ngs2RenderBuffer outputs[2];
    unsigned count,channels;
} ContinuityFixture;

static ContinuityFixture *continuity_open(unsigned count,unsigned channels) {
    ContinuityFixture *f=calloc(1,sizeof(*f)); assert(f && count<=257);
    f->count=count; f->channels=channels;
    ngs2_atrac9_setting=1; ngs2_render_mix_setting=1;
    ngs2_b2f2a_setting=0; ngs2_b2f2c_setting=1;
    atomic_store(&ngs2_audio_diag_setting,0);
    unsigned char sysopt[0x40]={0},rackopt[0xa8]={0};
    put32(sysopt,0,0x40); put32(sysopt,0x20,256); put32(sysopt,0x24,48000);
    put32(rackopt,0,0xa8); put32(rackopt,0x20,count); memcpy(rackopt+8,"Sampler",8);
    assert(!ngs2_system_create(sysopt,NULL,&f->system));
    assert(!ngs2_rack_create(f->system,0x1000,rackopt,NULL,&f->rack));
    for (unsigned i=0;i<count;++i) {
        assert(!ngs2_voice_handle(f->rack,i,&f->voices[i]));
        setup_voice(f->voices[i],channels==1);
        /* No decoder is exercised: frames are injected directly into its FIFO. */
        pthread_mutex_lock(&ngs2_lock);
        ngs2_voice_locked(f->voices[i])->waveform.channels=channels;
        pthread_mutex_unlock(&ngs2_lock);
        float matrix[64]={0};
        matrix[(i==count-1 ? 0 : channels)]=i==count-1 ? 0.05f : 0.0005f;
        unsigned char gain[0x18]; make_cmd(gain,sizeof(gain),1);
        put32(gain,12,8*channels); putptr(gain,16,(uintptr_t)matrix);
        assert(!ngs2_voice_control(f->voices[i],gain));
    }
    f->outputs[0]=(Ngs2RenderBuffer){f->master,sizeof(f->master),0x18,8};
    f->outputs[1]=(Ngs2RenderBuffer){f->aux,sizeof(f->aux),0x18,8};
    return f;
}
static void continuity_close(ContinuityFixture *f) {
    assert(!ngs2_system_destroy(f->system,NULL)); free(f);
}
static uint64_t voice_clock(Ngs2Handle *voice) {
    unsigned char state[0x30]; uint64_t result;
    assert(!ngs2_voice_state(voice,state,sizeof(state)));
    memcpy(&result,state+0x10,sizeof(result)); return result;
}
static void refill(ContinuityFixture *f) {
    pthread_mutex_lock(&ngs2_lock);
    for (unsigned i=0;i<f->count;++i) add_grain(ngs2_voice_locked(f->voices[i]),8192);
    pthread_mutex_unlock(&ngs2_lock);
}
static void commit(ContinuityFixture *f,unsigned sources) {
    pthread_mutex_lock(&ngs2_lock);
    Ngs2System *s=ngs2_system_locked(f->system);
    uint64_t before=s->grains_consumed+s->virtualized_grains;
    pthread_mutex_unlock(&ngs2_lock);
    for (unsigned retry=0;retry<1000;++retry) {
        assert(!ngs2_render(f->system,f->outputs,2));
        pthread_mutex_lock(&ngs2_lock);
        s=ngs2_system_locked(f->system);
        int done=s->grains_consumed+s->virtualized_grains==before+sources;
        pthread_mutex_unlock(&ngs2_lock);
        if (done) return;
    }
    assert(!"render did not commit");
}
static void expect_target(ContinuityFixture *f) {
    float target=8192.f/32768.f*0.05f*0.25f;
    float others=8192.f/32768.f*0.0005f*0.25f*(f->count-1);
    for (unsigned frame=0;frame<256;++frame) {
        assert(fabsf(f->master[frame*8]-target)<1e-6f);
        assert(fabsf(f->master[frame*8+1]-others)<1e-6f);
    }
}
static void continuous(unsigned count,unsigned channels) {
    ContinuityFixture *f=continuity_open(count,channels);
    for (unsigned q=0;q<2*count;++q) {
        refill(f); commit(f,count); expect_target(f);
        for (unsigned i=0;i<count;++i) assert(voice_clock(f->voices[i])==(uint64_t)(q+1)*256);
    }
    pthread_mutex_lock(&ngs2_lock);
    assert(ngs2_system_locked(f->system)->virtualized_grains==0);
    pthread_mutex_unlock(&ngs2_lock);
    printf("CONTINUITY PASS voices=%u channels=%u quanta=%u no_missing_frames\n",count,channels,2*count);
    continuity_close(f);
}
static void state_command(Ngs2Handle *voice,uint32_t action) {
    unsigned char command[0x0c]; make_cmd(command,sizeof(command),6); put32(command,8,action);
    assert(!ngs2_voice_control(voice,command));
}
static Ngs2Handle *continuity_pause_target;
static unsigned continuity_pause_calls;
static void continuity_pause_during_mix(void *unused) {
    (void)unused; ++continuity_pause_calls;
    state_command(continuity_pause_target,4);
}
static void pause_resume(void) {
    ContinuityFixture *f=continuity_open(86,4);
    Ngs2Handle *target=f->voices[85];
    for (unsigned cycle=0;cycle<12;++cycle) {
        refill(f); state_command(target,4); commit(f,85);
        assert(voice_clock(target)==cycle*256u);
        for (unsigned frame=0;frame<256;++frame) assert(f->master[frame*8]==0.f);
        state_command(target,5);
        /* Other FIFOs were consumed; refill only those, keeping the paused head. */
        pthread_mutex_lock(&ngs2_lock);
        for (unsigned i=0;i<85;++i) add_grain(ngs2_voice_locked(f->voices[i]),8192);
        pthread_mutex_unlock(&ngs2_lock);
        commit(f,86); expect_target(f);
        assert(voice_clock(target)==(cycle+1)*256u);
        for (unsigned i=0;i<85;++i) assert(voice_clock(f->voices[i])==(cycle+1)*512u);
    }
    /* A pause of the formerly overflowed source during the unlocked mix must
     * cancel the entire publication, preserving every other source as well. */
    refill(f); continuity_pause_target=target; continuity_pause_calls=0;
    ngs2_set_render_mix_hook(continuity_pause_during_mix,NULL);
    for (unsigned retry=0;retry<1000 && !continuity_pause_calls;++retry)
        assert(!ngs2_render(f->system,f->outputs,2));
    ngs2_set_render_mix_hook(NULL,NULL); assert(continuity_pause_calls==1);
    for (unsigned i=0;i<86;++i) {
        assert(voice_clock(f->voices[i])==(i==85 ? 12u*256 : 12u*512));
        assert(ngs2_voice_locked(f->voices[i])->atrac9->pcm_count==1);
    }
    for (unsigned i=0;i<256*8;++i) assert(f->master[i]==0.f);
    state_command(target,5); commit(f,86); expect_target(f);
    assert(voice_clock(target)==13u*256);
    for (unsigned i=0;i<85;++i) assert(voice_clock(f->voices[i])==25u*256);
    continuity_close(f); puts("PAUSE PASS 12 cycles and mid-mix cancellation; FIFO and consumed-sample clock preserved");
}
static void over_capacity(void) {
    ContinuityFixture *f=continuity_open(257,1); refill(f);
    assert(ngs2_render(f->system,f->outputs,2)==NGS2_INVALID);
    for (unsigned i=0;i<257;++i) {
        assert(voice_clock(f->voices[i])==0);
        assert(ngs2_voice_locked(f->voices[i])->atrac9->pcm_count==1);
    }
    /* Returning within the supported capacity must recover without reinitialization. */
    state_command(f->voices[0],4); commit(f,256);
    assert(voice_clock(f->voices[0])==0);
    for (unsigned i=1;i<257;++i) assert(voice_clock(f->voices[i])==256);
    continuity_close(f);
    /* Capacity counts mixed sources, not zero-matrix FIFO drains. */
    f=continuity_open(257,1);
    float zeros[8]={0}; unsigned char gain[0x18]; make_cmd(gain,sizeof(gain),1);
    put32(gain,12,8); putptr(gain,16,(uintptr_t)zeros);
    assert(!ngs2_voice_control(f->voices[0],gain));
    refill(f); commit(f,257);
    for (unsigned i=0;i<257;++i) assert(voice_clock(f->voices[i])==256);
    assert(ngs2_system_locked(f->system)->grains_consumed==256);
    assert(ngs2_system_locked(f->system)->virtualized_grains==1);
    for (unsigned frame=0;frame<256;++frame)
        assert(fabsf(f->master[frame*8]-8192.f/32768.f*0.05f*0.25f)<1e-6f);
    continuity_close(f); puts("CAPACITY PASS 257 rejected without progress; recovery at 256; zero-matrix bypass independent");
}
static int compare_us(const void *a,const void *b) {
    uint64_t x=*(const uint64_t *)a,y=*(const uint64_t *)b; return (x>y)-(x<y);
}
static void benchmark(unsigned count,unsigned channels) {
    ContinuityFixture *f=continuity_open(count,channels);
    uint64_t times[500];
    for (unsigned q=0;q<520;++q) {
        refill(f); uint64_t start=ngs2_now_us(); commit(f,count);
        uint64_t elapsed=ngs2_now_us()-start;
        if (q>=20) times[q-20]=elapsed;
        expect_target(f);
    }
    qsort(times,500,sizeof(*times),compare_us);
    printf("BENCH voices=%u channels=%u samples=500 p50_us=%llu p95_us=%llu p99_us=%llu max_us=%llu workspace_bytes=%zu\n",
           count,channels,(unsigned long long)times[250],(unsigned long long)times[475],
           (unsigned long long)times[495],(unsigned long long)times[499],sizeof(ngs2_render_snapshots));
    continuity_close(f);
}
int main(int argc,char **argv) {
    if (argc==2 && !strcmp(argv[1],"--bench")) {
        benchmark(32,4); benchmark(86,4); benchmark(256,8); return 0;
    }
    continuous(33,1); continuous(40,4); continuous(86,4); continuous(256,8);
    pause_resume(); over_capacity();
    puts("PASS: all source continuity contracts"); return 0;
}
