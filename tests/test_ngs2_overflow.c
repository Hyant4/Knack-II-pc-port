/* SPDX-License-Identifier: GPL-2.0-or-later
 * HLE-2.2D-A: bounded overflow voices must never silence the master bus.
 * Compile in the project: clang -std=c11 -Wall -Wextra -Werror
 * -Iport/src -Iport tests/test_ngs2_overflow.c -pthread -lm -o overflow-test
 */
#include "native_test_platform.h"
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../port/src/runtime_tlg.c"

void runtime_restart(void) { abort(); }
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
    for (size_t i=0;i<count;++i)
        if (!strcmp(table[i].name,name)) return (uintptr_t)table[i].function;
    return 0;
}
int runtime_memory_copy_snapshot(void *dst,uintptr_t address,uint64_t size) {
    if (!dst || !address || !size || size>SIZE_MAX) return 0;
    memcpy(dst,(const void *)address,(size_t)size); return 1;
}
static void put16(void *dst,size_t off,uint16_t v) { memcpy((char *)dst+off,&v,2); }
static void put32(void *dst,size_t off,uint32_t v) { memcpy((char *)dst+off,&v,4); }
static void putptr(void *dst,size_t off,uintptr_t v) { memcpy((char *)dst+off,&v,sizeof(v)); }
static void make_cmd(void *dst,uint16_t size,uint32_t id) {
    memset(dst,0,size); put16(dst,0,size); put32(dst,4,id);
}
static void setup_voice(Ngs2Handle *handle,int hevag) {
    unsigned char cmd[0x28]; make_cmd(cmd,sizeof(cmd),0x10000000u);
    put32(cmd,8,hevag ? 0x1cu : 0x40u);
    put32(cmd,12,hevag ? 1u : 4u);
    put32(cmd,16,48000);
    put32(cmd,20,hevag ? 0u : 0xfe7a17f0u);
    assert(!ngs2_voice_control(handle,cmd));
    unsigned char play[0x0c]; make_cmd(play,sizeof(play),6);
    assert(!ngs2_voice_control(handle,play));
    float matrix[32]={0}; matrix[0]=0.05f;
    unsigned char gain[0x18]; make_cmd(gain,sizeof(gain),1);
    put32(gain,12,hevag ? 8u : 32u); putptr(gain,16,(uintptr_t)matrix);
    assert(!ngs2_voice_control(handle,gain));
}
static void add_grain(Ngs2Voice *voice,int16_t amplitude) {
    Ngs2Atrac9Voice *s=voice->atrac9;
    assert(s && s->pcm_count<NGS2_ATRAC9_FIFO_CAPACITY);
    Ngs2PcmGrain *g=&s->fifo[s->pcm_write];
    g->generation=s->generation; g->frames=256; g->channels=voice->waveform.channels;
    for (unsigned i=0;i<256u*g->channels;++i) g->pcm[i]=amplitude;
    s->pcm_write=(s->pcm_write+1u)%NGS2_ATRAC9_FIFO_CAPACITY;
    ++s->pcm_count;
}

