/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "runtime_atrac9.h"

typedef struct {
    _Atomic int active;
} MockHandle;

static _Atomic int init_active,decode_active,violation,max_decodes;
static pthread_mutex_t block_lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t block_condition=PTHREAD_COND_INITIALIZER;
static int block_decode,decode_entered,allow_decode_return,release_done;

void *Atrac9GetHandle(void) { return calloc(1,sizeof(MockHandle)); }
void Atrac9ReleaseHandle(void *opaque) {
    MockHandle *handle=opaque; assert(!atomic_load(&handle->active)); free(handle);
}
int Atrac9InitDecoder(void *opaque,unsigned char *config) {
    (void)opaque; (void)config;
    assert(atomic_fetch_add(&init_active,1)==0);
    if (atomic_load(&decode_active)) atomic_store(&violation,1);
    sched_yield();
    atomic_fetch_sub(&init_active,1); return 0;
}
int Atrac9GetCodecInfo(void *opaque,Atrac9CodecInfo *info) {
    (void)opaque; memset(info,0,sizeof(*info)); info->channels=4; info->samplingRate=48000;
    info->frameSamples=256; info->framesInSuperframe=4; info->superframeSize=768; return 0;
}
int Atrac9Decode(void *opaque,const unsigned char *input,short *pcm,int *bytes_used) {
    MockHandle *handle=opaque; (void)input;
    assert(atomic_fetch_add(&handle->active,1)==0);
    int current=atomic_fetch_add(&decode_active,1)+1;
    int seen=atomic_load(&max_decodes);
    while (current>seen && !atomic_compare_exchange_weak(&max_decodes,&seen,current)) {}
    if (atomic_load(&init_active)) atomic_store(&violation,1);
    pthread_mutex_lock(&block_lock);
    if (block_decode) {
        decode_entered=1; pthread_cond_broadcast(&block_condition);
        while (!allow_decode_return) pthread_cond_wait(&block_condition,&block_lock);
    }
    pthread_mutex_unlock(&block_lock);
    sched_yield(); pcm[0]=123; *bytes_used=1;
    atomic_fetch_sub(&decode_active,1); atomic_fetch_sub(&handle->active,1); return 0;
}

typedef struct { RuntimeAtrac9 *decoder; int iterations; } Worker;
static void *init_worker(void *opaque) {
    Worker *worker=opaque; Atrac9CodecInfo info;
    const unsigned char configs[2][4]={{0xfe,0x7a,0x17,0xf0},{0xfe,0x78,0x29,0xf0}};
    for (int i=0;i<worker->iterations;++i)
        assert(!runtime_atrac9_init(worker->decoder,configs[i&1],&info));
    return NULL;
}
static void *decode_worker(void *opaque) {
    Worker *worker=opaque; unsigned char input[2]={0}; int16_t pcm[8];
    for (int i=0;i<worker->iterations;++i) { int used=0; assert(!runtime_atrac9_decode(worker->decoder,input,pcm,&used) && used==1); }
    return NULL;
}
static void *one_decode(void *opaque) { return decode_worker(&(Worker){opaque,1}); }
static void *release_worker(void *opaque) {
    runtime_atrac9_release(opaque);
    pthread_mutex_lock(&block_lock); release_done=1; pthread_cond_broadcast(&block_condition); pthread_mutex_unlock(&block_lock);
    return NULL;
}

int main(void) {
    RuntimeAtrac9 *init_decoder=runtime_atrac9_create();
    RuntimeAtrac9 *left=runtime_atrac9_create(),*right=runtime_atrac9_create();
    assert(init_decoder && left && right);
    Worker init={init_decoder,10000},decode_left={left,10000},decode_right={right,10000};
    pthread_t threads[3];
    assert(!pthread_create(&threads[0],NULL,init_worker,&init));
    assert(!pthread_create(&threads[1],NULL,decode_worker,&decode_left));
    assert(!pthread_create(&threads[2],NULL,decode_worker,&decode_right));
    for (size_t i=0;i<3;++i) assert(!pthread_join(threads[i],NULL));
    assert(!atomic_load(&violation));
    assert(atomic_load(&max_decodes)>=2); /* independent handles retain read concurrency */
    runtime_atrac9_release(init_decoder); runtime_atrac9_release(left); runtime_atrac9_release(right);

    RuntimeAtrac9 *closing=runtime_atrac9_create(); assert(closing);
    pthread_mutex_lock(&block_lock); block_decode=1; pthread_mutex_unlock(&block_lock);
    pthread_t decoder_thread,releaser_thread;
    assert(!pthread_create(&decoder_thread,NULL,one_decode,closing));
    pthread_mutex_lock(&block_lock);
    while (!decode_entered) pthread_cond_wait(&block_condition,&block_lock);
    pthread_mutex_unlock(&block_lock);
    assert(!pthread_create(&releaser_thread,NULL,release_worker,closing));
    struct timespec delay={0,20000000}; nanosleep(&delay,NULL);
    pthread_mutex_lock(&block_lock);
    assert(!release_done); allow_decode_return=1; pthread_cond_broadcast(&block_condition);
    pthread_mutex_unlock(&block_lock);
    assert(!pthread_join(decoder_thread,NULL)); assert(!pthread_join(releaser_thread,NULL));
    assert(release_done);
    puts("PASS: shared ATRAC9 gate, per-instance exclusion, release wait and 30000 operations");
    return 0;
}
