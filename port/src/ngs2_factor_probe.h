/* Opt-in Knack 2 evaluator observation. No playback policy or guest writes. */
#ifndef KNACK_NGS2_FACTOR_PROBE_H
#define KNACK_NGS2_FACTOR_PROBE_H
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdatomic.h>
#include <time.h>

#define FACTOR_CALL_OFFSET UINT64_C(0xb62830)
#define FACTOR_EVALUATOR_OFFSET UINT64_C(0xb4a420)
#define FACTOR_TRACKED 64u
#define FACTOR_CHANGE_LIMIT 512u
#define FACTOR_SUMMARY_LIMIT 512u
typedef void (ABI *FactorEvaluator)(void *,void *);
typedef int (*FactorRead)(void *,uintptr_t,size_t);
typedef struct {
    uintptr_t object,slot;
    uint64_t calls,last_us;
    uint32_t words[6];
    uint16_t index;
    int seen;
} FactorEntry;
static FactorEvaluator factor_original;
static FactorRead factor_read;
static FactorEntry factor_entries[FACTOR_TRACKED];
static unsigned char factor_indices[256];
static int factor_filter;
static atomic_flag factor_lock=ATOMIC_FLAG_INIT;
static _Atomic uint64_t factor_dropped;
static uint64_t factor_sequence;
static uint32_t factor_changes,factor_summaries;

