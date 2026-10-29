/* Opt-in observation of the original movie/audio synchronizer. No debugger. */
#ifndef KNACK_NGS2_SYNC_PROBE_H
#define KNACK_NGS2_SYNC_PROBE_H
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdatomic.h>
#include <time.h>

typedef void (ABI *SyncControl)(void *,float);
typedef int (ABI *SyncQuery)(void *,uint32_t,float *,float *);
typedef int (*SyncRead)(void *,uintptr_t,size_t);
static SyncControl sync_control_original;
static SyncQuery sync_query_original;
static SyncRead sync_read;
static const uint64_t sync_call_sites[2]={0x5b5062,0x5b5077};
#define SYNC_CONTROL_OFFSET UINT64_C(0x5ce5e0)
#define SYNC_QUERY_OFFSET UINT64_C(0x5ce3b0)
#define SYNC_QUERY_TABLE UINT64_C(0x1217828)
#define SYNC_LIMIT 1024u

typedef struct SyncFrame {
    struct SyncFrame *previous;
    void *context;
    uint32_t reference,mask,before,after,position,difference;
    int result,query_valid;
    unsigned queries;
} SyncFrame;
static _Thread_local SyncFrame *sync_frame;
typedef struct {uintptr_t context;uint32_t mask,state;uint64_t last_us;} SyncEntry;
static SyncEntry sync_entries[8];
static atomic_flag sync_lock=ATOMIC_FLAG_INIT;
static _Atomic uint64_t sync_dropped;
static uint32_t sync_lines;
static uint64_t sync_sequence;

static int sync_call_signature(const unsigned char *bytes,size_t size,unsigned which) {
    static const unsigned char expected[2][5]={{0xe8,0x79,0x95,0x01,0},{0xe8,0x64,0x95,0x01,0}};
    return which<2 && bytes && size>=5 && !memcmp(bytes,expected[which],5);
}
static uint64_t sync_now_us(void) {
    struct timespec now;timespec_get(&now,TIME_UTC);
    return (uint64_t)now.tv_sec*UINT64_C(1000000)+(uint64_t)now.tv_nsec/1000u;
}
static int ABI sync_query_wrapper(void *context,uint32_t mask,float *position,float *difference) {
    int result=sync_query_original(context,mask,position,difference);
    SyncFrame *frame=sync_frame;
    if(frame && frame->context==context) {
        ++frame->queries;frame->result=result;
        frame->query_valid=position && difference &&
            sync_read(&frame->position,(uintptr_t)position,4) &&
            sync_read(&frame->difference,(uintptr_t)difference,4);
    }
    return result;
}
static void sync_observe(const SyncFrame *frame) {
    uint64_t now=sync_now_us();
    if(atomic_flag_test_and_set_explicit(&sync_lock,memory_order_acquire)) {
        atomic_fetch_add_explicit(&sync_dropped,1,memory_order_relaxed);return;
    }
    SyncEntry *entry=NULL;
    for(unsigned i=0;i<8;++i) {
        if(sync_entries[i].context==(uintptr_t)frame->context){entry=&sync_entries[i];break;}
        if(!sync_entries[i].context && !entry)entry=&sync_entries[i];
    }
    char line[640]={0};
    if(!entry)atomic_fetch_add_explicit(&sync_dropped,1,memory_order_relaxed);
    else {
        int changed=!entry->context || entry->mask!=frame->mask || entry->state!=frame->after || frame->before!=frame->after;
        if(changed || now<entry->last_us || now-entry->last_us>=500000) {
            if(sync_lines<=SYNC_LIMIT) {
                if(sync_lines++==SYNC_LIMIT)
                    snprintf(line,sizeof(line),"NGS2-SYNC: trace_incomplete=1 limit=%u\n",SYNC_LIMIT);
                else snprintf(line,sizeof(line),
                    "NGS2-SYNC: seq=%llu host_utc_us=%llu context=%p reference=%08x mask=%08x before=%08x after=%08x queries=%u query_valid=%d result=%d position=%08x difference=%08x dropped=%llu\n",
                    (unsigned long long)++sync_sequence,(unsigned long long)now,frame->context,
                    frame->reference,frame->mask,frame->before,frame->after,frame->queries,
                    frame->query_valid,frame->result,frame->position,frame->difference,
                    (unsigned long long)atomic_load_explicit(&sync_dropped,memory_order_relaxed));
            }
            entry->last_us=now;
        }
        entry->context=(uintptr_t)frame->context;entry->mask=frame->mask;entry->state=frame->after;
    }
    atomic_flag_clear_explicit(&sync_lock,memory_order_release);
    if(line[0])fputs(line,stdout);
}
static void ABI sync_control_wrapper(void *context,float reference) {
    SyncFrame frame={0};frame.previous=sync_frame;frame.context=context;
    memcpy(&frame.reference,&reference,4);
    uintptr_t address=(uintptr_t)context;
    int valid=context && address<=UINTPTR_MAX-16 &&
        sync_read(&frame.mask,address+8,4) && sync_read(&frame.before,address+12,4);
    sync_frame=&frame;
    sync_control_original(context,reference);
    sync_frame=frame.previous;
    if(valid && sync_read(&frame.after,address+12,4))sync_observe(&frame);
    else atomic_fetch_add_explicit(&sync_dropped,1,memory_order_relaxed);
}
#endif
