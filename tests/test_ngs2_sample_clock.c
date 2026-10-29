/* API clock regression. Reuse real mixer fixtures; no second mixer model. */
#define main prior_mixer_main
#include "test_ngs2_mixer.c"
#undef main

static uint64_t sample_clock(Ngs2Handle *voice) {
    unsigned char out[0x38];memset(out,0xa5,sizeof(out));
    assert(!ngs2_voice_state(voice,out,0x30));
    for(unsigned i=4;i<0x30;++i)if(i<0x10 || i>=0x18)assert(out[i]==0);
    for(unsigned i=0x30;i<sizeof(out);++i)assert(out[i]==0xa5);
    uint64_t result;memcpy(&result,out+0x10,8);return result;
}
static void render_clock(Ngs2Handle *system,Ngs2RenderBuffer *buffers,Ngs2Handle *voice,uint64_t target) {
    for(unsigned retry=0;retry<100 && sample_clock(voice)!=target;++retry) {
        assert(!ngs2_render(system,buffers,2));Sleep(1);
    }
    assert(sample_clock(voice)==target);
}
static Ngs2Handle *pause_target;
static unsigned pause_calls;
static void pause_during_mix(void *unused) {
    (void)unused;++pause_calls;set_voice_state(pause_target,4);
}
int main(void) {
    ngs2_atrac9_setting=1;ngs2_render_mix_setting=1;ngs2_b2f2c_setting=0;
    unsigned char sysopt[0x40],rackopt[0xa8];
    system_option(sysopt);rack_option(rackopt,2,"Sampler");
    Ngs2Handle *system=NULL,*rack=NULL,*a=NULL,*b=NULL;
    assert(!ngs2_system_create(sysopt,NULL,&system));
    assert(!ngs2_rack_create(system,0x1000,rackopt,NULL,&rack));
    assert(!ngs2_voice_handle(rack,0,&a));assert(!ngs2_voice_handle(rack,1,&b));
    setup_waveform(a,1);setup_waveform(b,4);set_voice_state(a,0);set_voice_state(b,0);
    float matrix[8]={1},matrix_b[32]={1};set_voice_matrix(a,matrix,8);set_voice_matrix(b,matrix_b,32);
    unsigned char short_state[24];memset(short_state,0xa5,sizeof(short_state));
    assert(!ngs2_voice_state(a,short_state,0x10));
    for(unsigned i=4;i<16;++i)assert(short_state[i]==0);
    for(unsigned i=16;i<sizeof(short_state);++i)assert(short_state[i]==0xa5);
    float out0[256*8],out1[256*8];
    Ngs2RenderBuffer buffers[2]={{out0,sizeof(out0),0x18,8},{out1,sizeof(out1),0x18,8}};
    /* Underrun and repeated observations never manufacture progress. */
    assert(!ngs2_render(system,buffers,2));assert(sample_clock(a)==0);
    pthread_mutex_lock(&ngs2_lock);
    push_synthetic_grain(ngs2_voice_locked(a),4096);push_synthetic_grain(ngs2_voice_locked(b),8192);
    ngs2_voice_locked(a)->atrac9->decoded_frames=999; /* Decode-ahead is not playback. */
    pthread_mutex_unlock(&ngs2_lock);
    for(unsigned i=0;i<20;++i)assert(sample_clock(a)==0);
    render_clock(system,buffers,a,256);assert(sample_clock(b)==256);
    /* Counts sample frames, independent of channel count. */
    set_voice_state(a,4);
    pthread_mutex_lock(&ngs2_lock);
    push_synthetic_grain(ngs2_voice_locked(a),4096);push_synthetic_grain(ngs2_voice_locked(b),8192);
    pthread_mutex_unlock(&ngs2_lock);
    render_clock(system,buffers,b,512);assert(sample_clock(a)==256);
    set_voice_state(a,5);render_clock(system,buffers,a,512);assert(sample_clock(b)==512);
    /* A cancelled render must publish neither PCM nor clock advancement. */
    pthread_mutex_lock(&ngs2_lock);
    push_synthetic_grain(ngs2_voice_locked(a),4096);push_synthetic_grain(ngs2_voice_locked(b),8192);
    pthread_mutex_unlock(&ngs2_lock);
    pause_target=a;ngs2_set_render_mix_hook(pause_during_mix,NULL);
    for(unsigned i=0;i<100 && !pause_calls;++i){assert(!ngs2_render(system,buffers,2));Sleep(1);}
    ngs2_set_render_mix_hook(NULL,NULL);assert(pause_calls==1);
    assert(sample_clock(a)==512 && sample_clock(b)==512);
    set_voice_state(a,5);
    /* Zero-matrix virtualization still consumes real playback time. */
    ngs2_b2f2c_setting=1;memset(matrix,0,sizeof(matrix));set_voice_matrix(a,matrix,8);
    render_clock(system,buffers,a,768);assert(sample_clock(b)==768);
    set_voice_state(a,2);assert(sample_clock(a)==0 && sample_clock(b)==768);
    set_voice_state(a,0);assert(sample_clock(a)==0);
    pthread_mutex_lock(&ngs2_lock);
    Ngs2Voice *voice=ngs2_voice_locked(a);push_synthetic_grain(voice,4096);
    voice->atrac9->stream_end_requested=1;
    pthread_mutex_unlock(&ngs2_lock);
    render_clock(system,buffers,a,256);
    unsigned char finished[0x30];assert(!ngs2_voice_state(a,finished,sizeof(finished)));
    uint32_t flags;memcpy(&flags,finished,4);assert(flags==0x20);
    assert(!ngs2_render(system,buffers,2));assert(sample_clock(a)==256);
    set_voice_state(a,0);assert(sample_clock(a)==0);
    assert(!ngs2_system_destroy(system,NULL));
    puts("PASS: sample clock commits, pause/resume, cancellation, virtualization, isolation, EOF and reuse");
    return 0;
}