/* Explicit byte signatures: no patch is possible on mismatch. */
static int factor_signatures(const unsigned char *call,size_t call_size,
                              const unsigned char *entry,size_t entry_size) {
    const unsigned char expected_call[]={0xe8,0xeb,0x7b,0xfe,0xff};
    const unsigned char expected_entry[]={0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54};
    return call && entry && call_size>=sizeof(expected_call) && entry_size>=sizeof(expected_entry) &&
        !memcmp(call,expected_call,sizeof(expected_call)) && !memcmp(entry,expected_entry,sizeof(expected_entry));
}
static int factor_call_bytes(uintptr_t call,uintptr_t target,unsigned char out[5]) {
    int64_t delta=(int64_t)target-(int64_t)(call+5);
    if (delta<INT32_MIN || delta>INT32_MAX) return 0;
    int32_t rel=(int32_t)delta; out[0]=0xe8; memcpy(out+1,&rel,4); return 1;
}
static void factor_stub_bytes(unsigned char out[12],uintptr_t wrapper) {
    out[0]=0x48; out[1]=0xb8; memcpy(out+2,&wrapper,8); out[10]=0xff; out[11]=0xe0;
}
/* Filtering applies only to diagnostics; indices are not lifetime identities. */
static int factor_set_filter(const char *text) {
    memset(factor_indices,0,sizeof(factor_indices)); factor_filter=0;
    const char *p=text;
    while (*p) {
        while (*p==' ' || *p=='\t') ++p;
        if (!*p) break;
        if (*p<'0' || *p>'9') return 0;
        unsigned index=0;
        do { index=index*10u+(unsigned)(*p++-'0'); if (index>255u) return 0; }
        while (*p>='0' && *p<='9');
        factor_indices[index]=1; factor_filter=1;
        while (*p==' ' || *p=='\t') ++p;
        if (!*p) break;
        if (*p++!=',' || !*p) return 0;
    }
    return 1;
}
static uint64_t factor_now_us(void) {
    struct timespec t; timespec_get(&t,TIME_UTC);
    return (uint64_t)t.tv_sec*UINT64_C(1000000)+(uint64_t)t.tv_nsec/1000u;
}
static int factor_collect(uintptr_t object,uintptr_t output,uint32_t words[6],
                           uintptr_t *slot,uint16_t *index) {
    static const uint32_t offsets[4]={0x2c,0x34,0x38,0x88};
    static const uint32_t masks[4]={2,8,16,UINT32_C(0x1000000)};
    if (!object || !output || object>UINTPTR_MAX-0x408 || output>UINTPTR_MAX-0x8c) return 0;
    /* Output exists for multiple guest object layouts. Slot identity is optional;
     * a failed identity read must not hide the factor values we are measuring. */
    *slot=0; *index=UINT16_MAX;
    if (!factor_read(slot,object+0x400,sizeof(*slot)) || !*slot || *slot>UINTPTR_MAX-10 ||
        !factor_read(index,*slot+8,sizeof(*index))) { *slot=0; *index=UINT16_MAX; }
    if (factor_filter && (*index>=256 || !factor_indices[*index])) return -1;
    memset(words,0,6*sizeof(*words));
    if (!factor_read(words,output,4) || !factor_read(words+1,output+0x20,4)) return 0;
    for (unsigned i=0;i<4;++i)
        if ((words[0]&masks[i]) && !factor_read(words+2+i,output+offsets[i],4)) return 0;
    return 1;
}
static void factor_observe(void *object,void *output) {
    uint32_t words[6]; uintptr_t slot=0; uint16_t index=0;
    int read=factor_collect((uintptr_t)object,(uintptr_t)output,words,&slot,&index);
    if (read<0) return;
    if (!read) { atomic_fetch_add_explicit(&factor_dropped,1,memory_order_relaxed); return; }
    uintptr_t descriptor=0,parent=0,table=0,callback=0;
    uint32_t own_flags=0,own_b=0,descriptor_flags=0,descriptor_b=0,metadata_valid=0;
    uintptr_t address=(uintptr_t)object;
    if (factor_read(&descriptor,address,8)) metadata_valid|=1;
    if (factor_read(&parent,address+8,8)) metadata_valid|=2;
    if (factor_read(&own_flags,address+0xbc,4) && factor_read(&own_b,address+0xf0,4)) metadata_valid|=4;
    if (factor_read(&table,address+0x90,8) && table && table<=UINTPTR_MAX-0x50 &&
        factor_read(&callback,table+0x48,8)) metadata_valid|=8;
    if (descriptor && descriptor<=UINTPTR_MAX-0x2d4 &&
        factor_read(&descriptor_flags,descriptor+0x29c,4) && factor_read(&descriptor_b,descriptor+0x2d0,4)) metadata_valid|=16;
    uint64_t now=factor_now_us();
    if (atomic_flag_test_and_set_explicit(&factor_lock,memory_order_acquire)) {
        atomic_fetch_add_explicit(&factor_dropped,1,memory_order_relaxed); return;
    }
    FactorEntry *entry=NULL;
    for (unsigned i=0;i<FACTOR_TRACKED;++i) {
        if (factor_entries[i].object==(uintptr_t)object && factor_entries[i].slot==slot &&
            factor_entries[i].index==index) { entry=&factor_entries[i]; break; }
        if (!factor_entries[i].object && !entry) entry=&factor_entries[i];
    }
    char line[1024]; line[0]=0;
    if (!entry) { atomic_fetch_add_explicit(&factor_dropped,1,memory_order_relaxed); }
    else {
        int changed=!entry->seen || memcmp(entry->words,words,sizeof(words));
        entry->object=(uintptr_t)object; entry->slot=slot; entry->index=index; ++entry->calls;
        int due=changed || now<entry->last_us || now-entry->last_us>=UINT64_C(2000000);
        if (due) {
            uint32_t *budget=changed?&factor_changes:&factor_summaries;
            uint32_t limit=changed?FACTOR_CHANGE_LIMIT:FACTOR_SUMMARY_LIMIT;
            if (*budget<=limit) {
                uint64_t seq=++factor_sequence;
                if ((*budget)++==limit)
                    snprintf(line,sizeof(line),"NGS2-FACTORS: seq=%llu budget=%s trace_incomplete=1 dropped=%llu\n",
                        (unsigned long long)seq,changed?"changes":"summaries",
                        (unsigned long long)atomic_load_explicit(&factor_dropped,memory_order_relaxed));
                else
                    snprintf(line,sizeof(line),
                        "NGS2-FACTORS: seq=%llu host_utc_us=%llu event=%s object=%p slot=%p guest_index=%u "
                        "calls=%llu flags=%08x activity=%08x A=%08x B=%08x C=%08x G=%08x dropped=%llu "
                        "metadata_valid=%x descriptor=%p parent=%p table=%p callback=%p own_flags=%08x own_B=%08x descriptor_flags=%08x descriptor_B=%08x\n",
                        (unsigned long long)seq,(unsigned long long)now,changed?"change":"summary",
                        object,(void *)slot,(unsigned)index,(unsigned long long)entry->calls,
                        words[0],words[1],words[2],words[3],words[4],words[5],
                        (unsigned long long)atomic_load_explicit(&factor_dropped,memory_order_relaxed),
                        metadata_valid,(void *)descriptor,(void *)parent,(void *)table,(void *)callback,
                        own_flags,own_b,descriptor_flags,descriptor_b);
            }
            entry->last_us=now;
        }
        memcpy(entry->words,words,sizeof(words)); entry->seen=1;
    }
    atomic_flag_clear_explicit(&factor_lock,memory_order_release);
    if (line[0]) fputs(line,stdout);
}
/* Only two pointer arguments are consumed by this evaluator. At the validated
 * call site RAX/XMM return values are unused; output is read through RSI. */
static ABI __attribute__((noinline)) void factor_wrapper(void *object,void *output) {
    factor_original(object,output);
    factor_observe(object,output);
}
#endif
