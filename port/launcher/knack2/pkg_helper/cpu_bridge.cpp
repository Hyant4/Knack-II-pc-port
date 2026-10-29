// SPDX-License-Identifier: GPL-2.0-or-later
// Crypto++ 8.9 expects these MS-compatible symbols when built with Windows Clang.
#include <intrin.h>

extern "C" unsigned long long CPUID64(unsigned int leaf, unsigned int subleaf, unsigned int* out) {
    __cpuidex(reinterpret_cast<int*>(out), leaf, subleaf);
    return 0;
}

extern "C" unsigned long long XGETBV64(unsigned int index) {
    return _xgetbv(index);
}
