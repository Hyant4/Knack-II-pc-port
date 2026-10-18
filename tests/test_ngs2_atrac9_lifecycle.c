/* SPDX-License-Identifier: GPL-2.0-or-later
 * Deterministic blocking mocks for NGS2 enqueue/decode cancellation and destruction.
 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../port/src/runtime_tlg.c"

struct RuntimeAtrac9 { unsigned frame; };
static pthread_mutex_t mock_lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t mock_cond=PTHREAD_COND_INITIALIZER;
static int snapshot_block,snapshot_entered,decode_block,decode_entered;

RuntimeAtrac9 *runtime_atrac9_create(void) { return calloc(1,sizeof(RuntimeAtrac9)); }
void runtime_atrac9_release(RuntimeAtrac9 *decoder) { free(decoder); }
int runtime_atrac9_init(RuntimeAtrac9 *decoder,const unsigned char config[4],Atrac9CodecInfo *info) {
    assert(decoder && !memcmp(config,"\xfe\x7a\x17\xf0",4)); memset(info,0,sizeof(*info));
    info->channels=4; info->samplingRate=48000; info->superframeSize=768;
    info->framesInSuperframe=4; info->frameSamples=256; return 0;
}
int runtime_atrac9_decode(RuntimeAtrac9 *decoder,const unsigned char *input,int16_t *pcm,int *used) {
    (void)input;
    pthread_mutex_lock(&mock_lock); decode_entered=1; pthread_cond_broadcast(&mock_cond);
    while (decode_block) pthread_cond_wait(&mock_cond,&mock_lock);
    pthread_mutex_unlock(&mock_lock);
    *used=decoder->frame%4u ? 191 : 195;
    for (unsigned i=0;i<256u*4u;++i) pcm[i]=(int16_t)(decoder->frame+i);
    ++decoder->frame; return 0;
}
int runtime_memory_copy_snapshot(void *dst,uintptr_t address,uint64_t size) {
    pthread_mutex_lock(&mock_lock); snapshot_entered=1; pthread_cond_broadcast(&mock_cond);
    while (snapshot_block) pthread_cond_wait(&mock_cond,&mock_lock);
    pthread_mutex_unlock(&mock_lock);
    if (!dst || !address || !size || size>SIZE_MAX) return 0;
    memcpy(dst,(const void *)address,(size_t)size); return 1;
}
void runtime_restart(void) { assert(!"Unexpected restart in NGS2 test"); }
int64_t runtime_file_getdents(int fd,char *buffer,uint64_t size,int64_t *base) {
    (void)fd; (void)buffer; (void)size; (void)base; return -1;
}
void runtime_wait_begin(const char *kind,const void *object,const char *name,
                        const void *const *owner,uintptr_t guest_frame) {
    (void)kind; (void)object; (void)name; (void)owner; (void)guest_frame;
}
void runtime_wait_end(void) {}
const char *runtime_symbol(const char *name) { return name; }
uintptr_t runtime_lookup(const RuntimeExport *table,size_t count,const char *name) {
    for (size_t i=0;i<count;++i) if (!strcmp(table[i].name,name)) return (uintptr_t)table[i].function;
    return 0;
}

static void put_u16(void *data,size_t offset,uint16_t value) { memcpy((char *)data+offset,&value,2); }
static void put_u32(void *data,size_t offset,uint32_t value) { memcpy((char *)data+offset,&value,4); }
static void put_uptr(void *data,size_t offset,uintptr_t value) { memcpy((char *)data+offset,&value,sizeof(value)); }
static void command(void *data,uint16_t size,uint32_t id) {
    memset(data,0,size); put_u16(data,0,size); put_u32(data,4,id);
}
static void options(unsigned char sys[0x40],unsigned char rack[0xa8]) {
    memset(sys,0,0x40); put_u32(sys,0,0x40); put_u32(sys,0x20,256); put_u32(sys,0x24,48000);
    memset(rack,0,0xa8); put_u32(rack,0,0xa8); put_u32(rack,0x20,1);
}
static void setup(Ngs2Handle **system,Ngs2Handle **rack,Ngs2Handle **voice) {
    unsigned char sysopt[0x40],rackopt[0xa8],cmd[0x28]; options(sysopt,rackopt);
    assert(!ngs2_system_create(sysopt,NULL,system)); assert(!ngs2_rack_create(*system,1,rackopt,NULL,rack));
    assert(!ngs2_voice_handle(*rack,0,voice)); command(cmd,sizeof(cmd),UINT32_C(0x10000000));
    put_u32(cmd,8,0x40); put_u32(cmd,0x0c,4); put_u32(cmd,0x10,48000);
    put_u32(cmd,0x14,UINT32_C(0xfe7a17f0)); assert(!ngs2_voice_control(*voice,cmd));
}
static void play_or_stop(Ngs2Handle *voice,uint32_t event) {
    unsigned char cmd[0x0c]; command(cmd,sizeof(cmd),6); put_u32(cmd,8,event);
    assert(!ngs2_voice_control(voice,cmd));
}
typedef struct { Ngs2Handle *voice; unsigned char *bytes; int done; } EnqueueData;
static void *enqueue_thread(void *opaque) {
    EnqueueData *data=opaque; unsigned char descriptor[0x20],cmd[0x20];
    memset(descriptor,0,sizeof(descriptor)); put_u32(descriptor,4,768);
    command(cmd,sizeof(cmd),UINT32_C(0x10000001)); put_uptr(cmd,8,(uintptr_t)data->bytes);
    put_uptr(cmd,0x18,(uintptr_t)descriptor); assert(!ngs2_voice_control(data->voice,cmd));
    pthread_mutex_lock(&mock_lock); data->done=1; pthread_cond_broadcast(&mock_cond);
    pthread_mutex_unlock(&mock_lock); return NULL;
}
typedef struct { Ngs2Handle *handle; int system,did_finish; } DestroyData;
static void *destroy_thread(void *opaque) {
    DestroyData *data=opaque;
    int32_t result=data->system ? ngs2_system_destroy(data->handle,NULL) : ngs2_rack_destroy(data->handle,NULL);
    assert(!result); pthread_mutex_lock(&mock_lock); data->did_finish=1;
    pthread_cond_broadcast(&mock_cond); pthread_mutex_unlock(&mock_lock); return NULL;
}
static void wait_flag(int *flag) {
    pthread_mutex_lock(&mock_lock);
    while (!*flag) pthread_cond_wait(&mock_cond,&mock_lock);
    pthread_mutex_unlock(&mock_lock);
}
static void enqueue_direct(Ngs2Handle *voice,unsigned char *bytes) {
    EnqueueData data={voice,bytes,0}; enqueue_thread(&data); assert(data.done);
}

int main(void) {
    unsigned char bytes[768]; memset(bytes,0x5a,sizeof(bytes));

    Ngs2Handle *system=NULL,*rack=NULL,*voice=NULL; setup(&system,&rack,&voice);
    enqueue_direct(voice,bytes);
    pthread_mutex_lock(&mock_lock); decode_block=1; decode_entered=0; pthread_mutex_unlock(&mock_lock);
    play_or_stop(voice,0); wait_flag(&decode_entered);
    unsigned char silent[64]; memset(silent,0xa5,sizeof(silent));
    Ngs2RenderBuffer render={silent,sizeof(silent),0,1}; assert(!ngs2_render(system,&render,1));
    for (size_t i=0;i<sizeof(silent);++i) assert(!silent[i]);
    play_or_stop(voice,5);
    pthread_mutex_lock(&mock_lock); decode_block=0; pthread_cond_broadcast(&mock_cond); pthread_mutex_unlock(&mock_lock);
    pthread_mutex_lock(&ngs2_lock);
    Ngs2Atrac9Voice *state=ngs2_voice_locked(voice)->atrac9;
    while (state->decode_inflight) pthread_cond_wait(&ngs2_cond,&ngs2_lock);
    assert(!state->pcm_count && !state->active_valid && !state->decoded_frames && state->cancellations==1);
    pthread_mutex_unlock(&ngs2_lock); assert(!ngs2_system_destroy(system,NULL));

    setup(&system,&rack,&voice); enqueue_direct(voice,bytes);
    pthread_mutex_lock(&mock_lock); decode_block=1; decode_entered=0; pthread_mutex_unlock(&mock_lock);
    play_or_stop(voice,0); wait_flag(&decode_entered);
    DestroyData decode_destroy={rack,0,0}; pthread_t destroyer;
    assert(!pthread_create(&destroyer,NULL,destroy_thread,&decode_destroy)); Sleep(20);
    pthread_mutex_lock(&mock_lock); assert(!decode_destroy.did_finish); decode_block=0;
    pthread_cond_broadcast(&mock_cond); pthread_mutex_unlock(&mock_lock);
    assert(!pthread_join(destroyer,NULL)); assert(decode_destroy.did_finish);
    assert(!ngs2_system_destroy(system,NULL));

    setup(&system,&rack,&voice);
    pthread_mutex_lock(&mock_lock); snapshot_block=1; snapshot_entered=0; pthread_mutex_unlock(&mock_lock);
    EnqueueData enqueue={voice,bytes,0}; pthread_t enqueuer;
    assert(!pthread_create(&enqueuer,NULL,enqueue_thread,&enqueue)); wait_flag(&snapshot_entered);
    DestroyData snapshot_destroy={rack,0,0};
    assert(!pthread_create(&destroyer,NULL,destroy_thread,&snapshot_destroy)); Sleep(20);
    pthread_mutex_lock(&mock_lock); assert(!snapshot_destroy.did_finish); snapshot_block=0;
    pthread_cond_broadcast(&mock_cond); pthread_mutex_unlock(&mock_lock);
    assert(!pthread_join(enqueuer,NULL)); assert(!pthread_join(destroyer,NULL));
    assert(enqueue.done && snapshot_destroy.did_finish && !ngs2_compressed_bytes);
    assert(!ngs2_system_destroy(system,NULL));

    puts("PASS: NGS2 HLE-2.2A1 cancel-during-decode and enqueue/destroy quiescence");
    return 0;
}
