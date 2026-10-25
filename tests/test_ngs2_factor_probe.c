/* Exercise actual SysV call redirection against a synthetic evaluator, not guest code. */
#include <assert.h>
#include <windows.h>
#include <xmmintrin.h>
#include "../port/src/runtime.h"
#include "../port/src/ngs2_factor_probe.h"

static unsigned calls;
static uint32_t c_value=UINT32_C(0x3f800000);
static int reject_read;
static void ABI original(void *object,void *output) {
    (void)object; ++calls;
    unsigned char *p=output;
    memset(p,0,0x90);
    uint32_t flags=0x0100001a,activity=0x321,one=0x3f800000;
    memcpy(p,&flags,4); memcpy(p+0x20,&activity,4);
    memcpy(p+0x2c,&one,4); memcpy(p+0x34,&one,4);
    memcpy(p+0x38,&c_value,4); memcpy(p+0x88,&one,4);
}
static int read_memory(void *dst,uintptr_t address,size_t bytes) {
    if (reject_read) return 0;
    SIZE_T got=0;
    return ReadProcessMemory(GetCurrentProcess(),(void *)address,dst,bytes,&got) && got==bytes;
}
int main(void) {
    unsigned char call_sig[]={0xe8,0xeb,0x7b,0xfe,0xff};
    unsigned char entry_sig[]={0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54};
    assert(factor_signatures(call_sig,5,entry_sig,12));
    assert(!factor_signatures(call_sig,4,entry_sig,12));
    for (unsigned i=0;i<sizeof(call_sig);++i) {
        call_sig[i]^=1; assert(!factor_signatures(call_sig,5,entry_sig,12)); call_sig[i]^=1;
    }
    for (unsigned i=0;i<sizeof(entry_sig);++i) {
        entry_sig[i]^=1; assert(!factor_signatures(call_sig,5,entry_sig,12)); entry_sig[i]^=1;
    }
    unsigned char rel[5]; memset(rel,0xa5,sizeof(rel));
    assert(!factor_call_bytes(0x10000,UINT64_C(0x900000000),rel));
    for (unsigned i=0;i<sizeof(rel);++i) assert(rel[i]==0xa5);
    assert(factor_call_bytes(0x10000,0x10005,rel));
    assert(!memcmp(rel,"\xe8\0\0\0\0",5));
    assert(factor_set_filter("12, 20,28"));
    assert(factor_indices[12] && factor_indices[20] && factor_indices[28] && !factor_indices[29]);
    assert(!factor_set_filter("256")); assert(!factor_set_filter("28,"));
    assert(!factor_set_filter("-1")); assert(factor_set_filter("28"));

    unsigned char object[0x408]={0},object_copy[sizeof(object)],slot[16]={0};
    unsigned char descriptor[0x2d4]={0},table[0x50]={0};
    uintptr_t descriptor_ptr=(uintptr_t)descriptor,table_ptr=(uintptr_t)table,callback_ptr=0x1234;
    uint32_t metadata_flags=8,quarter=0x3e800000,half=0x3f000000;
    memcpy(object,&descriptor_ptr,8); memcpy(object+0x90,&table_ptr,8);
    memcpy(table+0x48,&callback_ptr,8);
    memcpy(object+0xbc,&metadata_flags,4); memcpy(object+0xf0,&quarter,4);
    memcpy(descriptor+0x29c,&metadata_flags,4); memcpy(descriptor+0x2d0,&half,4);
    uintptr_t ptr=(uintptr_t)slot; uint16_t index=28;
    memcpy(object+0x400,&ptr,sizeof(ptr)); memcpy(slot+8,&index,sizeof(index));
    memcpy(object_copy,object,sizeof(object));
    unsigned char expected[0x90],actual[0x90];
    original(object,expected); assert(calls==1);
    factor_original=original; factor_read=read_memory;
    unsigned char *code=VirtualAlloc(NULL,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    assert(code);
    /* The synthetic caller preserves SysV stack alignment at its normal CALL. */
    memcpy(code,"\x48\x83\xec\x08",4);
    assert(factor_call_bytes((uintptr_t)(code+4),(uintptr_t)(code+64),code+4));
    memcpy(code+9,"\x48\x83\xc4\x08\xc3",5);
    factor_stub_bytes(code+64,(uintptr_t)factor_wrapper);
    DWORD old; assert(VirtualProtect(code,4096,PAGE_EXECUTE_READ,&old));
    assert(FlushInstructionCache(GetCurrentProcess(),code,4096));
    FactorEvaluator redirected=(FactorEvaluator)(uintptr_t)code;
    unsigned control=_mm_getcsr()&0xffc0u;
    redirected(object,actual);
    assert(calls==2 && !memcmp(expected,actual,sizeof(actual)));
    assert(!memcmp(object,object_copy,sizeof(object)));
    assert((_mm_getcsr()&0xffc0u)==control);
    assert(factor_entries[0].calls==1 && factor_changes==1);
    redirected(object,actual);
    assert(calls==3 && factor_entries[0].calls==2 && factor_changes==1);
    c_value=0; redirected(object,actual);
    assert(calls==4 && factor_changes==2 && !factor_entries[0].words[4]);
    assert(factor_entries[0].words[2]==0x3f800000 && factor_entries[0].words[3]==0x3f800000);
    factor_entries[0].last_us=0; redirected(object,actual);
    assert(calls==5 && factor_summaries==1);
    assert(factor_set_filter(""));
    reject_read=1; redirected(object,actual); reject_read=0;
    assert(calls==6 && atomic_load(&factor_dropped)==1);
    assert(factor_set_filter("28"));
    c_value=0; original(object,expected); assert(!memcmp(actual,expected,sizeof(actual)));
    /* Filtered observations still execute original, without consuming budgets. */
    index=29; memcpy(slot+8,&index,2); unsigned before=calls;
    redirected(object,actual); assert(calls==before+1 && factor_entries[0].calls==4);
    index=28; memcpy(slot+8,&index,2);
    factor_changes=FACTOR_CHANGE_LIMIT; c_value=0x3f000000; redirected(object,actual);
    assert(factor_changes==FACTOR_CHANGE_LIMIT+1);
    c_value=0x3e800000; redirected(object,actual); assert(factor_changes==FACTOR_CHANGE_LIMIT+1);
    factor_summaries=FACTOR_SUMMARY_LIMIT; factor_entries[0].last_us=0;
    redirected(object,actual); assert(factor_summaries==FACTOR_SUMMARY_LIMIT+1);
    /* Lock contention drops observation without blocking or skipping evaluator. */
    assert(!atomic_flag_test_and_set(&factor_lock)); before=calls;
    redirected(object,actual); assert(calls==before+1 && atomic_load(&factor_dropped)==2);
    atomic_flag_clear(&factor_lock);
    /* A source object without a bus-layout slot still yields factors. */
    assert(factor_set_filter(""));
    uintptr_t null_slot=0; memcpy(object+0x400,&null_slot,sizeof(null_slot));
    uint32_t raw[6]; uintptr_t observed_slot; uint16_t observed_index;
    assert(factor_collect((uintptr_t)object,(uintptr_t)actual,raw,&observed_slot,&observed_index)==1);
    assert(!observed_slot && observed_index==UINT16_MAX && raw[0]==0x0100001a);
    assert(VirtualFree(code,0,MEM_RELEASE));
    puts("PASS: actual SysV CALL/stub/wrapper, exactly-once evaluator, buffer/control parity, filters, signatures, dedup, budgets and nonblocking drops");
    return 0;
}
