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
#include <stdatomic.h>
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
#define NGS2_CODEC_LOG_LIMIT 96u
#define NGS2_SEGMENT_ADDRESS_HISTORY 32u
#define NGS2_ATRAC9_SEGMENT_LIMIT (16u*1024u*1024u)
#define NGS2_ATRAC9_VOICE_BUDGET (16u*1024u*1024u)
#define NGS2_ATRAC9_GLOBAL_BUDGET (128u*1024u*1024u)
#define NGS2_ATRAC9_EVENT_CAPACITY 8u
#define NGS2_ATRAC9_FIFO_CAPACITY 8u
#define NGS2_ATRAC9_MAX_CHANNELS 8u
#define NGS2_ATRAC9_MAX_FRAME_SAMPLES 256u
#define NGS2_ATRAC9_MAX_FRAMES_PER_SUPERFRAME 8u
#define NGS2_HEVAG_FRAME_BYTES 16u
#define NGS2_HEVAG_FRAME_SAMPLES 28u
#define NGS2_WAVEFORM_HEVAG UINT32_C(0x1c)
#define NGS2_WAVEFORM_ATRAC9 UINT32_C(0x40)

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
    int32_t history[4];
    int16_t carry[NGS2_ATRAC9_MAX_FRAME_SAMPLES];
    uint32_t carry_samples;
    uint64_t decoded_frames,decoded_samples,tail_flushes;
    uint8_t last_flag;
    int end_requested,terminal_seen;
} Ngs2HevagState;
typedef struct {
    RuntimeAtrac9 *decoder;
    Atrac9CodecInfo info;
    uint64_t decoder_generation,generation;
    Ngs2SegmentEvent segments[NGS2_ATRAC9_EVENT_CAPACITY],active;
    uint32_t segment_read,segment_write,segment_count,segment_reservations;
    Ngs2PcmGrain fifo[NGS2_ATRAC9_FIFO_CAPACITY];
    uint32_t pcm_read,pcm_write,pcm_count;
    uint64_t queued_compressed_bytes,decoded_frames,decoded_bytes,decode_errors,cancellations;
    uint64_t events_enqueued,events_rejected,underruns;
    uint32_t max_segment_depth,max_fifo_depth,snapshot_inflight;
    Ngs2HevagState hevag;
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
    uint32_t codec_last_command;
    uint64_t codec_last_sequence;
    Ngs2SegmentObservation trace_segment;
    Ngs2Atrac9Voice *atrac9;
    /* B0: diagnostic-only per-voice history, never controls playback. */
    uint64_t diag_onset_sequence;
    uint32_t diag_waveforms_seen;
    uint8_t diag_onset_pending,diag_terminal_reported;
};
struct Ngs2Rack {
    Ngs2Handle *handle;
    Ngs2System *system;
    Ngs2Rack *next;
    uint32_t rack_id, instance, capacity;
    char name[16];
    Ngs2Voice *voices;
};
/* Experimental B2-F2A: key survives FIFO consumption but not voice reuse. */
#define NGS2_B2F2A_COHORT_SLOTS 32u
typedef struct {
    Ngs2Handle *handle;
    uint64_t generation;
} Ngs2B2F2AKey;

struct Ngs2System {
    Ngs2Handle *handle;
    Ngs2System *next;
    Ngs2Rack *racks;
    uint32_t sample_rate, grain_samples, next_rack_instance;
    pthread_t worker;
    uint64_t compressed_bytes,snapshot_bytes,snapshot_us,decoded_frames,decoded_bytes;
    uint64_t hevag_frames,hevag_samples,hevag_grains,hevag_tail_flushes;
    uint64_t decode_errors,cancellations,events_enqueued,events_rejected;
    uint32_t max_segment_depth,max_fifo_depth;
    int atrac9_enabled,render_mix_enabled,worker_started,closing;
    uint32_t render_voice_cursor;
    /* Host-only, opt-in B2-F2A cohort. Never part of guest-facing structs. */
    Ngs2B2F2AKey b2f2a_keys[NGS2_B2F2A_COHORT_SLOTS];
    uint64_t b2f2a_commits, b2f2a_retained, b2f2a_new;
    uint64_t main_renders,ambiguous_renders_ignored,voices_mixed,grains_consumed;
    uint64_t underruns,trylock_contentions,render_cancellations,render_time_us;
    uint64_t render_limit_exceeded;
    uint64_t virtualized_grains,virtual_overflow_untracked;
    float peak_pcm;
    double sum_sq_pcm;
    uint64_t total_samples_rendered;
    /* Diagnostic-only counters, internal struct (no guest ABI). */
    uint64_t diag_last_us,diag_calls,diag_cnt1_master,diag_cnt1_other,diag_multi_master,diag_multi_other;
    uint64_t diag_hevag_mix,diag_at9_mix,diag_voice_queries[4],diag_cmd6[6],diag_cmd2,diag_cmd3;
    uint64_t diag_hevag_fifo_drop,diag_at9_fifo_drop;
    float diag_peak_interval;
    unsigned diag_mask_interval;
    int diag_hevag_matrix_logged;
    /* B0: passive counters; independent of the 96-line codec trace limit. */
    uint64_t diag_voice_handles,diag_waveform_configs,diag_waveform_reconfigs;
    uint64_t diag_segment_data,diag_segment_empty,diag_segment_accepted;
    uint64_t diag_reject_terminal,diag_reject_format,diag_reject_descriptor;
    uint64_t diag_reject_capacity,diag_reject_budget,diag_reject_copy,diag_reject_race;
    uint64_t diag_first_mix_hevag,diag_first_mix_at9,diag_first_zero_hevag,diag_first_zero_at9;
    uint64_t diag_first_virtual_hevag,diag_first_virtual_at9;
    uint64_t diag_virtual_hevag,diag_virtual_at9;
    uint64_t diag_empty_hevag,diag_empty_at9,diag_empty_terminal;
    uint64_t diag_terminal_frames,diag_control_starts,diag_matrix_updates;
    unsigned diag_b0_event_lines;
    /* B2-DIAG: separate byte-budget failures and source-concurrency pressure.
     * These counters do not participate in admission or render policy. */
    uint64_t diag_b2_voice_only_hevag,diag_b2_voice_only_at9;
    uint64_t diag_b2_global_only_hevag,diag_b2_global_only_at9;
    uint64_t diag_b2_both_hevag,diag_b2_both_at9;
    uint64_t diag_b2_rejected_bytes,diag_b2_max_voice_held;
    uint64_t diag_b2_max_global_held,diag_b2_max_request_bytes;
    uint64_t diag_b2_eligible_sum,diag_b2_overflow_33_64;
    uint64_t diag_b2_overflow_65_128,diag_b2_overflow_129_plus;
    uint32_t diag_b2_max_eligible;
    uint64_t diag_b2_clipped_samples,diag_b2_nonfinite_samples;
    /* B2-F2B-DIAG: passive attribution, never used for admission or playback. */
    /* B2-F2C: committed zero-matrix bypasses; no change to PCM/GPU ABI. */
    uint64_t diag_f2c_bypassed,diag_f2c_bypass_quanta;
    uint64_t diag_f2b_silent_mixed,diag_f2b_audible_virtualized;
    uint64_t diag_f2b_wasted_quanta,diag_f2b_midstream_hevag,diag_f2b_midstream_at9;
    uint64_t diag_f2b_reconfig_active_drop,diag_f2b_reconfig_same_hevag_drop;
    uint64_t diag_f2b_end_while_live,diag_f2b_boundary_gt_010,diag_f2b_boundary_checks;
    float diag_f2b_boundary_max,diag_f2b_prev_last[8];
    uint8_t diag_f2b_prev_valid;
};

static pthread_mutex_t ngs2_lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t ngs2_cond=PTHREAD_COND_INITIALIZER;
static Ngs2System *ngs2_systems;
static _Atomic uint64_t ngs2_global_trylock_contentions;
static _Atomic uintptr_t ngs2_render_owner;
typedef void (*Ngs2RenderMixHook)(void *arg);
static Ngs2RenderMixHook ngs2_render_mix_hook;
static void *ngs2_render_mix_hook_arg;
static inline __attribute__((unused)) void ngs2_set_render_mix_hook(Ngs2RenderMixHook hook,void *arg) {
    ngs2_render_mix_hook=hook;
    ngs2_render_mix_hook_arg=arg;
}
typedef void (*Ngs2HevagDecodeHook)(void *arg);
static Ngs2HevagDecodeHook ngs2_hevag_decode_hook;
static void *ngs2_hevag_decode_hook_arg;
static inline __attribute__((unused)) void ngs2_set_hevag_decode_hook(Ngs2HevagDecodeHook hook,void *arg) {
    ngs2_hevag_decode_hook=hook; ngs2_hevag_decode_hook_arg=arg;
}
static uint64_t ngs2_next_handle=1;
static uint64_t ngs2_segment_sequence,ngs2_compressed_bytes,ngs2_compressed_bytes_max;
static unsigned ngs2_unknown_logs,ngs2_descriptor_logs;
static unsigned ngs2_atrac9_fault_logs;
static unsigned ngs2_atrac9_decode_logs,ngs2_hevag_decode_logs,ngs2_hevag_route_logs,ngs2_mixer_pcm_logs;
static unsigned ngs2_codec_logs;
static int ngs2_segment_trace_setting=-1;
static int ngs2_atrac9_setting=-1;
static int ngs2_ini_enabled(const char *key_name);
static _Atomic int ngs2_audio_diag_setting=-1;
static int ngs2_audio_diag_enabled(void) {
    int value=atomic_load_explicit(&ngs2_audio_diag_setting,memory_order_acquire);
    if (value>=0) return value;
    value=ngs2_ini_enabled("ngs2_audio_diag");
    atomic_store_explicit(&ngs2_audio_diag_setting,value,memory_order_release);
    return value;
}
static uint64_t ngs2_trace_command_sequence;
static uint64_t ngs2_codec_sequence;
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
            printf("Runtime: NGS2 codec HLE-2.2C enabled (ATRAC9 + mono HEVAG/48000 Hz)\n");
    }
    return ngs2_atrac9_setting;
}
/* One feature flag, default OFF. If both experiments are enabled, C takes priority. */
static int ngs2_b2f2c_setting=-1;
static int ngs2_b2f2c_enabled(void) {
    if (ngs2_b2f2c_setting<0) {
        ngs2_b2f2c_setting=ngs2_ini_enabled("ngs2_b2f2c_zero_matrix_bypass");
        if (ngs2_b2f2c_setting)
            printf("Runtime: NGS2 B2-F2C zero-matrix bypass enabled\n");
    }
    return ngs2_b2f2c_setting;
}

static int ngs2_b2f2a_setting=-1;
static int ngs2_b2f2a_enabled(void) {
    if (ngs2_b2f2a_setting<0) {
        ngs2_b2f2a_setting=ngs2_ini_enabled("ngs2_b2f2a_stable_cohort");
        if (ngs2_b2f2a_setting)
            printf("Runtime: NGS2 B2-F2A experimental stable cohort enabled\n");
    }
    return ngs2_b2f2a_setting;
}

static int ngs2_render_mix_setting=-1;
static int ngs2_render_mix_enabled(void) {
    if (ngs2_render_mix_setting<0) {
        ngs2_render_mix_setting=ngs2_ini_enabled("ngs2_render_mix");
        if (ngs2_render_mix_setting)
            printf("Runtime: NGS2 render mixer enabled (F32 8ch master bus)\n");
    }
    return ngs2_render_mix_setting;
}
static int ngs2_atrac9_waveform(const Ngs2Waveform *waveform) {
    return waveform->valid && waveform->format==NGS2_WAVEFORM_ATRAC9 && waveform->codec_config;
}
static int ngs2_hevag_waveform(const Ngs2Waveform *waveform) {
    return waveform->valid && waveform->format==NGS2_WAVEFORM_HEVAG &&
           waveform->channels==1 && waveform->sample_rate==48000;
}
static int ngs2_codec_waveform(const Ngs2Waveform *waveform) {
    return ngs2_atrac9_waveform(waveform) || ngs2_hevag_waveform(waveform);
}
static const char *ngs2_codec_dispatch(const Ngs2Waveform *waveform) {
    if (ngs2_atrac9_waveform(waveform)) return "atrac9";
    if (ngs2_hevag_waveform(waveform)) return "hevag";
    if (waveform->valid && waveform->format==NGS2_WAVEFORM_HEVAG) return "reject-hevag-contract";
    return "reject-unsupported";
}

/* Sony HE-VAG reference coefficients copied from
 * https://github.com/vgmstream/vgmstream/blob/master/src/coding/psv_decoder.c
 * Copyright (c) 2008-2025 Adam Gashlin, Fastelbja, Ronny Elfert, bnnm,
 * Christopher Snowhill, NicknineTheEagle, bxaimc, Thealexbarney, CyberBotX,
 * EdnessP, et al. Permission to use, copy, modify, and distribute this software
 * for any purpose with or without fee is granted, provided this notice appears
 * in all copies. Software is provided "AS IS", without warranty. */
