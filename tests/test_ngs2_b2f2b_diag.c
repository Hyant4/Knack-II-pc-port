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
static const uint32_t live=40;
static void assign_gain(Ngs2Voice *v,float g){
  uint32_t bits;memcpy(&bits,&g,sizeof(bits));
  v->descriptor_words=8;
  memset(v->descriptors,0,sizeof(v->descriptors));
  v->descriptors[0]=bits;
}
static void setup(void){
  sys=calloc(1,sizeof(*sys));rack=calloc(1,sizeof(*rack));
  rack->voices=calloc(live,sizeof(*rack->voices));states=calloc(live,sizeof(*states));
  assert(sys&&rack&&states&&rack->voices);
  sys->handle=(Ngs2Handle*)(uintptr_t)0x800000;
  sys->render_mix_enabled=1;sys->atrac9_enabled=1;sys->racks=rack;ngs2_systems=sys;
  rack->system=sys;rack->handle=(Ngs2Handle*)(uintptr_t)0x900000;
  rack->rack_id=0x1000;rack->instance=3;rack->capacity=live;
  for(uint32_t i=0;i<live;i++){
    Ngs2Voice *v=&rack->voices[i];Ngs2Atrac9Voice *s=&states[i];
    v->rack=rack;v->handle=(Ngs2Handle*)(uintptr_t)(0xa00000+i*16);
    v->index=i;v->internal_state=NGS2_VOICE_PLAYING;v->atrac9=s;
    v->waveform=(Ngs2Waveform){.valid=1,.format=NGS2_WAVEFORM_HEVAG,
      .channels=1,.sample_rate=48000};
    assign_gain(v,i<16?0.0f:1.0f);
    s->generation=1;s->pcm_count=1;
    s->fifo[0].generation=1;s->fifo[0].channels=1;
    s->fifo[0].frames=256;
    for(uint32_t f=0;f<256;++f)s->fifo[0].pcm[f]=1000;
  }
  ngs2_atrac9_setting=1;ngs2_render_mix_setting=1;ngs2_b2f2a_setting=0;
  atomic_store(&ngs2_audio_diag_setting,1);
}
static void teardown(void){ngs2_systems=NULL;free(rack->voices);free(states);free(rack);free(sys);}
static void refill_all(void){
  for(uint32_t i=0;i<live;i++){
    Ngs2Atrac9Voice *s=&states[i];s->pcm_write=s->pcm_read;
    Ngs2PcmGrain *p=&s->fifo[s->pcm_write];
    memset(p,0,sizeof(*p));p->generation=s->generation;p->frames=256;p->channels=1;
    for(uint32_t j=0;j<256;++j)p->pcm[j]=1000;
    s->pcm_count=1;
  }
}
static void test_matrix_classifier(void){
  setup();
  assert(!ngs2_b2f2b_matrix_audible_locked(&rack->voices[0]));
  assert(ngs2_b2f2b_matrix_audible_locked(&rack->voices[16]));
  assign_gain(&rack->voices[16],1000.f);
  assert(!ngs2_b2f2b_matrix_audible_locked(&rack->voices[16]));
  assign_gain(&rack->voices[16],1.f/10000000.f);
  assert(ngs2_b2f2b_matrix_audible_locked(&rack->voices[16]));
  teardown();
}
static void test_real_render_classification(void){
  setup();
  float master[256*8],other[256*8];
  Ngs2RenderBuffer outputs[]={{master,sizeof(master),0x18,8},{other,sizeof(other),0x18,8}};
  assert(ngs2_render(sys->handle,outputs,2)==0);
  assert(sys->grains_consumed==32);
  assert(sys->virtualized_grains==8);
  assert(sys->diag_f2b_silent_mixed==16);
  assert(sys->diag_f2b_audible_virtualized==8);
  assert(sys->diag_f2b_wasted_quanta==1);
  assert(master[0]>0.01f);
  refill_all();
  assert(ngs2_render(sys->handle,outputs,2)==0);
  assert(sys->diag_f2b_boundary_checks==8);
  assert(sys->diag_f2b_wasted_quanta==2);
  /* One playable voice with compressed work but no PCM: worker backlog marker. */
  for(uint32_t i=0;i<live;++i)states[i].pcm_count=0;
  states[0].active_valid=1;
  assert(ngs2_render(sys->handle,outputs,2)==0);
  assert(sys->diag_f2b_midstream_hevag==1);
  /* This is only a classification: the mixer still used the baseline 32 slots. */
  assert(sys->render_limit_exceeded==2);
  teardown();
}
static void test_pcm_identical_with_diagnostics_disabled(void){
  float reference[256*8],other[256*8],captured[256*8];
  setup();
  Ngs2RenderBuffer output[]={{reference,sizeof(reference),0x18,8},{other,sizeof(other),0x18,8}};
  atomic_store(&ngs2_audio_diag_setting,0);
  assert(!ngs2_render(sys->handle,output,2));
  memcpy(captured,reference,sizeof(captured));
  assert(sys->diag_f2b_silent_mixed==0);
  teardown();
  setup();
  assert(!ngs2_render(sys->handle,output,2));
  assert(memcmp(captured,reference,sizeof(captured))==0);
  assert(sys->diag_f2b_silent_mixed==16);
  teardown();
}
static void test_early_end_and_reconfiguration(void){
  setup();
  Ngs2Voice *v=&rack->voices[20];Ngs2Atrac9Voice *s=v->atrac9;
  unsigned char empty[32]={0};
  assert(!ngs2_segment_control(v->handle,empty,sizeof(empty)));
  assert(s->hevag.end_requested);
  assert(sys->diag_f2b_end_while_live==1);
  unsigned char waveform[40]={0};
  uint32_t command=0x10000000, fields[]={NGS2_WAVEFORM_HEVAG,1,48000,0,0,0,0};
  memcpy(waveform+4,&command,sizeof(command));
  memcpy(waveform+8,fields,sizeof(fields));
  uint64_t generation=s->generation;
  assert(!ngs2_control_locked(v,waveform,0x28,command,0,0,0));
  /* B2-F2B-DIAG intentionally does NOT fix the old early drop. */
  assert(s->pcm_count==0);
  assert(s->generation==generation+1);
  assert(sys->diag_f2b_reconfig_active_drop==1);
  assert(sys->diag_f2b_reconfig_same_hevag_drop==1);
  teardown();
}
int main(void){
  test_matrix_classifier();
  test_real_render_classification();
  test_pcm_identical_with_diagnostics_disabled();
  test_early_end_and_reconfiguration();
  puts("PASS: B2-F2B-DIAG passive classification, commit-only counters, boundary, midstream, early HEVAG drop");
  return 0;
}
