/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "runtime.h"
#include <string.h>

int main(int argc, char **argv) {
    if (argc < 2) return 2;
    typedef int32_t (ABI *LoadExec)(const char *, char *const *);
    LoadExec call = (LoadExec)runtime_knack_resolve("JoBqSQt1yyA#libSceSystemService");
    if (!call) return 3;
    if (runtime_knack_resolve("not-an-import")) return 4;
    int32_t result = call(argv[1], argc > 2 ? argv + 2 : NULL);
    return result == (int32_t)0x80020016 ? 0 : 5;
}
