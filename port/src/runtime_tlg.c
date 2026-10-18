/* System-library functions first needed by The Last Guardian (CUSA03627 v1.03).
 *
 * Event flags are a complete implementation. NGS2 is a silent placeholder that lets the game's
 * sndx engine initialize and run (AudioOut still paces the audio thread); TLG_NGS2=native links
 * the firmware's NGS2 instead (see analysis/03_audio.md). Audio3d lives in runtime_gr2_audio3d.c.
 * Everything else is a small contract the game needs at boot: kernel queries, time-zone
 * conversion, PlayGo dialog, LoadExec.
 */
#define _GNU_SOURCE
#include "runtime.h"
#include "runtime_atrac9.h"
#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#define KERR(n) ((int32_t)(UINT32_C(0x80020000)|(n)))
#define KERR_ESRCH KERR(3)
#define KERR_EPERM KERR(1)
#define KERR_EBUSY KERR(16)
#define KERR_EINVAL KERR(22)
#define KERR_ENOMEM KERR(12)
#define KERR_ETIMEDOUT KERR(60)
#define KERR_ECANCELED KERR(85)

static void once(const char *what) {
    /* First call of a placeholder is logged; the list is short and fixed. */
    static const char *seen[64];
    static int count;
    static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_lock(&lock);
    for (int i=0;i<count;++i) if (seen[i]==what) { pthread_mutex_unlock(&lock); return; }
    if (count<64) seen[count++]=what;
    pthread_mutex_unlock(&lock);
    printf("Runtime: %s (placeholder)\n",what);
}

/* ---- Event flags ---- */
#define EF_MAGIC UINT32_C(0x45564654)
#define EF_ATTR_MULTI 0x20
#define EF_WAIT_AND 1
#define EF_WAIT_OR 2
#define EF_CLEAR_ALL 0x10
#define EF_CLEAR_PAT 0x20
typedef struct {
    uint32_t magic, attr;
    uint64_t bits;
    int waiters, deleted, cancel_generation;
    pthread_mutex_t lock;
    pthread_cond_t cond;
    char name[32];
} EventFlag;

static ABI int32_t ef_create(EventFlag **out,const char *name,uint32_t attr,uint64_t init,const void *param) {
    (void)param;
    if (!out || (attr & ~UINT32_C(0x33))) return KERR_EINVAL;
    EventFlag *ef=calloc(1,sizeof(*ef));
    if (!ef) return KERR_ENOMEM;
    ef->magic=EF_MAGIC; ef->attr=attr; ef->bits=init;
    pthread_mutex_init(&ef->lock,NULL);
    pthread_cond_init(&ef->cond,NULL);
    snprintf(ef->name,sizeof(ef->name),"%s",name ? name : "");
    *out=ef; return 0;
}
static int ef_valid(const EventFlag *ef) { return ef && ef->magic==EF_MAGIC && !ef->deleted; }
static ABI int32_t ef_delete(EventFlag *ef) {
    if (!ef_valid(ef)) return KERR_ESRCH;
    pthread_mutex_lock(&ef->lock);
    ef->deleted=1;
    pthread_cond_broadcast(&ef->cond);
    int busy=ef->waiters;
    pthread_mutex_unlock(&ef->lock);
    /* Waiters return ESRCH; the object is leaked if any were blocked, since they still read it. */
    if (!busy) { ef->magic=0; pthread_cond_destroy(&ef->cond); pthread_mutex_destroy(&ef->lock); free(ef); }
    return 0;
}
static ABI int32_t ef_set(EventFlag *ef,uint64_t bits) {
    if (!ef_valid(ef)) return KERR_ESRCH;
    pthread_mutex_lock(&ef->lock);
    ef->bits|=bits;
    pthread_cond_broadcast(&ef->cond);
    pthread_mutex_unlock(&ef->lock);
    return 0;
}
static ABI int32_t ef_clear(EventFlag *ef,uint64_t mask) {
    if (!ef_valid(ef)) return KERR_ESRCH;
    pthread_mutex_lock(&ef->lock);
    ef->bits&=mask;
    pthread_mutex_unlock(&ef->lock);
    return 0;
}
/* sceKernelCancelEventFlag: the waiters return ECANCELED, the bits become `pattern`. */
static ABI int32_t ef_cancel(EventFlag *ef,uint64_t pattern,int32_t *waiters) {
    if (!ef_valid(ef)) return KERR_ESRCH;
    pthread_mutex_lock(&ef->lock);
    if (waiters) *waiters=ef->waiters;
    ef->bits=pattern;
    ++ef->cancel_generation;
    pthread_cond_broadcast(&ef->cond);
    pthread_mutex_unlock(&ef->lock);
    return 0;
}
static int ef_match(uint64_t bits,uint64_t pattern,uint32_t mode) {
    return (mode & EF_WAIT_AND) ? (bits & pattern)==pattern : (bits & pattern)!=0;
}
static void ef_consume(EventFlag *ef,uint64_t pattern,uint32_t mode,uint64_t *result) {
    if (result) *result=ef->bits;
    if (mode & EF_CLEAR_ALL) ef->bits=0;
    else if (mode & EF_CLEAR_PAT) ef->bits&=~pattern;
}
static int ef_mode_ok(uint32_t mode) {
    uint32_t wait=mode & 3, clear=mode & 0x30;
    return (wait==EF_WAIT_AND || wait==EF_WAIT_OR) && clear!=0x30 && !(mode & ~UINT32_C(0x33));
}
static ABI int32_t ef_poll(EventFlag *ef,uint64_t pattern,uint32_t mode,uint64_t *result) {
    if (!ef_valid(ef)) return KERR_ESRCH;
    if (!pattern || !ef_mode_ok(mode)) return KERR_EINVAL;
    pthread_mutex_lock(&ef->lock);
    int32_t r=KERR_EBUSY;
    if (!(ef->waiters && !(ef->attr & EF_ATTR_MULTI)) && ef_match(ef->bits,pattern,mode)) {
        ef_consume(ef,pattern,mode,result); r=0;
    } else if (result) *result=ef->bits;
    pthread_mutex_unlock(&ef->lock);
    return r;
}
static ABI int32_t ef_wait(EventFlag *ef,uint64_t pattern,uint32_t mode,uint64_t *result,uint32_t *timeout_us) {
    if (!ef_valid(ef)) return KERR_ESRCH;
    if (!pattern || !ef_mode_ok(mode)) return KERR_EINVAL;
    pthread_mutex_lock(&ef->lock);
    if (ef->waiters && !(ef->attr & EF_ATTR_MULTI)) { pthread_mutex_unlock(&ef->lock); return KERR_EPERM; }
    struct timespec start, deadline;
    clock_gettime(CLOCK_REALTIME,&start);
    if (timeout_us) {
        uint64_t ns=(uint64_t)start.tv_nsec+(uint64_t)*timeout_us*1000;
        deadline.tv_sec=start.tv_sec+(time_t)(ns/1000000000); deadline.tv_nsec=(long)(ns%1000000000);
    }
    int32_t r=0;
    const int generation=ef->cancel_generation;
    ++ef->waiters;
    runtime_wait_begin(timeout_us ? "event flag (timed)" : "event flag",ef,ef->name,NULL,RUNTIME_GUEST_FRAME());
    while (!ef->deleted && ef->cancel_generation==generation && !ef_match(ef->bits,pattern,mode)) {
        int e=timeout_us ? pthread_cond_timedwait(&ef->cond,&ef->lock,&deadline) : pthread_cond_wait(&ef->cond,&ef->lock);
        if (e==ETIMEDOUT) { r=KERR_ETIMEDOUT; break; }
    }
    runtime_wait_end();
    --ef->waiters;
    if (ef->deleted) r=KERR_ESRCH;
    else if (ef->cancel_generation!=generation) { r=KERR_ECANCELED; if (result) *result=ef->bits; }
    else if (!r) ef_consume(ef,pattern,mode,result);
    else if (result) *result=ef->bits;
    pthread_mutex_unlock(&ef->lock);
    if (timeout_us) {
        if (r==KERR_ETIMEDOUT) *timeout_us=0;
        else {
            struct timespec now; clock_gettime(CLOCK_REALTIME,&now);
            int64_t spent=(int64_t)(now.tv_sec-start.tv_sec)*1000000+(now.tv_nsec-start.tv_nsec)/1000;
            *timeout_us=spent>=(int64_t)*timeout_us ? 0 : (uint32_t)(*timeout_us-spent);
        }
    }
    return r;
}

