/* SPDX-License-Identifier: GPL-2.0-or-later
 * Supplemental API regressions for completion boundaries and conservative policy.
 * Codec and snapshot mocks are inherited; no second state-machine implementation.
 */
#define main previous_completion_main
#include "test_ngs2_completion.c"
#undef main

static uint32_t edge_flags(Ngs2Handle *voice) {
    unsigned char out[0x30]; uint32_t flags;
    memset(out,0xa5,sizeof(out)); assert(!ngs2_voice_state(voice,out,sizeof(out)));
    memcpy(&flags,out,sizeof(flags));
    /* Sampler +0x10..+0x17 now reports consumed sample frames; other fields
     * keep their legacy zeros. Dedicated sample_clock tests verify the count. */
    for (size_t i=4;i<sizeof(out);++i)
        if(i<0x10 || i>=0x18)assert(out[i]==0);
    return flags;
}
static Ngs2Voice *edge_voice(Ngs2Handle *handle) {
    pthread_mutex_lock(&ngs2_lock);
    Ngs2Voice *v=ngs2_voice_locked(handle); assert(v);
    pthread_mutex_unlock(&ngs2_lock); return v;
}
static void edge_complete(CompletionFixture *f) {
    unsigned char data[768]={0};
    enqueue_segment(f->voice_a,data,sizeof(data));
    enqueue_null_command(f->voice_a);
    drain_voice(f,edge_voice(f->voice_a));
    assert(edge_flags(f->voice_a)==0x20);
}

