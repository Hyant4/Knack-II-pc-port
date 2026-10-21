/* Synthetic UserService provider for standalone pad tests. Never linked into Core. */
#include "../src/runtime.h"
#include <string.h>

int32_t pad_test_initial=1;
int32_t pad_test_users[4]={1,-1,-1,-1};
int pad_test_service_error;
static ABI int32_t get_initial(int32_t *out) {
    if (pad_test_service_error) return -1;
    *out=pad_test_initial;
    return 0;
}
static ABI int32_t get_list(int32_t *out) {
    if (pad_test_service_error) return -1;
    memcpy(out,pad_test_users,sizeof(pad_test_users));
    return 0;
}
uintptr_t runtime_services_resolve(const char *name) {
    if (!strcmp(name,"CdWp0oHWGr0#libSceUserService")) return (uintptr_t)get_initial;
    if (!strcmp(name,"fPhymKNvK-A#libSceUserService")) return (uintptr_t)get_list;
    return 0;
}