/* ---- Kernel queries ---- */
/* Base PS4 (not Pro): 1080p output path and the 0xb0000000 GPU memory layout. */
static ABI int32_t is_neo_mode(void) { return 0; }
static ABI int32_t available_flexible_memory(uint64_t *size) {
    if (!size) return KERR_EINVAL;
    *size=UINT64_C(448)*1024*1024; return 0;
}
static ABI int32_t getdents(int32_t fd,char *buffer,uint64_t size) {
    return (int32_t)runtime_file_getdents(fd,buffer,size,NULL);
}
static ABI int32_t prt_aperture(int32_t index,void **address,uint64_t *size) {
    if (index<0 || index>2 || !address || !size) return KERR_EINVAL;
    *address=NULL; *size=0; return 0;
}
/* The libc unwinder asks for a module's .eh_frame_hdr when a C++ exception is thrown. No
 * exception has been observed yet; report "no module" so it fails loudly instead of misbehaving. */
static ABI int32_t module_info_for_unwind(uintptr_t address,int32_t flags,void *info) {
    (void)flags; (void)info;
    fprintf(stderr,"Runtime: sceKernelGetModuleInfoForUnwind(%#lx) not implemented (C++ exception?)\n",(unsigned long)address);
    return KERR_ESRCH;
}
static ABI int32_t module_segment_info(void *info) {
    (void)info; once("sceKernelInternalMemoryGetModuleSegmentInfo"); return KERR_ESRCH;
}

/* ---- Time zone ---- */
/* Local time and its offset from UTC in seconds (Windows has no tm_gmtoff: the offset is the
 * local broken-down time read back as UTC, minus the instant). */
static int tlg_localtime(time_t t,struct tm *out,int64_t *offset) {
#ifdef _WIN32
    if (localtime_s(out,&t)) return 0;
    struct tm copy=*out;
    *offset=(int64_t)_mkgmtime(&copy)-(int64_t)t;
#else
    if (!localtime_r(&t,out)) return 0;
    *offset=out->tm_gmtoff;
#endif
    return 1;
}
typedef struct { int64_t t; uint32_t west_sec, dst_sec; } Timesec;
typedef struct { int32_t minutes_west, dst_time; } Timezone;
static ABI int32_t utc_to_local(int64_t utc,int64_t *local,Timesec *ts,uint64_t *dst) {
    struct tm tm; int64_t offset;
    if (!tlg_localtime((time_t)utc,&tm,&offset)) return KERR_EINVAL;
    int64_t dst_sec=tm.tm_isdst>0 ? 3600 : 0;
    if (local) *local=utc+offset;
    if (ts) { ts->t=utc; ts->west_sec=(uint32_t)(offset-dst_sec); ts->dst_sec=(uint32_t)dst_sec; }
    if (dst) *dst=(uint64_t)dst_sec;
    return 0;
}
static ABI int32_t local_to_utc(int64_t local,int64_t unused,int64_t *utc,Timezone *tz,int32_t *dst) {
    (void)unused;
    struct tm tm; int64_t offset;
    if (!tlg_localtime((time_t)local,&tm,&offset)) return KERR_EINVAL;
    if (utc) *utc=local-offset;
    if (tz) { tz->minutes_west=(int32_t)(-offset/60); tz->dst_time=tm.tm_isdst>0; }
    if (dst) *dst=tm.tm_isdst>0 ? 3600 : 0;
    return 0;
}

/* ---- libc internal heap tracing: report no tracing ---- */
typedef struct { uint64_t size; uint32_t flag, get_segment_info; uint64_t *mspace_id_mask, *mstate_table; } HeapTraceInfo;
static ABI int32_t libc_heap_trace_info(HeapTraceInfo *info) {
    static uint64_t mask, table[64];
    if (!info) return -1;
    info->get_segment_info=0; info->mspace_id_mask=&mask; info->mstate_table=table;
    return 0;
}

/* Needed by libSceNgs2.sprx (console firmware module, TLG_NGS2=native). */
static ABI int32_t compiled_sdk_version(int32_t *version) {
    if (!version) return KERR_EINVAL;
    *version = 0x04008061; /* the SDK the game's own modules report (FIOS2 log) */
    return 0;
}
static ABI int32_t posix_pthread_equal(void *a, void *b) { return a == b; }
/* libSceLibcInternal export the game's libc.prx does not carry. */
static ABI void guest_sincosf(float x, float *s, float *c) { *s = sinf(x); *c = cosf(x); }

/* ---- System services ---- */
static ABI int32_t load_exec(const char *path,char *const *argv) {
    printf("Runtime: sceSystemServiceLoadExec(%s%s) -> restart\n",path ? path : "(null)",argv && argv[0] ? " ..." : "");
    fflush(NULL);
    runtime_restart();
    return 0;
}
static ABI int32_t pad_speaker_mix(int32_t handle,int32_t level) { (void)handle; (void)level; return 0; }
static ABI int32_t pad_motion_sensor(int32_t handle,int32_t enable) { (void)handle; (void)enable; return 0; }
#define DISC_MAP_NO_BITMAP ((int32_t)0x81100004)
static ABI int32_t discmap_7c98(const char *path,int64_t offset,int64_t size,int32_t *a,int32_t *b,int32_t *c) {
    (void)path; (void)offset; (void)size; (void)a; (void)b; (void)c; return DISC_MAP_NO_BITMAP;
}

/* PlayGo dialog: only opened when a chunk is missing, which never happens here. */
#define DIALOG_STATUS_FINISHED 3
static ABI int32_t playgo_dialog_init(void) { return 0; }
static ABI int32_t playgo_dialog_open(const void *param) { (void)param; once("scePlayGoDialogOpen"); return 0; }
static ABI int32_t playgo_dialog_status(void) { return DIALOG_STATUS_FINISHED; }
static ABI int32_t playgo_dialog_result(int32_t *result) { if (result) *result=0; return 0; }
static ABI int32_t playgo_dialog_term(void) { return 0; }

/* ---- NGS2 HLE core ----
 * HLE-1 owns object lifetime, persistent handles, validated command snapshots and internal voice
 * state. Rendering deliberately remains silent. Published voice state remains the legacy idle
 * state until decoded sample progress can provide truthful completion in a later gate. */
#define NGS2_INVALID ((int32_t)0x804A0106)
#define NGS2_MAX_RACK_VOICES 4096u
#define NGS2_MAX_DESCRIPTOR_WORDS 64u
#define NGS2_MAX_COMMAND_SIZE 4096u
#define NGS2_MAX_RENDER_BYTES (1024u*1024u)
#define NGS2_HANDLE_BASE UINT64_C(0x00004e4700000000)
#define NGS2_SEGMENT_COMMAND_SIZE 0x20u
#define NGS2_SEGMENT_DESCRIPTOR_SIZE 0x20u
#define NGS2_SEGMENT_SAMPLE_BYTES 32u
#define NGS2_SEGMENT_LOG_LIMIT 64u
#define NGS2_SEGMENT_ADDRESS_HISTORY 32u
#define NGS2_ATRAC9_SEGMENT_LIMIT (16u*1024u*1024u)
#define NGS2_ATRAC9_VOICE_BUDGET (16u*1024u*1024u)
#define NGS2_ATRAC9_GLOBAL_BUDGET (128u*1024u*1024u)
#define NGS2_ATRAC9_EVENT_CAPACITY 8u
#define NGS2_ATRAC9_FIFO_CAPACITY 8u
#define NGS2_ATRAC9_MAX_CHANNELS 8u
#define NGS2_ATRAC9_MAX_FRAME_SAMPLES 256u
#define NGS2_ATRAC9_MAX_FRAMES_PER_SUPERFRAME 8u

/* Isolated HLE harnesses include this file without the production adapter and memory module. */
extern int runtime_memory_copy_snapshot(void *dst,uintptr_t address,uint64_t size) __attribute__((weak));
extern RuntimeAtrac9 *runtime_atrac9_create(void) __attribute__((weak));
extern void runtime_atrac9_release(RuntimeAtrac9 *decoder) __attribute__((weak));
extern int runtime_atrac9_init(RuntimeAtrac9 *decoder,const unsigned char config[4],
                               Atrac9CodecInfo *info) __attribute__((weak));
extern int runtime_atrac9_decode(RuntimeAtrac9 *decoder,const unsigned char *input,int16_t *pcm,
                                 int *bytes_used) __attribute__((weak));

typedef struct Ngs2Handle Ngs2Handle; /* Opaque token. Never dereferenced. */
typedef struct Ngs2System Ngs2System;
typedef struct Ngs2Rack Ngs2Rack;
typedef struct Ngs2Voice Ngs2Voice;
typedef struct { void *host_buffer; uint64_t host_size; uintptr_t reserved[5]; uintptr_t user_data; } Ngs2BufferInfo;
typedef struct { void *buffer; uint64_t size; uint32_t waveform, channels; } Ngs2RenderBuffer;

