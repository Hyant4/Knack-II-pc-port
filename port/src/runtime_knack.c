/* SPDX-License-Identifier: GPL-2.0-or-later
 * Knack 2's boot executable hands control to /app0/game.elf through LoadExec.
 * The supervisor starts a clean host process after reading this bounded request.
 */
#include "runtime.h"
#include <string.h>
#include <stdatomic.h>
#include <unistd.h>

static int write_string(FILE *f, const char *text) {
    size_t size = strlen(text);
    if (size > 4096) return -1;
    uint32_t n = (uint32_t)size;
    return fwrite(&n, 4, 1, f) != 1 || fwrite(text, 1, n, f) != n ? -1 : 0;
}

static ABI int32_t knack_load_exec(const char *path, char *const *argv) {
    const char *request = getenv("KNACK2_EXEC_REQUEST");
    if (!request || !path || (strcmp(path, "/app0/game.elf") && strcmp(path, "/app0/eboot.bin")))
        return (int32_t)0x80020016; /* EINVAL */
    uint32_t argc = 0;
    if (argv) {
        while (argc < 64 && argv[argc]) {
            if (strlen(argv[argc]) > 4096) return (int32_t)0x80020016;
            ++argc;
        }
        if (argc == 64) return (int32_t)0x80020016;
    }
    FILE *f = fopen(request, "wb");
    if (!f) return (int32_t)0x80020005; /* EIO */
    int failed = fwrite("K2EXEC01", 1, 8, f) != 8 || write_string(f, path) || fwrite(&argc, 4, 1, f) != 1;
    for (uint32_t i = 0; i < argc && !failed; ++i) failed = write_string(f, argv[i]);
    if (fclose(f)) failed = 1;
    if (failed) return (int32_t)0x80020005;
    printf("Knack2: LoadExec %s, %u arguments; transferring to supervisor\n", path, argc);
    fflush(NULL);
    _exit(75);
}

/* This runtime does not install the console AddressSanitizer instrumentation. */
static ABI int32_t knack_asan_enabled(void) { return 0; }

static ABI int32_t knack_fsst_param(uint32_t affinity, int32_t priority) {
    printf("Knack2: sceKernelSetFsstParam(%#x, %d): console filesystem scheduling is unavailable; host I/O scheduler used\n",
           affinity, priority);
    return (int32_t)0x8002004e; /* ENOSYS: there is no console filesystem service thread. */
}

/* Registration contract follows shadPS4 e6c79a5 kernel/coredump (GPL-2.0-or-later).
 * Host crash diagnostics remain authoritative; guest coredump generation is not implemented.
 */
static _Atomic(uintptr_t) coredump_handler;
static ABI int32_t knack_coredump_register(void *handler, uint64_t stack_size, void *common) {
    (void)common;
    if (!handler || stack_size >= 0x20000000) return (int32_t)0x81180000;
    uintptr_t empty = 0;
    if (!atomic_compare_exchange_strong(&coredump_handler, &empty, (uintptr_t)handler))
        return (int32_t)0x81180002;
    puts("Knack2: coredump handler registered; crash reports use host diagnostics");
    return 0;
}
static ABI int32_t knack_coredump_write(const void *data, uint64_t size) {
    (void)data; (void)size;
    return (int32_t)0x81180003; /* No guest coredump handler is active. */
}

/* ABI and bounds verified against shadPS4 e6c79a5, ulobjmgr.cpp
 * (Copyright 2024 shadPS4 Emulator Project, GPL-2.0-or-later).
 * Retain registrations for the lifetime of this process; these are bookkeeping
 * handles, not ownership of guest memory. Errors use positive POSIX errno.
 */
static _Atomic(uintptr_t) user_objects[0x4000];
static ABI int32_t knack_object_register(uint64_t object, int32_t type, uint32_t *handle) {
    if (!object || !type || !handle) return 22;
    for (uint32_t i = 0; i < 0x4000; ++i) {
        uintptr_t empty = 0;
        if (atomic_compare_exchange_strong(&user_objects[i], &empty, (uintptr_t)object)) {
            *handle = i;
            return 0;
        }
    }
    return 12;
}
static ABI int32_t knack_object_unregister(uint32_t handle) {
    if (handle >= 0x4000) return 22;
    atomic_store(&user_objects[handle], 0);
    return 0;
}

/* Profiling annotations are discarded while Razor CPU capture is disabled.
 * These do not dispatch jobs: the original libSceJobManager runs those jobs.
 */
static ABI int32_t knack_razor_annotation(void) { return 0; }

/* CUSA08014/01.01 ATTRIBUTE=0x20800002: CPU mode bits 15 and 16 are clear.
 * Return the console's normal mode, independent of the host core count.
 */
static ABI int32_t knack_cpu_mode(void) { return 0; }

/* Offline compatibility lifecycle only. Enabling permission never initiates a
 * broadcast. ABI reference: shadPS4 e6c79a5 gamelivestreaming.cpp (GPL-2.0-or-later).
 * Actual broadcasting is deliberately unsupported in this milestone.
 */
