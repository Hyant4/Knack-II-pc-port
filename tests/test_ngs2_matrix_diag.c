/* Diagnostic ON/OFF must preserve PCM, state bytes, generations and FIFO. */
#define main previous_mixer_main
#include "test_ngs2_mixer.c"
#undef main

static uint64_t digest=UINT64_C(14695981039346656037);
static void digest_data(const void *data,size_t size) {
    const unsigned char *bytes=data;
    for (size_t i=0;i<size;++i) { digest^=bytes[i]; digest*=UINT64_C(1099511628211); }
}
static void query(Ngs2Handle *handle,uint32_t expected) {
    unsigned char state[0x30]; memset(state,0xa5,sizeof(state));
    assert(!ngs2_voice_state(handle,state,sizeof(state)));
    uint32_t flags; memcpy(&flags,state,sizeof(flags)); assert(flags==expected);
    for (unsigned i=4;i<sizeof(state);++i)
        if(i<0x10 || i>=0x18)assert(!state[i]);
    digest_data(state,sizeof(state));
}
static float render(Ngs2Handle *sys) {
    float master[256*8],aux[256*8];
    Ngs2RenderBuffer buffers[]={{master,sizeof(master),0x18,8},{aux,sizeof(aux),0x18,8}};
    for (unsigned retry=0;retry<100;++retry) {
        pthread_mutex_lock(&ngs2_lock);
        Ngs2System *s=ngs2_system_locked(sys); Ngs2Voice *v=&s->racks->voices[0];
        int pending=v->internal_state==NGS2_VOICE_PLAYING && v->atrac9->pcm_count;
        uint64_t before=s->grains_consumed+s->virtualized_grains;
        pthread_mutex_unlock(&ngs2_lock);
        assert(!ngs2_render(sys,buffers,2));
        pthread_mutex_lock(&ngs2_lock);
        int committed=s->grains_consumed+s->virtualized_grains>before;
        pthread_mutex_unlock(&ngs2_lock);
        if (!pending || committed) { digest_data(master,sizeof(master)); return master[0]; }
        /* A render attempt may roll back after the snapshot if worker owns the lock. */
        Sleep(1);
    }
    assert(!"No committed render within bounded retry window"); return 0.f;
}
int main(int argc,char **argv) {
    assert(argc==2); int on=!strcmp(argv[1],"on");
    assert(ngs2_matrix_diag_enabled()==on);
    ngs2_atrac9_setting=1; ngs2_render_mix_setting=1;
    ngs2_b2f2a_setting=0; ngs2_b2f2c_setting=1;
    atomic_store(&ngs2_audio_diag_setting,0);
    unsigned char so[0x40],ro[0xa8]; system_option(so); rack_option(ro,1,"Sampler");
    Ngs2Handle *sys=NULL,*rack=NULL,*handle=NULL;
    assert(!ngs2_system_create(so,NULL,&sys));
    assert(!ngs2_rack_create(sys,0x1000,ro,NULL,&rack));
    assert(!ngs2_voice_handle(rack,0,&handle)); setup_waveform(handle,4);
    float matrix[32]={1.f};
    set_voice_matrix(handle,matrix,32); set_voice_matrix(handle,matrix,32);
    set_voice_state(handle,0);
    pthread_mutex_lock(&ngs2_lock); Ngs2Voice *v=ngs2_voice_locked(handle);
    assert(v->matrix_diag_calls==(on ? 2u : 0u));
    uint64_t generation=v->atrac9->generation; pthread_mutex_unlock(&ngs2_lock);
    query(handle,3); uint32_t lines=ngs2_matrix_diag_states;
    query(handle,3); assert(ngs2_matrix_diag_states==lines);
    /* A rejected Cmd 1 must not appear as accepted or change descriptors. */
    unsigned char invalid[0x18]; command(invalid,sizeof(invalid),1);
    put_u32(invalid,0x0c,NGS2_MAX_DESCRIPTOR_WORDS+1);
    assert(ngs2_voice_control(handle,invalid)==NGS2_INVALID);
    assert(v->descriptor_words==32 && v->matrix_diag_calls==(on ? 2u : 0u));
    pthread_mutex_lock(&ngs2_lock); push_synthetic_grain(v,16384); pthread_mutex_unlock(&ngs2_lock);
    set_voice_state(handle,4); query(handle,1);
    assert(render(sys)==0.f); assert(v->atrac9->pcm_count==1);
    assert(v->atrac9->generation==generation);
    memset(matrix,0,sizeof(matrix));
    set_voice_matrix(handle,matrix,32); set_voice_matrix(handle,matrix,32);
    set_voice_state(handle,5); query(handle,3);
    set_voice_matrix(handle,matrix,32); /* First identical Cmd 1 after RESUME is visible. */
    assert(render(sys)==0.f && v->atrac9->pcm_count==0);
    matrix[0]=1.f; set_voice_matrix(handle,matrix,32);
    pthread_mutex_lock(&ngs2_lock); push_synthetic_grain(v,16384); pthread_mutex_unlock(&ngs2_lock);
    assert(render(sys)>0.f);
    set_voice_matrix(handle,matrix,32); /* Identical payload increments calls but is deduplicated. */
    pthread_mutex_lock(&ngs2_lock); v->matrix_diag_summary_us=0; pthread_mutex_unlock(&ngs2_lock);
    query(handle,3); assert(v->matrix_diag_calls==(on ? 7u : 0u));
    /* Raw nonzero is not effective audibility: NaN/Inf/range/-0/outside effective area. */
    float classified[64]={INFINITY,NAN,65.f,-0.f}; classified[40]=1.f;
    set_voice_matrix(handle,classified,64); set_voice_matrix(handle,matrix,32);
    /* Each budget has one explicit marker; matrix exhaustion must not hide action/query evidence. */
    ngs2_matrix_diag_changes=NGS2_MATRIX_DIAG_CHANGE_LIMIT;
    memset(matrix,0,sizeof(matrix)); set_voice_matrix(handle,matrix,32);
    matrix[0]=1.f; set_voice_matrix(handle,matrix,32);
    set_voice_state(handle,4); query(handle,1);
    ngs2_matrix_diag_states=NGS2_MATRIX_DIAG_STATE_LIMIT;
    set_voice_state(handle,5); query(handle,3);
    assert(v->atrac9->generation==generation && v->atrac9->pcm_count==0);
    digest_data(v->descriptors,sizeof(v->descriptors));
    printf("PARITY=%016llx state=%u fifo=%u generation_delta=%llu calls=%llu\n",
           (unsigned long long)digest,v->internal_state,v->atrac9->pcm_count,
           (unsigned long long)(v->atrac9->generation-generation),
           (unsigned long long)v->matrix_diag_calls);
    assert(!ngs2_system_destroy(sys,NULL));
    puts("PASS MATRIX DIAGNOSTICS"); return 0;
}
