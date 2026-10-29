/* SPDX-License-Identifier: GPL-2.0-or-later
 * NGS2 HLE-2.2C HE-VAG reference, carry, termination, and lifecycle tests.
 */
#include "native_test_platform.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../port/src/runtime_tlg.c"

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
int runtime_memory_copy_snapshot(void *dst,uintptr_t address,uint64_t size) {
    if (!dst || !address || !size || size>SIZE_MAX) return 0;
    memcpy(dst,(const void *)address,(size_t)size); return 1;
}
static pthread_mutex_t hook_lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t hook_cond=PTHREAD_COND_INITIALIZER;
static int hook_block,hook_entered;
static void blocking_decode_hook(void *arg) {
    (void)arg; pthread_mutex_lock(&hook_lock); hook_entered=1; pthread_cond_broadcast(&hook_cond);
    while (hook_block) pthread_cond_wait(&hook_cond,&hook_lock);
    pthread_mutex_unlock(&hook_lock);
}
static void wait_hook(void) {
    pthread_mutex_lock(&hook_lock); while (!hook_entered) pthread_cond_wait(&hook_cond,&hook_lock);
    pthread_mutex_unlock(&hook_lock);
}
typedef struct { Ngs2Handle *system; int done; } DestroyData;
static void *destroy_system(void *opaque) {
    DestroyData *data=opaque; assert(!ngs2_system_destroy(data->system,NULL));
    pthread_mutex_lock(&hook_lock); data->done=1; pthread_cond_broadcast(&hook_cond);
    pthread_mutex_unlock(&hook_lock); return NULL;
}

static void put_u16(void *data,size_t offset,uint16_t value) { memcpy((char *)data+offset,&value,2); }
static void put_u32(void *data,size_t offset,uint32_t value) { memcpy((char *)data+offset,&value,4); }
static void put_uptr(void *data,size_t offset,uintptr_t value) {
    memcpy((char *)data+offset,&value,sizeof(value));
}
static void command(void *data,uint16_t size,uint32_t id) {
    memset(data,0,size); put_u16(data,0,size); put_u32(data,4,id);
}
static void configure_hevag(Ngs2Handle *voice) {
    unsigned char cmd[0x28]; command(cmd,sizeof(cmd),UINT32_C(0x10000000));
    put_u32(cmd,8,NGS2_WAVEFORM_HEVAG); put_u32(cmd,0x0c,1); put_u32(cmd,0x10,48000);
    assert(!ngs2_voice_control(voice,cmd));
}
static void set_state(Ngs2Handle *voice,uint32_t event) {
    unsigned char cmd[0x0c]; command(cmd,sizeof(cmd),6); put_u32(cmd,8,event);
    assert(!ngs2_voice_control(voice,cmd));
}
static void enqueue(Ngs2Handle *voice,const unsigned char *data,uint32_t bytes,uint32_t marker) {
    unsigned char descriptor[0x20],cmd[0x20]; memset(descriptor,0,sizeof(descriptor));
    put_u32(descriptor,0,marker); put_u32(descriptor,4,bytes);
    command(cmd,sizeof(cmd),UINT32_C(0x10000001)); put_uptr(cmd,8,(uintptr_t)data);
    put_u32(cmd,0x10,marker); put_uptr(cmd,0x18,(uintptr_t)descriptor);
    assert(!ngs2_voice_control(voice,cmd));
}
static void end_segments(Ngs2Handle *voice) {
    unsigned char cmd[0x20]; command(cmd,sizeof(cmd),UINT32_C(0x10000001));
    assert(!ngs2_voice_control(voice,cmd));
}
static void setup(Ngs2Handle **system,Ngs2Handle **rack,Ngs2Handle **voice,uint32_t capacity) {
    unsigned char sys[0x40],rackopt[0xa8]; memset(sys,0,sizeof(sys)); memset(rackopt,0,sizeof(rackopt));
    put_u32(sys,0,sizeof(sys)); put_u32(sys,0x20,256); put_u32(sys,0x24,48000);
    put_u32(rackopt,0,sizeof(rackopt)); put_u32(rackopt,0x20,capacity);
    assert(!ngs2_system_create(sys,NULL,system));
    assert(!ngs2_rack_create(*system,0x1000,rackopt,NULL,rack));
    assert(!ngs2_voice_handle(*rack,0,voice)); configure_hevag(*voice);
}
static void fill_frames(unsigned char *data,uint32_t frames,uint8_t index,uint8_t shift,uint8_t flag_last) {
    for (uint32_t f=0;f<frames;++f) {
        unsigned char *frame=data+f*NGS2_HEVAG_FRAME_BYTES;
        frame[0]=(unsigned char)(((index&0x0fu)<<4)|(shift&0x0fu));
        frame[1]=(unsigned char)((index&0x70u)|((f+1u==frames) ? flag_last : 0u));
        for (unsigned i=0;i<14;++i) frame[2+i]=(unsigned char)(0x13u+i*37u+f*11u);
    }
}
static Ngs2Atrac9Voice *wait_decoded(Ngs2Handle *voice,uint64_t frames,int idle) {
    for (unsigned spin=0;spin<10000;++spin) {
        pthread_mutex_lock(&ngs2_lock);
        Ngs2Voice *item=ngs2_voice_locked(voice); Ngs2Atrac9Voice *state=item ? item->atrac9 : NULL;
        int done=state && state->hevag.decoded_frames>=frames &&
                 (!idle || (!state->active_valid && !state->segment_count && !state->decode_inflight));
        pthread_mutex_unlock(&ngs2_lock);
        if (done) return state;
        Sleep(1);
    }
    assert(!"HEVAG worker timeout"); return NULL;
}
static uint64_t hash_pcm(const Ngs2Atrac9Voice *state) {
    uint64_t hash=UINT64_C(1469598103934665603);
    for (uint32_t g=0,index=state->pcm_read;g<state->pcm_count;++g,index=(index+1u)%NGS2_ATRAC9_FIFO_CAPACITY) {
        const Ngs2PcmGrain *grain=&state->fifo[index];
        for (uint32_t i=0;i<grain->frames*grain->channels;++i) {
            uint16_t sample=(uint16_t)grain->pcm[i];
            hash=(hash^(uint8_t)sample)*UINT64_C(1099511628211);
            hash=(hash^(uint8_t)(sample>>8))*UINT64_C(1099511628211);
        }
    }
    return hash;
}