static _Atomic(int) streaming_initialized, streaming_permitted;
static ABI int32_t knack_stream_init(void) {
    atomic_store(&streaming_initialized, 1);
    puts("Knack2: GameLiveStreaming initialized in offline compatibility mode; broadcasting unavailable");
    return 0;
}
static ABI int32_t knack_stream_enable(int32_t enable) {
    atomic_store(&streaming_permitted, enable != 0);
    return 0;
}
static ABI int32_t knack_stream_term(void) {
    atomic_store(&streaming_initialized, 0);
    atomic_store(&streaming_permitted, 0);
    return 0;
}
static _Atomic(int) share_initialized, share_prohibited;
static ABI int32_t knack_share_init(void) {
    atomic_store(&share_initialized, 1);
    puts("Knack2: SharePlay initialized in offline compatibility mode; no remote session");
    return 0;
}
static ABI int32_t knack_share_prohibit(int32_t prohibit) {
    atomic_store(&share_prohibited, prohibit != 0);
    return 0;
}
static ABI int32_t knack_share_term(void) {
    atomic_store(&share_initialized, 0);
    return 0;
}

/* Optional console USB keyboard. Host keyboard input remains mapped to Pad.
 * Layout/errors: shadPS4 e6c79a5 ime_common.h/ime.cpp, GPL-2.0-or-later.
 */
typedef struct { uint32_t option, reserved1; void *arg, *handler; uint64_t reserved2; } KnackKeyboardParam;
_Static_assert(sizeof(KnackKeyboardParam)==32,"IME keyboard parameter ABI");
static _Atomic(int32_t) keyboard_user = -1;
#define IME_ERROR(code) ((int32_t)(0x80bc0000u | (code)))
static ABI int32_t knack_keyboard_open(int32_t user,const KnackKeyboardParam *param) {
    if (!param) return IME_ERROR(0x31);
    if (!param->handler) return IME_ERROR(0x22);
    if (user<0) return IME_ERROR(0x10);
    if (param->option & ~63u) return IME_ERROR(0x15);
    if (param->reserved1 || param->reserved2) return IME_ERROR(0x32);
    int32_t empty=-1;
    if (!atomic_compare_exchange_strong(&keyboard_user,&empty,user)) return IME_ERROR(1);
    puts("Knack2: IME opened; no console USB keyboard, host keyboard uses Pad mapping");
    return 0;
}
static ABI int32_t knack_keyboard_resources(int32_t user,int32_t *resources) {
    if (!resources) return IME_ERROR(0x31);
    if (user<0) return IME_ERROR(0x10);
    memset(resources,0,24);
    resources[0]=user;
    return atomic_load(&keyboard_user)==user ? IME_ERROR(4) : IME_ERROR(2);
}
static ABI int32_t knack_keyboard_close(int32_t user) {
    if (user<0) return IME_ERROR(0x10);
    int32_t expected=user;
    return atomic_compare_exchange_strong(&keyboard_user,&expected,-1) ? 0 : IME_ERROR(2);
}
static ABI int32_t knack_ime_update(void *handler) {
    (void)handler;
    return atomic_load(&keyboard_user)>=0 ? 0 : IME_ERROR(2); /* No device events to dispatch. */
}

uintptr_t runtime_knack_resolve(const char *name) {
    if (!getenv("KNACK2_PROFILE")) return 0;
    if (!strcmp(name, "JoBqSQt1yyA#libSceSystemService")) return (uintptr_t)knack_load_exec;
    if (!strcmp(name, "jh+8XiK4LeE#libkernel")) return (uintptr_t)knack_asan_enabled;
    if (!strcmp(name, "VjBtg5Btl94#libkernel")) return (uintptr_t)knack_fsst_param;
    if (!strcmp(name, "8zLSfEfW5AU#libSceCoredump")) return (uintptr_t)knack_coredump_register;
    if (!strcmp(name, "Dbbkj6YHWdo#libSceCoredump")) return (uintptr_t)knack_coredump_write;
    if (!strcmp(name, "BG26hBGiNlw#ulobjmgr")) return (uintptr_t)knack_object_register;
    if (!strcmp(name, "Smf+fUNblPc#ulobjmgr")) return (uintptr_t)knack_object_unregister;
    if (!strcmp(name, "VOx8NGmHXTs#libkernel")) return (uintptr_t)knack_cpu_mode;
    if (!strcmp(name, "kvYEw2lBndk#libSceGameLiveStreaming")) return (uintptr_t)knack_stream_init;
    if (!strcmp(name, "dWM80AX39o4#libSceGameLiveStreaming")) return (uintptr_t)knack_stream_enable;
    if (!strcmp(name, "9yK6Fk8mKOQ#libSceGameLiveStreaming")) return (uintptr_t)knack_stream_term;
    if (!strcmp(name, "isruqthpYcw#libSceSharePlay")) return (uintptr_t)knack_share_init;
    if (!strcmp(name, "co2NCj--pnc#libSceSharePlay")) return (uintptr_t)knack_share_prohibit;
    if (!strcmp(name, "UaLjloJinow#libSceSharePlay")) return (uintptr_t)knack_share_term;
    if (!strcmp(name, "eaFXjfJv3xs#libSceIme")) return (uintptr_t)knack_keyboard_open;
    if (!strcmp(name, "dKadqZFgKKQ#libSceIme")) return (uintptr_t)knack_keyboard_resources;
    if (!strcmp(name, "PMVehSlfZ94#libSceIme")) return (uintptr_t)knack_keyboard_close;
    if (!strcmp(name, "-4GCfYdNF1s#libSceIme")) return (uintptr_t)knack_ime_update;
    if (!strcmp(name, "dnEdyY4+klQ#libSceRazorCpu") ||
        !strcmp(name, "KP+TBWGHlgs#libSceRazorCpu") ||
        !strcmp(name, "9FowWFMEIM8#libSceRazorCpu")) return (uintptr_t)knack_razor_annotation;
    return 0;
}
