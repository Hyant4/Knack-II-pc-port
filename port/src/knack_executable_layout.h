/* SPDX-License-Identifier: GPL-2.0-or-later
 * Code identity for exact-hash validated Knack executables. Never change title/save identity. */
#ifndef KNACK_EXECUTABLE_LAYOUT_H
#define KNACK_EXECUTABLE_LAYOUT_H
#include <string.h>

static inline int knack_code_profile(const char *serial, const char *version, const char *layout,
                                    const char **code_serial, const char **code_version) {
    if (!serial || !version || !code_serial || !code_version) return 0;
    const int us = !strcmp(serial,"CUSA08014");
    const int us100 = us && !strcmp(version,"01.00");
    const int us101 = us && !strcmp(version,"01.01");
    const int eu101 = !strcmp(serial,"CUSA07670") && !strcmp(version,"01.01");
    if (!us100 && !us101 && !eu101) return 0;
    if (!layout) layout = eu101 ? "elf-0b7843d3" : "elf-80ccc228";
    if (!strcmp(layout,"elf-80ccc228") && (us100 || us101)) {
        *code_serial="CUSA08014";
        *code_version=us100 ? "01.00" : "01.01";
        return 1;
    }
    if (!strcmp(layout,"elf-0b7843d3") && (us101 || eu101)) {
        *code_serial="CUSA07670";
        *code_version="01.01";
        return 1;
    }
    return 0;
}
#endif
