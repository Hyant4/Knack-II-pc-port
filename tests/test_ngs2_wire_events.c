/* Knack CUSA08014 01.01: wire events verified independently against guest SoundX. */
#define main existing_mixer_suite_main
#include "test_ngs2_mixer.c"
#undef main
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"WIRE CONTRACT FAILED line %d: %s\n",__LINE__,#x); return 1; } } while (0)

typedef struct {Ngs2Handle *sys,*rack,*voice;float master[256*8],aux[256*8];} WireFixture;
static void wire_open(WireFixture *f) {
    memset(f,0,sizeof(*f));unsigned char so[0x40],ro[0xa8];system_option(so);rack_option(ro,1,"Sampler");
    assert(!ngs2_system_create(so,NULL,&f->sys));assert(!ngs2_rack_create(f->sys,0x1000,ro,NULL,&f->rack));
    assert(!ngs2_voice_handle(f->rack,0,&f->voice));setup_waveform(f->voice,4);set_voice_state(f->voice,0);
    float matrix[32]={0};matrix[0]=1.0f;set_voice_matrix(f->voice,matrix,32);
    pthread_mutex_lock(&ngs2_lock);Ngs2Voice *v=ngs2_voice_locked(f->voice);
    push_synthetic_grain(v,16384);push_synthetic_grain(v,8192);pthread_mutex_unlock(&ngs2_lock);
}
static Ngs2RenderBuffer *wire_buffers(WireFixture *f,Ngs2RenderBuffer out[2]) {
    out[0]=(Ngs2RenderBuffer){f->master,sizeof(f->master),0x18,8};
    out[1]=(Ngs2RenderBuffer){f->aux,sizeof(f->aux),0x18,8};return out;
}
static int wire_pause_resume(void) {
    WireFixture f;wire_open(&f);Ngs2RenderBuffer buffers[2];wire_buffers(&f,buffers);
    Ngs2Voice *v=ngs2_voice_locked(f.voice);uint64_t generation=v->atrac9->generation;
    uint32_t read=v->atrac9->pcm_read;v->atrac9->hevag.carry_samples=3;
    set_voice_state(f.voice,4); /* guest VOICE_PAUSE at 0xb2ae86 */
    CHECK(v->internal_state==NGS2_VOICE_PAUSED);CHECK(v->atrac9->pcm_count==2);
    CHECK(v->atrac9->generation==generation && v->atrac9->pcm_read==read);
    CHECK(v->atrac9->hevag.carry_samples==3);
    set_voice_state(f.voice,4);CHECK(!test_render_retry(f.sys,buffers,2));
    CHECK(v->atrac9->pcm_count==2 && v->atrac9->pcm_read==read);
    for (unsigned i=0;i<256u*8u;++i) CHECK(f.master[i]==0.0f);
    set_voice_state(f.voice,5); /* guest VOICE_RESUME at 0xb2af96 */
    set_voice_state(f.voice,5);CHECK(v->internal_state==NGS2_VOICE_PLAYING);
    CHECK(v->atrac9->generation==generation && v->atrac9->pcm_count==2);
    CHECK(!test_render_retry(f.sys,buffers,2));CHECK(fabsf(f.master[0]-0.125f)<1e-6f);
    CHECK(v->atrac9->pcm_count==1);CHECK(!test_render_retry(f.sys,buffers,2));
    CHECK(fabsf(f.master[0]-0.0625f)<1e-6f && v->atrac9->pcm_count==0);
    CHECK(!ngs2_system_destroy(f.sys,NULL));puts("PASS guest PAUSE/RESUME preserve FIFO, generation, sample order and repeated events");return 0;
}
static int wire_stop(void) {
    WireFixture f;wire_open(&f);Ngs2RenderBuffer buffers[2];wire_buffers(&f,buffers);
    Ngs2Voice *v=ngs2_voice_locked(f.voice);uint64_t generation=v->atrac9->generation;
    set_voice_state(f.voice,2); /* guest VOICE_STOP at 0xb2ad76 */
    CHECK(v->internal_state==NGS2_VOICE_STOPPED && v->atrac9->pcm_count==0);
    CHECK(v->atrac9->generation!=generation);set_voice_state(f.voice,5);
    CHECK(v->internal_state==NGS2_VOICE_STOPPED);CHECK(!test_render_retry(f.sys,buffers,2));
    for (unsigned i=0;i<256u*8u;++i) CHECK(f.master[i]==0.0f);
    CHECK(!ngs2_system_destroy(f.sys,NULL));puts("PASS guest STOP clears pending audio and RESUME cannot restart stopped voice");return 0;
}
static int wire_release(void) {
    WireFixture f;wire_open(&f);Ngs2Voice *v=ngs2_voice_locked(f.voice);
    set_voice_state(f.voice,1); /* guest VOICE_RELEASE at 0xb2b0a6 */
    CHECK(v->internal_state==NGS2_VOICE_RELEASING && v->atrac9->pcm_count==0);
    CHECK(!ngs2_system_destroy(f.sys,NULL));puts("PASS guest RELEASE reaches existing HLE release handler (no envelope emulation)");return 0;
}
int main(void) {
    setvbuf(stdout,NULL,_IONBF,0);ngs2_atrac9_setting=1;ngs2_render_mix_setting=1;
    ngs2_b2f2a_setting=0;ngs2_b2f2c_setting=0;atomic_store(&ngs2_audio_diag_setting,0);
    if (wire_pause_resume() || wire_stop() || wire_release()) return 1;
    puts("PASS VERIFIED GUEST WIRE CONTRACTS");return 0;
}