static void test_reference_vectors(void) {
    static const int16_t expected29[28]={
        19274,-5593,-17542,-1865,8986,3006,-2299,-604,97,-1366,-420,1495,870,-777,
        -675,231,229,-68,44,159,-18,-211,-118,45,75,-3,9,14
    };
    static const int16_t expected127[28]={
        5550,6700,8602,6931,7516,7338,7003,6966,6782,6589,6447,6272,6133,5984,
        5867,5727,5574,5465,5329,5229,5121,4965,4841,4707,4594,4469,4383,4269
    };
    unsigned char frame[16]={0}; for (unsigned i=0;i<14;++i) frame[2+i]=(unsigned char)(0x13u+i*37u);
    int16_t actual[28]; int32_t history[4]={12000,-8000,4000,-2000}; uint8_t flag=0;
    frame[0]=(unsigned char)((29u<<4)|9u); frame[1]=(unsigned char)(29u&0x70u);
    ngs2_hevag_decode_frame(frame,history,actual,&flag);
    assert(!flag && !memcmp(actual,expected29,sizeof(actual)));
    assert(history[0]==14 && history[1]==9 && history[2]==-3 && history[3]==75);
    memcpy(history,(int32_t[4]){12000,-8000,4000,-2000},sizeof(history));
    frame[0]=(unsigned char)((127u<<4)|10u); frame[1]=(unsigned char)(127u&0x70u);
    ngs2_hevag_decode_frame(frame,history,actual,&flag);
    assert(!memcmp(actual,expected127,sizeof(actual)));
    assert(history[0]==4269 && history[1]==4383 && history[2]==4469 && history[3]==4594);
    frame[1]=(frame[1]&0xf0u)|7u; memset(actual,0xa5,sizeof(actual));
    ngs2_hevag_decode_frame(frame,history,actual,&flag); assert(flag==7u);
    for (unsigned i=0;i<28;++i) assert(actual[i]==0);
}

static void test_grains_and_segment_continuity(void) {
    unsigned char all[64u*16u],left[32u*16u],right[32u*16u]; fill_frames(all,64,29,9,0);
    memcpy(left,all,sizeof(left)); memcpy(right,all+sizeof(left),sizeof(right));
    Ngs2Handle *system=NULL,*rack=NULL,*voice0=NULL; setup(&system,&rack,&voice0,2);
    Ngs2Handle *voice1=NULL; assert(!ngs2_voice_handle(rack,1,&voice1)); configure_hevag(voice1);
    enqueue(voice0,all,sizeof(all),1); enqueue(voice1,left,sizeof(left),2); enqueue(voice1,right,sizeof(right),3);
    set_state(voice0,0); set_state(voice1,0);
    Ngs2Atrac9Voice *state0=wait_decoded(voice0,64,1),*state1=wait_decoded(voice1,64,1);
    pthread_mutex_lock(&ngs2_lock);
    assert(state0->pcm_count==7 && state1->pcm_count==7);
    assert(state0->hevag.carry_samples==0 && state1->hevag.carry_samples==0);
    uint64_t hash0=hash_pcm(state0),hash1=hash_pcm(state1); assert(hash0==hash1);
    assert(!memcmp(state0->hevag.history,state1->hevag.history,sizeof(state0->hevag.history)));
    pthread_mutex_unlock(&ngs2_lock);
    printf("HEVAG PCM evidence: 64 frames, 1792 samples, 7 grains, fnv64=%016llx\n",
           (unsigned long long)hash0);
    assert(!ngs2_system_destroy(system,NULL));
}