static const float ngs2_hevag_coefs[128][4]={
    {-0.0f,-0.0f,-0.0f,-0.0f},{0.9375f,-0.0f,-0.0f,-0.0f},
    {1.796875f,-0.8125f,-0.0f,-0.0f},{1.53125f,-0.859375f,-0.0f,-0.0f},
    {1.90625f,-0.9375f,-0.0f,-0.0f},{1.7982178f,-0.86169434f,-0.0f,-0.0f},
    {1.770874f,-0.89916992f,-0.0f,-0.0f},{1.6992188f,-0.91821289f,-0.0f,-0.0f},
    {1.6031494f,-0.9375f,-0.0f,-0.0f},{1.4682617f,-0.9375f,-0.0f,-0.0f},
    {1.3139648f,-0.9375f,-0.0f,-0.0f},{1.1424561f,-0.9375f,-0.0f,-0.0f},
    {0.95605469f,-0.9375f,-0.0f,-0.0f},{0.75695801f,-0.9375f,-0.0f,-0.0f},
    {0.54785156f,-0.9375f,-0.0f,-0.0f},{0.33166504f,-0.9375f,-0.0f,-0.0f},
    {0.11108398f,-0.9375f,-0.0f,-0.0f},{-0.11108398f,-0.9375f,-0.0f,-0.0f},
    {-0.33166504f,-0.9375f,-0.0f,-0.0f},{-0.54785156f,-0.9375f,-0.0f,-0.0f},
    {-0.75695801f,-0.9375f,-0.0f,-0.0f},{-0.95605469f,-0.9375f,-0.0f,-0.0f},
    {-1.1424561f,-0.9375f,-0.0f,-0.0f},{-1.3139648f,-0.9375f,-0.0f,-0.0f},
    {-1.4682617f,-0.9375f,-0.0f,-0.0f},{-1.6031494f,-0.9375f,-0.0f,-0.0f},
    {-1.6992188f,-0.91821289f,-0.0f,-0.0f},{-1.770874f,-0.89916992f,-0.0f,-0.0f},
    {-1.7982178f,-0.86169434f,-0.0f,-0.0f},{0.65625f,-1.125f,0.40625f,-0.375f},
    {-0.78125f,-0.875f,-0.40625f,-0.28125f},{-1.28125f,-0.90625f,-0.4375f,-0.125f},
    {-0.020385742f,-0.33227539f,-0.060302734f,-0.066040039f},
    {-0.90698242f,-0.27111816f,-0.28051758f,0.051757812f},
    {-0.97668457f,-0.38647461f,-0.34350586f,0.03527832f},
    {0.73461914f,-0.57983398f,0.32336426f,-0.15844727f},
    {0.46362305f,-0.84790039f,0.47302246f,-0.1484375f},
    {-1.0054932f,-0.31689453f,-0.25280762f,0.027709961f},
    {1.1229248f,0.24194336f,-0.16870117f,-0.28271484f},
    {1.5894775f,-0.37158203f,-0.46289062f,0.15466309f},
    {1.6005859f,-0.54772949f,-0.2746582f,0.20324707f},
    {-0.20361328f,-0.45703125f,-0.78808594f,0.10253906f},
    {0.95446777f,-0.52832031f,0.25769043f,-0.061767578f},
    {1.168335f,-0.16308594f,-0.092407227f,0.059448242f},
    {1.2246094f,-0.31274414f,0.036621094f,0.024291992f},
    {-0.57922363f,-0.50317383f,-0.66967773f,-0.18225098f},
    {-0.71972656f,0.2902832f,-0.58435059f,-0.84802246f},
    {-0.14562988f,-1.112915f,-0.15100098f,-0.38012695f},
    {0.33972168f,-0.86767578f,-0.19226074f,-0.17663574f},
    {-0.89526367f,-0.25170898f,-0.27001953f,0.054443359f},
    {0.7479248f,-0.3145752f,-0.038452148f,-0.0021972656f},
    {1.1544189f,-0.22680664f,0.012451172f,0.031494141f},
    {0.96142578f,-0.54724121f,0.25952148f,-0.065673828f},
    {-0.87548828f,-0.21911621f,-0.25256348f,0.058837891f},
    {-0.89819336f,-0.2565918f,-0.27258301f,0.053710938f},
    {-1.1193848f,-0.42834473f,-0.32641602f,-0.047729492f},
    {-0.32202148f,-0.32312012f,-0.23547363f,-0.1998291f},
    {0.2286377f,1.1209717f,0.22705078f,-0.70141602f},
    {1.1247559f,0.22692871f,-0.13720703f,-0.29626465f},
    {1.6118164f,-0.36767578f,-0.50524902f,0.16723633f},
    {1.5181885f,-0.58496094f,-0.03125f,0.075927734f},
    {-0.32385254f,-0.13964844f,-0.38842773f,-0.83959961f},
    {1.1390381f,-0.12792969f,-0.10107422f,0.061889648f},
    {0.20043945f,-0.075683594f,-0.11547852f,-0.51623535f},
    {0.51831055f,-0.92590332f,-0.065063477f,-0.27575684f},
    {-1.097168f,-0.47497559f,-0.34265137f,0.0053710938f},
    {-0.31274414f,-0.3338623f,-0.21118164f,-0.23181152f},
    {0.38842773f,-0.058959961f,-0.087158203f,-0.17346191f},
    {0.96887207f,-0.46923828f,0.34436035f,-0.12438965f},
    {1.229126f,-0.31848145f,0.038330078f,0.023803711f},
    {1.0253906f,-0.40246582f,0.18933105f,-0.018920898f},
    {-1.0411377f,-0.33874512f,-0.296875f,-0.041015625f},
    {1.1568604f,-0.22973633f,0.013183594f,0.03125f},
    {0.0091552734f,-0.27355957f,-0.036376953f,-0.84680176f},
    {-1.1160889f,-0.5078125f,-0.36169434f,0.00061035156f},
    {-0.88745117f,-0.23901367f,-0.26318359f,0.056152344f},
    {-0.33447266f,0.45715332f,0.72460938f,-0.13293457f},
    {1.0977783f,0.23779297f,-0.083374023f,-0.33007812f},
    {1.5992432f,-0.34606934f,-0.47045898f,0.12878418f},
    {1.164917f,-0.23937988f,0.015869141f,0.030517578f},
    {0.64355469f,-0.52124023f,0.38134766f,-0.38537598f},
    {-0.93945312f,-0.41296387f,-0.3548584f,-0.055664062f},
    {0.89221191f,0.3079834f,0.052978516f,-0.30041504f},
    {1.2542725f,-0.34997559f,0.047729492f,0.020996094f},
    {1.3354492f,-0.45422363f,0.081176758f,0.01184082f},
    {0.0029296875f,-0.037841797f,-0.15405273f,0.0390625f},
    {-0.99145508f,-0.29431152f,-0.28210449f,-0.033081055f},
    {-1.0389404f,-0.37438965f,-0.28527832f,0.019897461f},
    {0.039794922f,-0.46948242f,0.051147461f,-0.1138916f},
    {1.0858154f,0.26782227f,-0.066040039f,-0.3515625f},
    {1.4737549f,-0.22900391f,-0.24621582f,-0.073364258f},
    {1.0655518f,-0.41784668f,0.2043457f,-0.020629883f},
    {1.5808105f,-0.46960449f,-0.36706543f,0.23754883f},
    {1.2253418f,-0.3137207f,0.036865234f,0.024169922f},
    {1.1456299f,-0.33654785f,0.12304688f,0.0050048828f},
    {-0.57617188f,-0.61108398f,-0.34814453f,-0.14172363f},
    {0.96057129f,-0.52807617f,0.26062012f,-0.061157227f},
    {0.29907227f,-1.0494385f,0.15856934f,-0.33935547f},
    {1.2441406f,-0.33728027f,0.043945312f,0.022094727f},
    {1.3809814f,-0.51428223f,0.10168457f,0.0064697266f},
    {1.239502f,-0.33154297f,0.042114258f,0.022583008f},
    {1.1765137f,-0.17297363f,-0.08996582f,0.058837891f},
    {0.47045898f,-0.5559082f,0.3470459f,-0.41467285f},
    {0.81774902f,-0.6907959f,0.27453613f,-0.13110352f},
    {1.3527832f,-0.47705078f,0.088867188f,0.009765625f},
    {-0.12524414f,-1.1975098f,-0.098266602f,-0.42260742f},
    {1.269043f,-0.45727539f,0.16687012f,-0.01171875f},
    {1.2557373f,0.12060547f,-0.23376465f,-0.17541504f},
    {0.9708252f,0.47338867f,-0.093261719f,-0.39831543f},
    {1.5489502f,-0.4119873f,-0.40942383f,0.25378418f},
    {0.81066895f,0.38647461f,0.028198242f,-0.25500488f},
    {-0.28662109f,-0.89770508f,-0.23730469f,-0.50317383f},
    {1.1340332f,-0.49304199f,0.23010254f,-0.030029297f},
    {0.56555176f,-0.78161621f,0.21337891f,-0.19763184f},
    {1.3729248f,-0.50354004f,0.097900391f,0.0074462891f},
    {1.1971436f,-0.27880859f,0.026733398f,0.027099609f},
    {1.1884766f,-0.1875f,-0.086181641f,0.057739258f},
    {1.0302734f,-0.41943359f,0.19067383f,-0.021484375f},
    {1.1361084f,-0.12463379f,-0.10192871f,0.062133789f},
    {0.20727539f,-1.1016846f,0.083984375f,-0.37072754f},
    {1.2468262f,-0.34069824f,0.044921875f,0.021850586f},
    {1.0241699f,0.39648438f,-0.092529297f,-0.36486816f},
    {0.87902832f,0.40478516f,0.0056152344f,-0.3190918f},
    {-0.010742188f,-0.95324707f,-0.065673828f,-0.5579834f},
    {0.75598145f,-0.63342285f,0.33691406f,-0.15197754f},
    {1.5045166f,-0.1574707f,-0.40087891f,0.030883789f},
    {1.5947266f,-0.49743652f,-0.34472656f,0.22912598f},
    {0.65100098f,0.36608887f,0.094604492f,-0.13818359f}
};