typedef struct {
    Ngs2Handle *system,*rack,*voices[40];
    float master[256*8],aux[256*8];
    Ngs2RenderBuffer outputs[2];
} OverflowFixture;
static void fixture_open(OverflowFixture *f) {
    memset(f,0,sizeof(*f));
    ngs2_atrac9_setting=1; ngs2_render_mix_setting=1;
    atomic_store(&ngs2_audio_diag_setting,0);
    unsigned char sysopt[0x40]={0},rackopt[0xa8]={0};
    put32(sysopt,0,0x40); put32(sysopt,0x20,256); put32(sysopt,0x24,48000);
    put32(rackopt,0,0xa8); put32(rackopt,0x20,40);
    memcpy(rackopt+8,"Sampler",8);
    assert(!ngs2_system_create(sysopt,NULL,&f->system));
    assert(!ngs2_rack_create(f->system,0x1000,rackopt,NULL,&f->rack));
    for (unsigned i=0;i<40;++i) {
        assert(!ngs2_voice_handle(f->rack,i,&f->voices[i]));
        setup_voice(f->voices[i],(i&1u)!=0);
    }
    f->outputs[0]=(Ngs2RenderBuffer){f->master,sizeof(f->master),0x18,8};
    f->outputs[1]=(Ngs2RenderBuffer){f->aux,sizeof(f->aux),0x18,8};
}
static void fixture_push(OverflowFixture *f,unsigned n) {
    pthread_mutex_lock(&ngs2_lock);
    for (unsigned i=0;i<40;++i) {
        Ngs2Voice *v=ngs2_voice_locked(f->voices[i]);
        for (unsigned j=0;j<n;++j) add_grain(v,8192);
    }
    pthread_mutex_unlock(&ngs2_lock);
}
static void fixture_close(OverflowFixture *f) {
    assert(!ngs2_system_destroy(f->system,NULL));
}
static void render_once(OverflowFixture *f) {
    /* There should be no pending worker commands in this synthetic fixture. */
    uint64_t before=ngs2_system_locked(f->system)->main_renders;
    for (unsigned i=0;i<100;++i) {
        assert(!ngs2_render(f->system,f->outputs,2));
        pthread_mutex_lock(&ngs2_lock);
        uint64_t after=ngs2_system_locked(f->system)->main_renders;
        pthread_mutex_unlock(&ngs2_lock);
        if (after>before) return;
    }
    assert(!"Render could not acquire NGS2 gate");
}
static void test_40_voices(void) {
    OverflowFixture *f=calloc(1,sizeof(*f)); assert(f);
    fixture_open(f); fixture_push(f,2);
    render_once(f);
    assert(f->master[0]>0.0f);
    pthread_mutex_lock(&ngs2_lock);
    Ngs2System *sys=ngs2_system_locked(f->system);
    assert(sys->grains_consumed==40);
    assert(sys->virtualized_grains==0);
    assert(sys->render_limit_exceeded==1);
    for (unsigned i=0;i<40;++i) assert(ngs2_voice_locked(f->voices[i])->atrac9->pcm_count==1);
    pthread_mutex_unlock(&ngs2_lock);
    render_once(f);
    assert(f->master[0]>0.0f);
    pthread_mutex_lock(&ngs2_lock);
    sys=ngs2_system_locked(f->system);
    assert(sys->grains_consumed==80 && sys->virtualized_grains==0);
    for (unsigned i=0;i<40;++i) assert(ngs2_voice_locked(f->voices[i])->atrac9->pcm_count==0);
    pthread_mutex_unlock(&ngs2_lock);
    fixture_close(f); free(f);
    puts("PASS: 40 voices, two renders, non-silent, all 40 FIFOs advanced");
}
static Ngs2Handle *mutate_voice;
static void mutate_virtual(void *unused) {
    (void)unused;
    unsigned char stop[0x0c]; make_cmd(stop,sizeof(stop),6); put32(stop,8,2);
    assert(!ngs2_voice_control(mutate_voice,stop));
    make_cmd(stop,sizeof(stop),6);
    assert(!ngs2_voice_control(mutate_voice,stop));
    pthread_mutex_lock(&ngs2_lock);
    add_grain(ngs2_voice_locked(mutate_voice),777);
    pthread_mutex_unlock(&ngs2_lock);
}
static void test_virtual_generation(void) {
    OverflowFixture *f=calloc(1,sizeof(*f)); assert(f);
    fixture_open(f); fixture_push(f,1);
    mutate_voice=f->voices[39];
    /* Only a zero-matrix source may be virtualized in the normal path. */
    ngs2_b2f2c_setting=1;
    pthread_mutex_lock(&ngs2_lock);
    memset(ngs2_voice_locked(mutate_voice)->descriptors,0,
           sizeof(ngs2_voice_locked(mutate_voice)->descriptors));
    pthread_mutex_unlock(&ngs2_lock);
    ngs2_set_render_mix_hook(mutate_virtual,NULL);
    render_once(f);
    ngs2_set_render_mix_hook(NULL,NULL);
    pthread_mutex_lock(&ngs2_lock);
    Ngs2Voice *v=ngs2_voice_locked(mutate_voice);
    assert(v->atrac9->pcm_count==1);
    assert(v->atrac9->fifo[v->atrac9->pcm_read].pcm[0]==777);
    assert(ngs2_system_locked(f->system)->virtualized_grains==0);
    assert(ngs2_system_locked(f->system)->grains_consumed==39);
    pthread_mutex_unlock(&ngs2_lock);
    fixture_close(f); free(f);
    puts("PASS: overflow generation replaced during mix, new FIFO grain preserved");
}
static void test_selected_cancellation(void) {
    OverflowFixture *f=calloc(1,sizeof(*f)); assert(f);
    fixture_open(f); fixture_push(f,1);
    mutate_voice=f->voices[0];
    ngs2_set_render_mix_hook(mutate_virtual,NULL);
    render_once(f);
    ngs2_set_render_mix_hook(NULL,NULL);
    pthread_mutex_lock(&ngs2_lock);
    Ngs2System *sys=ngs2_system_locked(f->system);
    assert(sys->render_cancellations==1);
    assert(sys->grains_consumed==0 && sys->virtualized_grains==0);
    for (unsigned i=1;i<40;++i) assert(ngs2_voice_locked(f->voices[i])->atrac9->pcm_count==1);
    pthread_mutex_unlock(&ngs2_lock);
    assert(f->master[0]==0.0f);
    fixture_close(f); free(f);
    puts("PASS: selected generation cancellation aborts entire commit");
}
int main(void) {
    test_40_voices();
    test_virtual_generation();
    test_selected_cancellation();
    puts("ALL 3 HLE-2.2D-A OVERFLOW TESTS PASSED");
    return 0;
}