static void test_tail_lifecycle(void) {
    unsigned char first[16],second[16]; fill_frames(first,1,29,9,0); fill_frames(second,1,29,9,0);
    Ngs2Handle *system=NULL,*rack=NULL,*voice=NULL; setup(&system,&rack,&voice,1);
    enqueue(voice,first,sizeof(first),1); set_state(voice,0);
    Ngs2Atrac9Voice *state=wait_decoded(voice,1,1);
    pthread_mutex_lock(&ngs2_lock);
    assert(state->pcm_count==0 && state->hevag.carry_samples==28 && !state->hevag.terminal_seen);
    uint64_t generation=state->generation; pthread_mutex_unlock(&ngs2_lock);

    enqueue(voice,second,sizeof(second),2); state=wait_decoded(voice,2,1);
    pthread_mutex_lock(&ngs2_lock);
    assert(state->pcm_count==0 && state->hevag.carry_samples==56 && !state->hevag.terminal_seen);
    pthread_mutex_unlock(&ngs2_lock);
    end_segments(voice);
    for (unsigned spin=0;spin<10000;++spin) {
        pthread_mutex_lock(&ngs2_lock); state=ngs2_voice_locked(voice)->atrac9;
        int flushed=state->hevag.terminal_seen; pthread_mutex_unlock(&ngs2_lock);
        if (flushed) break;
        Sleep(1);
    }
    pthread_mutex_lock(&ngs2_lock);
    assert(state->pcm_count==1 && state->hevag.carry_samples==0 && state->hevag.terminal_seen);
    assert(state->hevag.tail_flushes==1 && state->fifo[state->pcm_read].frames==256);
    for (unsigned i=56;i<256;++i) assert(state->fifo[state->pcm_read].pcm[i]==0);
    pthread_mutex_unlock(&ngs2_lock);

    set_state(voice,2);
    pthread_mutex_lock(&ngs2_lock); state=ngs2_voice_locked(voice)->atrac9;
    assert(state->generation!=generation && !state->pcm_count && !state->hevag.carry_samples &&
           !state->hevag.terminal_seen);
    for (unsigned i=0;i<4;++i) assert(!state->hevag.history[i]);
    pthread_mutex_unlock(&ngs2_lock);

    configure_hevag(voice); enqueue(voice,first,sizeof(first),3); set_state(voice,0);
    state=wait_decoded(voice,3,1);
    pthread_mutex_lock(&ngs2_lock); assert(state->hevag.carry_samples==28); pthread_mutex_unlock(&ngs2_lock);
    assert(!ngs2_rack_destroy(rack,NULL)); assert(!ngs2_system_destroy(system,NULL));
    assert(!ngs2_compressed_bytes);
}

static void test_stop_generation_and_destroy_during_decode(void) {
    unsigned char frame[16]; fill_frames(frame,1,127,10,0);
    Ngs2Handle *system=NULL,*rack=NULL,*voice=NULL; setup(&system,&rack,&voice,1);
    pthread_mutex_lock(&hook_lock); hook_block=1; hook_entered=0; pthread_mutex_unlock(&hook_lock);
    ngs2_set_hevag_decode_hook(blocking_decode_hook,NULL);
    enqueue(voice,frame,sizeof(frame),1); set_state(voice,0); wait_hook();
    pthread_mutex_lock(&ngs2_lock); Ngs2Atrac9Voice *state=ngs2_voice_locked(voice)->atrac9;
    uint64_t generation=state->generation; assert(state->decode_inflight); pthread_mutex_unlock(&ngs2_lock);
    set_state(voice,2);
    pthread_mutex_lock(&hook_lock); hook_block=0; pthread_cond_broadcast(&hook_cond); pthread_mutex_unlock(&hook_lock);
    pthread_mutex_lock(&ngs2_lock);
    while (state->decode_inflight) pthread_cond_wait(&ngs2_cond,&ngs2_lock);
    assert(state->generation!=generation && !state->pcm_count && !state->hevag.carry_samples &&
           !state->hevag.decoded_frames && state->cancellations==1);
    pthread_mutex_unlock(&ngs2_lock); ngs2_set_hevag_decode_hook(NULL,NULL);
    assert(!ngs2_system_destroy(system,NULL));

    setup(&system,&rack,&voice,1);
    pthread_mutex_lock(&hook_lock); hook_block=1; hook_entered=0; pthread_mutex_unlock(&hook_lock);
    ngs2_set_hevag_decode_hook(blocking_decode_hook,NULL);
    enqueue(voice,frame,sizeof(frame),2); set_state(voice,0); wait_hook();
    DestroyData destroy={system,0}; pthread_t thread;
    assert(!pthread_create(&thread,NULL,destroy_system,&destroy)); Sleep(20);
    pthread_mutex_lock(&hook_lock); assert(!destroy.done); hook_block=0;
    pthread_cond_broadcast(&hook_cond); pthread_mutex_unlock(&hook_lock);
    assert(!pthread_join(thread,NULL)); assert(destroy.done); ngs2_set_hevag_decode_hook(NULL,NULL);
    assert(!ngs2_compressed_bytes);
}

int main(void) {
    test_reference_vectors();
    test_grains_and_segment_continuity();
    test_tail_lifecycle();
    test_stop_generation_and_destroy_during_decode();
    puts("PASS: NGS2 HLE-2.2C HEVAG reference, carry, terminal flush, Stop, generation and Destroy");
    return 0;
}
