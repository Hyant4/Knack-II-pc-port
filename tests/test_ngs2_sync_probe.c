/* Actual executable CALL, SysV float/pointer forwarding and observational bounds. */
#include <assert.h>
#include <windows.h>
#include <xmmintrin.h>
#include "../port/src/runtime.h"
#include "../port/src/ngs2_sync_probe.h"
static unsigned controls,queries;
static int bad_read;
static SyncQuery table_entry;
static float expected_reference;
static int read_memory(void *dst,uintptr_t address,size_t bytes) {
    if(bad_read)return 0;
    SIZE_T got=0;return ReadProcessMemory(GetCurrentProcess(),(void *)address,dst,bytes,&got) && got==bytes;
}
static int ABI original_query(void *context,uint32_t mask,float *position,float *difference) {
    (void)context;assert(mask==0x200000);++queries;*position=1.25f;*difference=-0.5f;return -7;
}
static void ABI original_control(void *context,float reference) {
    assert(reference==expected_reference);++controls;
    float position=0,difference=0;
    assert(table_entry(context,0x200000,&position,&difference)==-7);
    assert(position==1.25f && difference==-0.5f);
    ((unsigned char *)context)[12]=1;
}
int main(void) {
    unsigned char signatures[2][5]={{0xe8,0x79,0x95,1,0},{0xe8,0x64,0x95,1,0}};
    for(unsigned i=0;i<2;++i) {
        assert(sync_call_signature(signatures[i],5,i));
        assert(!sync_call_signature(signatures[i],4,i));
        signatures[i][1]^=1;assert(!sync_call_signature(signatures[i],5,i));
    }
    assert(!sync_call_signature(signatures[0],5,2));
    sync_read=read_memory;sync_query_original=original_query;sync_control_original=original_control;
    unsigned char expected[16]={0},actual[16]={0};uint32_t mask=0x200000;
    memcpy(expected+8,&mask,4);memcpy(actual,expected,16);
    table_entry=original_query;expected_reference=3.5f;original_control(expected,3.5f);
    unsigned char *code=VirtualAlloc(NULL,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);assert(code);
    memcpy(code,"\x48\x83\xec\x08\xe8",5);
    int32_t relative=64-9;memcpy(code+5,&relative,4);
    memcpy(code+9,"\x48\x83\xc4\x08\xc3",5);
    uintptr_t wrapper=(uintptr_t)sync_control_wrapper;
    memcpy(code+64,"\x48\xb8",2);memcpy(code+66,&wrapper,8);memcpy(code+74,"\xff\xe0",2);
    DWORD old;assert(VirtualProtect(code,4096,PAGE_EXECUTE_READ,&old));
    assert(FlushInstructionCache(GetCurrentProcess(),code,4096));
    table_entry=sync_query_wrapper;unsigned mxcsr=_mm_getcsr()&0xffc0u;
    ((SyncControl)(uintptr_t)code)(actual,3.5f);
    assert(controls==2 && queries==2 && !memcmp(expected,actual,16));
    assert(!sync_frame && sync_lines==1 && sync_sequence==1);
    assert((_mm_getcsr()&0xffc0u)==mxcsr);
    ((SyncControl)(uintptr_t)code)(actual,3.5f);assert(sync_lines==1);
    sync_entries[0].last_us=0;((SyncControl)(uintptr_t)code)(actual,3.5f);assert(sync_lines==2);
    bad_read=1;unsigned before=controls;((SyncControl)(uintptr_t)code)(actual,3.5f);bad_read=0;
    assert(controls==before+1 && !sync_frame && atomic_load(&sync_dropped)==1);
    assert(!atomic_flag_test_and_set(&sync_lock));before=controls;
    ((SyncControl)(uintptr_t)code)(actual,3.5f);atomic_flag_clear(&sync_lock);
    assert(controls==before+1 && atomic_load(&sync_dropped)==2);
    sync_lines=SYNC_LIMIT;sync_entries[0].last_us=0;
    ((SyncControl)(uintptr_t)code)(actual,3.5f);assert(sync_lines==SYNC_LIMIT+1);
    sync_entries[0].last_us=0;((SyncControl)(uintptr_t)code)(actual,3.5f);assert(sync_lines==SYNC_LIMIT+1);
    float position,difference;assert(sync_query_wrapper(actual,mask,&position,&difference)==-7);
    assert(!sync_frame);VirtualFree(code,0,MEM_RELEASE);
    puts("PASS: sync original once, float and query parity, bounded observation, no debugger");return 0;
}
