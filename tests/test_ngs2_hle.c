/* SPDX-License-Identifier: GPL-2.0-or-later
 * Synthetic NGS2 HLE-1 tests. No game executable, audio asset or device is used.
 */
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
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

static void put_u16(void *data,size_t offset,uint16_t value) {
    memcpy((unsigned char *)data+offset,&value,sizeof(value));
}
static void put_u32(void *data,size_t offset,uint32_t value) {
    memcpy((unsigned char *)data+offset,&value,sizeof(value));
}
static void put_uptr(void *data,size_t offset,uintptr_t value) {
    memcpy((unsigned char *)data+offset,&value,sizeof(value));
}
static void put_float(void *data,size_t offset,float value) {
    memcpy((unsigned char *)data+offset,&value,sizeof(value));
}
static void command(void *data,uint16_t size,uint16_t sub_target,uint32_t id) {
    memset(data,0,size); put_u16(data,0,size); put_u16(data,2,sub_target); put_u32(data,4,id);
}
static void system_option(unsigned char option[0x40]) {
    memset(option,0,0x40); put_u32(option,0,0x40); put_u32(option,0x20,256); put_u32(option,0x24,48000);
}
static void rack_option(unsigned char option[0xa8],uint32_t capacity,const char *name) {
    memset(option,0,0xa8); put_u32(option,0,0xa8); put_u32(option,0x20,capacity);
    snprintf((char *)option+8,16,"%s",name);
}
static Ngs2Voice *voice_object(Ngs2Handle *handle) {
    pthread_mutex_lock(&ngs2_lock);
    Ngs2Voice *voice=ngs2_voice_locked(handle);
    pthread_mutex_unlock(&ngs2_lock);
    assert(voice); return voice;
}

typedef struct { Ngs2Handle *rack,*expected; int failed; } ThreadData;
static void *same_handle_thread(void *opaque) {
    ThreadData *data=opaque;
    for (int i=0;i<1000;++i) {
        Ngs2Handle *handle=NULL;
        if (ngs2_voice_handle(data->rack,3,&handle) || handle!=data->expected) data->failed=1;
    }
    return NULL;
}

