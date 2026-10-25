/* SPDX-License-Identifier: GPL-2.0-or-later
 * Synthetic NGS2 HLE-2.2A1 integration test. Encoded fixture path is supplied externally.
 */
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

static void put_u16(void *data,size_t offset,uint16_t value) {
    memcpy((unsigned char *)data+offset,&value,sizeof(value));
}
static void put_u32(void *data,size_t offset,uint32_t value) {
    memcpy((unsigned char *)data+offset,&value,sizeof(value));
}
static void put_uptr(void *data,size_t offset,uintptr_t value) {
    memcpy((unsigned char *)data+offset,&value,sizeof(value));
}
static void command(void *data,uint16_t size,uint32_t id) {
    memset(data,0,size); put_u16(data,0,size); put_u32(data,4,id);
}
static void system_option(unsigned char option[0x40]) {
    memset(option,0,0x40); put_u32(option,0,0x40); put_u32(option,0x20,256); put_u32(option,0x24,48000);
}
static void rack_option(unsigned char option[0xa8],uint32_t capacity) {
    memset(option,0,0xa8); put_u32(option,0,0xa8); put_u32(option,0x20,capacity);
}
static void waveform(Ngs2Handle *voice) {
    unsigned char cmd[0x28]; command(cmd,sizeof(cmd),UINT32_C(0x10000000));
    put_u32(cmd,8,0x40); put_u32(cmd,0x0c,4); put_u32(cmd,0x10,48000);
    put_u32(cmd,0x14,UINT32_C(0xfe7a17f0));
    assert(!ngs2_voice_control(voice,cmd));
}
static void state_command(Ngs2Handle *voice,uint32_t event) {
    unsigned char cmd[0x0c]; command(cmd,sizeof(cmd),6); put_u32(cmd,8,event);
    assert(!ngs2_voice_control(voice,cmd));
}
static void segment(Ngs2Handle *voice,const unsigned char *data,uint32_t bytes,uint32_t marker) {
    unsigned char descriptor[0x20],cmd[0x20]; memset(descriptor,0,sizeof(descriptor));
    put_u32(descriptor,0,marker); put_u32(descriptor,4,bytes);
    command(cmd,sizeof(cmd),UINT32_C(0x10000001)); put_uptr(cmd,8,(uintptr_t)data);
    put_u32(cmd,0x10,marker); put_uptr(cmd,0x18,(uintptr_t)descriptor);
    assert(!ngs2_voice_control(voice,cmd));
}
static uint64_t hash_grain(uint64_t hash,const Ngs2PcmGrain *grain) {
    size_t count=(size_t)grain->frames*grain->channels;
    for (size_t i=0;i<count;++i) {
        uint16_t sample=(uint16_t)grain->pcm[i];
        hash=(hash^(unsigned char)sample)*UINT64_C(1099511628211);
        hash=(hash^(unsigned char)(sample>>8))*UINT64_C(1099511628211);
    }
    return hash;
}
static int drain_pair(Ngs2Handle *left,Ngs2Handle *right,uint64_t target,
                      uint64_t *left_hash,uint64_t *right_hash) {
    for (unsigned spin=0;spin<10000;++spin) {
        pthread_mutex_lock(&ngs2_lock);
        Ngs2Voice *voices[2]={ngs2_voice_locked(left),ngs2_voice_locked(right)};
        uint64_t *hashes[2]={left_hash,right_hash}; int done=1;
        for (unsigned v=0;v<2;++v) {
            assert(voices[v] && voices[v]->atrac9); Ngs2Atrac9Voice *state=voices[v]->atrac9;
            while (state->pcm_count) {
                *hashes[v]=hash_grain(*hashes[v],&state->fifo[state->pcm_read]);
                state->pcm_read=(state->pcm_read+1u)%NGS2_ATRAC9_FIFO_CAPACITY; --state->pcm_count;
            }
            if (state->decoded_frames<target || state->active_valid || state->segment_count ||
                state->decode_inflight) done=0;
        }
        pthread_cond_broadcast(&ngs2_cond); pthread_mutex_unlock(&ngs2_lock);
        if (done) return 1;
        Sleep(1);
    }
    return 0;
}

int main(int argc,char **argv) {
    assert(argc==2);
    FILE *file=fopen(argv[1],"rb"); assert(file);
    assert(!fseek(file,0,SEEK_END)); long length=ftell(file); assert(length==6144);
    assert(!fseek(file,0,SEEK_SET)); unsigned char *encoded=malloc((size_t)length); assert(encoded);
    assert(fread(encoded,1,(size_t)length,file)==(size_t)length); fclose(file);

    unsigned char sysopt[0x40],rackopt[0xa8]; system_option(sysopt); rack_option(rackopt,3);
    Ngs2Handle *system=NULL,*rack=NULL,*voices[3]={0};
    assert(!ngs2_system_create(sysopt,NULL,&system));
    assert(!ngs2_rack_create(system,0x1000,rackopt,NULL,&rack));
    for (uint32_t i=0;i<3;++i) { assert(!ngs2_voice_handle(rack,i,&voices[i])); waveform(voices[i]); }

    segment(voices[0],encoded,(uint32_t)length,11);
    segment(voices[1],encoded,(uint32_t)length,12);
    state_command(voices[0],0); state_command(voices[1],0);
    uint64_t hash0=UINT64_C(1469598103934665603),hash1=hash0;
    assert(drain_pair(voices[0],voices[1],32,&hash0,&hash1));
    assert(hash0==UINT64_C(0xa79abed856a64db5) && hash1==hash0);
    pthread_mutex_lock(&ngs2_lock);
    Ngs2Atrac9Voice *state0=ngs2_voice_locked(voices[0])->atrac9;
    Ngs2Atrac9Voice *state1=ngs2_voice_locked(voices[1])->atrac9;
    assert(state0->decoder && state1->decoder && state0->decoder!=state1->decoder);
    assert(state0->decoded_bytes==(uint64_t)length && state1->decoded_bytes==(uint64_t)length);
    pthread_mutex_unlock(&ngs2_lock);

    for (uint32_t i=0;i<NGS2_ATRAC9_EVENT_CAPACITY+1u;++i)
        segment(voices[2],encoded,768,100+i);
    pthread_mutex_lock(&ngs2_lock);
    Ngs2Atrac9Voice *bounded=ngs2_voice_locked(voices[2])->atrac9;
    assert(bounded->segment_count==NGS2_ATRAC9_EVENT_CAPACITY);
    assert(bounded->events_enqueued==NGS2_ATRAC9_EVENT_CAPACITY && bounded->events_rejected==1);
    pthread_mutex_unlock(&ngs2_lock);
    state_command(voices[2],2);

    unsigned char output[1024]; memset(output,0xa5,sizeof(output));
    Ngs2RenderBuffer render={output,sizeof(output),0,1}; assert(!ngs2_render(system,&render,1));
    for (size_t i=0;i<sizeof(output);++i) assert(!output[i]);
    unsigned char external_state[0x30]; memset(external_state,0xa5,sizeof(external_state));
    assert(!ngs2_voice_state(voices[0],external_state,sizeof(external_state)));
    uint32_t flags; memcpy(&flags,external_state,sizeof(flags)); assert(flags==3);
    for (size_t i=sizeof(flags);i<sizeof(external_state);++i) assert(!external_state[i]);

    assert(!ngs2_system_destroy(system,NULL)); free(encoded);
    assert(!ngs2_compressed_bytes);
    puts("PASS: NGS2 HLE-2.2A1 continuity, independent voices, duplicate events, bounds and silent render");
    return 0;
}
