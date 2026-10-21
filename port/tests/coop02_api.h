/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Test-only ABI access through the real runtime_services_resolve and NID table. */
#ifndef COOP02_API_H
#define COOP02_API_H
#include "../src/runtime.h"
#include <assert.h>
typedef struct {
    int32_t (ABI *initialize)(const void *);
    int32_t (ABI *initialize2)(int32_t);
    int32_t (ABI *terminate)(void);
    int32_t (ABI *initial)(int32_t *);
    int32_t (ABI *list)(int32_t *);
    int32_t (ABI *name)(int32_t,char *,uint64_t);
    int32_t (ABI *event)(int32_t *);
} Coop02Users;
static Coop02Users coop02_users(void) {
    Coop02Users u;
    u.initialize=(void *)runtime_services_resolve("j3YMu1MVNNo#libSceUserService");
    u.initialize2=(void *)runtime_services_resolve("az-0R6eviZ0#libSceUserService");
    u.terminate=(void *)runtime_services_resolve("bwFjS+bX9mA#libSceUserService");
    u.initial=(void *)runtime_services_resolve("CdWp0oHWGr0#libSceUserService");
    u.list=(void *)runtime_services_resolve("fPhymKNvK-A#libSceUserService");
    u.name=(void *)runtime_services_resolve("1xxcMiGu2fo#libSceUserService");
    u.event=(void *)runtime_services_resolve("yH17Q6NWtVg#libSceUserService");
    assert(u.initialize && u.initialize2 && u.terminate && u.initial && u.list && u.name && u.event);
    return u;
}
#define COOP02_INVALID ((int32_t)0x80960005)
#define COOP02_NO_EVENT ((int32_t)0x80960007)
#define COOP02_SHORT ((int32_t)0x8096000a)
#endif
