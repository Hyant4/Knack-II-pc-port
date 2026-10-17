// SPDX-License-Identifier: GPL-2.0-or-later
#include <cassert>
#include "video_core/texture_cache/buffer_clear.h"

int main() {
    using VideoCore::BufferClears;
    BufferClears clears;
    const BufferClears::Pattern opaque{0, 0x3c000000, 0, 0x3c000000};
    const BufferClears::Pattern nonuniform{0, 0x3c000000, 0, 0};
    assert(BufferClears::Repeats64(opaque));
    assert(!BufferClears::Repeats64(nonuniform));

    // A clear recorded before the first image exists survives until the exact image bind.
    clears.Record(0x1000, 0x1000, opaque);
    assert(!clears.Take(0x1000, 0x800)); // Partial surface cannot prove a uniform full clear.
    assert(!clears.Take(0x1800, 0x1000));
    assert(clears.Take(0x1000, 0x1000) == opaque);
    assert(!clears.Take(0x1000, 0x1000)); // Consumed: never re-clear after drawing.

    // CPU/GPU writes, including an overlap starting before the surface, cancel stale proof.
    clears.Record(0x1000, 0x1000, opaque);
    clears.Forget(0x1800, 4);
    assert(!clears.Take(0x1000, 0x1000));
    clears.Record(0x1000, 0x1000, opaque);
    clears.Forget(0xffc, 8);
    assert(!clears.Take(0x1000, 0x1000));

    clears.Record(0x1000, 0x1000, opaque);
    clears.Record(0x3000, 0x1000, opaque);
    clears.Forget(0x2000, 0x1000); // Adjacent ranges are untouched.
    clears.Forget(0x1000, 0);
    assert(clears.Take(0x1000, 0x1000) == opaque);
    assert(clears.Take(0x3000, 0x1000) == opaque);

    clears.Record(0x1000, 0x1000, opaque);
    clears.Record(0x1000, 0x1000, {}); // Latest dispatch wins.
    assert(clears.Take(0x1000, 0x1000) == BufferClears::Pattern{});
}
