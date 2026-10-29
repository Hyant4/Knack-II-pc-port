/* Passive route diagnostics: output parity, wire fields, disabled path and cap. */
#define main previous_mixer_tests_main
#include "test_ngs2_mixer.c"
#undef main

static uint64_t digest=UINT64_C(1469598103934665603);
static void digest_bytes(const void *data,size_t size) {
    const unsigned char *p=data;
    for(size_t i=0;i<size;++i){digest^=p[i];digest*=UINT64_C(1099511628211);}
}
int main(int argc,char **argv) {
    assert(argc==2);int expect_on=!strcmp(argv[1],"on");
    setvbuf(stdout,NULL,_IONBF,0);ngs2_atrac9_setting=1;ngs2_render_mix_setting=1;
    atomic_store(&ngs2_audio_diag_setting,0);ngs2_b2f2a_setting=0;ngs2_b2f2c_setting=0;
    assert(ngs2_route_diag_enabled()==expect_on);
    unsigned char so[0x40],ro[0xa8];system_option(so);rack_option(ro,1,"Sampler");
    Ngs2Handle *sys=NULL,*rack=NULL,*voice=NULL,*br=NULL,*bus=NULL;
    assert(!ngs2_system_create(so,NULL,&sys));assert(!ngs2_rack_create(sys,0x1000,ro,NULL,&rack));
    assert(!ngs2_voice_handle(rack,0,&voice));setup_waveform(voice,4);
    rack_option(ro,1,"PreMaster");assert(!ngs2_rack_create(sys,0x2000,ro,NULL,&br));
    assert(!ngs2_voice_handle(br,0,&bus));set_voice_state(bus,0);set_voice_state(voice,0);
    float matrix[32]={0};matrix[0]=1.0f;set_voice_matrix(voice,matrix,32);
    unsigned char cmd[0x18];command(cmd,sizeof(cmd),5);put_uptr(cmd,0x10,(uintptr_t)bus);
    assert(!ngs2_voice_control(voice,cmd));
    unsigned char gain[0x10];command(gain,sizeof(gain),2);float half=0.5f;
    memcpy(gain+0x0c,&half,4);assert(!ngs2_voice_control(voice,gain));
    command(gain,sizeof(gain),3);assert(!ngs2_voice_control(bus,gain));
    uint32_t lines_before=ngs2_route_diag_lines;command(gain,sizeof(gain),2);put_u32(gain,8,8);
    assert(ngs2_voice_control(voice,gain)==NGS2_INVALID);assert(ngs2_route_diag_lines==lines_before);
    pthread_mutex_lock(&ngs2_lock);Ngs2Voice *v=ngs2_voice_locked(voice);push_synthetic_grain(v,16384);
    uint64_t generation=v->atrac9->generation;pthread_mutex_unlock(&ngs2_lock);
    float out[256*8],aux[256*8];Ngs2RenderBuffer b[2]={{out,sizeof(out),0x18,8},{aux,sizeof(aux),0x18,8}};
    set_voice_state(voice,4);assert(!test_render_retry(sys,b,2));digest_bytes(out,sizeof(out));
    assert(v->atrac9->pcm_count==1 && v->atrac9->generation==generation);
    set_voice_state(voice,5);assert(!test_render_retry(sys,b,2));digest_bytes(out,sizeof(out));
    assert(fabsf(out[0]-0.125f)<1e-6f && v->atrac9->pcm_count==0);
    set_voice_state(voice,2);assert(!test_render_retry(sys,b,2));digest_bytes(out,sizeof(out));
    printf("PCM_DIGEST=%016llx STATE=%u FIFO=%u GENERATION_DELTA=%llu\n",(unsigned long long)digest,
           v->internal_state,v->atrac9->pcm_count,(unsigned long long)(v->atrac9->generation-generation));
    /* Limit emits one explicit incomplete marker and stops, without modifying voice state. */
    atomic_store(&ngs2_route_diag_setting,1);ngs2_route_diag_lines=NGS2_ROUTE_DIAG_LIMIT;
    command(gain,sizeof(gain),2);char line[512]={0};pthread_mutex_lock(&ngs2_lock);
    ngs2_route_diag_locked(v,gain,sizeof(gain),2,line,sizeof(line));
    assert(strstr(line,"trace_incomplete=1"));line[0]=0;
    ngs2_route_diag_locked(v,gain,sizeof(gain),2,line,sizeof(line));assert(!line[0]);
    assert(v->internal_state==NGS2_VOICE_STOPPED && v->atrac9->pcm_count==0);
    pthread_mutex_unlock(&ngs2_lock);assert(!ngs2_system_destroy(sys,NULL));
    puts("PASS PASSIVE ROUTE DIAGNOSTICS");return 0;
}