int main(void) {
    unsigned char sysopt[0x40],rackopt[0xa8]; system_option(sysopt);
    Ngs2Handle *system=NULL;
    assert(!ngs2_system_create(sysopt,(void *)(uintptr_t)0x1234,&system) && system);
    pthread_mutex_lock(&ngs2_lock);
    Ngs2System *system_object=ngs2_system_locked(system);
    assert(system_object && system_object->grain_samples==256 && system_object->sample_rate==48000);
    assert(!system_object->atrac9_enabled && !system_object->worker_started);
    pthread_mutex_unlock(&ngs2_lock);

    rack_option(rackopt,4,"Sampler");
    Ngs2BufferInfo info; memset(&info,0xa5,sizeof(info));
    assert(!ngs2_query_buffer(0x1000,rackopt,&info) && info.host_size==4096);
    Ngs2Handle *rack1=NULL,*rack2=NULL;
    assert(!ngs2_rack_create(system,0x1000,rackopt,NULL,&rack1));
    assert(!ngs2_rack_create(system,0x1000,rackopt,NULL,&rack2));
    assert(rack1 && rack2 && rack1!=rack2);

    Ngs2Handle *voice1=NULL,*voice1_again=NULL,*voice2=NULL,*invalid=(Ngs2Handle *)(uintptr_t)1;
    assert(!ngs2_voice_handle(rack1,3,&voice1));
    assert(!ngs2_voice_handle(rack1,3,&voice1_again) && voice1_again==voice1);
    assert(!ngs2_voice_handle(rack2,3,&voice2) && voice2!=voice1);
    assert(ngs2_voice_handle(rack1,4,&invalid)==NGS2_INVALID && !invalid);
    assert(ngs2_voice_handle((Ngs2Handle *)(uintptr_t)0x1111,0,&invalid)==NGS2_INVALID);

    ThreadData thread_data={rack1,voice1,0}; pthread_t threads[8];
    for (size_t i=0;i<8;++i) assert(!pthread_create(&threads[i],NULL,same_handle_thread,&thread_data));
    for (size_t i=0;i<8;++i) assert(!pthread_join(threads[i],NULL));
    assert(!thread_data.failed);

    unsigned char cmd[0x40]; uint32_t descriptors[16];
    for (uint32_t i=0;i<16;++i) descriptors[i]=0x100+i;
    command(cmd,0x18,0,1); put_u32(cmd,0x0c,16); put_uptr(cmd,0x10,(uintptr_t)descriptors);
    assert(!ngs2_voice_control(voice1,cmd)); descriptors[0]=0xffffffff;
    Ngs2Voice *object=voice_object(voice1);
    assert(object->descriptor_words==16 && object->descriptors[0]==0x100 && object->descriptors[15]==0x10f);
    command(cmd,0x18,0,1); put_u32(cmd,0x0c,65); put_uptr(cmd,0x10,(uintptr_t)descriptors);
    assert(ngs2_voice_control(voice1,cmd)==NGS2_INVALID);
    command(cmd,0x17,0,1); assert(ngs2_voice_control(voice1,cmd)==NGS2_INVALID);

    command(cmd,0x10,7,2); put_u32(cmd,8,2); put_float(cmd,0x0c,0.5f);
    assert(!ngs2_voice_control(voice1,cmd));
    command(cmd,0x10,9,3); put_u32(cmd,8,4); put_float(cmd,0x0c,-0.25f);
    assert(!ngs2_voice_control(voice1,cmd));
    object=voice_object(voice1);
    assert(object->command2[2].valid && object->command2[2].sub_target==7 && object->command2[2].gain==0.5f);
    assert(object->command3[4].valid && object->command3[4].sub_target==9 && object->command3[4].gain==-0.25f);
    command(cmd,0x10,0,2); put_u32(cmd,8,8); put_float(cmd,0x0c,1.0f);
    assert(ngs2_voice_control(voice1,cmd)==NGS2_INVALID);

    command(cmd,0x18,0,5); put_u32(cmd,8,2); put_u32(cmd,0x0c,6); put_uptr(cmd,0x10,(uintptr_t)voice2);
    assert(!ngs2_voice_control(voice1,cmd));
    object=voice_object(voice1);
    assert(object->connection==voice2 && object->connection_input==2 && object->connection_output==6);
    assert(!ngs2_rack_destroy(rack2,NULL));
    object=voice_object(voice1); assert(!object->connection);
    assert(ngs2_voice_control(voice2,cmd)==NGS2_INVALID);

    command(cmd,0x28,0,UINT32_C(0x10000000));
    put_u32(cmd,8,0x42); put_u32(cmd,0x0c,2); put_u32(cmd,0x10,48000); put_u32(cmd,0x14,0xfe800000);
    put_u32(cmd,0x18,0x1234); put_u32(cmd,0x1c,0x5678); put_u32(cmd,0x20,0x9abc);
    assert(!ngs2_voice_control(voice1,cmd));
    object=voice_object(voice1);
    assert(object->waveform.valid && object->waveform.format==0x42 && object->waveform.channels==2);
    assert(object->waveform.sample_rate==48000 && object->waveform.field_18==0x1234);
    command(cmd,0x28,0,UINT32_C(0x10000000)); put_u32(cmd,0x0c,0); put_u32(cmd,0x10,48000);
    assert(ngs2_voice_control(voice1,cmd)==NGS2_INVALID);

    command(cmd,0x10,0,UINT32_C(0x10000005)); put_float(cmd,8,1.25f);
    assert(!ngs2_voice_control(voice1,cmd) && voice_object(voice1)->pitch_ratio==1.25f);
    put_float(cmd,8,NAN); assert(ngs2_voice_control(voice1,cmd)==NGS2_INVALID);
    put_float(cmd,8,0.0f); assert(ngs2_voice_control(voice1,cmd)==NGS2_INVALID);

    const uint32_t events[]={0,4,5,1,2};
    const uint32_t states[]={NGS2_VOICE_PLAYING,NGS2_VOICE_PAUSED,NGS2_VOICE_PLAYING,
                             NGS2_VOICE_RELEASING,NGS2_VOICE_STOPPED};
    for (size_t i=0;i<5;++i) {
        command(cmd,0x0c,0,6); put_u32(cmd,8,events[i]); assert(!ngs2_voice_control(voice1,cmd));
        assert(voice_object(voice1)->internal_state==states[i]);
    }
    command(cmd,8,0,0xdeadbeef); assert(!ngs2_voice_control(voice1,cmd));
    command(cmd,7,0,6); assert(ngs2_voice_control(voice1,cmd)==NGS2_INVALID);
    assert(ngs2_voice_control(voice1,NULL)==NGS2_INVALID);

    command(cmd,0x0c,0,6); put_u32(cmd,8,0); assert(!ngs2_voice_control(voice1,cmd));
    unsigned char pcm[1024]; memset(pcm,0xa5,sizeof(pcm));
    Ngs2RenderBuffer render={pcm,sizeof(pcm),0,1};
    assert(!ngs2_render(system,&render,1));
    for (size_t i=0;i<sizeof(pcm);++i) assert(!pcm[i]);
    assert(voice_object(voice1)->internal_state==NGS2_VOICE_PLAYING);

    unsigned char guarded[0x32]; memset(guarded,0xa5,sizeof(guarded));
    assert(!ngs2_voice_state(voice1,guarded+1,0x10));
    assert(guarded[0]==0xa5 && guarded[0x11]==0xa5);
    for (size_t i=1;i<=0x10;++i) assert(!guarded[i]);
    memset(guarded,0xa5,sizeof(guarded));
    assert(!ngs2_voice_state(voice1,guarded+1,0x30));
    assert(guarded[0]==0xa5 && guarded[0x31]==0xa5);
    memset(guarded,0xa5,sizeof(guarded));
    assert(ngs2_voice_state(voice1,guarded+1,0x0f)==NGS2_INVALID && guarded[1]==0xa5);

    assert(!ngs2_rack_destroy(rack1,&info));
    assert(ngs2_voice_state(voice1,guarded+1,0x10)==NGS2_INVALID);
    assert(ngs2_voice_handle(rack1,0,&invalid)==NGS2_INVALID);
    assert(!ngs2_system_destroy(system,&info));
    assert(ngs2_render(system,NULL,0)==NGS2_INVALID);
    assert(ngs2_system_destroy(system,NULL)==NGS2_INVALID);

    Ngs2Handle *previous=system;
    for (int i=0;i<100;++i) {
        Ngs2Handle *temporary=NULL;
        assert(!ngs2_system_create(sysopt,NULL,&temporary) && temporary!=previous);
        previous=temporary;
        assert(!ngs2_system_destroy(temporary,NULL));
    }
    Ngs2Handle *system2=NULL,*rack3=NULL,*voice3=NULL;
    assert(!ngs2_system_create(sysopt,NULL,&system2));
    assert(!ngs2_rack_create(system2,0x1000,rackopt,NULL,&rack3));
    assert(!ngs2_voice_handle(rack3,0,&voice3));
    assert(!ngs2_system_destroy(system2,NULL));
    assert(ngs2_voice_state(voice3,guarded+1,0x10)==NGS2_INVALID);
    assert(ngs2_voice_handle(rack3,0,&invalid)==NGS2_INVALID);
    puts("PASS: NGS2 HLE-1 handles, racks, commands, state, bounds, concurrency and silent render");
    return 0;
}
