/* Admission/accounting proof with synthetic bytes; no game data or real decoder. */
#define main overflow_fixture_original_main
#include "test_ngs2_overflow.c"
#undef main

static void freeze_worker(OverflowFixture *f) {
    pthread_mutex_lock(&ngs2_lock);
    Ngs2System *s=ngs2_system_locked(f->system);
    s->closing=1;pthread_cond_broadcast(&ngs2_cond);
    pthread_mutex_unlock(&ngs2_lock);
    if (s->worker_started) pthread_join(s->worker,NULL);
    pthread_mutex_lock(&ngs2_lock);
    s->worker_started=0;s->closing=0;
    atomic_store(&ngs2_audio_diag_setting,1);
    pthread_mutex_unlock(&ngs2_lock);
}
static void wave8(Ngs2Handle *voice) {
    unsigned char cmd[0x28];make_cmd(cmd,sizeof(cmd),0x10000000u);
    put32(cmd,8,0x40);put32(cmd,12,8);put32(cmd,16,48000);put32(cmd,20,0xfe7829f0u);
    assert(!ngs2_voice_control(voice,cmd));
}
static void submit(Ngs2Handle *voice,const void *data,uint32_t size) {
    unsigned char descriptor[0x20]={0},cmd[0x20];
    put32(descriptor,4,size);make_cmd(cmd,sizeof(cmd),0x10000001u);
    putptr(cmd,8,(uintptr_t)data);putptr(cmd,0x18,(uintptr_t)descriptor);
    assert(!ngs2_voice_control(voice,cmd));
}
static void stop(Ngs2Handle *voice) {
    unsigned char cmd[0x0c];make_cmd(cmd,sizeof(cmd),6);put32(cmd,8,2);
    assert(!ngs2_voice_control(voice,cmd));
}
static void expect_bytes(OverflowFixture *f,uint64_t bytes) {
    pthread_mutex_lock(&ngs2_lock);
    Ngs2System *s=ngs2_system_locked(f->system);
    assert(s->compressed_bytes==bytes && ngs2_compressed_bytes==bytes);
    pthread_mutex_unlock(&ngs2_lock);
}
int main(void) {
    const uint32_t mib=1024u*1024u;
    OverflowFixture *f=calloc(1,sizeof(*f));assert(f);fixture_open(f);freeze_worker(f);
    unsigned char *data=calloc(1,32u*mib);assert(data);
    Ngs2System *s=ngs2_system_locked(f->system);
    for (unsigned i=0;i<10;i+=2) wave8(f->voices[i]);
    const uint32_t sizes[]={16u*mib,17426304u,20298432u,32u*mib};
    for (unsigned i=0;i<sizeof(sizes)/sizeof(sizes[0]);++i) {
        uint64_t before=s->diag_segment_accepted;
        submit(f->voices[0],data,sizes[i]);
        assert(s->diag_segment_accepted==before+1);
        expect_bytes(f,sizes[i]);stop(f->voices[0]);expect_bytes(f,0);
    }
    puts("PASS 16 MiB, both long-dialogue sizes, 32 MiB admitted and released");
    uint64_t capacity=s->diag_reject_capacity;
    submit(f->voices[0],data,32u*mib+1);
    assert(s->diag_reject_capacity==capacity+1);expect_bytes(f,0);
    for (unsigned i=0;i<8;++i) submit(f->voices[0],data,1024);
    capacity=s->diag_reject_capacity;submit(f->voices[0],data,1024);
    assert(s->diag_reject_capacity==capacity+1);expect_bytes(f,8192);stop(f->voices[0]);
    puts("PASS segment ceiling and eight-event queue still enforced");
    submit(f->voices[0],data,20298432);
    uint64_t budget=s->diag_reject_budget;submit(f->voices[0],data,20298432);
    assert(s->diag_reject_budget==budget+1);expect_bytes(f,20298432);stop(f->voices[0]);
    for (unsigned i=0;i<8;i+=2) submit(f->voices[i],data,32u*mib);
    expect_bytes(f,128ull*mib);budget=s->diag_reject_budget;
    submit(f->voices[8],data,1);assert(s->diag_reject_budget==budget+1);expect_bytes(f,128ull*mib);
    for (unsigned i=0;i<8;i+=2) stop(f->voices[i]);
    expect_bytes(f,0);
    puts("PASS per-voice/global budgets and complete release");
    uint64_t copied=s->diag_reject_copy;
    submit(f->voices[0],NULL,1024);
    assert(s->diag_reject_copy==copied+1);expect_bytes(f,0);
    submit(f->voices[0],data,20298432);expect_bytes(f,20298432);
    /* Reconfiguration releases an admitted large segment. */
    setup_voice(f->voices[0],0);expect_bytes(f,0);
    submit(f->voices[0],data,20298432);expect_bytes(f,20298432);
    fixture_close(f);assert(ngs2_compressed_bytes==0);
    free(data);free(f);
    puts("PASS failed copy, reconfiguration and destruction release accounting");
    return 0;
}
