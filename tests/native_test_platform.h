/* Host facilities for synthetic tests only; no guest ABI or runtime changes. */
#ifndef KNACK_NATIVE_TEST_PLATFORM_H
#define KNACK_NATIVE_TEST_PLATFORM_H
#ifndef _WIN32
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#endif
#include <stddef.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <errno.h>
#include <sys/mman.h>
#include <sys/uio.h>
#include <time.h>
#include <unistd.h>

static inline void native_test_sleep(unsigned long milliseconds) {
    struct timespec remaining = {(time_t)(milliseconds / 1000),
                                 (long)(milliseconds % 1000) * 1000000L};
    while (nanosleep(&remaining, &remaining) && errno == EINTR) {}
}
#define Sleep native_test_sleep
#endif

static inline int native_test_read_memory(void *dst, const void *address, size_t bytes) {
#ifdef _WIN32
    SIZE_T got = 0;
    return ReadProcessMemory(GetCurrentProcess(), address, dst, bytes, &got) && got == bytes;
#else
    const struct iovec local = {dst, bytes}, remote = {(void *)address, bytes};
    return process_vm_readv(getpid(), &local, 1, &remote, 1, 0) == (ssize_t)bytes;
#endif
}

static inline void *native_test_allocate_code(size_t bytes) {
#ifdef _WIN32
    return VirtualAlloc(NULL, bytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
#else
    void *memory = mmap(NULL, bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    return memory == MAP_FAILED ? NULL : memory;
#endif
}

static inline int native_test_executable(void *memory, size_t bytes) {
#ifdef _WIN32
    DWORD previous = 0;
    return VirtualProtect(memory, bytes, PAGE_EXECUTE_READ, &previous) &&
           FlushInstructionCache(GetCurrentProcess(), memory, bytes);
#else
    if (mprotect(memory, bytes, PROT_READ | PROT_EXEC)) return 0;
    __builtin___clear_cache((char *)memory, (char *)memory + bytes);
    return 1;
#endif
}

static inline int native_test_release_code(void *memory, size_t bytes) {
#ifdef _WIN32
    (void)bytes;
    return VirtualFree(memory, 0, MEM_RELEASE) != 0;
#else
    return munmap(memory, bytes) == 0;
#endif
}
#endif