static void run_edge(const char *name) {
    CompletionFixture f; open_fixture(&f);
    Ngs2Voice *v=edge_voice(f.voice_a);
    unsigned char data[768]={0};

    if (!strcmp(name,"finished_reuse")) {
        set_voice_state(f.voice_a,0); edge_complete(&f);
        assert(edge_flags(f.voice_a)==0x20 && edge_flags(f.voice_a)==0x20);
        uint64_t generation=v->atrac9->generation;
        set_voice_state(f.voice_a,0);
        assert(edge_flags(f.voice_a)==3 && v->atrac9->generation>generation);
        edge_complete(&f);
        generation=v->atrac9->generation;
        /* Input may be submitted before PLAY on the reused stopped voice. */
        enqueue_segment(f.voice_a,data,sizeof(data)); enqueue_null_command(f.voice_a);
        assert(v->atrac9->generation>generation && edge_flags(f.voice_a)==0);
        set_voice_state(f.voice_a,0); drain_voice(&f,v);
        assert(edge_flags(f.voice_a)==0x20);
    } else if (!strcmp(name,"eof_before_play")) {
        enqueue_segment(f.voice_a,data,sizeof(data)); enqueue_null_command(f.voice_a);
        assert(edge_flags(f.voice_a)==0);
        set_voice_state(f.voice_a,0); drain_voice(&f,v);
        assert(edge_flags(f.voice_a)==0x20);
    } else if (!strcmp(name,"empty_eof_and_short_command")) {
        set_voice_state(f.voice_a,0);
        unsigned char short_cmd[8]; command(short_cmd,sizeof(short_cmd),0x10000001);
        assert(!ngs2_voice_control(f.voice_a,short_cmd));
        assert(edge_flags(f.voice_a)==3);
        enqueue_null_command(f.voice_a);
        assert(edge_flags(f.voice_a)==0x20);
        set_voice_state(f.voice_a,2); assert(edge_flags(f.voice_a)==0);
    } else if (!strcmp(name,"descriptor_control_boundaries")) {
        /* Unknown metadata stays unresolved; inconsistent sample bounds fault;
         * a positive one-sample range is now implemented and completes. */
        const unsigned offsets[]={0,8,12,16,20,24};
        for (unsigned i=0;i<sizeof(offsets)/sizeof(offsets[0]);++i) {
            set_voice_state(f.voice_a,2); set_voice_state(f.voice_a,0);
            unsigned char descriptor[0x20]={0},cmd[0x20];
            put_u32(descriptor,4,sizeof(data)); put_u32(descriptor,offsets[i],1);
            command(cmd,sizeof(cmd),0x10000001);
            put_uptr(cmd,8,(uintptr_t)data); put_u32(cmd,16,1); put_u32(cmd,20,1);
            put_uptr(cmd,24,(uintptr_t)descriptor);
            assert(!ngs2_voice_control(f.voice_a,cmd)); enqueue_null_command(f.voice_a);
            if (offsets[i]==8 || offsets[i]==12) {
                pthread_mutex_lock(&ngs2_lock);
                while (!v->atrac9->faulted) pthread_cond_wait(&ngs2_cond,&ngs2_lock);
                assert(!v->atrac9->pcm_count);
                pthread_mutex_unlock(&ngs2_lock);
                assert((edge_flags(f.voice_a)&0x20)==0);
            } else {
                drain_voice(&f,v);
                assert(edge_flags(f.voice_a)==(offsets[i]==16 ? 0x20u : 3u));
            }
        }
    } else if (!strcmp(name,"cancel_during_snapshot_then_reuse")) {
        set_voice_state(f.voice_a,0);
        pthread_mutex_lock(&mock_lock); snapshot_block=1; snapshot_entered=0; pthread_mutex_unlock(&mock_lock);
        EnqueueThreadParam param={f.voice_a,data,sizeof(data),0}; pthread_t th;
        assert(!pthread_create(&th,NULL,thread_enqueue_worker,&param));
        pthread_mutex_lock(&mock_lock);
        while (!snapshot_entered) pthread_cond_wait(&mock_cond,&mock_lock);
        pthread_mutex_unlock(&mock_lock);
        enqueue_null_command(f.voice_a); set_voice_state(f.voice_a,3);
        set_voice_state(f.voice_a,0);
        pthread_mutex_lock(&mock_lock); snapshot_block=0; pthread_cond_broadcast(&mock_cond); pthread_mutex_unlock(&mock_lock);
        assert(!pthread_join(th,NULL)); assert(param.done);
        assert(edge_flags(f.voice_a)==3);
        edge_complete(&f);
    } else if (!strcmp(name,"input_failure_not_natural_completion")) {
        set_voice_state(f.voice_a,0); fail_snapshot=1;
        enqueue_segment(f.voice_a,data,sizeof(data)); fail_snapshot=0;
        enqueue_null_command(f.voice_a); assert(edge_flags(f.voice_a)==3);
        set_voice_state(f.voice_a,2); set_voice_state(f.voice_a,0);
        pthread_mutex_lock(&mock_lock); fail_decode=1; pthread_mutex_unlock(&mock_lock);
        enqueue_segment(f.voice_a,data,sizeof(data)); enqueue_null_command(f.voice_a);
        pthread_mutex_lock(&ngs2_lock);
        while (!v->atrac9->faulted) pthread_cond_wait(&ngs2_cond,&ngs2_lock);
        pthread_mutex_unlock(&ngs2_lock);
        assert((edge_flags(f.voice_a)&0x20)==0);
        pthread_mutex_lock(&mock_lock); fail_decode=0; pthread_mutex_unlock(&mock_lock);
    } else if (!strcmp(name,"hevag_terminal_drain")) {
        unsigned char cmd[0x28]; command(cmd,sizeof(cmd),0x10000000);
        put_u32(cmd,8,NGS2_WAVEFORM_HEVAG); put_u32(cmd,12,1); put_u32(cmd,16,48000);
        assert(!ngs2_voice_control(f.voice_a,cmd)); set_voice_state(f.voice_a,0);
        unsigned char frame[16]={0}; frame[1]=1;
        enqueue_segment(f.voice_a,frame,sizeof(frame));
        pthread_mutex_lock(&ngs2_lock);
        while (!v->atrac9->hevag.terminal_seen) pthread_cond_wait(&ngs2_cond,&ngs2_lock);
        assert(v->atrac9->pcm_count>0);
        pthread_mutex_unlock(&ngs2_lock);
        assert(edge_flags(f.voice_a)==3); drain_voice(&f,v);
        assert(edge_flags(f.voice_a)==0x20);
    } else {
        assert(!"unknown supplemental case");
    }
    close_fixture(&f); printf("PASS_COMPLETION_EDGE %s\n",name);
}
int main(int argc,char **argv) {
    assert(argc==2); setvbuf(stdout,NULL,_IONBF,0);
    ngs2_atrac9_setting=ngs2_render_mix_setting=1;
    ngs2_b2f2a_setting=ngs2_b2f2c_setting=0;
    atomic_store(&ngs2_audio_diag_setting,0); atomic_store(&ngs2_route_diag_setting,0);
    run_edge(argv[1]); return 0;
}