enum { NGS2_VOICE_STOPPED, NGS2_VOICE_PLAYING, NGS2_VOICE_PAUSED, NGS2_VOICE_RELEASING };
typedef struct {
    uint32_t format, channels, sample_rate, codec_config;
    uint32_t field_18, data_size, field_20;
    int valid;
} Ngs2Waveform;
typedef struct { uint32_t channel; uint16_t sub_target; float gain; int valid; } Ngs2ChannelGain;
typedef struct {
    uintptr_t address;
    uint32_t byte_count, sample_bytes;
    uint64_t fingerprint;
    int readable, valid;
} Ngs2SegmentObservation;
typedef struct {
    uintptr_t address;
    uint64_t fingerprint, sequence;
    uint32_t sample_bytes, rack_instance, voice_index;
    int readable, valid;
} Ngs2SegmentAddressHistory;
typedef struct {
    uint64_t sequence,generation;
    uintptr_t guest_address;
    uint32_t byte_count,flag_10,flag_14,descriptor_words[6];
    uint64_t descriptor_18;
    unsigned char *host_bytes;
    size_t cursor;
} Ngs2SegmentEvent;
typedef struct {
    uint64_t generation;
    uint32_t frames,channels;
    int16_t pcm[NGS2_ATRAC9_MAX_FRAME_SAMPLES*NGS2_ATRAC9_MAX_CHANNELS];
} Ngs2PcmGrain;
typedef struct {
    RuntimeAtrac9 *decoder;
    Atrac9CodecInfo info;
    uint64_t decoder_generation,generation;
    Ngs2SegmentEvent segments[NGS2_ATRAC9_EVENT_CAPACITY],active;
    uint32_t segment_read,segment_write,segment_count,segment_reservations;
    Ngs2PcmGrain fifo[NGS2_ATRAC9_FIFO_CAPACITY];
    uint32_t pcm_read,pcm_write,pcm_count;
    uint64_t queued_compressed_bytes,decoded_frames,decoded_bytes,decode_errors,cancellations;
    uint64_t events_enqueued,events_rejected;
    uint32_t max_segment_depth,max_fifo_depth,snapshot_inflight;
    int active_valid,decode_inflight,faulted,closing;
} Ngs2Atrac9Voice;

struct Ngs2Voice {
    Ngs2Handle *handle;
    Ngs2Rack *rack;
    uint32_t index, internal_state;
    uint32_t descriptor_words, descriptors[NGS2_MAX_DESCRIPTOR_WORDS];
    Ngs2ChannelGain command2[8], command3[8];
    Ngs2Handle *connection;
    Ngs2Rack *connection_rack;
    uint32_t connection_input, connection_output;
    Ngs2Waveform waveform;
    float pitch_ratio;
    uint32_t trace_last_command;
    uint64_t trace_last_command_sequence, trace_segment_sequence;
    Ngs2SegmentObservation trace_segment;
    Ngs2Atrac9Voice *atrac9;
};
struct Ngs2Rack {
    Ngs2Handle *handle;
    Ngs2System *system;
    Ngs2Rack *next;
    uint32_t rack_id, instance, capacity;
    char name[16];
    Ngs2Voice *voices;
};
struct Ngs2System {
    Ngs2Handle *handle;
    Ngs2System *next;
    Ngs2Rack *racks;
    uint32_t sample_rate, grain_samples, next_rack_instance;
    pthread_t worker;
    uint64_t compressed_bytes,snapshot_bytes,snapshot_us,decoded_frames,decoded_bytes;
    uint64_t decode_errors,cancellations,events_enqueued,events_rejected;
    uint32_t max_segment_depth,max_fifo_depth;
    int atrac9_enabled,worker_started,closing;
};

static pthread_mutex_t ngs2_lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t ngs2_cond=PTHREAD_COND_INITIALIZER;
static Ngs2System *ngs2_systems;
static uint64_t ngs2_next_handle=1;
static uint64_t ngs2_segment_sequence,ngs2_compressed_bytes,ngs2_compressed_bytes_max;
static unsigned ngs2_unknown_logs,ngs2_descriptor_logs;
static unsigned ngs2_atrac9_fault_logs;
static int ngs2_segment_trace_setting=-1;
static int ngs2_atrac9_setting=-1;
static uint64_t ngs2_trace_command_sequence;
static unsigned ngs2_segment_logs,ngs2_segment_history_next;
static Ngs2SegmentAddressHistory ngs2_segment_history[NGS2_SEGMENT_ADDRESS_HISTORY];

