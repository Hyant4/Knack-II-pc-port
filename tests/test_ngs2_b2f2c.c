#define _GNU_SOURCE
#include "native_test_platform.h"
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../port/src/runtime_tlg.c"
void runtime_restart(void){abort();}
int64_t runtime_file_getdents(int32_t fd,char *p,uint64_t n,int64_t *o){(void)fd;(void)p;(void)n;(void)o;return -1;}
void runtime_wait_begin(const char*a,const void*b,const char*c,const void*const*d,uintptr_t e){(void)a;(void)b;(void)c;(void)d;(void)e;}
void runtime_wait_end(void){}
const char *runtime_symbol(const char*s){return s;}
uintptr_t runtime_lookup(const RuntimeExport*t,size_t n,const char*s){(void)t;(void)n;(void)s;return 0;}
int runtime_memory_copy_snapshot(void*d,uintptr_t a,uint64_t n){if(!d||!a)return 0;memcpy(d,(const void*)a,(size_t)n);return 1;}

static Ngs2System *system_test;
static Ngs2Rack *rack_test;
static Ngs2Atrac9Voice *states;
static void gain(Ngs2Voice *v,float f) {
    uint32_t bits;memcpy(&bits,&f,sizeof(bits));
    v->descriptor_words=8;memset(v->descriptors,0,sizeof(v->descriptors));v->descriptors[0]=bits;
}
static void setup(uint32_t count,uint32_t zero_count,int pilot,int diag) {
    assert(count>0 && zero_count<=count);
    system_test=calloc(1,sizeof(*system_test));rack_test=calloc(1,sizeof(*rack_test));
    states=calloc(count,sizeof(*states));rack_test->voices=calloc(count,sizeof(*rack_test->voices));
    assert(system_test&&rack_test&&states&&rack_test->voices);
    system_test->handle=(Ngs2Handle*)(uintptr_t)0x100000;
    system_test->render_mix_enabled=system_test->atrac9_enabled=1;
    system_test->racks=rack_test;ngs2_systems=system_test;
    rack_test->system=system_test;rack_test->handle=(Ngs2Handle*)(uintptr_t)0x200000;
    rack_test->rack_id=0x1000;rack_test->instance=1;rack_test->capacity=count;
    for(uint32_t i=0;i<count;++i){
        Ngs2Voice*v=&rack_test->voices[i];Ngs2Atrac9Voice*st=&states[i];
        v->rack=rack_test;v->handle=(Ngs2Handle*)(uintptr_t)(0x400000+i*16);
        v->index=i;v->internal_state=NGS2_VOICE_PLAYING;v->atrac9=st;
        v->waveform=(Ngs2Waveform){.valid=1,.format=NGS2_WAVEFORM_HEVAG,.channels=1,.sample_rate=48000};
        gain(v,i<zero_count?0.f:1.f);
        st->generation=1;st->pcm_count=1;st->fifo[0].generation=1;
        st->fifo[0].channels=1;st->fifo[0].frames=256;
        for(uint32_t f=0;f<256;++f)st->fifo[0].pcm[f]=1000;
    }
    ngs2_atrac9_setting=1;ngs2_render_mix_setting=1;
    ngs2_b2f2a_setting=0;ngs2_b2f2c_setting=pilot;
    atomic_store(&ngs2_audio_diag_setting,diag);
    atomic_store(&ngs2_render_owner,0);
}
static void teardown(void) {
    assert(ngs2_render_owner==0);
    ngs2_systems=NULL;
    free(rack_test->voices);free(states);free(rack_test);free(system_test);
}
static float render_one(void) {
    float master[256*8],other[256*8];
    Ngs2RenderBuffer buffers[]={ {master,sizeof(master),0x18,8}, {other,sizeof(other),0x18,8} };
    assert(!ngs2_render(system_test->handle,buffers,2));
    for(uint32_t s=0;s<256*8;++s)assert(isfinite(master[s]));
    return master[0];
}
static void test_40_mixed_on_off(void) {
    setup(40,16,0,1);
    float baseline=render_one();
    assert(system_test->grains_consumed==40 && system_test->virtualized_grains==0);
    assert(system_test->diag_f2b_silent_mixed==16);
    assert(system_test->diag_f2b_audible_virtualized==0);
    assert(system_test->diag_f2b_wasted_quanta==0);
    assert(system_test->diag_f2c_bypassed==0);
    teardown();
    setup(40,16,1,1);
    float bypass=render_one();
    assert(system_test->grains_consumed==24 && system_test->virtualized_grains==16);
    assert(system_test->diag_f2b_silent_mixed==0);
    assert(system_test->diag_f2b_audible_virtualized==0);
    assert(system_test->diag_f2b_wasted_quanta==0);
    assert(system_test->diag_f2c_bypassed==16 && system_test->diag_f2c_bypass_quanta==1);
    for(uint32_t i=0;i<40;++i)assert(states[i].pcm_count==0);
    assert(fabsf(bypass-24.f*1000.f/32768.f*0.25f)<0.00002f);
    assert(bypass==baseline);
    teardown();
}
static void test_zero_only_and_diag_off(void) {
    setup(40,40,1,1);
    assert(render_one()==0.f);
    assert(system_test->grains_consumed==0 && system_test->virtualized_grains==40);
    assert(system_test->render_voice_cursor==0);
    assert(system_test->diag_f2c_bypassed==40);
    for(uint32_t i=0;i<40;++i)assert(states[i].pcm_count==0);
    teardown();
    setup(40,16,1,0);
    float value=render_one();
    assert(system_test->grains_consumed==24 && system_test->virtualized_grains==16);
    assert(system_test->diag_f2c_bypassed==0 && system_test->diag_f2b_silent_mixed==0);
    teardown();
    setup(40,16,1,1);
    assert(render_one()==value);
    teardown();
}
static void test_overload_and_matrix_limits(void) {
    setup(49,16,1,1); /* All 33 audible sources mix; only the 16 zero matrices bypass. */
    assert(render_one()>0.f);
    assert(system_test->grains_consumed==33 && system_test->virtualized_grains==16);
    assert(system_test->diag_f2b_audible_virtualized==0);
    assert(system_test->diag_f2c_bypassed==16);
    assert(system_test->diag_f2b_wasted_quanta==0);
    teardown();
    setup(40,16,1,1);
    /* A non-zero matrix coefficient outside the 8*channels effective area cannot contribute. */
    Ngs2Voice *v=&rack_test->voices[0];
    v->descriptor_words=9;
    union{float f;uint32_t u;} cv={.f=1.f};v->descriptors[8]=cv.u;
    assert(!ngs2_b2f2b_matrix_audible_locked(v));
    /* Inf/NaN/nonfinite and out-of-range values are ignored by both classifier and mixer. */
    cv.f=INFINITY;v->descriptors[0]=cv.u;
    assert(!ngs2_b2f2b_matrix_audible_locked(v));
    cv.f=65.f;v->descriptors[0]=cv.u;
    assert(!ngs2_b2f2b_matrix_audible_locked(v));
    assert(render_one()>0.f);
    assert(system_test->grains_consumed==24);
    teardown();
}
static void invalidate_mid_mix(void *ctx) {(void)ctx;++states[16].generation;}
static void test_revalidation_and_rollback(void) {
    setup(40,16,1,1);
    ngs2_set_render_mix_hook(invalidate_mid_mix,NULL);
    assert(render_one()==0.f);
    ngs2_set_render_mix_hook(NULL,NULL);
    assert(system_test->render_cancellations==1);
    assert(system_test->grains_consumed==0 && system_test->virtualized_grains==0);
    assert(system_test->diag_f2c_bypassed==0);
    for (uint32_t i=0;i<40;++i) assert(states[i].pcm_count==1);
    teardown();
    setup(16,8,0,0);
    float old=render_one();
    teardown();
    setup(16,8,1,0);
    float now=render_one();
    assert(old==now);
    assert(system_test->grains_consumed==8 && system_test->virtualized_grains==8);
    teardown();
}
int main(void) {
    test_40_mixed_on_off();test_zero_only_and_diag_off();test_overload_and_matrix_limits();test_revalidation_and_rollback();
    puts("PASS: B2-F2C zero-matrix bypass opt-in, 40 voices, zero-only, overflow, diag independence");
    return 0;
}
