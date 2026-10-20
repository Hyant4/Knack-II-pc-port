#define _GNU_SOURCE
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "../port/src/runtime_tlg.c"
void runtime_restart(void){abort();}
int64_t runtime_file_getdents(int32_t fd,char *b,uint64_t s,int64_t *p){(void)fd;(void)b;(void)s;(void)p;return -1;}
void runtime_wait_begin(const char*a,const void*b,const char*c,const void *const *d,uintptr_t e){(void)a;(void)b;(void)c;(void)d;(void)e;}
void runtime_wait_end(void){}
const char *runtime_symbol(const char*s){return s;}
uintptr_t runtime_lookup(const RuntimeExport*t,size_t n,const char*s){(void)t;(void)n;(void)s;return 0;}
int runtime_memory_copy_snapshot(void*d,uintptr_t a,uint64_t s){if(!d||!a)return 0;memcpy(d,(void*)a,(size_t)s);return 1;}

static Ngs2System *sys;
static Ngs2Rack *rack;
static Ngs2Atrac9Voice *states;
static uint32_t live=60;

static void setup(void){
  sys=calloc(1,sizeof(*sys));rack=calloc(1,sizeof(*rack));
  rack->voices=calloc(live,sizeof(*rack->voices));states=calloc(live,sizeof(*states));
  assert(sys&&rack&&states&&rack->voices);
  sys->handle=(Ngs2Handle*)(uintptr_t)0x800000;
  sys->render_mix_enabled=1;sys->racks=rack;ngs2_systems=sys;
  rack->system=sys;rack->handle=(Ngs2Handle*)(uintptr_t)0x900000;
  rack->rack_id=0x1000;rack->instance=3;rack->capacity=live;
  for(uint32_t i=0;i<live;i++){
    Ngs2Voice *v=&rack->voices[i];Ngs2Atrac9Voice *s=&states[i];
    v->rack=rack;v->handle=(Ngs2Handle*)(uintptr_t)(0xa00000+i*16);
    v->index=i;v->internal_state=NGS2_VOICE_PLAYING;v->atrac9=s;
    v->waveform=(Ngs2Waveform){.valid=1,.format=i<40?NGS2_WAVEFORM_ATRAC9:NGS2_WAVEFORM_HEVAG,
      .channels=i<40?2:1,.sample_rate=48000,.codec_config=i<40?1:0};
    s->generation=1;s->pcm_count=1;
    s->fifo[0].generation=1;s->fifo[0].channels=v->waveform.channels;
    s->fifo[0].frames=256;
  }
}
static void teardown(void){ngs2_systems=NULL;free(rack->voices);free(states);free(rack);free(sys);}
static unsigned count_codec(const Ngs2B2F2AKey *keys,unsigned n,uint32_t fmt){
  unsigned c=0;for(unsigned i=0;i<n;i++){
    Ngs2Voice *v=ngs2_voice_locked(keys[i].handle);
    assert(v&&v->atrac9->generation==keys[i].generation);
    c+=v->waveform.format==fmt;
  }return c;
}
static void test_cohort(void){
  setup();
  Ngs2B2F2AKey keys[32]={{0}},old[32]={{0}};
  uint32_t retained=0,n=ngs2_b2f2a_prepare_locked(sys,60,keys,&retained);
  assert(n==32&&retained==0);
  assert(count_codec(keys,n,NGS2_WAVEFORM_ATRAC9)==24);
  assert(count_codec(keys,n,NGS2_WAVEFORM_HEVAG)==8);
  memcpy(sys->b2f2a_keys,keys,sizeof(keys));
  sys->b2f2a_commits=1;sys->render_voice_cursor=1;
  memcpy(old,keys,sizeof(keys));
  memset(keys,0,sizeof(keys));
  n=ngs2_b2f2a_prepare_locked(sys,60,keys,&retained);
  assert(n==32&&retained==32);
  assert(memcmp(keys,old,sizeof(keys))==0);
  /* First 16th successful quantum rotates a short HEVAG voice only. */
  sys->b2f2a_commits=15;
  n=ngs2_b2f2a_prepare_locked(sys,60,keys,&retained);
  assert(n==32&&retained==31);
  assert(count_codec(keys,n,NGS2_WAVEFORM_HEVAG)==8);
  assert(count_codec(keys,n,NGS2_WAVEFORM_ATRAC9)==24);
  /* Voice generation change invalidates only the affected key. */
  memcpy(sys->b2f2a_keys,keys,sizeof(keys));
  Ngs2Voice *victim=ngs2_voice_locked(keys[0].handle);
  ++victim->atrac9->generation;
  victim->atrac9->fifo[0].generation=victim->atrac9->generation;
  sys->b2f2a_commits=18;
  n=ngs2_b2f2a_prepare_locked(sys,60,keys,&retained);
  assert(n==32&&retained==31);
  assert(count_codec(keys,n,NGS2_WAVEFORM_HEVAG)==8);
  /* At undercap baseline selects without this cohort selector. */
  for(uint32_t i=30;i<60;++i)states[i].pcm_count=0;
  assert(!ngs2_b2f2a_eligible_locked(&rack->voices[59]));
  teardown();
}
static void refill(void){
  for(uint32_t i=0;i<live;++i){
    Ngs2Atrac9Voice *st=&states[i];
    if(st->pcm_count)continue;
    st->pcm_write=st->pcm_read;
    Ngs2PcmGrain *grain=&st->fifo[st->pcm_write];
    memset(grain,0,sizeof(*grain));
    grain->generation=st->generation;
    grain->channels=rack->voices[i].waveform.channels;
    grain->frames=256;
    st->pcm_count=1;
  }
}
static void test_render_and_baseline(void){
  setup();
  ngs2_atrac9_setting=1;ngs2_render_mix_setting=1;
  atomic_store(&ngs2_audio_diag_setting,0);
  ngs2_b2f2a_setting=1;
  float master[256*8],unused[256*8];
  Ngs2RenderBuffer output[]={{master,sizeof(master),0x18,8},
                             {unused,sizeof(unused),0x18,8}};
  uint64_t last_virtual=0;
  for(int q=0;q<5;++q){
    refill();
    assert(!ngs2_render(sys->handle,output,2));
    assert(sys->grains_consumed==(uint64_t)(q+1)*32);
    assert(sys->virtualized_grains==(uint64_t)(q+1)*28);
    assert(sys->b2f2a_commits==(uint64_t)(q+1));
    assert(sys->virtualized_grains>last_virtual);
    last_virtual=sys->virtualized_grains;
  }
  assert(sys->b2f2a_retained>=4*32);
  assert(sys->render_limit_exceeded==5);
  /* Roll back without modifying PCM path: no stable-cohort commits. */
  ngs2_b2f2a_setting=0;
  refill();
  assert(!ngs2_render(sys->handle,output,2));
  assert(sys->b2f2a_commits==5);
  assert(sys->grains_consumed==6*32);
  teardown();
}
int main(void){test_cohort();test_render_and_baseline();
  puts("PASS: B2-F2A synthetic cohort stability, codec slots, generation, real render, fallback");return 0;}