static uint16_t ngs2_u16(const void *data,size_t offset) {
    uint16_t value; memcpy(&value,(const unsigned char *)data+offset,sizeof(value)); return value;
}
static uint32_t ngs2_u32(const void *data,size_t offset) {
    uint32_t value; memcpy(&value,(const unsigned char *)data+offset,sizeof(value)); return value;
}
static uint64_t ngs2_u64(const void *data,size_t offset) {
    uint64_t value; memcpy(&value,(const unsigned char *)data+offset,sizeof(value)); return value;
}
static uintptr_t ngs2_uptr(const void *data,size_t offset) {
    uintptr_t value; memcpy(&value,(const unsigned char *)data+offset,sizeof(value)); return value;
}
static float ngs2_float(const void *data,size_t offset) {
    float value; memcpy(&value,(const unsigned char *)data+offset,sizeof(value)); return value;
}
static int ngs2_enabled_value(const char *value) {
    return value && (!strcmp(value,"1") || !strcmp(value,"true") || !strcmp(value,"yes"));
}
static int ngs2_ini_enabled(const char *key_name) {
    int enabled=0;
    const char *path=getenv("TLG_CONFIG");
    FILE *file=path && *path ? fopen(path,"rb") : NULL;
    if (!file) return 0;
    char line[256]; size_t key_size=strlen(key_name);
    while (fgets(line,sizeof(line),file)) {
        char *key=line; while (*key==' ' || *key=='\t') ++key;
        if (*key=='#') continue;
        char *equals=strchr(key,'=');
        if (!equals) continue;
        char *key_end=equals; while (key_end>key && (key_end[-1]==' ' || key_end[-1]=='\t')) --key_end;
        if ((size_t)(key_end-key)!=key_size || memcmp(key,key_name,key_size)) continue;
        char *value=equals+1; while (*value==' ' || *value=='\t') ++value;
        char *end=value+strlen(value); while (end>value && strchr(" \t\r\n",end[-1])) --end; *end=0;
        enabled=ngs2_enabled_value(value); break;
    }
    fclose(file); return enabled;
}
static int ngs2_segment_trace_enabled(void) {
    if (ngs2_segment_trace_setting>=0) return ngs2_segment_trace_setting;
    const char *environment=getenv("TLG_NGS2_SEGMENT_TRACE");
    if (environment) ngs2_segment_trace_setting=ngs2_enabled_value(environment);
    else {
        ngs2_segment_trace_setting=ngs2_ini_enabled("ngs2_segment_trace");
    }
    if (ngs2_segment_trace_setting)
        printf("Runtime: NGS2 VOICE_SEGMENT trace enabled (max %u events, %u-byte fingerprints)\n",
               NGS2_SEGMENT_LOG_LIMIT,NGS2_SEGMENT_SAMPLE_BYTES);
    return ngs2_segment_trace_setting;
}
static int ngs2_atrac9_enabled(void) {
    if (ngs2_atrac9_setting<0) {
        ngs2_atrac9_setting=ngs2_ini_enabled("ngs2_atrac9_hle");
        if (ngs2_atrac9_setting)
            printf("Runtime: NGS2 ATRAC9 HLE-2.2A1 enabled (decode-only, render remains silent)\n");
    }
    return ngs2_atrac9_setting;
}
static uint64_t ngs2_now_us(void) {
    struct timespec value;
    timespec_get(&value,TIME_UTC);
    return (uint64_t)value.tv_sec*UINT64_C(1000000)+(uint64_t)value.tv_nsec/1000u;
}
static int ngs2_range_end(uintptr_t address,uint64_t size,uintptr_t *end) {
    if (!address || !size || size>(uint64_t)UINTPTR_MAX || address>UINTPTR_MAX-(uintptr_t)size) return 0;
    *end=address+(uintptr_t)size; return 1;
}
static int ngs2_stack_range(uintptr_t address,uint64_t size) {
    uintptr_t end; if (!ngs2_range_end(address,size,&end)) return 0;
    void *base=NULL; size_t stack_size=0;
#ifdef _WIN32
    ULONG_PTR low=0,high=0; GetCurrentThreadStackLimits(&low,&high);
    base=(void *)low; stack_size=(size_t)(high-low);
#else
    pthread_attr_t attribute;
    if (!pthread_getattr_np(pthread_self(),&attribute)) {
        pthread_attr_getstack(&attribute,&base,&stack_size); pthread_attr_destroy(&attribute);
    }
#endif
    uintptr_t stack_low=(uintptr_t)base;
    return base && stack_size<=UINTPTR_MAX-stack_low && address>=stack_low && end<=stack_low+stack_size;
}
static uint64_t ngs2_fingerprint(const unsigned char *data,size_t size) {
    uint64_t hash=UINT64_C(1469598103934665603);
    for (size_t i=0;i<size;++i) { hash^=data[i]; hash*=UINT64_C(1099511628211); }
    return hash;
}
static int ngs2_observe_segment(uintptr_t address,uint32_t byte_count,Ngs2SegmentObservation *out) {
    memset(out,0,sizeof(*out)); out->address=address; out->byte_count=byte_count; out->valid=1;
    if (!byte_count || !runtime_memory_copy_snapshot) return 0;
    unsigned char sample[NGS2_SEGMENT_SAMPLE_BYTES];
    size_t sample_size=byte_count<sizeof(sample) ? byte_count : sizeof(sample);
    if (!runtime_memory_copy_snapshot(sample,address,sample_size)) return 0;
    out->sample_bytes=(uint32_t)sample_size; out->fingerprint=ngs2_fingerprint(sample,sample_size);
    out->readable=1; return 1;
}
static int ngs2_read_segment_descriptor(uintptr_t address,unsigned char descriptor[NGS2_SEGMENT_DESCRIPTOR_SIZE],
                                        const char **storage) {
    if (ngs2_stack_range(address,NGS2_SEGMENT_DESCRIPTOR_SIZE)) *storage="stack";
    else if (runtime_memory_copy_snapshot &&
             runtime_memory_copy_snapshot(descriptor,address,NGS2_SEGMENT_DESCRIPTOR_SIZE)) {
        *storage="guest"; return 1;
    }
    else { *storage="invalid"; return 0; }
    memcpy(descriptor,(const void *)address,NGS2_SEGMENT_DESCRIPTOR_SIZE); return 1;
}
static const char *ngs2_segment_lifetime(const Ngs2SegmentObservation *previous) {
    if (!previous->valid) return "none";
    Ngs2SegmentObservation current;
    if (!ngs2_observe_segment(previous->address,previous->byte_count,&current)) return "unreadable";
    return previous->readable && current.sample_bytes==previous->sample_bytes &&
           current.fingerprint==previous->fingerprint ? "stable" : "changed";
}
static const char *ngs2_segment_reuse(const Ngs2Voice *voice,const Ngs2SegmentObservation *current,
                                      uint64_t sequence) {
    const char *result="new";
    for (unsigned i=0;i<NGS2_SEGMENT_ADDRESS_HISTORY;++i) {
        Ngs2SegmentAddressHistory *old=&ngs2_segment_history[i];
        if (!old->valid || old->address!=current->address) continue;
        if (old->rack_instance!=voice->rack->instance || old->voice_index!=voice->index) result="other-voice";
        else if (old->readable && current->readable && old->sample_bytes==current->sample_bytes &&
                 old->fingerprint==current->fingerprint) result="same-bytes";
        else result="recycled-or-changed";
        *old=(Ngs2SegmentAddressHistory){current->address,current->fingerprint,sequence,current->sample_bytes,
                                        voice->rack->instance,voice->index,current->readable,1};
        return result;
    }
    Ngs2SegmentAddressHistory *slot=&ngs2_segment_history[ngs2_segment_history_next++%NGS2_SEGMENT_ADDRESS_HISTORY];
    *slot=(Ngs2SegmentAddressHistory){current->address,current->fingerprint,sequence,current->sample_bytes,
                                     voice->rack->instance,voice->index,current->readable,1};
    return result;
}
static void ngs2_trace_segment_locked(Ngs2Voice *voice,const void *params,uint16_t size,uint64_t sequence,
                                      uint32_t prior_command,uint64_t prior_sequence) {
    if (ngs2_segment_logs>=NGS2_SEGMENT_LOG_LIMIT) return;
    ++ngs2_segment_logs;
    if (size!=NGS2_SEGMENT_COMMAND_SIZE) {
        printf("Runtime: NGS2 VOICE_SEGMENT cmd=%llu rack=%#x/%u voice=%u rejected-for-trace size=%u expected=%u\n",
               (unsigned long long)sequence,voice->rack->rack_id,voice->rack->instance,voice->index,size,
               NGS2_SEGMENT_COMMAND_SIZE);
        return;
    }
    uintptr_t data_address=ngs2_uptr(params,0x08), descriptor_address=ngs2_uptr(params,0x18);
    uint32_t flag_10=ngs2_u32(params,0x10),flag_14=ngs2_u32(params,0x14);
    if (!data_address && !descriptor_address && !flag_10 && !flag_14) {
        const char *lifetime=ngs2_segment_lifetime(&voice->trace_segment);
        printf("Runtime: NGS2 VOICE_SEGMENT cmd=%llu rack=%#x/%u voice=%u reset life=%s\n",
               (unsigned long long)sequence,voice->rack->rack_id,voice->rack->instance,
               voice->index,lifetime);
        memset(&voice->trace_segment,0,sizeof(voice->trace_segment));
        return;
    }
    unsigned char descriptor[NGS2_SEGMENT_DESCRIPTOR_SIZE]; const char *descriptor_storage;
    if (!ngs2_read_segment_descriptor(descriptor_address,descriptor,&descriptor_storage)) {
        printf("Runtime: NGS2 VOICE_SEGMENT cmd=%llu rack=%#x/%u voice=%u data=%#llx meta=%#llx/%s flags=%u,%u\n",
               (unsigned long long)sequence,voice->rack->rack_id,voice->rack->instance,voice->index,
               (unsigned long long)data_address,(unsigned long long)descriptor_address,descriptor_storage,
               flag_10,flag_14);
        return;
    }
    uint32_t field_00=ngs2_u32(descriptor,0),byte_count=ngs2_u32(descriptor,4);
    uint32_t field_08=ngs2_u32(descriptor,8),field_0c=ngs2_u32(descriptor,0x0c);
    uint32_t field_10=ngs2_u32(descriptor,0x10),field_14=ngs2_u32(descriptor,0x14);
    uint64_t field_18=ngs2_u64(descriptor,0x18);
    const char *lifetime=ngs2_segment_lifetime(&voice->trace_segment);
    Ngs2SegmentObservation current; ngs2_observe_segment(data_address,byte_count,&current);
    const char *reuse=ngs2_segment_reuse(voice,&current,sequence);
    voice->trace_segment=current; ++voice->trace_segment_sequence;
    printf("Runtime: NGS2 VOICE_SEGMENT cmd=%llu seg=%llu rack=%#x/%u voice=%u prev=%#x@%llu "
           "fmt=%#x ch=%u rate=%u codec=%#x data=%#llx bytes=%u readable=%d hash=%016llx "
           "flags=%u,%u meta=%#llx/%s fields=%u,%u,%u,%u,%u,%u,%llu life=%s reuse=%s\n",
           (unsigned long long)sequence,(unsigned long long)voice->trace_segment_sequence,
           voice->rack->rack_id,voice->rack->instance,voice->index,prior_command,
           (unsigned long long)prior_sequence,voice->waveform.format,voice->waveform.channels,
           voice->waveform.sample_rate,voice->waveform.codec_config,(unsigned long long)data_address,
           byte_count,current.readable,(unsigned long long)current.fingerprint,flag_10,flag_14,
           (unsigned long long)descriptor_address,descriptor_storage,field_00,byte_count,field_08,
           field_0c,field_10,field_14,(unsigned long long)field_18,lifetime,reuse);
}
static Ngs2Handle *ngs2_new_handle_locked(void) {
    uintptr_t token=(uintptr_t)(NGS2_HANDLE_BASE|ngs2_next_handle++);
    if (!ngs2_next_handle) ngs2_next_handle=1;
    return (Ngs2Handle *)token;
}
static Ngs2System *ngs2_system_locked(const Ngs2Handle *handle) {
    if (!handle) return NULL;
    for (Ngs2System *system=ngs2_systems;system;system=system->next)
        if (system->handle==handle) return system;
    return NULL;
}
static Ngs2Rack *ngs2_rack_locked(const Ngs2Handle *handle) {
    if (!handle) return NULL;
    for (Ngs2System *system=ngs2_systems;system;system=system->next)
        for (Ngs2Rack *rack=system->racks;rack;rack=rack->next)
            if (rack->handle==handle) return rack;
    return NULL;
}
static Ngs2Voice *ngs2_voice_locked(const Ngs2Handle *handle) {
    if (!handle) return NULL;
    for (Ngs2System *system=ngs2_systems;system;system=system->next)
        for (Ngs2Rack *rack=system->racks;rack;rack=rack->next)
            for (uint32_t i=0;i<rack->capacity;++i)
                if (rack->voices[i].handle==handle) return &rack->voices[i];
    return NULL;
}
static int ngs2_system_option(const void *option,uint32_t *grain,uint32_t *rate) {
    *grain=256; *rate=48000;
    if (!option) return 1;
    uint32_t size=ngs2_u32(option,0);
    if (size<0x28 || size>NGS2_MAX_COMMAND_SIZE) return 0;
    *grain=ngs2_u32(option,0x20); *rate=ngs2_u32(option,0x24);
    return *grain && *grain<=2048 && *rate>=8000 && *rate<=192000;
}
static int ngs2_rack_option(const void *option,uint32_t *capacity,char name[16]) {
    if (!option) return 0;
    uint32_t size=ngs2_u32(option,0);
    if (size<0x24 || size>NGS2_MAX_COMMAND_SIZE) return 0;
    *capacity=ngs2_u32(option,0x20);
    if (!*capacity || *capacity>NGS2_MAX_RACK_VOICES) return 0;
    memset(name,0,16);
    if (size>=0x18) memcpy(name,(const unsigned char *)option+8,15);
    return 1;
}
static uint32_t ngs2_atrac9_depth(const Ngs2Atrac9Voice *state) {
    return state->segment_count+state->segment_reservations+(state->active_valid ? 1u : 0u);
}
static void ngs2_atrac9_account_drop_locked(Ngs2System *system,Ngs2Atrac9Voice *state,
                                            Ngs2SegmentEvent *event) {
    uint64_t bytes=event->byte_count;
    if (bytes<=state->queued_compressed_bytes) state->queued_compressed_bytes-=bytes;
    else state->queued_compressed_bytes=0;
    if (bytes<=system->compressed_bytes) system->compressed_bytes-=bytes;
    else system->compressed_bytes=0;
    if (bytes<=ngs2_compressed_bytes) ngs2_compressed_bytes-=bytes;
    else ngs2_compressed_bytes=0;
    free(event->host_bytes); memset(event,0,sizeof(*event));
}
static void ngs2_atrac9_clear_pending_locked(Ngs2Voice *voice,int count_cancellation) {
    Ngs2Atrac9Voice *state=voice->atrac9;
    if (!state) return;
    Ngs2System *system=voice->rack->system;
    int had_work=state->segment_count || state->segment_reservations || state->active_valid ||
                 state->decode_inflight || state->pcm_count;
    while (state->segment_count) {
        Ngs2SegmentEvent *event=&state->segments[state->segment_read];
        ngs2_atrac9_account_drop_locked(system,state,event);
        state->segment_read=(state->segment_read+1u)%NGS2_ATRAC9_EVENT_CAPACITY;
        --state->segment_count;
    }
    if (state->active_valid && !state->decode_inflight) {
        ngs2_atrac9_account_drop_locked(system,state,&state->active); state->active_valid=0;
    }
    state->pcm_read=state->pcm_write=state->pcm_count=0; state->faulted=0;
    ++state->generation;
    if (!state->generation) ++state->generation;
    if (count_cancellation && had_work) {
        ++state->cancellations; ++system->cancellations;
    }
    pthread_cond_broadcast(&ngs2_cond);
}
static void ngs2_atrac9_close_voice_locked(Ngs2Voice *voice) {
    Ngs2Atrac9Voice *state=voice->atrac9;
    if (!state) return;
    state->closing=1; ngs2_atrac9_clear_pending_locked(voice,1);
}
static int ngs2_rack_busy_locked(const Ngs2Rack *rack) {
    for (uint32_t i=0;i<rack->capacity;++i) {
        const Ngs2Atrac9Voice *state=rack->voices[i].atrac9;
        if (state && (state->decode_inflight || state->snapshot_inflight)) return 1;
    }
    return 0;
}
static void ngs2_free_rack_resources(Ngs2Rack *rack) {
    for (uint32_t i=0;i<rack->capacity;++i) {
        Ngs2Atrac9Voice *state=rack->voices[i].atrac9;
        if (!state) continue;
        if (state->decoder && runtime_atrac9_release) runtime_atrac9_release(state->decoder);
        free(state); rack->voices[i].atrac9=NULL;
    }
    free(rack->voices); free(rack);
}
static int ngs2_atrac9_info_valid(const Atrac9CodecInfo *info,const Ngs2Waveform *waveform) {
    return info->channels>=1 && info->channels<=(int)NGS2_ATRAC9_MAX_CHANNELS &&
           info->frameSamples>=1 && info->frameSamples<=(int)NGS2_ATRAC9_MAX_FRAME_SAMPLES &&
           info->framesInSuperframe>=1 &&
           info->framesInSuperframe<=(int)NGS2_ATRAC9_MAX_FRAMES_PER_SUPERFRAME &&
           info->superframeSize>=info->framesInSuperframe &&
           info->samplingRate==(int)waveform->sample_rate && info->channels==(int)waveform->channels;
}
static void ngs2_atrac9_config(uint32_t value,unsigned char config[4]) {
    config[0]=(unsigned char)(value>>24); config[1]=(unsigned char)(value>>16);
    config[2]=(unsigned char)(value>>8); config[3]=(unsigned char)value;
}
static Ngs2Voice *ngs2_worker_voice_locked(Ngs2System *system) {
    for (Ngs2Rack *rack=system->racks;rack;rack=rack->next)
        for (uint32_t i=0;i<rack->capacity;++i) {
            Ngs2Voice *voice=&rack->voices[i]; Ngs2Atrac9Voice *state=voice->atrac9;
            if (!state || state->closing || state->faulted || state->decode_inflight ||
                voice->internal_state!=NGS2_VOICE_PLAYING) continue;
            if (!state->active_valid && state->segment_count) {
                state->active=state->segments[state->segment_read];
                memset(&state->segments[state->segment_read],0,sizeof(state->segments[0]));
                state->segment_read=(state->segment_read+1u)%NGS2_ATRAC9_EVENT_CAPACITY;
                --state->segment_count; state->active_valid=1;
            }
            if (!state->active_valid) continue;
            if (state->active.generation!=state->generation) {
                ngs2_atrac9_account_drop_locked(system,state,&state->active); state->active_valid=0;
                continue;
            }
            uint32_t frames=state->decoder_generation==state->generation ?
                            (uint32_t)state->info.framesInSuperframe : NGS2_ATRAC9_FIFO_CAPACITY;
            if (frames>NGS2_ATRAC9_FIFO_CAPACITY-state->pcm_count) continue;
            return voice;
        }
    return NULL;
}
static void *ngs2_atrac9_worker(void *opaque) {
    Ngs2System *system=opaque;
    pthread_mutex_lock(&ngs2_lock);
    for (;;) {
        Ngs2Voice *voice=ngs2_worker_voice_locked(system);
        if (!voice) {
            if (system->closing) break;
            pthread_cond_wait(&ngs2_cond,&ngs2_lock); continue;
        }
        Ngs2Atrac9Voice *state=voice->atrac9;
        uint64_t generation=state->generation;
        RuntimeAtrac9 *decoder=state->decoder,*old_decoder=NULL;
        int need_init=!decoder || state->decoder_generation!=generation;
        if (need_init) { old_decoder=decoder; state->decoder=NULL; decoder=NULL; }
        unsigned char config[4]; ngs2_atrac9_config(voice->waveform.codec_config,config);
        Ngs2Waveform waveform=voice->waveform;
        size_t cursor=state->active.cursor;
        unsigned char *input=state->active.host_bytes;
        uint32_t byte_count=state->active.byte_count;
        state->decode_inflight=1;
        pthread_mutex_unlock(&ngs2_lock);

        if (old_decoder && runtime_atrac9_release) runtime_atrac9_release(old_decoder);
        Atrac9CodecInfo info; memset(&info,0,sizeof(info)); int error=0;
        if (need_init) {
            if (!runtime_atrac9_create || !runtime_atrac9_init || !runtime_atrac9_decode)
                error=-1;
            else if (!(decoder=runtime_atrac9_create())) error=-1;
            else if ((error=runtime_atrac9_init(decoder,config,&info)) ||
                     !ngs2_atrac9_info_valid(&info,&waveform)) error=error ? error : -1;
        } else info=state->info;
        Ngs2PcmGrain decoded[NGS2_ATRAC9_MAX_FRAMES_PER_SUPERFRAME];
        memset(decoded,0,sizeof(decoded)); uint32_t frame_count=0; uint32_t consumed=0;
        uint64_t decode_start=ngs2_now_us();
        if (!error && (uint64_t)cursor+(uint64_t)info.superframeSize<=byte_count) {
            for (int frame=0;frame<info.framesInSuperframe;++frame) {
                int used=0; decoded[frame].generation=generation;
                decoded[frame].frames=(uint32_t)info.frameSamples;
                decoded[frame].channels=(uint32_t)info.channels;
                error=runtime_atrac9_decode(decoder,input+cursor+consumed,decoded[frame].pcm,&used);
                if (error || used<=0 || consumed+(uint32_t)used>(uint32_t)info.superframeSize) {
                    error=error ? error : -1; break;
                }
                consumed+=(uint32_t)used; ++frame_count;
            }
            if (!error && consumed!=(uint32_t)info.superframeSize) error=-1;
        } else if (!error) error=-1;
        uint64_t decode_us=ngs2_now_us()-decode_start;

        RuntimeAtrac9 *release_decoder=NULL;
        pthread_mutex_lock(&ngs2_lock);
        state=voice->atrac9;
        int current=state && !state->closing && state->generation==generation &&
                    state->active_valid && state->active.generation==generation;
        if (need_init) {
            if (current && !error) {
                state->decoder=decoder; state->decoder_generation=generation; state->info=info;
                decoder=NULL;
            } else release_decoder=decoder;
        }
        if (current && !error && frame_count<=(NGS2_ATRAC9_FIFO_CAPACITY-state->pcm_count)) {
            for (uint32_t i=0;i<frame_count;++i) {
                state->fifo[state->pcm_write]=decoded[i];
                state->pcm_write=(state->pcm_write+1u)%NGS2_ATRAC9_FIFO_CAPACITY; ++state->pcm_count;
            }
            state->active.cursor+=consumed; state->decoded_frames+=frame_count;
            state->decoded_bytes+=consumed; system->decoded_frames+=frame_count;
            system->decoded_bytes+=consumed;
            if (state->pcm_count>state->max_fifo_depth) state->max_fifo_depth=state->pcm_count;
            if (state->pcm_count>system->max_fifo_depth) system->max_fifo_depth=state->pcm_count;
            if (state->active.cursor==state->active.byte_count) {
                ngs2_atrac9_account_drop_locked(system,state,&state->active); state->active_valid=0;
            }
        } else if (current && error) {
            ++state->decode_errors; ++system->decode_errors; state->faulted=1;
            ngs2_atrac9_account_drop_locked(system,state,&state->active); state->active_valid=0;
            while (state->segment_count) {
                Ngs2SegmentEvent *event=&state->segments[state->segment_read];
                ngs2_atrac9_account_drop_locked(system,state,event);
                state->segment_read=(state->segment_read+1u)%NGS2_ATRAC9_EVENT_CAPACITY;
                --state->segment_count;
            }
            if (ngs2_atrac9_fault_logs<16) {
                ++ngs2_atrac9_fault_logs;
                printf("Runtime: NGS2 ATRAC9 voice fault rack=%#x/%u voice=%u generation=%llu error=%d decode_us=%llu\n",
                       voice->rack->rack_id,voice->rack->instance,voice->index,
                       (unsigned long long)generation,error,(unsigned long long)decode_us);
            }
        } else if (state && state->active_valid && state->active.generation==generation) {
            ngs2_atrac9_account_drop_locked(system,state,&state->active); state->active_valid=0;
        }
        if (state) state->decode_inflight=0;
        pthread_cond_broadcast(&ngs2_cond);
        pthread_mutex_unlock(&ngs2_lock);
        if (release_decoder && runtime_atrac9_release) runtime_atrac9_release(release_decoder);
        pthread_mutex_lock(&ngs2_lock);
    }
    pthread_mutex_unlock(&ngs2_lock); return NULL;
}
static void ngs2_clear_connections_locked(const Ngs2Rack *removed) {
    for (Ngs2System *system=ngs2_systems;system;system=system->next)
        for (Ngs2Rack *rack=system->racks;rack;rack=rack->next)
            for (uint32_t i=0;i<rack->capacity;++i) {
                Ngs2Voice *voice=&rack->voices[i];
                if (voice->connection_rack==removed) { voice->connection=NULL; voice->connection_rack=NULL; }
            }
}
static void ngs2_detach_rack_locked(Ngs2Rack *rack) {
    ngs2_clear_connections_locked(rack);
    for (uint32_t i=0;i<rack->capacity;++i) ngs2_atrac9_close_voice_locked(&rack->voices[i]);
}
static ABI int32_t ngs2_system_create(const void *option,const void *allocator,Ngs2Handle **out) {
    (void)allocator; /* Guest callbacks are not invoked by HLE-1. */
    if (!out) return NGS2_INVALID;
    *out=NULL;
    uint32_t grain,rate;
    if (!ngs2_system_option(option,&grain,&rate)) return NGS2_INVALID;
    Ngs2System *system=calloc(1,sizeof(*system));
    if (!system) return NGS2_INVALID;
    system->grain_samples=grain; system->sample_rate=rate;
    system->atrac9_enabled=ngs2_atrac9_enabled();
    pthread_mutex_lock(&ngs2_lock);
    system->handle=ngs2_new_handle_locked(); system->next=ngs2_systems; ngs2_systems=system;
    if (system->atrac9_enabled) {
        if (!pthread_create(&system->worker,NULL,ngs2_atrac9_worker,system)) system->worker_started=1;
        else {
            system->atrac9_enabled=0;
            printf("Runtime: NGS2 ATRAC9 worker unavailable; system remains HLE-1 silent\n");
        }
    }
    *out=system->handle;
    static int logged;
    if (!logged) { logged=1; printf("Runtime: NGS2 HLE core initialized (silent, %u Hz, %u samples)\n",rate,grain); }
    pthread_mutex_unlock(&ngs2_lock);
    return 0;
}
static ABI int32_t ngs2_system_destroy(Ngs2Handle *handle,Ngs2BufferInfo *info) {
    pthread_mutex_lock(&ngs2_lock);
    Ngs2System **link=&ngs2_systems;
    while (*link && (*link)->handle!=handle) link=&(*link)->next;
    if (!*link) { pthread_mutex_unlock(&ngs2_lock); return NGS2_INVALID; }
    Ngs2System *system=*link; system->closing=1;
    for (Ngs2Rack *rack=system->racks;rack;rack=rack->next) ngs2_detach_rack_locked(rack);
    *link=system->next; pthread_cond_broadcast(&ngs2_cond);
    pthread_mutex_unlock(&ngs2_lock);
    if (system->worker_started) pthread_join(system->worker,NULL);
    pthread_mutex_lock(&ngs2_lock);
    for (Ngs2Rack *rack=system->racks;rack;rack=rack->next)
        while (ngs2_rack_busy_locked(rack)) pthread_cond_wait(&ngs2_cond,&ngs2_lock);
    pthread_mutex_unlock(&ngs2_lock);
    if (system->atrac9_enabled)
        printf("Runtime: NGS2 ATRAC9 summary events=%llu rejected=%llu snapshots=%llu bytes/%llu us "
               "decoded=%llu frames/%llu bytes errors=%llu cancellations=%llu queue_max=%u fifo_max=%u\n",
               (unsigned long long)system->events_enqueued,(unsigned long long)system->events_rejected,
               (unsigned long long)system->snapshot_bytes,(unsigned long long)system->snapshot_us,
               (unsigned long long)system->decoded_frames,(unsigned long long)system->decoded_bytes,
               (unsigned long long)system->decode_errors,(unsigned long long)system->cancellations,
               system->max_segment_depth,system->max_fifo_depth);
    while (system->racks) {
        Ngs2Rack *rack=system->racks; system->racks=rack->next; ngs2_free_rack_resources(rack);
    }
    free(system);
    if (info) memset(info,0,sizeof(*info));
    return 0;
}
static ABI int32_t ngs2_render(Ngs2Handle *handle,const Ngs2RenderBuffer *buffers,uint32_t count) {
    pthread_mutex_lock(&ngs2_lock);
    int valid=ngs2_system_locked(handle)!=NULL;
    pthread_mutex_unlock(&ngs2_lock);
    if (!valid || (count && !buffers)) return NGS2_INVALID;
    for (uint32_t i=0;i<count;++i) {
        if (buffers[i].size>NGS2_MAX_RENDER_BYTES) return NGS2_INVALID;
        if (buffers[i].buffer) memset(buffers[i].buffer,0,(size_t)buffers[i].size);
    }
    return 0;
}
static ABI int32_t ngs2_query_buffer(uint32_t rack_id,const void *option,Ngs2BufferInfo *info) {
    (void)rack_id;
    uint32_t capacity; char name[16];
    if (!info || !ngs2_rack_option(option,&capacity,name)) return NGS2_INVALID;
    memset(info,0,sizeof(*info)); info->host_size=4096; return 0;
}
static ABI int32_t ngs2_rack_create(Ngs2Handle *system_handle,uint32_t rack_id,const void *option,const void *buffer,Ngs2Handle **out) {
    (void)buffer; /* Host storage and guest allocator callbacks remain separate in HLE-1. */
    if (!out) return NGS2_INVALID;
    *out=NULL;
    uint32_t capacity; char name[16];
    if (!ngs2_rack_option(option,&capacity,name)) return NGS2_INVALID;
    Ngs2Rack *rack=calloc(1,sizeof(*rack));
    Ngs2Voice *voices=calloc(capacity,sizeof(*voices));
    if (!rack || !voices) { free(voices); free(rack); return NGS2_INVALID; }
    rack->rack_id=rack_id; rack->capacity=capacity; rack->voices=voices; memcpy(rack->name,name,sizeof(rack->name));
    pthread_mutex_lock(&ngs2_lock);
    Ngs2System *system=ngs2_system_locked(system_handle);
    if (!system) { pthread_mutex_unlock(&ngs2_lock); free(voices); free(rack); return NGS2_INVALID; }
    rack->system=system; rack->instance=++system->next_rack_instance; rack->handle=ngs2_new_handle_locked();
    for (uint32_t i=0;i<capacity;++i) { voices[i].rack=rack; voices[i].index=i; voices[i].pitch_ratio=1.0f; }
    rack->next=system->racks; system->racks=rack; *out=rack->handle;
    pthread_mutex_unlock(&ngs2_lock);
    return 0;
}
static ABI int32_t ngs2_rack_destroy(Ngs2Handle *handle,Ngs2BufferInfo *info) {
    pthread_mutex_lock(&ngs2_lock);
    Ngs2Rack *rack=ngs2_rack_locked(handle);
    if (!rack) { pthread_mutex_unlock(&ngs2_lock); return NGS2_INVALID; }
    Ngs2Rack **link=&rack->system->racks;
    while (*link!=rack) link=&(*link)->next;
    ngs2_detach_rack_locked(rack); *link=rack->next;
    while (ngs2_rack_busy_locked(rack)) pthread_cond_wait(&ngs2_cond,&ngs2_lock);
    pthread_mutex_unlock(&ngs2_lock);
    ngs2_free_rack_resources(rack);
    if (info) memset(info,0,sizeof(*info));
    return 0;
}
static ABI int32_t ngs2_voice_handle(Ngs2Handle *rack_handle,uint32_t index,Ngs2Handle **out) {
    if (!out) return NGS2_INVALID;
    *out=NULL;
    pthread_mutex_lock(&ngs2_lock);
    Ngs2Rack *rack=ngs2_rack_locked(rack_handle);
    if (!rack || index>=rack->capacity) { pthread_mutex_unlock(&ngs2_lock); return NGS2_INVALID; }
    Ngs2Voice *voice=&rack->voices[index];
    if (!voice->handle) voice->handle=ngs2_new_handle_locked();
    *out=voice->handle;
    pthread_mutex_unlock(&ngs2_lock);
    return 0;
}
static void ngs2_atrac9_reject_locked(Ngs2System *system,Ngs2Atrac9Voice *state) {
    ++system->events_rejected;
    if (state) ++state->events_rejected;
}
static int32_t ngs2_segment_control(Ngs2Handle *handle,const void *params,uint16_t size) {
    uintptr_t data_address=size>=0x10 ? ngs2_uptr(params,0x08) : 0;
    uint32_t flag_10=size>=0x14 ? ngs2_u32(params,0x10) : 0;
    uint32_t flag_14=size>=0x18 ? ngs2_u32(params,0x14) : 0;
    uintptr_t descriptor_address=size>=0x20 ? ngs2_uptr(params,0x18) : 0;
    unsigned char descriptor[NGS2_SEGMENT_DESCRIPTOR_SIZE];

    pthread_mutex_lock(&ngs2_lock);
    Ngs2Voice *voice=ngs2_voice_locked(handle);
    if (!voice) { pthread_mutex_unlock(&ngs2_lock); return NGS2_INVALID; }
    uint64_t trace_sequence=ngs2_segment_trace_enabled() ? ++ngs2_trace_command_sequence : 0;
    uint32_t prior_command=voice->trace_last_command;
    uint64_t prior_sequence=voice->trace_last_command_sequence;
    if (trace_sequence) {
        ngs2_trace_segment_locked(voice,params,size,trace_sequence,prior_command,prior_sequence);
        voice->trace_last_command=UINT32_C(0x10000001);
        voice->trace_last_command_sequence=trace_sequence;
    }
    Ngs2System *system=voice->rack->system;
    if (!system->atrac9_enabled) { pthread_mutex_unlock(&ngs2_lock); return 0; }
    if (!data_address && !descriptor_address && !flag_10 && !flag_14) {
        pthread_mutex_unlock(&ngs2_lock); return 0;
    }
    const char *storage="invalid";
    int descriptor_ok=size==NGS2_SEGMENT_COMMAND_SIZE && descriptor_address &&
                      ngs2_read_segment_descriptor(descriptor_address,descriptor,&storage);
    uint32_t byte_count=descriptor_ok ? ngs2_u32(descriptor,4) : 0;
    Ngs2Atrac9Voice *state=voice->atrac9;
    if (!state) {
        state=calloc(1,sizeof(*state));
        if (state) { state->generation=1; voice->atrac9=state; }
    }
    if (!state || !voice->waveform.valid || !descriptor_ok || !byte_count ||
        byte_count>NGS2_ATRAC9_SEGMENT_LIMIT || !runtime_memory_copy_snapshot ||
        ngs2_atrac9_depth(state)>=NGS2_ATRAC9_EVENT_CAPACITY ||
        state->queued_compressed_bytes+byte_count>NGS2_ATRAC9_VOICE_BUDGET ||
        ngs2_compressed_bytes+byte_count>NGS2_ATRAC9_GLOBAL_BUDGET) {
        ngs2_atrac9_reject_locked(system,state); pthread_mutex_unlock(&ngs2_lock); return 0;
    }
    uint64_t generation=state->generation,sequence=++ngs2_segment_sequence;
    ++state->segment_reservations; ++state->snapshot_inflight;
    state->queued_compressed_bytes+=byte_count; system->compressed_bytes+=byte_count;
    ngs2_compressed_bytes+=byte_count;
    if (ngs2_compressed_bytes>ngs2_compressed_bytes_max) ngs2_compressed_bytes_max=ngs2_compressed_bytes;
    uint32_t depth=ngs2_atrac9_depth(state);
    if (depth>state->max_segment_depth) state->max_segment_depth=depth;
    if (depth>system->max_segment_depth) system->max_segment_depth=depth;
    pthread_mutex_unlock(&ngs2_lock);

    uint64_t snapshot_start=ngs2_now_us();
    unsigned char *host_bytes=malloc(byte_count);
    int copied=host_bytes && runtime_memory_copy_snapshot(host_bytes,data_address,byte_count);
    uint64_t snapshot_us=ngs2_now_us()-snapshot_start;

    pthread_mutex_lock(&ngs2_lock);
    --state->segment_reservations; --state->snapshot_inflight;
    int accept=copied && !state->closing && state->generation==generation &&
               state->segment_count<NGS2_ATRAC9_EVENT_CAPACITY;
    if (accept) {
        Ngs2SegmentEvent *event=&state->segments[state->segment_write];
        memset(event,0,sizeof(*event)); event->sequence=sequence; event->generation=generation;
        event->guest_address=data_address; event->byte_count=byte_count;
        event->flag_10=flag_10; event->flag_14=flag_14; event->host_bytes=host_bytes;
        for (unsigned i=0;i<6;++i) event->descriptor_words[i]=ngs2_u32(descriptor,i*4u);
        event->descriptor_18=ngs2_u64(descriptor,0x18);
        state->segment_write=(state->segment_write+1u)%NGS2_ATRAC9_EVENT_CAPACITY;
        ++state->segment_count; ++state->events_enqueued; ++system->events_enqueued;
        system->snapshot_bytes+=byte_count; system->snapshot_us+=snapshot_us;
    } else {
        Ngs2SegmentEvent rejected; memset(&rejected,0,sizeof(rejected));
        rejected.byte_count=byte_count; rejected.host_bytes=host_bytes;
        ngs2_atrac9_account_drop_locked(system,state,&rejected);
        ngs2_atrac9_reject_locked(system,state);
    }
    pthread_cond_broadcast(&ngs2_cond); pthread_mutex_unlock(&ngs2_lock);
    return 0;
}
static int32_t ngs2_control_locked(Ngs2Voice *voice,const void *params,uint16_t size,uint32_t command,
                                   uint64_t trace_sequence,uint32_t prior_command,uint64_t prior_sequence) {
    switch (command) {
    case 1: {
        if (size<0x18) return NGS2_INVALID;
        uint32_t words=ngs2_u32(params,0x0c);
        const uint32_t *source=(const uint32_t *)ngs2_uptr(params,0x10);
        if (words>NGS2_MAX_DESCRIPTOR_WORDS || (words && !source))
            return NGS2_INVALID;
        if (words) memcpy(voice->descriptors,source,(size_t)words*sizeof(*source));
        if (words<voice->descriptor_words)
            memset(voice->descriptors+words,0,(voice->descriptor_words-words)*sizeof(*source));
        voice->descriptor_words=words;
        if (ngs2_descriptor_logs<8) {
            unsigned nonzero=0; for (uint32_t i=0;i<words;++i) nonzero+=voice->descriptors[i]!=0;
            printf("Runtime: NGS2 descriptor snapshot rack %#x voice %u: %u dwords, %u nonzero\n",
                   voice->rack->rack_id,voice->index,words,nonzero);
            ++ngs2_descriptor_logs;
        }
        return 0;
    }
    case 2: case 3: {
        if (size<0x10) return NGS2_INVALID;
        uint32_t channel=ngs2_u32(params,8); float gain=ngs2_float(params,0x0c);
        if (channel>=8 || !isfinite(gain)) return NGS2_INVALID;
        Ngs2ChannelGain *slot=command==2 ? &voice->command2[channel] : &voice->command3[channel];
        *slot=(Ngs2ChannelGain){channel,ngs2_u16(params,2),gain,1}; return 0;
    }
    case 5: {
        if (size<0x18) return NGS2_INVALID;
        Ngs2Handle *target=(Ngs2Handle *)ngs2_uptr(params,0x10);
        Ngs2Voice *target_voice=target ? ngs2_voice_locked(target) : NULL;
        if (target && !target_voice) return NGS2_INVALID;
        voice->connection_input=ngs2_u32(params,8); voice->connection_output=ngs2_u32(params,0x0c);
        voice->connection=target; voice->connection_rack=target_voice ? target_voice->rack : NULL; return 0;
    }
    case 6: {
        if (size<0x0c) return NGS2_INVALID;
        switch (ngs2_u32(params,8)) {
        case 0: voice->internal_state=NGS2_VOICE_PLAYING; pthread_cond_broadcast(&ngs2_cond); break;
        case 1: if (voice->internal_state==NGS2_VOICE_PLAYING) voice->internal_state=NGS2_VOICE_PAUSED; break;
        case 2: if (voice->internal_state==NGS2_VOICE_PAUSED) {
            voice->internal_state=NGS2_VOICE_PLAYING; pthread_cond_broadcast(&ngs2_cond);
        } break;
        case 4:
            if (voice->internal_state!=NGS2_VOICE_STOPPED) voice->internal_state=NGS2_VOICE_RELEASING;
            ngs2_atrac9_clear_pending_locked(voice,1); break;
        case 5: voice->internal_state=NGS2_VOICE_STOPPED; ngs2_atrac9_clear_pending_locked(voice,1); break;
        default: break; /* Preserve boot compatibility for unclassified events. */
        }
        return 0;
    }
    case UINT32_C(0x10000000): {
        if (size<0x28) return NGS2_INVALID;
        Ngs2Waveform value={ngs2_u32(params,8),ngs2_u32(params,0x0c),ngs2_u32(params,0x10),
            ngs2_u32(params,0x14),ngs2_u32(params,0x18),ngs2_u32(params,0x1c),ngs2_u32(params,0x20),1};
        if (!value.channels || value.channels>8 || value.sample_rate<8000 || value.sample_rate>192000)
            return NGS2_INVALID;
        if (voice->atrac9 && (!voice->waveform.valid || memcmp(&voice->waveform,&value,sizeof(value))))
            ngs2_atrac9_clear_pending_locked(voice,1);
        voice->waveform=value; return 0;
    }
    case UINT32_C(0x10000001):
        if (trace_sequence)
            ngs2_trace_segment_locked(voice,params,size,trace_sequence,prior_command,prior_sequence);
        return 0;
    case UINT32_C(0x10000005): {
        if (size<0x10) return NGS2_INVALID;
        float ratio=ngs2_float(params,8);
        if (!isfinite(ratio) || ratio<=0.0f || ratio>16.0f) return NGS2_INVALID;
        voice->pitch_ratio=ratio; return 0;
    }
    default:
        if (ngs2_unknown_logs<32) {
            printf("Runtime: NGS2 command %#x size %u accepted without state change\n",command,size);
            ++ngs2_unknown_logs;
        }
        return 0;
    }
}
static ABI int32_t ngs2_voice_control(Ngs2Handle *handle,const void *params) {
    if (!params) return NGS2_INVALID;
    uint16_t size=ngs2_u16(params,0); uint32_t command=ngs2_u32(params,4);
    if (size<8 || size>NGS2_MAX_COMMAND_SIZE) return NGS2_INVALID;
    if (command==UINT32_C(0x10000001)) return ngs2_segment_control(handle,params,size);
    pthread_mutex_lock(&ngs2_lock);
    Ngs2Voice *voice=ngs2_voice_locked(handle);
    uint64_t trace_sequence=voice && ngs2_segment_trace_enabled() ? ++ngs2_trace_command_sequence : 0;
    uint32_t prior_command=voice ? voice->trace_last_command : 0;
    uint64_t prior_sequence=voice ? voice->trace_last_command_sequence : 0;
    int32_t result=voice ? ngs2_control_locked(voice,params,size,command,trace_sequence,
                                               prior_command,prior_sequence) : NGS2_INVALID;
    if (voice && trace_sequence && !result) {
        voice->trace_last_command=command; voice->trace_last_command_sequence=trace_sequence;
    }
    pthread_mutex_unlock(&ngs2_lock);
    return result;
}
static ABI int32_t ngs2_voice_state(Ngs2Handle *handle,void *state,uint64_t size) {
    if (!state || (size!=0x10 && size!=0x30)) return NGS2_INVALID;
    pthread_mutex_lock(&ngs2_lock);
    int valid=ngs2_voice_locked(handle)!=NULL;
    pthread_mutex_unlock(&ngs2_lock);
    if (!valid) return NGS2_INVALID;
    memset(state,0,(size_t)size); /* External legacy state: idle, never a fabricated FINISHED state. */
    return 0;
}

