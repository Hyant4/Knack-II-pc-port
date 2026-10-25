/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void snapshot_hook(void);
#define RUNTIME_MEMORY_SNAPSHOT_TEST_HOOK() snapshot_hook()
#include "../port/src/runtime_memory.c"

static int32_t test_errno;
int32_t *runtime_errno(void) { return &test_errno; }
void runtime_wait_begin(const char *kind,const void *object,const char *name,
                        const void *const *owner,uintptr_t frame) {
    (void)kind; (void)object; (void)name; (void)owner; (void)frame;
}
void runtime_wait_end(void) {}
uintptr_t runtime_lookup(const RuntimeExport *table,size_t count,const char *name) {
    for (size_t i=0;i<count;++i) if (!strcmp(table[i].name,name)) return (uintptr_t)table[i].function;
    return 0;
}

enum { TEST_PAGES=1024 };
static const uintptr_t guest_base=UINT64_C(0x1000000000);
static pthread_mutex_t hook_lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t hook_condition=PTHREAD_COND_INITIALIZER;
static int hook_enabled,hook_entered,hook_release,writer_started;
static _Atomic int writer_acquired;

static void snapshot_hook(void) {
    pthread_mutex_lock(&hook_lock);
    if (hook_enabled) {
        hook_entered=1; pthread_cond_broadcast(&hook_condition);
        while (!hook_release) pthread_cond_wait(&hook_condition,&hook_lock);
    }
    pthread_mutex_unlock(&hook_lock);
}
static void set_regions(int split,int prot) {
    write_lock();
    vma_count=0;
    assert(!vma_insert(0,(Vma){guest_base,guest_base+(split ? 2*PAGE : TEST_PAGES*PAGE),KIND_DIRECT,prot,0,0}));
    if (split) assert(!vma_insert(1,(Vma){guest_base+2*PAGE,guest_base+4*PAGE,KIND_FLEXIBLE,prot,0,2*PAGE}));
    write_unlock();
}
typedef struct { int operation; } Writer;
static void *writer_thread(void *opaque) {
    Writer *writer=opaque;
    pthread_mutex_lock(&hook_lock); writer_started=1; pthread_cond_broadcast(&hook_condition); pthread_mutex_unlock(&hook_lock);
    write_lock(); atomic_store(&writer_acquired,1);
    if (writer->operation==0) vma_count=0;                 /* unmap */
    else if (writer->operation==1) vmas[0].prot=0;         /* protect */
    else vmas[0].phys=PAGE;                                /* replacement map */
    write_unlock(); return NULL;
}
typedef struct { unsigned char *dst; uint64_t size; int result; } Snapshot;
static void *snapshot_thread(void *opaque) {
    Snapshot *snapshot=opaque;
    snapshot->result=runtime_memory_copy_snapshot(snapshot->dst,guest_base,snapshot->size);
    return NULL;
}
static void concurrent_operation(int operation) {
    set_regions(0,3);
    const uint64_t size=TEST_PAGES*PAGE;
    unsigned char *dst=malloc((size_t)size); assert(dst); memset(dst,0,(size_t)size);
    Snapshot snapshot={dst,size,0}; Writer writer={operation};
    pthread_mutex_lock(&hook_lock);
    hook_enabled=1; hook_entered=hook_release=writer_started=0; atomic_store(&writer_acquired,0);
    pthread_mutex_unlock(&hook_lock);
    pthread_t snapshot_id,writer_id; assert(!pthread_create(&snapshot_id,NULL,snapshot_thread,&snapshot));
    pthread_mutex_lock(&hook_lock);
    while (!hook_entered) pthread_cond_wait(&hook_condition,&hook_lock);
    assert(!pthread_create(&writer_id,NULL,writer_thread,&writer));
    while (!writer_started) pthread_cond_wait(&hook_condition,&hook_lock);
    pthread_mutex_unlock(&hook_lock);
    struct timespec delay={0,20000000}; nanosleep(&delay,NULL);
    assert(!atomic_load(&writer_acquired));
    pthread_mutex_lock(&hook_lock); hook_release=1; pthread_cond_broadcast(&hook_condition); pthread_mutex_unlock(&hook_lock);
    assert(!pthread_join(snapshot_id,NULL)); assert(!pthread_join(writer_id,NULL));
    assert(snapshot.result && !memcmp(dst,backing_base,(size_t)size));
    pthread_mutex_lock(&hook_lock); hook_enabled=0; pthread_mutex_unlock(&hook_lock);
    free(dst);
}

int main(void) {
    const size_t bytes=(size_t)TEST_PAGES*PAGE;
    backing_base=malloc(bytes); assert(backing_base);
    for (size_t i=0;i<bytes;++i) backing_base[i]=(unsigned char)(i*37u+11u);

    unsigned char tiny=0; set_regions(0,3);
    assert(runtime_memory_copy_snapshot(&tiny,guest_base+123,1));
    assert(tiny==backing_base[123]);

    set_regions(0,3); vmas[0].kind=KIND_FLEXIBLE;
    assert(!split_at(guest_base+2*PAGE));
    assert(vma_count==2 && vmas[1].phys==2*PAGE);
    unsigned char split_copy[4*PAGE];
    assert(runtime_memory_copy_snapshot(split_copy,guest_base,sizeof(split_copy)));
    assert(!memcmp(split_copy,backing_base,sizeof(split_copy)));

    unsigned char multi[4*PAGE]; memset(multi,0,sizeof(multi)); set_regions(1,3);
    assert(runtime_memory_copy_snapshot(multi,guest_base,sizeof(multi)));
    assert(!memcmp(multi,backing_base,sizeof(multi)));

    unsigned char unchanged[64]; memset(unchanged,0xa5,sizeof(unchanged));
    set_regions(1,3); vmas[1].start+=PAGE;
    assert(!runtime_memory_copy_snapshot(unchanged,guest_base,sizeof(multi)));
    for (size_t i=0;i<sizeof(unchanged);++i) assert(unchanged[i]==0xa5);
    set_regions(0,0);
    assert(!runtime_memory_copy_snapshot(unchanged,guest_base,sizeof(unchanged)));
    for (size_t i=0;i<sizeof(unchanged);++i) assert(unchanged[i]==0xa5);
    assert(!runtime_memory_copy_snapshot(NULL,guest_base,1));
    assert(!runtime_memory_copy_snapshot(unchanged,0,1));
    assert(!runtime_memory_copy_snapshot(unchanged,UINTPTR_MAX-7,16));
    assert(!runtime_memory_copy_snapshot(unchanged,guest_base,0));

    concurrent_operation(0); concurrent_operation(1); concurrent_operation(2);

    set_regions(0,0x32); /* GPU/CPU writable view; backing remains directly readable. */
    unsigned char *large=malloc(bytes); assert(large);
    struct timespec begin,end; clock_gettime(CLOCK_MONOTONIC,&begin);
    assert(runtime_memory_copy_snapshot(large,guest_base,bytes));
    clock_gettime(CLOCK_MONOTONIC,&end);
    assert(!memcmp(large,backing_base,bytes));
    uint64_t elapsed_ns=(uint64_t)(end.tv_sec-begin.tv_sec)*UINT64_C(1000000000)+(uint64_t)(end.tv_nsec-begin.tv_nsec);
    printf("PASS: guest snapshot bytes=%zu elapsed_ns=%" PRIu64 "\n",bytes,elapsed_ns);
    free(large); free(backing_base); backing_base=NULL; free(vmas); vmas=NULL; vma_count=vma_capacity=0;
    return 0;
}