static int16_t ngs2_clamp16(int32_t sample) {
    if (sample>INT16_MAX) return INT16_MAX;
    if (sample<INT16_MIN) return INT16_MIN;
    return (int16_t)sample;
}
static void ngs2_hevag_decode_frame(const unsigned char frame[NGS2_HEVAG_FRAME_BYTES],
                                    int32_t history[4],int16_t pcm[NGS2_HEVAG_FRAME_SAMPLES],
                                    uint8_t *out_flag) {
    unsigned index=((unsigned)frame[0]>>4)|((unsigned)frame[1]&0xf0u);
    unsigned shift=(unsigned)frame[0]&0x0fu;
    uint8_t flag=frame[1]&0x0fu;
    if (index>127u) index=0;
    for (unsigned i=0;i<NGS2_HEVAG_FRAME_SAMPLES;++i) {
        int32_t sample=0;
        if (flag<7u) {
            unsigned byte=frame[2u+i/2u];
            int32_t code=i&1u ? (int32_t)((int8_t)(byte&0xf0u)>>4) :
                                  (int32_t)((int8_t)(byte<<4)>>4);
            int32_t scaled=code*(int32_t)(UINT32_C(1)<<(12u-(shift>12u ? 12u : shift)));
            if (shift>12u) scaled>>=(shift-12u);
            float predicted=(float)history[0]*ngs2_hevag_coefs[index][0]+
                            (float)history[1]*ngs2_hevag_coefs[index][1]+
                            (float)history[2]*ngs2_hevag_coefs[index][2]+
                            (float)history[3]*ngs2_hevag_coefs[index][3];
            sample=scaled+(int32_t)predicted;
        }
        pcm[i]=ngs2_clamp16(sample);
        history[3]=history[2]; history[2]=history[1]; history[1]=history[0]; history[0]=sample;
    }
    *out_flag=flag;
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
    if (ngs2_audio_diag_enabled() && state->pcm_count) {
        if (voice->waveform.format==NGS2_WAVEFORM_HEVAG)
            system->diag_hevag_fifo_drop+=state->pcm_count;
        else if (voice->waveform.format==NGS2_WAVEFORM_ATRAC9)
            system->diag_at9_fifo_drop+=state->pcm_count;
    }
    state->pcm_read=state->pcm_write=state->pcm_count=0; state->faulted=0;
    memset(state->hevag.history,0,sizeof(state->hevag.history));
    memset(state->hevag.carry,0,sizeof(state->hevag.carry));
    state->hevag.carry_samples=0; state->hevag.last_flag=0; state->hevag.terminal_seen=0;
    state->hevag.end_requested=0;
    voice->diag_onset_pending=0;
    voice->diag_terminal_reported=0;
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
static void ngs2_hevag_flush_tail_locked(Ngs2System *system,Ngs2Atrac9Voice *state) {
    if (!state->hevag.end_requested || state->hevag.terminal_seen || state->active_valid ||
        state->segment_count || state->segment_reservations || state->decode_inflight) return;
    if (state->hevag.carry_samples) {
        if (state->pcm_count>=NGS2_ATRAC9_FIFO_CAPACITY) return;
        Ngs2PcmGrain *grain=&state->fifo[state->pcm_write];
        memset(grain,0,sizeof(*grain)); grain->generation=state->generation;
        grain->frames=NGS2_ATRAC9_MAX_FRAME_SAMPLES; grain->channels=1;
        memcpy(grain->pcm,state->hevag.carry,
               (size_t)state->hevag.carry_samples*sizeof(state->hevag.carry[0]));
        state->pcm_write=(state->pcm_write+1u)%NGS2_ATRAC9_FIFO_CAPACITY; ++state->pcm_count;
        ++system->hevag_grains; ++state->hevag.tail_flushes; ++system->hevag_tail_flushes;
        state->hevag.carry_samples=0;
    }
    state->hevag.terminal_seen=1;
    pthread_cond_broadcast(&ngs2_cond);
}
static Ngs2Voice *ngs2_worker_voice_locked(Ngs2System *system) {
    for (Ngs2Rack *rack=system->racks;rack;rack=rack->next)
        for (uint32_t i=0;i<rack->capacity;++i) {
            Ngs2Voice *voice=&rack->voices[i]; Ngs2Atrac9Voice *state=voice->atrac9;
            if (!state || state->closing || state->faulted || state->decode_inflight ||
                voice->internal_state!=NGS2_VOICE_PLAYING) continue;
            if (ngs2_hevag_waveform(&voice->waveform))
                ngs2_hevag_flush_tail_locked(system,state);
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
            if (ngs2_hevag_waveform(&voice->waveform)) {
                if (state->hevag.terminal_seen) continue;
                uint32_t needed=1;
                if ((uint64_t)state->active.cursor+NGS2_HEVAG_FRAME_BYTES<=state->active.byte_count) {
                    uint8_t flag=state->active.host_bytes[state->active.cursor+1u]&0x0fu;
                    uint32_t samples=state->hevag.carry_samples+NGS2_HEVAG_FRAME_SAMPLES;
                    needed=samples/NGS2_ATRAC9_MAX_FRAME_SAMPLES;
                    if ((flag==1u || flag==7u) && samples%NGS2_ATRAC9_MAX_FRAME_SAMPLES) ++needed;
                    if (!needed) needed=1;
                }
                if (needed>NGS2_ATRAC9_FIFO_CAPACITY-state->pcm_count) continue;
                return voice;
            }
            uint32_t frames=state->decoder_generation==state->generation ?
                            (uint32_t)state->info.framesInSuperframe : NGS2_ATRAC9_FIFO_CAPACITY;
            if (frames>NGS2_ATRAC9_FIFO_CAPACITY-state->pcm_count) continue;
            return voice;
        }
    return NULL;
}
static void ngs2_hevag_worker_step(Ngs2System *system,Ngs2Voice *voice) {
    Ngs2Atrac9Voice *state=voice->atrac9;
    uint64_t generation=state->generation;
    size_t cursor=state->active.cursor;
    uint32_t byte_count=state->active.byte_count;
    unsigned char frame[NGS2_HEVAG_FRAME_BYTES];
    int32_t history[4]; memcpy(history,state->hevag.history,sizeof(history));
    int bounds=(uint64_t)cursor+NGS2_HEVAG_FRAME_BYTES<=byte_count;
    if (bounds) memcpy(frame,state->active.host_bytes+cursor,sizeof(frame));
    state->decode_inflight=1;
    pthread_mutex_unlock(&ngs2_lock);

    int16_t decoded[NGS2_HEVAG_FRAME_SAMPLES]; uint8_t flag=0;
    if (ngs2_hevag_decode_hook) ngs2_hevag_decode_hook(ngs2_hevag_decode_hook_arg);
    if (bounds) ngs2_hevag_decode_frame(frame,history,decoded,&flag);

    pthread_mutex_lock(&ngs2_lock);
    state=voice->atrac9;
    int current=state && !state->closing && state->generation==generation &&
                state->active_valid && state->active.generation==generation;
    if (current && bounds) {
        memcpy(state->hevag.history,history,sizeof(history)); state->hevag.last_flag=flag;
        ++state->hevag.decoded_frames; state->hevag.decoded_samples+=NGS2_HEVAG_FRAME_SAMPLES;
        ++system->hevag_frames; system->hevag_samples+=NGS2_HEVAG_FRAME_SAMPLES;
        state->active.cursor+=NGS2_HEVAG_FRAME_BYTES;
        for (unsigned i=0;i<NGS2_HEVAG_FRAME_SAMPLES;++i) {
            state->hevag.carry[state->hevag.carry_samples++]=decoded[i];
            if (state->hevag.carry_samples==NGS2_ATRAC9_MAX_FRAME_SAMPLES) {
                Ngs2PcmGrain *grain=&state->fifo[state->pcm_write];
                memset(grain,0,sizeof(*grain)); grain->generation=generation;
                grain->frames=NGS2_ATRAC9_MAX_FRAME_SAMPLES; grain->channels=1;
                memcpy(grain->pcm,state->hevag.carry,sizeof(state->hevag.carry));
                state->pcm_write=(state->pcm_write+1u)%NGS2_ATRAC9_FIFO_CAPACITY;
                ++state->pcm_count; ++system->hevag_grains; state->hevag.carry_samples=0;
            }
        }
        /* VAG flag 1 is end; flag 7 is silent end. Other flags preserve reference decode semantics. */
        if (flag==1u || flag==7u) {
            if (ngs2_audio_diag_enabled()) ++system->diag_terminal_frames;
            state->hevag.terminal_seen=1;
            if (state->hevag.carry_samples) {
                Ngs2PcmGrain *grain=&state->fifo[state->pcm_write];
                memset(grain,0,sizeof(*grain)); grain->generation=generation;
                grain->frames=NGS2_ATRAC9_MAX_FRAME_SAMPLES; grain->channels=1;
                memcpy(grain->pcm,state->hevag.carry,
                       (size_t)state->hevag.carry_samples*sizeof(state->hevag.carry[0]));
                state->pcm_write=(state->pcm_write+1u)%NGS2_ATRAC9_FIFO_CAPACITY;
                ++state->pcm_count; ++system->hevag_grains; ++state->hevag.tail_flushes;
                ++system->hevag_tail_flushes; state->hevag.carry_samples=0;
            }
            state->active.cursor=state->active.byte_count;
            while (state->segment_count) {
                Ngs2SegmentEvent *event=&state->segments[state->segment_read];
                ngs2_atrac9_account_drop_locked(system,state,event);
                state->segment_read=(state->segment_read+1u)%NGS2_ATRAC9_EVENT_CAPACITY;
                --state->segment_count;
            }
        }
        if (state->pcm_count>state->max_fifo_depth) state->max_fifo_depth=state->pcm_count;
        if (state->pcm_count>system->max_fifo_depth) system->max_fifo_depth=state->pcm_count;
        if (ngs2_hevag_decode_logs<16) {
            ++ngs2_hevag_decode_logs;
            unsigned index=((unsigned)frame[0]>>4)|((unsigned)frame[1]&0xf0u);
            printf("Runtime: NGS2 HEVAG decoded rack=%#x/%u voice=%u generation=%llu "
                   "index=%u shift=%u flag=%u cursor=%zu remaining=%llu carry=%u fifo=%u\n",
                   voice->rack->rack_id,voice->rack->instance,voice->index,
                   (unsigned long long)generation,index,(unsigned)frame[0]&0x0fu,flag,
                   cursor,(unsigned long long)((uint64_t)byte_count-(uint64_t)cursor),
                   state->hevag.carry_samples,state->pcm_count);
        }
        if (state->active.cursor==state->active.byte_count) {
            ngs2_atrac9_account_drop_locked(system,state,&state->active); state->active_valid=0;
        }
    } else if (current) {
        ++state->decode_errors; ++system->decode_errors; state->faulted=1;
        ngs2_atrac9_account_drop_locked(system,state,&state->active); state->active_valid=0;
    } else if (state && state->active_valid && state->active.generation==generation) {
        ngs2_atrac9_account_drop_locked(system,state,&state->active); state->active_valid=0;
    }
    if (state) state->decode_inflight=0;
    pthread_cond_broadcast(&ngs2_cond);
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
        if (ngs2_hevag_waveform(&voice->waveform)) {
            ngs2_hevag_worker_step(system,voice); continue;
        }
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
        const char *fault_stage="none";
        int bytes_used=0;
        uint32_t decode_frame=UINT32_MAX;
        if (need_init) {
            if (!runtime_atrac9_create || !runtime_atrac9_init || !runtime_atrac9_decode) {
                fault_stage="adapter_missing";
                error=-1;
            } else if (!(decoder=runtime_atrac9_create())) {
                fault_stage="decoder_create";
                error=-1;
            } else if ((error=runtime_atrac9_init(decoder,config,&info))) {
                fault_stage="decoder_init";
            } else if (!ngs2_atrac9_info_valid(&info,&waveform)) {
                fault_stage="codec_info";
                error=-1;
            }
        } else info=state->info;
        Ngs2PcmGrain decoded[NGS2_ATRAC9_MAX_FRAMES_PER_SUPERFRAME];
        memset(decoded,0,sizeof(decoded)); uint32_t frame_count=0; uint32_t consumed=0;
        uint32_t input_advance=0;
        uint64_t decode_start=ngs2_now_us();
        if (!error && (uint64_t)cursor+(uint64_t)info.superframeSize<=byte_count) {
            for (int frame=0;frame<info.framesInSuperframe;++frame) {
                int used=0; decoded[frame].generation=generation;
                decoded[frame].frames=(uint32_t)info.frameSamples;
                decoded[frame].channels=(uint32_t)info.channels;
                error=runtime_atrac9_decode(decoder,input+cursor+consumed,decoded[frame].pcm,&used);
                bytes_used=used; decode_frame=(uint32_t)frame;
                if (error) {
                    fault_stage="decoder_frame";
                    break;
                }
                if (used<=0) {
                    fault_stage="bytes_used_nonpositive";
                    error=-1; break;
                }
                if (consumed+(uint32_t)used>(uint32_t)info.superframeSize) {
                    fault_stage="bytes_used_overflow";
                    error=-1; break;
                }
                consumed+=(uint32_t)used; ++frame_count;
            }
            /* LibAtrac9 reports parsed frame bytes; superframeSize includes trailing padding. */
            if (!error) input_advance=(uint32_t)info.superframeSize;
        } else if (!error) {
            fault_stage="segment_bounds";
            error=-1;
        }
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
            state->active.cursor+=input_advance; state->decoded_frames+=frame_count;
            state->decoded_bytes+=input_advance; system->decoded_frames+=frame_count;
            system->decoded_bytes+=input_advance;
            if (state->pcm_count>state->max_fifo_depth) state->max_fifo_depth=state->pcm_count;
            if (state->pcm_count>system->max_fifo_depth) system->max_fifo_depth=state->pcm_count;
            if (ngs2_atrac9_decode_logs<16) {
                ++ngs2_atrac9_decode_logs;
                printf("Runtime: NGS2 ATRAC9 decoded rack=%#x/%u voice=%u generation=%llu "
                       "config=%02x%02x%02x%02x frames=%u parsed=%u advance=%u "
                       "cursor=%zu remaining=%llu fifo=%u decode_us=%llu\n",
                       voice->rack->rack_id,voice->rack->instance,voice->index,
                       (unsigned long long)generation,config[0],config[1],config[2],config[3],
                       frame_count,consumed,input_advance,cursor,
                       (unsigned long long)((uint64_t)byte_count-(uint64_t)cursor),
                       state->pcm_count,(unsigned long long)decode_us);
            }
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
                uint64_t remaining=cursor<=byte_count ? (uint64_t)byte_count-(uint64_t)cursor : 0;
                printf("Runtime: NGS2 ATRAC9 voice fault rack=%#x/%u voice=%u generation=%llu "
                       "stage=%s error=%d config=%02x%02x%02x%02x "
                       "info={channels=%d rate=%d frame_samples=%d frames=%d superframe=%d} "
                       "cursor=%zu remaining=%llu consumed=%u bytes_used=%d frame=%u decode_us=%llu\n",
                       voice->rack->rack_id,voice->rack->instance,voice->index,
                       (unsigned long long)generation,fault_stage,error,
                       config[0],config[1],config[2],config[3],
                       info.channels,info.samplingRate,info.frameSamples,
                       info.framesInSuperframe,info.superframeSize,cursor,
                       (unsigned long long)remaining,consumed,bytes_used,decode_frame,
                       (unsigned long long)decode_us);
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
    system->render_mix_enabled=system->atrac9_enabled && ngs2_render_mix_enabled();
    pthread_mutex_lock(&ngs2_lock);
    system->handle=ngs2_new_handle_locked(); system->next=ngs2_systems; ngs2_systems=system;
    if (system->atrac9_enabled) {
        if (!pthread_create(&system->worker,NULL,ngs2_atrac9_worker,system)) system->worker_started=1;
        else {
            system->atrac9_enabled=0;
            system->render_mix_enabled=0;
            printf("Runtime: NGS2 codec worker unavailable; system remains HLE-1 silent\n");
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
        printf("Runtime: NGS2 codec summary events=%llu rejected=%llu snapshots=%llu bytes/%llu us "
               "atrac9_decoded=%llu frames/%llu bytes errors=%llu cancellations=%llu queue_max=%u fifo_max=%u\n",
               (unsigned long long)system->events_enqueued,(unsigned long long)system->events_rejected,
               (unsigned long long)system->snapshot_bytes,(unsigned long long)system->snapshot_us,
               (unsigned long long)system->decoded_frames,(unsigned long long)system->decoded_bytes,
               (unsigned long long)system->decode_errors,(unsigned long long)system->cancellations,
               system->max_segment_depth,system->max_fifo_depth);
    if (system->hevag_frames)
        printf("Runtime: NGS2 HEVAG summary frames=%llu samples=%llu grains=%llu tail_flushes=%llu\n",
               (unsigned long long)system->hevag_frames,(unsigned long long)system->hevag_samples,
               (unsigned long long)system->hevag_grains,(unsigned long long)system->hevag_tail_flushes);
    if (system->render_mix_enabled) {
        system->trylock_contentions+=atomic_exchange_explicit(&ngs2_global_trylock_contentions,0,memory_order_relaxed);
        double rms=system->total_samples_rendered ?
                   sqrt(system->sum_sq_pcm/(double)system->total_samples_rendered) : 0.0;
        double avg_us=system->main_renders ?
                      (double)system->render_time_us/(double)system->main_renders : 0.0;
        printf("Runtime: NGS2 Mixer summary main_renders=%llu ambiguous_ignored=%llu "
               "voices_mixed=%llu grains_consumed=%llu underruns=%llu "
               "trylock_contentions=%llu cancellations=%llu limit_exceeded=%llu "
               "peak_pcm=%.4f rms_pcm=%.4f render_time_us=%llu (avg=%.2f us/render)\n",
               (unsigned long long)system->main_renders,
               (unsigned long long)system->ambiguous_renders_ignored,
               (unsigned long long)system->voices_mixed,
               (unsigned long long)system->grains_consumed,
               (unsigned long long)system->underruns,
               (unsigned long long)system->trylock_contentions,
               (unsigned long long)system->render_cancellations,
               (unsigned long long)system->render_limit_exceeded,
               system->peak_pcm,(float)rms,
               (unsigned long long)system->render_time_us,avg_us);
    }
    while (system->racks) {
        Ngs2Rack *rack=system->racks; system->racks=rack->next; ngs2_free_rack_resources(rack);
    }
    free(system);
    if (info) memset(info,0,sizeof(*info));
    return 0;
}

/* B2-F1: passive compressed-memory snapshot. Caller holds ngs2_lock.
 * Unpublished bytes can belong to in-flight snapshot reservations. */
typedef struct {
    uint64_t held, queued, active, unpublished, decoded, generation;
    size_t cursor;
    uint32_t rack_id, rack_instance, voice_index, voice_state;
    uint32_t segments, reservations, fifo;
    char codec;
} Ngs2B2F1Top;

/* B2-F2B-DIAG only: effective matrix classification, not an audio policy.
 * Mirrors the finite/[-64,64] eligibility check in the F32 mixer. */
static int ngs2_b2f2b_matrix_audible_locked(const Ngs2Voice *voice) {
    uint32_t chans=voice->waveform.channels;
    if (!chans || chans>8u) return 0;
    uint32_t words=voice->descriptor_words;
    if (words>NGS2_MAX_DESCRIPTOR_WORDS) words=NGS2_MAX_DESCRIPTOR_WORDS;
    uint32_t used=8u*chans;
    if (words>used) words=used;
    for (uint32_t i=0;i<words;++i) {
        union { uint32_t u; float f; } gain={voice->descriptors[i]};
        if (isfinite(gain.f) && gain.f>=-64.0f && gain.f<=64.0f && gain.f!=0.0f)
            return 1;
    }
    return 0;
}

static void ngs2_b2f1_snapshot_locked(Ngs2System *system,
                                       char *line, size_t line_cap,
                                       char *top_line, size_t top_cap) {
    uint64_t sum_voice=0, hevag=0, atrac9=0, other=0;
    uint64_t states[5]={0};
    uint64_t queued=0, active=0, unpublished=0, underaccount=0;
    uint64_t reservation_count=0;
    uint32_t queue_events=0, active_events=0;
    Ngs2B2F1Top top[5]={0};

    for (Ngs2Rack *rack=system->racks; rack; rack=rack->next) {
        for (uint32_t i=0; i<rack->capacity; ++i) {
            Ngs2Voice *voice=&rack->voices[i];
            Ngs2Atrac9Voice *st=voice->atrac9;
            if (!st) continue;

            uint64_t held=st->queued_compressed_bytes;
            uint64_t q=0;
            for (uint32_t j=0; j<st->segment_count &&
                               j<NGS2_ATRAC9_EVENT_CAPACITY; ++j) {
                uint32_t index=(st->segment_read+j)%
                               NGS2_ATRAC9_EVENT_CAPACITY;
                q+=st->segments[index].byte_count;
            }
            uint64_t a=st->active_valid ? st->active.byte_count : 0;
            uint64_t resident=q+a;
            uint64_t pending=held>=resident ? held-resident : 0;

            sum_voice+=held;
            queued+=q;
            active+=a;
            unpublished+=pending;
            if (resident>held) underaccount+=resident-held;
            reservation_count+=st->segment_reservations;
            queue_events+=st->segment_count;
            active_events+=st->active_valid ? 1u : 0u;

            char codec='?';
            if (ngs2_hevag_waveform(&voice->waveform)) {
                hevag+=held; codec='H';
            } else if (ngs2_atrac9_waveform(&voice->waveform)) {
                atrac9+=held; codec='A';
            } else {
                other+=held;
            }
            unsigned state=voice->internal_state<4 ?
                           voice->internal_state : 4;
            states[state]+=held;

            if (!held) continue;
            for (unsigned slot=0;slot<5;++slot) {
                if (held<=top[slot].held) continue;
                for (unsigned j=4;j>slot;--j) top[j]=top[j-1];
                top[slot]=(Ngs2B2F1Top){
                    .held=held, .queued=q, .active=a,
                    .unpublished=pending, .decoded=st->decoded_bytes,
                    .generation=st->generation,
                    .cursor=st->active_valid ? st->active.cursor : 0,
                    .rack_id=rack->rack_id,
                    .rack_instance=rack->instance,
                    .voice_index=voice->index,
                    .voice_state=voice->internal_state,
                    .segments=st->segment_count,
                    .reservations=st->segment_reservations,
                    .fifo=st->pcm_count,
                    .codec=codec
                };
                break;
            }
        }
    }

    uint64_t all_system=0;
    for (Ngs2System *s=ngs2_systems;s;s=s->next)
        all_system+=s->compressed_bytes;

    snprintf(line,line_cap,
        "NGS2-B2F1-MEM: global=%llu all_system=%llu system=%llu "
        "sum_voice=%llu codec_h/a/o=%llu/%llu/%llu "
        "state_stop/play/pause/release/other=%llu/%llu/%llu/%llu/%llu "
        "queued=%llu active=%llu unpublished=%llu underaccount=%llu "
        "reservations=%llu queue_events=%u active_events=%u\n",
        (unsigned long long)ngs2_compressed_bytes,
        (unsigned long long)all_system,
        (unsigned long long)system->compressed_bytes,
        (unsigned long long)sum_voice,
        (unsigned long long)hevag,
        (unsigned long long)atrac9,
        (unsigned long long)other,
        (unsigned long long)states[0],
        (unsigned long long)states[1],
        (unsigned long long)states[2],
        (unsigned long long)states[3],
        (unsigned long long)states[4],
        (unsigned long long)queued,
        (unsigned long long)active,
        (unsigned long long)unpublished,
        (unsigned long long)underaccount,
        (unsigned long long)reservation_count,
        queue_events,active_events);

    int n=snprintf(top_line,top_cap,"NGS2-B2F1-TOP:");
    if (n<0 || (size_t)n>=top_cap) return;
    size_t used=(size_t)n;
    for (unsigned i=0;i<5 && top[i].held;++i) {
        Ngs2B2F1Top *t=&top[i];
        n=snprintf(top_line+used,top_cap-used,
            " [%u rack=%#x/%u voice=%u codec=%c state=%u "
            "held=%llu q=%llu active=%llu unpublished=%llu "
            "segments=%u reservations=%u fifo=%u gen=%llu "
            "decoded=%llu cursor=%zu]",
            i+1,t->rack_id,t->rack_instance,t->voice_index,
            t->codec,t->voice_state,
            (unsigned long long)t->held,
            (unsigned long long)t->queued,
            (unsigned long long)t->active,
            (unsigned long long)t->unpublished,
            t->segments,t->reservations,t->fifo,
            (unsigned long long)t->generation,
            (unsigned long long)t->decoded,t->cursor);
        if (n<0 || (size_t)n>=top_cap-used) break;
        used+=(size_t)n;
    }
    if (used+1<top_cap) {
        top_line[used++]='\n';
        top_line[used]=0;
    }
}

#define NGS2_RENDER_MAX_VOICES 32u
#define NGS2_B0_EVENT_LOG_LIMIT 32u
/* Bound work in the render thread: overflow voices advance without mixing.
 * If more than this many are virtualized, the rest wait for a later round-robin
 * selection rather than silencing the entire master bus. */
#define NGS2_RENDER_MAX_VIRTUAL_VOICES 512u
typedef struct {
    Ngs2Handle *voice_handle;
    uint64_t generation;
    uint32_t expected_pcm_read;
    uint64_t expected_grain_generation;
    uint32_t channels;
    int diag_is_hevag;
    uint64_t diag_onset_sequence;
    uint8_t diag_onset_pending;
    float diag_onset_peak;
    uint32_t matrix_words;
    float matrix[NGS2_MAX_DESCRIPTOR_WORDS];
    Ngs2PcmGrain grain;
    int active;
    uint8_t diag_f2b_matrix_audible;
} Ngs2RenderSnapshot;
typedef struct {
    Ngs2Handle *rack_handle,*voice_handle;
    uint64_t generation,expected_grain_generation;
    uint32_t voice_index,expected_pcm_read;
    uint64_t diag_onset_sequence;
    uint8_t diag_f2b_matrix_audible;
    uint8_t f2c_zero_bypass; /* Revalidated/consumed only on successful publication. */
} Ngs2VirtualSnapshot;

/* The render gate protects both fixed workspaces; no heap allocation in render. */
static Ngs2RenderSnapshot ngs2_render_snapshots[NGS2_RENDER_MAX_VOICES];
static Ngs2VirtualSnapshot ngs2_render_virtual[NGS2_RENDER_MAX_VIRTUAL_VOICES];
static float ngs2_render_stage[256*8];

/* B2-F2A: experimental stable selection, protected by ngs2_lock. */
static int ngs2_b2f2a_eligible_locked(const Ngs2Voice *voice) {
    const Ngs2Atrac9Voice *st=voice->atrac9;
    return voice->internal_state==NGS2_VOICE_PLAYING && st &&
           !st->closing && !st->faulted && st->pcm_count &&
           st->fifo[st->pcm_read].generation==st->generation;
}
static int ngs2_b2f2a_has(const Ngs2B2F2AKey *keys,uint32_t count,
                            const Ngs2Voice *voice) {
    for (uint32_t i=0;i<count;++i)
        if (keys[i].handle==voice->handle &&
            keys[i].generation==voice->atrac9->generation) return 1;
    return 0;
}
/* Find one not-yet-selected candidate; round-robin offset is stable within
 * a quantum, and reconfiguration invalidates old generation keys. */
static Ngs2Voice *ngs2_b2f2a_find_locked(Ngs2System *sys,
        const Ngs2B2F2AKey *keys,uint32_t count,uint32_t start,
        uint32_t total,uint32_t format,Ngs2Handle *exclude) {
    Ngs2Voice *best=NULL;
    uint32_t best_distance=UINT32_MAX,ordinal=0;
    for (Ngs2Rack *rack=sys->racks;rack;rack=rack->next) {
        if (rack->rack_id==0x2000 || rack->rack_id==0x3000) continue;
        for (uint32_t v=0;v<rack->capacity;++v) {
            Ngs2Voice *voice=&rack->voices[v];
            if (!ngs2_b2f2a_eligible_locked(voice)) continue;
            uint32_t pos=ordinal++;
            if ((format && voice->waveform.format!=format) ||
                voice->handle==exclude || ngs2_b2f2a_has(keys,count,voice)) continue;
            uint32_t distance=pos>=start ? pos-start : total-start+pos;
            if (distance<best_distance) { best=voice; best_distance=distance; }
        }
    }
    return best;
}
static uint32_t ngs2_b2f2a_prepare_locked(Ngs2System *sys,uint32_t total,
                              Ngs2B2F2AKey selected[NGS2_B2F2A_COHORT_SLOTS],
                              uint32_t *retained_out) {
    uint32_t eligible_hevag=0,eligible_at9=0;
    for (Ngs2Rack *rack=sys->racks;rack;rack=rack->next) {
        if (rack->rack_id==0x2000 || rack->rack_id==0x3000) continue;
        for (uint32_t v=0;v<rack->capacity;++v) {
            Ngs2Voice *voice=&rack->voices[v];
            if (!ngs2_b2f2a_eligible_locked(voice)) continue;
            if (ngs2_hevag_waveform(&voice->waveform)) ++eligible_hevag;
            else if (ngs2_atrac9_waveform(&voice->waveform)) ++eligible_at9;
        }
    }
    /* Codec is only a provisional continuity class, NOT a SoundX priority.
     * Preserve up to 24 AT9 slots; reserve eight for short HEVAG effects. */
    uint32_t h_target=eligible_hevag<8u ? eligible_hevag : 8u;
    uint32_t a_target=eligible_at9<32u-h_target ? eligible_at9 : 32u-h_target;
    if (a_target+h_target<32u) {
        uint32_t room=32u-a_target-h_target;
        uint32_t extra=eligible_hevag-h_target;
        if (extra>room) extra=room;
        h_target+=extra;
    }
    uint32_t n=0,h_count=0,a_count=0;
    Ngs2B2F2AKey prev[NGS2_B2F2A_COHORT_SLOTS];
    memcpy(prev,sys->b2f2a_keys,sizeof(prev));
    /* Retain current voices by handle AND generation, never stale FIFO IDs. */
    for (uint32_t i=0;i<NGS2_B2F2A_COHORT_SLOTS;++i) {
        Ngs2B2F2AKey k=prev[i];
        if (!k.handle) continue;
        Ngs2Voice *voice=ngs2_voice_locked(k.handle);
        if (!voice || voice->rack->system!=sys ||
            !ngs2_b2f2a_eligible_locked(voice) ||
            voice->atrac9->generation!=k.generation ||
            ngs2_b2f2a_has(selected,n,voice)) continue;
        if (ngs2_hevag_waveform(&voice->waveform)) {
            if (h_count>=h_target) continue;
            ++h_count;
        } else if (ngs2_atrac9_waveform(&voice->waveform)) {
            if (a_count>=a_target) continue;
            ++a_count;
        }
        selected[n++]=k;
    }
    /* Very slow AT9 turnover, faster HEVAG turnover. The exclusions prevent
     * an evicted voice being immediately readmitted in the same quantum. */
    Ngs2Handle *exclude_a=NULL,*exclude_h=NULL;
    if (total>32u && n && (sys->b2f2a_commits+1u)%128u==0) {
        for (uint32_t i=0;i<n;++i) {
            Ngs2Voice *v=ngs2_voice_locked(selected[i].handle);
            if (v && ngs2_atrac9_waveform(&v->waveform) && a_count) {
                exclude_a=selected[i].handle;
                memmove(&selected[i],&selected[i+1],(n-i-1u)*sizeof(selected[0]));
                --n; --a_count; break;
            }
        }
    }
    if (total>32u && n && (sys->b2f2a_commits+1u)%16u==0) {
        for (uint32_t i=0;i<n;++i) {
            Ngs2Voice *v=ngs2_voice_locked(selected[i].handle);
            if (v && ngs2_hevag_waveform(&v->waveform) && h_count) {
                exclude_h=selected[i].handle;
                memmove(&selected[i],&selected[i+1],(n-i-1u)*sizeof(selected[0]));
                --n; --h_count; break;
            }
        }
    }
    /* New slots are filled on deterministic round-robin order, not by hash. */
    uint32_t start=total ? sys->render_voice_cursor%total : 0;
    while (h_count<h_target && n<32u) {
        Ngs2Voice *v=ngs2_b2f2a_find_locked(sys,selected,n,start,total,
                                                  NGS2_WAVEFORM_HEVAG,exclude_h);
        if (!v) break;
        selected[n++]=(Ngs2B2F2AKey){v->handle,v->atrac9->generation};
        ++h_count;
    }
    while (a_count<a_target && n<32u) {
        Ngs2Voice *v=ngs2_b2f2a_find_locked(sys,selected,n,start,total,
                                                  NGS2_WAVEFORM_ATRAC9,exclude_a);
        if (!v) break;
        selected[n++]=(Ngs2B2F2AKey){v->handle,v->atrac9->generation};
        ++a_count;
    }
    /* Capacity never exceeds baseline. If a codec is oversubscribed,
     * evicted voice intentionally waits until the next admission cycle. */
    while (n<32u) {
        Ngs2Voice *v=ngs2_b2f2a_find_locked(sys,selected,n,start,total,0,
                exclude_a ? exclude_a : exclude_h);
        if (!v && exclude_a && exclude_h)
            v=ngs2_b2f2a_find_locked(sys,selected,n,start,total,0,exclude_h);
        if (!v) break;
        selected[n++]=(Ngs2B2F2AKey){v->handle,v->atrac9->generation};
    }
    uint32_t retained=0;
    for (uint32_t i=0;i<n;++i)
        for (uint32_t j=0;j<NGS2_B2F2A_COHORT_SLOTS;++j)
            if (prev[j].handle==selected[i].handle &&
                prev[j].generation==selected[i].generation) { ++retained; break; }
    *retained_out=retained;
    return n;
}

static ABI int32_t ngs2_render(Ngs2Handle *handle,const Ngs2RenderBuffer *buffers,uint32_t count) {
    if (count && !buffers) return NGS2_INVALID;

    /* Double opt-in: both ngs2_atrac9_hle=1 and ngs2_render_mix=1 required */
    int mix_active=ngs2_atrac9_enabled() && ngs2_render_mix_enabled();
    if (!mix_active) {
        /* Legacy HLE-1 permits null destinations and clears each non-null buffer. */
        for (uint32_t i=0;i<count;++i) {
            Ngs2RenderBuffer descriptor;
            memcpy(&descriptor,&buffers[i],sizeof(descriptor));
            if (descriptor.size>NGS2_MAX_RENDER_BYTES) return NGS2_INVALID;
            if (descriptor.buffer) memset(descriptor.buffer,0,(size_t)descriptor.size);
        }
        pthread_mutex_lock(&ngs2_lock);
        int valid=(ngs2_system_locked(handle)!=NULL);
        pthread_mutex_unlock(&ngs2_lock);
        return valid ? 0 : NGS2_INVALID;
    }

    /* Experimental real-time mixer path: strictly non-blocking trylock */
    if (pthread_mutex_trylock(&ngs2_lock)!=0) {
        atomic_fetch_add_explicit(&ngs2_global_trylock_contentions,1,memory_order_relaxed);
        return 0; /* Non-blocking: returns silence with preserved FIFOs */
    }

    Ngs2System *system=ngs2_system_locked(handle);
    if (!system) {
        pthread_mutex_unlock(&ngs2_lock);
        return NGS2_INVALID;
    }

    if (system->closing || !system->render_mix_enabled) {
        pthread_mutex_unlock(&ngs2_lock);
        return 0;
    }

    uintptr_t expected_owner=0;
    if (!atomic_compare_exchange_strong_explicit(&ngs2_render_owner,&expected_owner,
                                                  (uintptr_t)handle,memory_order_acquire,
                                                  memory_order_relaxed)) {
        ++system->ambiguous_renders_ignored;
        pthread_mutex_unlock(&ngs2_lock);
        return 0;
    }

    int32_t render_result=0;
    const int diag_on=ngs2_audio_diag_enabled();
    char diag_hb_line[768]={0},diag_matrix_line[1000]={0},diag_b0_line[1050]={0};
    char diag_b2_line[650]={0},diag_b2f2a_line[180]={0};
    char diag_f2b_line[620]={0},diag_f2c_line[180]={0};
    char diag_b2f1_line[768]={0},diag_b2f1_top[1600]={0};
    Ngs2RenderBuffer master={0};
    for (uint32_t i=0;i<count;++i) {
        Ngs2RenderBuffer descriptor;
        memcpy(&descriptor,&buffers[i],sizeof(descriptor));
        if (descriptor.size>NGS2_MAX_RENDER_BYTES || !descriptor.buffer) {
            render_result=NGS2_INVALID;
            pthread_mutex_unlock(&ngs2_lock);
            goto render_done;
        }
        if (!i) master=descriptor;
        memset(descriptor.buffer,0,(size_t)descriptor.size);
    }

    if (diag_on) {
        ++system->diag_calls;
        int is_master=(count>0 && master.size==8192 && master.waveform==0x18 && master.channels==8);
        if (count==1) {
            if (is_master) ++system->diag_cnt1_master;
            else ++system->diag_cnt1_other;
        } else if (count>1) {
            if (is_master) ++system->diag_multi_master;
            else ++system->diag_multi_other;
        }
        uint64_t now=ngs2_now_us();
        if (!system->diag_last_us || now-system->diag_last_us>=UINT64_C(1000000)) {
            system->diag_last_us=now;
            snprintf(diag_hb_line,sizeof(diag_hb_line),
                "NGS2-HB: calls=%llu cnt1master=%llu cnt1other=%llu multimaster=%llu multiother=%llu "
                "render_main=%llu ignored=%llu limit32=%llu virt_grains=%llu virt_untracked=%llu "
                "cancelled=%llu lock_contentions=%llu underruns=%llu "
                "AT9_frames=%llu HEVAG_frames=%llu HEVAG_fifo_in=%llu "
                "AT9_mix=%llu HEVAG_mix=%llu fifo_drop_at9=%llu fifo_drop_hevag=%llu mixed_total=%llu cancels=%llu "
                "state_queries=%llu/%llu/%llu/%llu cmd6_pause=%llu release=%llu stop=%llu "
                "cmd2=%llu cmd3=%llu peak_max=%.7f chmask=0x%02x\n",
                (unsigned long long)system->diag_calls,
                (unsigned long long)system->diag_cnt1_master,
                (unsigned long long)system->diag_cnt1_other,
                (unsigned long long)system->diag_multi_master,
                (unsigned long long)system->diag_multi_other,
                (unsigned long long)system->main_renders,
                (unsigned long long)system->ambiguous_renders_ignored,
                (unsigned long long)system->render_limit_exceeded,
                (unsigned long long)system->virtualized_grains,
                (unsigned long long)system->virtual_overflow_untracked,
                (unsigned long long)system->render_cancellations,
                (unsigned long long)system->trylock_contentions,
                (unsigned long long)system->underruns,
                (unsigned long long)system->decoded_frames,
                (unsigned long long)system->hevag_frames,
                (unsigned long long)system->hevag_grains,
                (unsigned long long)system->diag_at9_mix,
                (unsigned long long)system->diag_hevag_mix,
                (unsigned long long)system->diag_at9_fifo_drop,
                (unsigned long long)system->diag_hevag_fifo_drop,
                (unsigned long long)system->grains_consumed,
                (unsigned long long)system->cancellations,
                (unsigned long long)system->diag_voice_queries[NGS2_VOICE_STOPPED],
                (unsigned long long)system->diag_voice_queries[NGS2_VOICE_PLAYING],
                (unsigned long long)system->diag_voice_queries[NGS2_VOICE_PAUSED],
                (unsigned long long)system->diag_voice_queries[NGS2_VOICE_RELEASING],
                (unsigned long long)system->diag_cmd6[1],
                (unsigned long long)system->diag_cmd6[4],
                (unsigned long long)system->diag_cmd6[5],
                (unsigned long long)system->diag_cmd2,
                (unsigned long long)system->diag_cmd3,
                system->diag_peak_interval,system->diag_mask_interval);
            snprintf(diag_b0_line,sizeof(diag_b0_line),
                "NGS2-B0: handles=%llu waveforms=%llu reconfig=%llu data=%llu end=%llu accepted=%llu "
                "reject_terminal=%llu reject_format=%llu reject_descriptor=%llu reject_capacity=%llu "
                "reject_budget=%llu reject_copy=%llu reject_race=%llu "
                "first_hevag=%llu/%llu first_at9=%llu/%llu first_virtual=%llu/%llu "
                "virtual_hevag=%llu virtual_at9=%llu "
                "empty_hevag=%llu empty_at9=%llu empty_terminal=%llu terminal_frames=%llu "
                "starts=%llu matrix_updates=%llu\n",
                (unsigned long long)system->diag_voice_handles,
                (unsigned long long)system->diag_waveform_configs,
                (unsigned long long)system->diag_waveform_reconfigs,
                (unsigned long long)system->diag_segment_data,
                (unsigned long long)system->diag_segment_empty,
                (unsigned long long)system->diag_segment_accepted,
                (unsigned long long)system->diag_reject_terminal,
                (unsigned long long)system->diag_reject_format,
                (unsigned long long)system->diag_reject_descriptor,
                (unsigned long long)system->diag_reject_capacity,
                (unsigned long long)system->diag_reject_budget,
                (unsigned long long)system->diag_reject_copy,
                (unsigned long long)system->diag_reject_race,
                (unsigned long long)system->diag_first_mix_hevag,
                (unsigned long long)system->diag_first_zero_hevag,
                (unsigned long long)system->diag_first_mix_at9,
                (unsigned long long)system->diag_first_zero_at9,
                (unsigned long long)system->diag_first_virtual_hevag,
                (unsigned long long)system->diag_first_virtual_at9,
                (unsigned long long)system->diag_virtual_hevag,
                (unsigned long long)system->diag_virtual_at9,
                (unsigned long long)system->diag_empty_hevag,
                (unsigned long long)system->diag_empty_at9,
                (unsigned long long)system->diag_empty_terminal,
                (unsigned long long)system->diag_terminal_frames,
                (unsigned long long)system->diag_control_starts,
                (unsigned long long)system->diag_matrix_updates);
            snprintf(diag_b2_line,sizeof(diag_b2_line),
                "NGS2-B2: budget_voice_h/a=%llu/%llu budget_global_h/a=%llu/%llu "
                "budget_both_h/a=%llu/%llu reject_bytes=%llu max_voice_held=%llu "
                "max_global_held=%llu max_request=%llu eligible_max=%u "
                "eligible_overflow_sum=%llu overflow_bins=%llu/%llu/%llu "
                "clipped=%llu nonfinite=%llu\n",
                (unsigned long long)system->diag_b2_voice_only_hevag,
                (unsigned long long)system->diag_b2_voice_only_at9,
                (unsigned long long)system->diag_b2_global_only_hevag,
                (unsigned long long)system->diag_b2_global_only_at9,
                (unsigned long long)system->diag_b2_both_hevag,
                (unsigned long long)system->diag_b2_both_at9,
                (unsigned long long)system->diag_b2_rejected_bytes,
                (unsigned long long)system->diag_b2_max_voice_held,
                (unsigned long long)system->diag_b2_max_global_held,
                (unsigned long long)system->diag_b2_max_request_bytes,
                system->diag_b2_max_eligible,
                (unsigned long long)system->diag_b2_eligible_sum,
                (unsigned long long)system->diag_b2_overflow_33_64,
                (unsigned long long)system->diag_b2_overflow_65_128,
                (unsigned long long)system->diag_b2_overflow_129_plus,
                (unsigned long long)system->diag_b2_clipped_samples,
                (unsigned long long)system->diag_b2_nonfinite_samples);
            ngs2_b2f1_snapshot_locked(system,diag_b2f1_line,sizeof(diag_b2f1_line),diag_b2f1_top,sizeof(diag_b2f1_top));
            snprintf(diag_f2b_line,sizeof(diag_f2b_line),
                "NGS2-B2F2B-DIAG: silent_mixed=%llu audible_virtualized=%llu "
                "wasted_quanta=%llu midstream_h/a=%llu/%llu "
                "reconfig_active_drop=%llu same_hevag_drop=%llu end_while_live=%llu "
                "boundary_max=%.7f boundary_gt_010=%llu boundary_checks=%llu\n",
                (unsigned long long)system->diag_f2b_silent_mixed,
                (unsigned long long)system->diag_f2b_audible_virtualized,
                (unsigned long long)system->diag_f2b_wasted_quanta,
                (unsigned long long)system->diag_f2b_midstream_hevag,
                (unsigned long long)system->diag_f2b_midstream_at9,
                (unsigned long long)system->diag_f2b_reconfig_active_drop,
                (unsigned long long)system->diag_f2b_reconfig_same_hevag_drop,
                (unsigned long long)system->diag_f2b_end_while_live,
                system->diag_f2b_boundary_max,
                (unsigned long long)system->diag_f2b_boundary_gt_010,
                (unsigned long long)system->diag_f2b_boundary_checks);
            if (ngs2_b2f2c_enabled()) {
                snprintf(diag_f2c_line,sizeof(diag_f2c_line),
                    "NGS2-B2F2C: zero_bypassed=%llu bypass_quanta=%llu mode=zero-matrix\n",
                    (unsigned long long)system->diag_f2c_bypassed,
                    (unsigned long long)system->diag_f2c_bypass_quanta);
            }
            if (ngs2_b2f2a_enabled() && !ngs2_b2f2c_enabled()) {
                snprintf(diag_b2f2a_line,sizeof(diag_b2f2a_line),
                    "NGS2-B2F2A: commits=%llu retained=%llu new=%llu cap=%u mode=stable\n",
                    (unsigned long long)system->b2f2a_commits,
                    (unsigned long long)system->b2f2a_retained,
                    (unsigned long long)system->b2f2a_new,
                    NGS2_RENDER_MAX_VOICES);
            }
            system->diag_peak_interval=0.0f;
            system->diag_mask_interval=0;
        }
    }

    /* Count alone cannot identify the master bus. Ambiguous calls remain silent. */
    if (count<=1 || master.size!=8192 || master.waveform!=0x18 || master.channels!=8) {
        ++system->ambiguous_renders_ignored;
        pthread_mutex_unlock(&ngs2_lock);
        goto render_done;
    }

    uint64_t render_start=ngs2_now_us();

    /* Ignore output-only racks; count only current-generation source grains. */
    const int b2f2c_pilot=ngs2_b2f2c_enabled();
    /* raw counts all FIFO sources (for legacy telemetry); total_eligible is
     * the subset that can contribute to the F32 master when C is enabled. */
    uint32_t total_eligible=0,total_eligible_raw=0;
    for (Ngs2Rack *rack=system->racks;rack;rack=rack->next) {
        if (rack->rack_id==0x2000 || rack->rack_id==0x3000) continue;
        for (uint32_t i=0;i<rack->capacity;++i) {
            Ngs2Voice *voice=&rack->voices[i];
            if (voice->internal_state==NGS2_VOICE_PLAYING && voice->atrac9 &&
                !voice->atrac9->closing && !voice->atrac9->faulted) {
                if (voice->atrac9->pcm_count>0) {
                    Ngs2PcmGrain *head=&voice->atrac9->fifo[voice->atrac9->pcm_read];
                    if (head->generation==voice->atrac9->generation) {
                        ++total_eligible_raw;
                        if (!b2f2c_pilot || ngs2_b2f2b_matrix_audible_locked(voice))
                            ++total_eligible;
                    }
                } else {
                    ++voice->atrac9->underruns;
                    ++system->underruns;
                    if (diag_on && (voice->atrac9->active_valid ||
                                    voice->atrac9->segment_count ||
                                    voice->atrac9->segment_reservations) &&
                        !(ngs2_hevag_waveform(&voice->waveform) &&
                          voice->atrac9->hevag.terminal_seen)) {
                        if (ngs2_hevag_waveform(&voice->waveform))
                            ++system->diag_f2b_midstream_hevag;
                        else if (ngs2_atrac9_waveform(&voice->waveform))
                            ++system->diag_f2b_midstream_at9;
                    }
                    if (diag_on) {
                        if (voice->waveform.format==NGS2_WAVEFORM_HEVAG) {
                            ++system->diag_empty_hevag;
                            if (voice->atrac9->hevag.terminal_seen)
                                ++system->diag_empty_terminal;
                        } else if (voice->waveform.format==NGS2_WAVEFORM_ATRAC9)
                            ++system->diag_empty_at9;
                    }
                }
            }
        }
    }

    /* Never mute the master because the source voice count exceeds the
     * mathematical mixing budget. Extra grains are virtualized at commit. */
    if (total_eligible_raw>NGS2_RENDER_MAX_VOICES) ++system->render_limit_exceeded;
    if (diag_on) {
        if (total_eligible_raw>system->diag_b2_max_eligible)
            system->diag_b2_max_eligible=total_eligible_raw;
        if (total_eligible_raw>NGS2_RENDER_MAX_VOICES) {
            system->diag_b2_eligible_sum+=total_eligible_raw;
            if (total_eligible_raw<=64) ++system->diag_b2_overflow_33_64;
            else if (total_eligible_raw<=128) ++system->diag_b2_overflow_65_128;
            else ++system->diag_b2_overflow_129_plus;
        }
    }

    /* A zero-matrix-only render must still drain every FIFO transactionally. */
    if (!total_eligible_raw) {
        system->render_time_us+=(ngs2_now_us()-render_start);
        ++system->main_renders;
        pthread_mutex_unlock(&ngs2_lock);
        goto render_done;
    }

    Ngs2RenderSnapshot *snapshots=ngs2_render_snapshots;
    Ngs2VirtualSnapshot *virtual=ngs2_render_virtual;
    memset(snapshots,0,sizeof(ngs2_render_snapshots));
    uint32_t active_snapshots=0,virtual_count=0,seen=0;
    uint32_t start=total_eligible ? system->render_voice_cursor%total_eligible : 0;
    const int b2f2a_pilot=!b2f2c_pilot && ngs2_b2f2a_enabled() &&
                             total_eligible>NGS2_RENDER_MAX_VOICES;
    Ngs2B2F2AKey b2f2a_selected[NGS2_B2F2A_COHORT_SLOTS]={{0}};
    uint32_t b2f2a_retained=0, b2f2a_count=0;
    if (b2f2a_pilot)
        b2f2a_count=ngs2_b2f2a_prepare_locked(system,total_eligible,
                                               b2f2a_selected,&b2f2a_retained);

    for (Ngs2Rack *rack=system->racks;rack;rack=rack->next) {
        if (rack->rack_id==0x2000 || rack->rack_id==0x3000) continue;
        for (uint32_t i=0;i<rack->capacity;++i) {
            Ngs2Voice *voice=&rack->voices[i];
            if (voice->internal_state!=NGS2_VOICE_PLAYING || !voice->atrac9) continue;
            Ngs2Atrac9Voice *state=voice->atrac9;
            if (state->closing || state->faulted || !state->pcm_count) continue;

            Ngs2PcmGrain *head=&state->fifo[state->pcm_read];
            if (head->generation!=state->generation) continue;

            /* Ordinal rotation distributes the 32 audible slots fairly while
             * keeping every selected or virtual FIFO tied to this quantum. */
            const int f2c_zero=b2f2c_pilot &&
                               !ngs2_b2f2b_matrix_audible_locked(voice);
            uint32_t ordinal=0;
            if (!f2c_zero) {
                ordinal=seen>=start ? seen-start : total_eligible-start+seen;
                ++seen;
            }
            if (f2c_zero || (b2f2a_pilot ?
                !ngs2_b2f2a_has(b2f2a_selected,b2f2a_count,voice) :
                ordinal>=NGS2_RENDER_MAX_VOICES)) {
                if (virtual_count<NGS2_RENDER_MAX_VIRTUAL_VOICES) {
                    Ngs2VirtualSnapshot *v=&virtual[virtual_count++];
                    *v=(Ngs2VirtualSnapshot){rack->handle,voice->handle,state->generation,
                        head->generation,i,state->pcm_read,
                        diag_on && voice->diag_onset_pending ? voice->diag_onset_sequence : 0,
                        diag_on ? (uint8_t)ngs2_b2f2b_matrix_audible_locked(voice) : 0,
                        (uint8_t)f2c_zero};
                } else {
                    ++system->virtual_overflow_untracked;
                }
                continue;
            }

            /* Snapshot grain and matrix under lock WITHOUT advancing FIFO */
            snapshots[active_snapshots].voice_handle=voice->handle;
            snapshots[active_snapshots].generation=state->generation;
            snapshots[active_snapshots].expected_pcm_read=state->pcm_read;
            snapshots[active_snapshots].expected_grain_generation=head->generation;
            snapshots[active_snapshots].channels=voice->waveform.channels;
            snapshots[active_snapshots].diag_f2b_matrix_audible=
                diag_on ? (uint8_t)ngs2_b2f2b_matrix_audible_locked(voice) : 0;
            snapshots[active_snapshots].diag_is_hevag=voice->waveform.format==NGS2_WAVEFORM_HEVAG;
            if (diag_on && voice->diag_onset_pending) {
                snapshots[active_snapshots].diag_onset_pending=1;
                snapshots[active_snapshots].diag_onset_sequence=voice->diag_onset_sequence;
            }
            if (diag_on && snapshots[active_snapshots].diag_is_hevag && !system->diag_hevag_matrix_logged) {
                size_t n=(size_t)snprintf(diag_matrix_line,sizeof(diag_matrix_line),
                    "NGS2-MATRIX: rack=%#x voice=%u codec=HEVAG cmd1_words=%u [channel:cmd1/cmd2/cmd3 (valid)] ",
                    rack->rack_id,voice->index,voice->descriptor_words);
                for (unsigned ch=0;ch<8 && n<sizeof(diag_matrix_line);++ch) {
                    union { uint32_t u; float f; } cvt={ch<voice->descriptor_words ? voice->descriptors[ch] : 0};
                    int len=snprintf(diag_matrix_line+n,sizeof(diag_matrix_line)-n,
                        "%u:%.4g/%.4g(%d)/%.4g(%d) ",ch,cvt.f,
                        voice->command2[ch].gain,voice->command2[ch].valid,
                        voice->command3[ch].gain,voice->command3[ch].valid);
                    if (len<0) break;
                    n+=(size_t)len;
                }
                if (n<sizeof(diag_matrix_line)-2) {
                    diag_matrix_line[n++]='\n'; diag_matrix_line[n]=0;
                }
                system->diag_hevag_matrix_logged=1;
            }
            uint32_t words=voice->descriptor_words;
            if (words>NGS2_MAX_DESCRIPTOR_WORDS) words=NGS2_MAX_DESCRIPTOR_WORDS;
            snapshots[active_snapshots].matrix_words=words;
            for (uint32_t w=0;w<words;++w) {
                union { uint32_t u; float f; } cvt={voice->descriptors[w]};
                snapshots[active_snapshots].matrix[w]=cvt.f;
            }
            snapshots[active_snapshots].grain=*head;
            snapshots[active_snapshots].active=1;
            ++active_snapshots;
        }
    }

    ++system->main_renders;
    pthread_mutex_unlock(&ngs2_lock);

    /* Test synchronization hook invoked outside lock during mix window */
    if (ngs2_render_mix_hook) {
        ngs2_render_mix_hook(ngs2_render_mix_hook_arg);
    }

    /* Mathematical mixing executes outside ngs2_lock in gate-owned static workspace. */
    float *stage_f32=ngs2_render_stage;
    memset(stage_f32,0,sizeof(ngs2_render_stage));

    for (uint32_t s=0;s<active_snapshots;++s) {
        if (!snapshots[s].active) continue;
        uint32_t num_ch=snapshots[s].channels;
        if (!num_ch || num_ch>8) continue;
        const int16_t *pcm=snapshots[s].grain.pcm;
        const float *mat=snapshots[s].matrix;
        uint32_t words=snapshots[s].matrix_words;

        for (uint32_t f=0;f<256;++f) {
            for (uint32_t dst=0;dst<8;++dst) {
                float sum=0.0f;
                for (uint32_t src=0;src<num_ch;++src) {
                    uint32_t idx=dst*num_ch+src;
                    if (idx<words) {
                        float gain=mat[idx];
                        if (isfinite(gain) && gain>=-64.0f && gain<=64.0f) {
                            float s_val=(float)pcm[f*num_ch+src]*(1.0f/32768.0f);
                            sum+=s_val*gain;
                        }
                    }
                }
                float contribution=sum*0.25f; /* -12 dB diagnostic master attenuation */
                stage_f32[f*8+dst]+=contribution;
                if (diag_on && snapshots[s].diag_onset_pending &&
                    fabsf(contribution)>snapshots[s].diag_onset_peak)
                    snapshots[s].diag_onset_peak=fabsf(contribution);
            }
        }
    }

    /* Defensive clamp [-1.0, 1.0] and NaN/Inf sanitization outside lock */
    uint64_t b2_clipped_local=0,b2_nonfinite_local=0;
    float peak=0.0f;
    unsigned diag_ch_mask=0;
    double sum_sq=0.0;
    for (uint32_t i=0;i<256*8;++i) {
        float val=stage_f32[i];
        if (diag_on) {
            if (!isfinite(val)) ++b2_nonfinite_local;
            else if (val>1.0f || val< -1.0f) ++b2_clipped_local;
        }
        if (!isfinite(val)) val=0.0f;
        else if (val>1.0f) val=1.0f;
        else if (val<-1.0f) val=-1.0f;
        stage_f32[i]=val;
        float abs_val=fabsf(val);
        if (abs_val>peak) peak=abs_val;
        if (diag_on && abs_val>0.00001f) diag_ch_mask|=1u<<(i%8);
        sum_sq+=(double)val*(double)val;
    }

    /* Transactional publication phase: non-blocking trylock revalidation */
    if (pthread_mutex_trylock(&ngs2_lock)!=0) {
        atomic_fetch_add_explicit(&ngs2_global_trylock_contentions,1,memory_order_relaxed);
        /* Contention: do not publish, do not advance FIFOs (grains preserved) */
        goto render_done;
    }

    system=ngs2_system_locked(handle);
    if (!system || system->closing) {
        pthread_mutex_unlock(&ngs2_lock);
        goto render_done;
    }

    /* Revalidate all voice snapshots against current system/rack/voice state */
    int any_cancelled=0;
    for (uint32_t s=0;s<active_snapshots;++s) {
        if (!snapshots[s].active) continue;
        Ngs2Voice *voice=ngs2_voice_locked(snapshots[s].voice_handle);
        if (!voice || voice->internal_state!=NGS2_VOICE_PLAYING ||
            !voice->atrac9 || voice->atrac9->closing || voice->atrac9->faulted ||
            voice->atrac9->generation!=snapshots[s].generation ||
            !voice->atrac9->pcm_count ||
            voice->atrac9->pcm_read!=snapshots[s].expected_pcm_read ||
            voice->atrac9->fifo[snapshots[s].expected_pcm_read].generation!=snapshots[s].expected_grain_generation) {
            any_cancelled=1;
            break;
        }
    }

    if (any_cancelled) {
        /* Safe diagnostic solution: silence for full quantum, surviving FIFOs preserved */
        ++system->render_cancellations;
    } else {
        /* Descriptor was snapshotted before unlock; never re-read caller-owned fields. */
        memcpy(master.buffer,stage_f32,sizeof(ngs2_render_stage));
        if (diag_on) {
            system->diag_b2_clipped_samples+=b2_clipped_local;
            system->diag_b2_nonfinite_samples+=b2_nonfinite_local;
        }

        /* B2-F2B-DIAG: boundary measurement on actually published master PCM,
         * after revalidation. Natural transients also contribute: not crackle proof. */
        if (diag_on) {
            if (system->diag_f2b_prev_valid) {
                for (uint32_t c=0;c<8u;++c) {
                    float delta=fabsf(stage_f32[c]-system->diag_f2b_prev_last[c]);
                    if (delta>system->diag_f2b_boundary_max)
                        system->diag_f2b_boundary_max=delta;
                    if (delta>0.10f) ++system->diag_f2b_boundary_gt_010;
                    ++system->diag_f2b_boundary_checks;
                }
            }
            for (uint32_t c=0;c<8u;++c)
                system->diag_f2b_prev_last[c]=stage_f32[255u*8u+c];
            system->diag_f2b_prev_valid=1;
        }
        uint32_t f2b_zero_mixed=0,f2b_audible_virtual=0,f2c_zero_drain=0;
        /* Publication succeeded: commit FIFO advances under the same lock. */
        for (uint32_t s=0;s<active_snapshots;++s) {
            Ngs2Voice *voice=ngs2_voice_locked(snapshots[s].voice_handle);
            Ngs2Atrac9Voice *state=voice->atrac9;
            state->pcm_read=(state->pcm_read+1u)%NGS2_ATRAC9_FIFO_CAPACITY;
            --state->pcm_count;
            ++system->grains_consumed;
            if (diag_on) {
                if (!snapshots[s].diag_f2b_matrix_audible) {
                    ++system->diag_f2b_silent_mixed;
                    ++f2b_zero_mixed;
                }
                if (snapshots[s].diag_is_hevag) ++system->diag_hevag_mix;
                else ++system->diag_at9_mix;
                if (snapshots[s].diag_onset_pending && voice->diag_onset_pending &&
                    snapshots[s].diag_onset_sequence==voice->diag_onset_sequence) {
                    int quiet=snapshots[s].diag_onset_peak<0.00001f;
                    if (snapshots[s].diag_is_hevag) {
                        if (quiet) ++system->diag_first_zero_hevag;
                        else ++system->diag_first_mix_hevag;
                    } else {
                        if (quiet) ++system->diag_first_zero_at9;
                        else ++system->diag_first_mix_at9;
                    }
                    voice->diag_onset_pending=0;
                    if (quiet && system->diag_b0_event_lines<NGS2_B0_EVENT_LOG_LIMIT) {
                        ++system->diag_b0_event_lines;
                        printf("NGS2-B0-EVENT: kind=first_quiet rack=%#x/%u voice=%u "
                               "codec=%s gen=%llu seq=%llu matrix=%u peak=%.7f\n",
                               voice->rack->rack_id,voice->rack->instance,voice->index,
                               snapshots[s].diag_is_hevag ? "HEVAG" : "ATRAC9",
                               (unsigned long long)state->generation,
                               (unsigned long long)voice->diag_onset_sequence,
                               snapshots[s].matrix_words,snapshots[s].diag_onset_peak);
                    }
                }
            }
        }
        /* Virtualized voices were not mixed. Revalidate each rack/handle,
         * generation and read index after the unlocked mixing window. Only
         * the successful render commit may advance an overflow FIFO. */
        for (uint32_t v=0;v<virtual_count;++v) {
            const Ngs2VirtualSnapshot *candidate=&virtual[v];
            Ngs2Rack *rack=ngs2_rack_locked(candidate->rack_handle);
            if (!rack || rack->system!=system || candidate->voice_index>=rack->capacity)
                continue;
            Ngs2Voice *voice=&rack->voices[candidate->voice_index];
            if (voice->handle!=candidate->voice_handle ||
                voice->internal_state!=NGS2_VOICE_PLAYING || !voice->atrac9) continue;
            Ngs2Atrac9Voice *state=voice->atrac9;
            if (state->closing || state->faulted ||
                state->generation!=candidate->generation || !state->pcm_count ||
                state->pcm_read!=candidate->expected_pcm_read ||
                state->fifo[state->pcm_read].generation!=candidate->expected_grain_generation)
                continue;
            state->pcm_read=(state->pcm_read+1u)%NGS2_ATRAC9_FIFO_CAPACITY;
            --state->pcm_count;
            ++system->virtualized_grains;
            if (diag_on && candidate->f2c_zero_bypass) {
                ++system->diag_f2c_bypassed;
                ++f2c_zero_drain;
            }
            if (diag_on) {
                if (candidate->diag_f2b_matrix_audible) {
                    ++system->diag_f2b_audible_virtualized;
                    ++f2b_audible_virtual;
                }
                int hevag=voice->waveform.format==NGS2_WAVEFORM_HEVAG;
                if (hevag) ++system->diag_virtual_hevag;
                else ++system->diag_virtual_at9;
                if (candidate->diag_onset_sequence && voice->diag_onset_pending &&
                    candidate->diag_onset_sequence==voice->diag_onset_sequence) {
                    if (hevag) ++system->diag_first_virtual_hevag;
                    else ++system->diag_first_virtual_at9;
                    voice->diag_onset_pending=0;
                    if (system->diag_b0_event_lines<NGS2_B0_EVENT_LOG_LIMIT) {
                        ++system->diag_b0_event_lines;
                        printf("NGS2-B0-EVENT: kind=first_virtual rack=%#x/%u voice=%u "
                               "codec=%s gen=%llu seq=%llu\n",
                               rack->rack_id,rack->instance,voice->index,
                               hevag ? "HEVAG" : "ATRAC9",
                               (unsigned long long)state->generation,
                               (unsigned long long)voice->diag_onset_sequence);
                    }
                }
            }
        }
        if (diag_on && f2b_zero_mixed && f2b_audible_virtual)
            ++system->diag_f2b_wasted_quanta;
        if (diag_on && f2c_zero_drain) ++system->diag_f2c_bypass_quanta;
        if (b2f2a_pilot) {
            memset(system->b2f2a_keys,0,sizeof(system->b2f2a_keys));
            memcpy(system->b2f2a_keys,b2f2a_selected,
                   b2f2a_count*sizeof(b2f2a_selected[0]));
            ++system->b2f2a_commits;
            system->b2f2a_retained+=b2f2a_retained;
            system->b2f2a_new+=b2f2a_count-b2f2a_retained;
        } else if (ngs2_b2f2a_enabled()) {
            /* Under the cap or C takes priority: discard stale A cohort. */
            memset(system->b2f2a_keys,0,sizeof(system->b2f2a_keys));
        }
        /* No division by zero in a quantum containing only zero-matrix voices. */
        if (total_eligible)
            system->render_voice_cursor=(start+1u)%total_eligible;
        pthread_cond_broadcast(&ngs2_cond);

        if (peak>system->peak_pcm) system->peak_pcm=peak;
        if (diag_on) {
            if (peak>system->diag_peak_interval) system->diag_peak_interval=peak;
            system->diag_mask_interval|=diag_ch_mask;
        }
        system->sum_sq_pcm+=sum_sq;
        system->total_samples_rendered+=256*8;
        system->voices_mixed+=active_snapshots;
        if (ngs2_mixer_pcm_logs<8) {
            ++ngs2_mixer_pcm_logs;
            printf("Runtime: NGS2 Mixer PCM grains_consumed=%llu voices_mixed=%llu peak_pcm=%.4f\n",
                   (unsigned long long)system->grains_consumed,
                   (unsigned long long)system->voices_mixed,system->peak_pcm);
        }
    }

    system->render_time_us+=(ngs2_now_us()-render_start);
    system->trylock_contentions+=atomic_exchange_explicit(&ngs2_global_trylock_contentions,0,memory_order_relaxed);
    pthread_mutex_unlock(&ngs2_lock);

render_done:
    atomic_store_explicit(&ngs2_render_owner,0,memory_order_release);
    /* Print diagnostics only after releasing NGS2 locks and the render gate. */
    if (diag_hb_line[0]) fputs(diag_hb_line,stdout);
    if (diag_b0_line[0]) fputs(diag_b0_line,stdout);
    if (diag_b2_line[0]) fputs(diag_b2_line,stdout);
    if (diag_b2f2a_line[0]) fputs(diag_b2f2a_line,stdout);
    if (diag_f2b_line[0]) fputs(diag_f2b_line,stdout);
    if (diag_f2c_line[0]) fputs(diag_f2c_line,stdout);
    if (diag_b2f1_line[0]) fputs(diag_b2f1_line,stdout);
    if (diag_b2f1_top[0]) fputs(diag_b2f1_top,stdout);
    if (diag_matrix_line[0]) fputs(diag_matrix_line,stdout);
    return render_result;
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
    if (!voice->handle) {
        voice->handle=ngs2_new_handle_locked();
        if (ngs2_audio_diag_enabled() && rack->rack_id==0x1000)
            ++rack->system->diag_voice_handles;
    }
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
    if (ngs2_audio_diag_enabled()) {
        if (!data_address && !descriptor_address && !flag_10 && !flag_14)
            ++system->diag_segment_empty;
        else ++system->diag_segment_data;
    }
    uint64_t codec_sequence=++ngs2_codec_sequence;
    uint32_t codec_prior_command=voice->codec_last_command;
    uint64_t codec_prior_sequence=voice->codec_last_sequence;
    voice->codec_last_command=UINT32_C(0x10000001);
    voice->codec_last_sequence=codec_sequence;
    if (!data_address && !descriptor_address && !flag_10 && !flag_14) {
        if (ngs2_codec_logs<NGS2_CODEC_LOG_LIMIT) {
            ++ngs2_codec_logs;
            printf("Runtime: NGS2 codec dispatch seq=%llu rack=%#x/%u voice=%u prev=%#x@%llu "
                   "format=%#x channels=%u rate=%u codec=%08x action=reset\n",
                   (unsigned long long)codec_sequence,voice->rack->rack_id,voice->rack->instance,
                   voice->index,codec_prior_command,(unsigned long long)codec_prior_sequence,
                   voice->waveform.format,voice->waveform.channels,voice->waveform.sample_rate,
                   voice->waveform.codec_config);
        }
        if (ngs2_hevag_waveform(&voice->waveform) && voice->atrac9) {
            Ngs2Atrac9Voice *st=voice->atrac9;
            if (ngs2_audio_diag_enabled() &&
                (st->pcm_count || st->active_valid || st->segment_count ||
                 st->decode_inflight))
                ++system->diag_f2b_end_while_live;
            voice->atrac9->hevag.end_requested=1;
            pthread_cond_broadcast(&ngs2_cond);
        }
        pthread_mutex_unlock(&ngs2_lock); return 0;
    }
    const char *storage="invalid";
    int descriptor_ok=size==NGS2_SEGMENT_COMMAND_SIZE && descriptor_address &&
                      ngs2_read_segment_descriptor(descriptor_address,descriptor,&storage);
    uint32_t byte_count=descriptor_ok ? ngs2_u32(descriptor,4) : 0;
    if (ngs2_codec_logs<NGS2_CODEC_LOG_LIMIT) {
        ++ngs2_codec_logs;
        printf("Runtime: NGS2 codec dispatch seq=%llu rack=%#x/%u voice=%u prev=%#x@%llu "
               "format=%#x channels=%u rate=%u codec=%08x bytes=%u descriptor=%s flags=%u,%u "
               "action=%s\n",
               (unsigned long long)codec_sequence,voice->rack->rack_id,voice->rack->instance,
               voice->index,codec_prior_command,(unsigned long long)codec_prior_sequence,
               voice->waveform.format,voice->waveform.channels,voice->waveform.sample_rate,
               voice->waveform.codec_config,byte_count,descriptor_ok ? storage : "invalid",
               flag_10,flag_14,ngs2_codec_dispatch(&voice->waveform));
    }
    if (!ngs2_codec_waveform(&voice->waveform)) {
        if (ngs2_audio_diag_enabled()) ++system->diag_reject_format;
        ++system->events_rejected; pthread_mutex_unlock(&ngs2_lock); return 0;
    }
    Ngs2Atrac9Voice *state=voice->atrac9;
    if (!state) {
        state=calloc(1,sizeof(*state));
        if (state) { state->generation=1; voice->atrac9=state; }
    }
    if (!state || !voice->waveform.valid || !descriptor_ok || !byte_count ||
        (ngs2_hevag_waveform(&voice->waveform) && state->hevag.terminal_seen) ||
        byte_count>NGS2_ATRAC9_SEGMENT_LIMIT || !runtime_memory_copy_snapshot ||
        ngs2_atrac9_depth(state)>=NGS2_ATRAC9_EVENT_CAPACITY ||
        state->queued_compressed_bytes+byte_count>NGS2_ATRAC9_VOICE_BUDGET ||
        ngs2_compressed_bytes+byte_count>NGS2_ATRAC9_GLOBAL_BUDGET) {
        if (ngs2_audio_diag_enabled()) {
            if (state && ngs2_hevag_waveform(&voice->waveform) &&
                state->hevag.terminal_seen) {
                ++system->diag_reject_terminal;
                if (!voice->diag_terminal_reported &&
                    system->diag_b0_event_lines<NGS2_B0_EVENT_LOG_LIMIT) {
                    voice->diag_terminal_reported=1;
                    ++system->diag_b0_event_lines;
                    printf("NGS2-B0-EVENT: kind=terminal_reject rack=%#x/%u voice=%u "
                           "gen=%llu bytes=%u depth=%u fifo=%u state=%u\n",
                           voice->rack->rack_id,voice->rack->instance,voice->index,
                           (unsigned long long)state->generation,byte_count,
                           ngs2_atrac9_depth(state),state->pcm_count,voice->internal_state);
                }
            } else if (!voice->waveform.valid || !descriptor_ok || !byte_count || !state ||
                       !runtime_memory_copy_snapshot)
                ++system->diag_reject_descriptor;
            else if (byte_count>NGS2_ATRAC9_SEGMENT_LIMIT ||
                     ngs2_atrac9_depth(state)>=NGS2_ATRAC9_EVENT_CAPACITY)
                ++system->diag_reject_capacity;
            else {
                ++system->diag_reject_budget;
                /* Both limits may fail simultaneously; buckets are exclusive. */
                int voice_over=(state->queued_compressed_bytes+byte_count>
                                NGS2_ATRAC9_VOICE_BUDGET);
                int global_over=(ngs2_compressed_bytes+byte_count>
                                 NGS2_ATRAC9_GLOBAL_BUDGET);
                int hevag=ngs2_hevag_waveform(&voice->waveform);
                if (voice_over && global_over) {
                    if (hevag) ++system->diag_b2_both_hevag;
                    else ++system->diag_b2_both_at9;
                } else if (voice_over) {
                    if (hevag) ++system->diag_b2_voice_only_hevag;
                    else ++system->diag_b2_voice_only_at9;
                } else if (global_over) {
                    if (hevag) ++system->diag_b2_global_only_hevag;
                    else ++system->diag_b2_global_only_at9;
                }
                system->diag_b2_rejected_bytes+=byte_count;
                if (state->queued_compressed_bytes>system->diag_b2_max_voice_held)
                    system->diag_b2_max_voice_held=state->queued_compressed_bytes;
                if (ngs2_compressed_bytes>system->diag_b2_max_global_held)
                    system->diag_b2_max_global_held=ngs2_compressed_bytes;
                if (byte_count>system->diag_b2_max_request_bytes)
                    system->diag_b2_max_request_bytes=byte_count;
            }
        }
        ngs2_atrac9_reject_locked(system,state); pthread_mutex_unlock(&ngs2_lock); return 0;
    }
    if (ngs2_hevag_waveform(&voice->waveform) && ngs2_hevag_route_logs<16) {
        uint32_t nonzero=0;
        for (uint32_t i=0;i<voice->descriptor_words;++i) {
            union { uint32_t u; float f; } value={voice->descriptors[i]};
            if (isfinite(value.f) && value.f!=0.0f) ++nonzero;
        }
        uint32_t command2_mask=0,command3_mask=0;
        for (uint32_t i=0;i<8;++i) {
            if (voice->command2[i].valid) command2_mask|=UINT32_C(1)<<i;
            if (voice->command3[i].valid) command3_mask|=UINT32_C(1)<<i;
        }
        ++ngs2_hevag_route_logs;
        printf("Runtime: NGS2 HEVAG routing rack=%#x/%u voice=%u descriptors=%u nonzero=%u "
               "gain2_mask=%#x gain3_mask=%#x connection=%s\n",
               voice->rack->rack_id,voice->rack->instance,voice->index,
               voice->descriptor_words,nonzero,command2_mask,command3_mask,
               voice->connection ? "present" : "absent");
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
               !(ngs2_hevag_waveform(&voice->waveform) && state->hevag.terminal_seen) &&
               state->segment_count<NGS2_ATRAC9_EVENT_CAPACITY;
    if (accept) {
        if (ngs2_audio_diag_enabled()) {
            ++system->diag_segment_accepted;
            /* Onset-after-idle, not claimed to identify a specific SFX event. */
            if (!state->pcm_count && !state->active_valid && !state->segment_count)
                voice->diag_onset_pending=1;
            if (voice->diag_onset_pending) voice->diag_onset_sequence=sequence;
        }
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
        if (ngs2_audio_diag_enabled()) {
            if (ngs2_hevag_waveform(&voice->waveform) && state->hevag.terminal_seen)
                ++system->diag_reject_race;
            else ++system->diag_reject_copy;
        }
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
        if (ngs2_audio_diag_enabled()) ++voice->rack->system->diag_matrix_updates;
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
        *slot=(Ngs2ChannelGain){channel,ngs2_u16(params,2),gain,1};
        if (ngs2_audio_diag_enabled()) {
            if (command==2) ++voice->rack->system->diag_cmd2;
            else ++voice->rack->system->diag_cmd3;
        }
        return 0;
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
        uint32_t action=ngs2_u32(params,8);
        if (ngs2_audio_diag_enabled() && action<6) ++voice->rack->system->diag_cmd6[action];
        switch (action) {
        case 0:
            if (ngs2_audio_diag_enabled()) ++voice->rack->system->diag_control_starts;
            voice->internal_state=NGS2_VOICE_PLAYING;
            if (voice->rack->system->atrac9_enabled && ngs2_codec_waveform(&voice->waveform) &&
                !voice->atrac9) {
                voice->atrac9=calloc(1,sizeof(*voice->atrac9));
                if (voice->atrac9) voice->atrac9->generation=1;
            }
            pthread_cond_broadcast(&ngs2_cond);
            break;
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
        if (voice->atrac9) {
            Ngs2Atrac9Voice *state=voice->atrac9;
            int waveform_changed=!voice->waveform.valid ||
                                 memcmp(&voice->waveform,&value,sizeof(value))!=0;
            /* B1: HEVAG one-shot reuse may keep the same waveform layout. A closed
             * stream must start a fresh generation; an open stream is preserved. */
            int hevag_closed=ngs2_hevag_waveform(&value) &&
                             (state->hevag.terminal_seen || state->hevag.end_requested);
            if (waveform_changed || hevag_closed) {
                if (ngs2_audio_diag_enabled() &&
                    (state->pcm_count || state->active_valid || state->segment_count ||
                     state->decode_inflight)) {
                    ++voice->rack->system->diag_f2b_reconfig_active_drop;
                    if (!waveform_changed && hevag_closed)
                        ++voice->rack->system->diag_f2b_reconfig_same_hevag_drop;
                }
                int count_cancellation=waveform_changed || !state->hevag.terminal_seen;
                ngs2_atrac9_clear_pending_locked(voice,count_cancellation);
            }
        }
        if (voice->rack->system->atrac9_enabled) {
            uint64_t codec_sequence=++ngs2_codec_sequence;
            uint32_t codec_prior_command=voice->codec_last_command;
            uint64_t codec_prior_sequence=voice->codec_last_sequence;
            voice->codec_last_command=UINT32_C(0x10000000);
            voice->codec_last_sequence=codec_sequence;
            if (ngs2_codec_logs<NGS2_CODEC_LOG_LIMIT) {
                ++ngs2_codec_logs;
                printf("Runtime: NGS2 waveform config seq=%llu rack=%#x/%u voice=%u prev=%#x@%llu "
                       "format=%#x channels=%u rate=%u codec=%08x fields=%u,%u,%u dispatch=%s\n",
                       (unsigned long long)codec_sequence,voice->rack->rack_id,voice->rack->instance,
                       voice->index,codec_prior_command,(unsigned long long)codec_prior_sequence,
                       value.format,value.channels,value.sample_rate,value.codec_config,
                       value.field_18,value.data_size,value.field_20,ngs2_codec_dispatch(&value));
            }
        }
        if (ngs2_audio_diag_enabled()) {
            ++voice->rack->system->diag_waveform_configs;
            if (voice->diag_waveforms_seen) ++voice->rack->system->diag_waveform_reconfigs;
            ++voice->diag_waveforms_seen;
            voice->diag_terminal_reported=0;
        }
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
    Ngs2Voice *voice=ngs2_voice_locked(handle);
    int valid=voice!=NULL;
    if (voice && ngs2_audio_diag_enabled() && voice->internal_state<4)
        ++voice->rack->system->diag_voice_queries[voice->internal_state];
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