static const RuntimeExport exports[]={
    {"sceKernelCreateEventFlag",ef_create}, {"sceKernelDeleteEventFlag",ef_delete},
    {"sceKernelSetEventFlag",ef_set}, {"sceKernelClearEventFlag",ef_clear},
    {"sceKernelPollEventFlag",ef_poll}, {"sceKernelWaitEventFlag",ef_wait},
    {"sceKernelCancelEventFlag",ef_cancel},
    {"sceKernelIsNeoMode",is_neo_mode}, {"sceKernelAvailableFlexibleMemorySize",available_flexible_memory},
    {"sceKernelGetdents",getdents}, {"sceKernelGetPrtAperture",prt_aperture},
    {"sceKernelGetModuleInfoForUnwind",module_info_for_unwind},
    {"sceKernelInternalMemoryGetModuleSegmentInfo",module_segment_info},
    {"sceKernelConvertUtcToLocaltime",utc_to_local}, {"sceKernelConvertLocaltimeToUtc",local_to_utc},
    {"sceLibcHeapGetTraceInfo",libc_heap_trace_info},
    {"sceKernelGetCompiledSdkVersion",compiled_sdk_version}, {"pthread_equal",posix_pthread_equal},
    {"sincosf",guest_sincosf},
    {"sceSystemServiceLoadExec",load_exec},
    {"sceAudioOutSetMixLevelPadSpk",pad_speaker_mix}, {"scePadSetMotionSensorState",pad_motion_sensor},
    {"sceDiscMap_7C980FFB0AA27E7A",discmap_7c98},
    {"scePlayGoDialogInitialize",playgo_dialog_init}, {"scePlayGoDialogOpen",playgo_dialog_open},
    {"scePlayGoDialogUpdateStatus",playgo_dialog_status}, {"scePlayGoDialogGetResult",playgo_dialog_result},
    {"scePlayGoDialogTerminate",playgo_dialog_term},
    {"sceNgs2SystemCreateWithAllocator",ngs2_system_create}, {"sceNgs2SystemDestroy",ngs2_system_destroy},
    {"sceNgs2SystemRender",ngs2_render}, {"sceNgs2RackQueryBufferSize",ngs2_query_buffer},
    {"sceNgs2RackCreate",ngs2_rack_create}, {"sceNgs2RackCreateWithAllocator",ngs2_rack_create},
    {"sceNgs2RackDestroy",ngs2_rack_destroy}, {"sceNgs2RackGetVoiceHandle",ngs2_voice_handle},
    {"sceNgs2VoiceControl",ngs2_voice_control}, {"sceNgs2VoiceGetState",ngs2_voice_state},
};
/* TLG_NGS2=native: libSceNgs2.sprx from the user's console firmware is linked as a module
 * (run_tlg.sh), so its exports must win over the silent placeholders above. */
static int ngs2_native(void) {
    static int value = -1;
    if (value < 0) { const char *v = getenv("TLG_NGS2"); value = v && !strcmp(v, "native"); }
    return value;
}
uintptr_t runtime_tlg_resolve(const char *name) {
    if (ngs2_native()) {
        const char *symbol = runtime_symbol(name);
        if (symbol && !strncmp(symbol, "sceNgs2", 7)) return 0;
    }
    return RUNTIME_LOOKUP(exports,name);
}
