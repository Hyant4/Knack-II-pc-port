/* Actual mixer/clock regression with synthetic ramps; no guest, GPU or device. */
#define main prior_mixer_main
#include "test_ngs2_mixer.c"
#undef main

typedef struct {
    Ngs2Handle *system,*rack,*voice;
    float main[256*8],aux[256*8];
} PitchFixture;
static uint64_t clock_of(Ngs2Handle *voice) {
    unsigned char bytes[0x30];uint64_t value;
    assert(!ngs2_voice_state(voice,bytes,sizeof(bytes)));memcpy(&value,bytes+0x10,8);return value;
}
static int32_t pitch(Ngs2Handle *voice,float ratio) {
    unsigned char cmd[0x10];command(cmd,sizeof(cmd),UINT32_C(0x10000005));
    memcpy(cmd+8,&ratio,4);return ngs2_voice_control(voice,cmd);
}
static void open_pitch(PitchFixture *f,int silent,int bypass) {
    memset(f,0,sizeof(*f));ngs2_atrac9_setting=1;ngs2_render_mix_setting=1;
    ngs2_b2f2a_setting=0;ngs2_b2f2c_setting=bypass;atomic_store(&ngs2_audio_diag_setting,0);
    unsigned char so[0x40],ro[0xa8];system_option(so);rack_option(ro,2,"Sampler");
    assert(!ngs2_system_create(so,NULL,&f->system));assert(!ngs2_rack_create(f->system,0x1000,ro,NULL,&f->rack));
    assert(!ngs2_voice_handle(f->rack,0,&f->voice));setup_waveform(f->voice,1);set_voice_state(f->voice,0);
    float matrix[8]={silent ? 0.f : 1.f};set_voice_matrix(f->voice,matrix,8);
    pthread_mutex_lock(&ngs2_lock);Ngs2Voice *v=ngs2_voice_locked(f->voice);
    for (unsigned i=0;i<8;++i) {
        push_synthetic_grain(v,0);
        for (unsigned j=0;j<256;++j)v->atrac9->fifo[i].pcm[j]=(int16_t)((i*256+j)*8);
    }
    pthread_mutex_unlock(&ngs2_lock);
}
static void close_pitch(PitchFixture *f) {assert(!ngs2_system_destroy(f->system,NULL));}
static void quantum(PitchFixture *f) {
    Ngs2RenderBuffer buffers[]={{f->main,sizeof(f->main),0x18,8},{f->aux,sizeof(f->aux),0x18,8}};
    Ngs2System *system=ngs2_system_locked(f->system);
    for (unsigned i=0;i<100;++i) {
        uint64_t before=system->trylock_contentions+atomic_load(&ngs2_global_trylock_contentions);
        assert(!ngs2_render(f->system,buffers,2));
        if (system->trylock_contentions+atomic_load(&ngs2_global_trylock_contentions)==before)return;
        Sleep(1);
    }
    assert(!"Render could not acquire test workspace");
}
static void ramp(const PitchFixture *f,double start,double rate,unsigned output_frames) {
    for (unsigned i=0;i<256;++i)for(unsigned c=0;c<8;++c) {
        float expected=i<output_frames && !c ? (float)((start+i*rate)*8/32768*.25) : 0;
        assert(fabsf(f->main[i*8+c]-expected)<1e-7f);
    }
}
static void zero_pcm(const PitchFixture *f) {for(unsigned i=0;i<256*8;++i)assert(f->main[i]==0);}
static void rates(void) {
    const float ratios[]={.5f,1,1.25f,2,4};
    for(unsigned i=0;i<sizeof(ratios)/sizeof(*ratios);++i) {
        PitchFixture f;open_pitch(&f,0,1);assert(!pitch(f.voice,ratios[i]));
        quantum(&f);ramp(&f,0,ratios[i],256);
        assert(clock_of(f.voice)==(uint64_t)(256*ratios[i]));
        quantum(&f);ramp(&f,256*ratios[i],ratios[i],256);
        assert(clock_of(f.voice)==(uint64_t)(512*ratios[i]));close_pitch(&f);
    }
    PitchFixture f;open_pitch(&f,0,1);assert(!pitch(f.voice,1.f/1024));
    for(unsigned i=0;i<4;++i){quantum(&f);assert(clock_of(f.voice)==(i+1)/4);}
    assert(!pitch(f.voice,1));quantum(&f);ramp(&f,1,1,256);assert(clock_of(f.voice)==257);close_pitch(&f);
}
static void hold_pause_and_invalid(void) {
    PitchFixture f;open_pitch(&f,0,1);assert(!pitch(f.voice,.5f));quantum(&f);
    assert(clock_of(f.voice)==128);assert(!pitch(f.voice,0));quantum(&f);zero_pcm(&f);
    assert(clock_of(f.voice)==128);
    const float invalid[]={-1,NAN,INFINITY,17};
    for(unsigned i=0;i<4;++i)assert(pitch(f.voice,invalid[i])==NGS2_INVALID);
    assert(ngs2_voice_locked(f.voice)->pitch_ratio==0);
    set_voice_state(f.voice,4);assert(!pitch(f.voice,2));quantum(&f);zero_pcm(&f);assert(clock_of(f.voice)==128);
    set_voice_state(f.voice,5);quantum(&f);ramp(&f,128,2,256);assert(clock_of(f.voice)==640);close_pitch(&f);
}
static void bypass_and_isolation(void) {
    for(int bypass=0;bypass<=1;++bypass) {
        PitchFixture f;open_pitch(&f,1,bypass);assert(!pitch(f.voice,.5f));
        quantum(&f);zero_pcm(&f);assert(clock_of(f.voice)==128);
        assert(!pitch(f.voice,4));quantum(&f);zero_pcm(&f);assert(clock_of(f.voice)==1152);
        assert(!pitch(f.voice,0));quantum(&f);zero_pcm(&f);assert(clock_of(f.voice)==1152);close_pitch(&f);
    }
    PitchFixture f;open_pitch(&f,0,1);Ngs2Handle *other=NULL;assert(!ngs2_voice_handle(f.rack,1,&other));
    setup_waveform(other,1);set_voice_state(other,0);float matrix[8]={1};set_voice_matrix(other,matrix,8);
    pthread_mutex_lock(&ngs2_lock);push_synthetic_grain(ngs2_voice_locked(other),1000);pthread_mutex_unlock(&ngs2_lock);
    assert(!pitch(f.voice,.5f));quantum(&f);assert(clock_of(f.voice)==128 && clock_of(other)==256);close_pitch(&f);
}
static void starvation_and_tail(void) {
    PitchFixture f;open_pitch(&f,0,1);assert(!pitch(f.voice,16));quantum(&f);ramp(&f,0,16,128);
    assert(clock_of(f.voice)==2048);quantum(&f);zero_pcm(&f);assert(clock_of(f.voice)==2048);
    pthread_mutex_lock(&ngs2_lock);Ngs2Voice *v=ngs2_voice_locked(f.voice);
    push_synthetic_grain(v,12000);pthread_mutex_unlock(&ngs2_lock);
    assert(!pitch(f.voice,1));quantum(&f);assert(clock_of(f.voice)==2304);close_pitch(&f);
    open_pitch(&f,0,1);pthread_mutex_lock(&ngs2_lock);v=ngs2_voice_locked(f.voice);
    v->atrac9->pcm_count=1;v->atrac9->pcm_write=1;v->atrac9->fifo[0].frames=7;
    v->atrac9->stream_end_requested=1;pthread_mutex_unlock(&ngs2_lock);
    assert(!pitch(f.voice,.5f));quantum(&f);
    for(unsigned i=0;i<14;++i)assert(fabsf(f.main[i*8]-(float)(fmin(i*.5,6)*8/32768*.25))<1e-7f);
    for(unsigned i=14*8;i<256*8;++i)assert(f.main[i]==0);
    assert(clock_of(f.voice)==7);
    unsigned char state[0x30];assert(!ngs2_voice_state(f.voice,state,sizeof(state)));assert(ngs2_u32(state,0)==0x20);
    close_pitch(&f);
}
static Ngs2Handle *cancel_voice;
static void hold_during_mix(void *unused) {(void)unused;assert(!pitch(cancel_voice,0));}
static void cancel_and_generation(void) {
    PitchFixture f;open_pitch(&f,0,1);cancel_voice=f.voice;
    ngs2_set_render_mix_hook(hold_during_mix,NULL);quantum(&f);ngs2_set_render_mix_hook(NULL,NULL);
    zero_pcm(&f);assert(clock_of(f.voice)==0 && ngs2_voice_locked(f.voice)->atrac9->pcm_count==8);
    assert(!pitch(f.voice,.5f));quantum(&f);ramp(&f,0,.5,256);assert(clock_of(f.voice)==128);
    set_voice_state(f.voice,2);set_voice_state(f.voice,0);assert(clock_of(f.voice)==0);
    pthread_mutex_lock(&ngs2_lock);Ngs2Voice *v=ngs2_voice_locked(f.voice);
    assert(!v->atrac9->pcm_frame_offset && v->atrac9->pcm_fraction==0);
    push_synthetic_grain(v,8000);pthread_mutex_unlock(&ngs2_lock);
    quantum(&f);assert(clock_of(f.voice)==128 && f.main[0]==8000.f/32768*.25);close_pitch(&f);
}
int main(void) {
    rates();hold_pause_and_invalid();bypass_and_isolation();starvation_and_tail();cancel_and_generation();
    puts("PASS: source-rate PCM, hold/resume, fractions, bypass, isolation, bounded starvation, EOF and cancellation");return 0;
}
