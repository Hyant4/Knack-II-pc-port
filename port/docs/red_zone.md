# Windows red-zone protection

Fixes the Windows-only crash `Fault: write access to 0x92 at guest offset 0x1cfaaf` (iss_pc #29, #41)
and the `read 0 at 0x1cf8f9` variant.

## The bug

- PS4 code is FreeBSD/clang code (SysV ABI). A leaf function may keep locals in the 128 bytes below
  `rsp` (the red zone) without moving `rsp`. Example: 0x1cf4e0, the text-quad writer, keeps its
  2nd argument at `[rsp-20h]`. It reads it back at 0x1cf9ac, then runs `add word [rsi+92h],4` at
  0x1cfaaf.
- The port write-protects guest pages (GPU write tracking) and read-protects them (readbacks), so
  guest threads fault thousands of times per second.
- Windows has no red zone. To dispatch an exception, the kernel writes CONTEXT, EXCEPTION_RECORD and
  the machine frame right below the faulting `rsp`, and the vectored handler (`vectored_fault`) runs
  on that same stack. The red zone gets overwritten.
- Linux skips 128 bytes before the signal frame, so Linux is fine.
- **Reproduced on Linux** with `TLG_CLOBBER_RED_ZONE=1`: after every fault it handles, the handler
  zeroes `[rsp-128, rsp)`. A 150 s run then dies at 0x1cfaaf, address 0x92.

## The fix (`scripts/red_zone.py`, `src/isa_rewrite.c`)

We follow shadPS4's "Windows static guest red-zone protection" design (upstream commit 15abf68,
`src/core/cpu_patches.cpp`, PRs #4802 and #5216). The code is ours: Python on iced-x86, plus the
existing G3ISA1 table and loader.

**The idea:** we do not move the guest's data. At every instruction that can fault while red-zone
data is live, we move `rsp` down by 128 bytes:

    site:  jmp stub            ; the original bytes, nop-filled
    stub:  lea rsp, [rsp-128]  ; lea does not change the flags
           <original instruction, re-encoded for the stub address>
           lea rsp, [rsp+128]
           jmp back

When that instruction faults, Windows writes its frame below `rsp-128`, that is, below the guest's red
zone. It does not matter how deep the handler goes, so no displacement D has to be chosen and no
handler stack switch is needed. The stack needs 128 more bytes, and Windows already uses more than
1.5 KiB there for any exception (guest thread stacks are fully committed, `runtime_thread.c`).

**Why not "move every `[rsp-k]` D bytes deeper"**, the plan we started with:
- every access to a slot would have to move together, and one skipped site would corrupt data on
  every run;
- red-zone pointers escape (`lea rax,[rsp-20h]`), and `[rsp+rcx*4-8]` has no known address;
- D would have to cover the exception frame (bigger with AVX state) plus the handler's whole stack
  (Vulkan calls).

With our scheme, a skipped site only matters if that exact instruction faults while the red zone is
live.

### Analysis (`isa_rewrite.py --red-zone`)

1. **Function bounds** come from the `.eh_frame_hdr` search tables found in the linked image: the
   eboot, libc and libSceFios2, 45,655 functions. Each FDE gives `[pc_begin, pc_begin+pc_range)`.
2. **Filter:** only functions with an explicit `[rsp - k]` operand use the red zone (196 here).
3. **Decoding** follows the control flow from the function start. Bounded PIC jump tables
   (`cmp/ja`, `lea base,[rip+t]`, `movsxd`, `add`, `jmp reg`) are resolved. A load between the `cmp`
   and the `ja` is accepted; upstream required them to be adjacent. The function becomes
   **conservative** (red zone live everywhere) when:
   - an indirect jump is not resolved;
   - a branch lands inside an instruction;
   - `lea reg,[rsp-k]` lets a pointer escape;
   - an index register is based on `rsp`.
4. **Liveness:** a backward pass, per byte, over `[rsp-128, rsp+4096)`. `rsp` is tracked through
   push, pop, `add/sub rsp,imm` and `lea rsp,[rsp+d]`. An unknown `rsp` write makes everything live
   before it, but only when something is live after it. Two differences from upstream:
   - the window above `rsp` is also tracked, so a value stored in the red zone before `sub rsp,n`
     and read back at `[rsp+k]` stays live;
   - the targets of a resolved jump table are successors of its `jmp`.
5. **Sites:** instructions that access memory that is not based on `rsp` (including RIP-relative,
   `rep movs`, VEX/SSE, `call [mem]`), while a red-zone byte is live. fs/gs TLS accesses are
   excluded. Skipped and reported:
   - instructions that also use `rsp` (`push [mem]`, `call [mem]`);
   - other control flow.
6. **Spans:** a site shorter than 5 bytes takes the next instructions with it, or the previous ones.
   A span never:
   - crosses a branch or jump-table target;
   - contains a call (the return address would point into the stub);
   - contains fs/gs instructions (Windows patches the TLS loads);
   - contains bytes of an ISA-emulation site.
   A terminator is allowed only as the last instruction.
7. **Relays**, when no span fits: a 2-byte `jmp short` at the site jumps to a `jmp near`. The
   `jmp near` sits in the nop/int3 padding between functions, in the nops of an earlier patch, or in
   a nearby 10-byte run of instructions that is moved to its own stub (shadPS4's last fallback).
8. **Stub code** is encoded by iced's BlockEncoder, which re-targets RIP-relative operands and
   branches, for the address where the loader maps it: `round_up(image) + round_up(sites*256)`, right
   after the emulation stubs.

### Table

The G3ISA1 header's last field (it used to be padding) gives the size of the section appended after
the records:

- `G3RZONE1`: code offset, code bytes, groups, group bytes, format version, and the CRC-32 of the
  scripts (the cache key);
- 11 statistics;
- the stub code;
- the groups. A group is (site, stub range, protected count, patches); a patch is (offset, length,
  original bytes, new bytes).

`isa_rewrite.py` keeps a table that already matches the image, the options and the scripts. The
analysis takes about 5 s.

### Loader (`probe.c`, `isa_rewrite.c`)

1. The emulation rewrite is applied first.
2. Then `isa_red_zone_apply`:
   - it copies the stub code to `image + code_offset`;
   - it writes each group only if all of its original bytes still match. A game patch, a hook, or an
     ISA site the emulation already rewrote makes the whole group skip.
3. **ISA emulation on the same instruction:** when the instruction is emulated, its group is
   skipped. The emulation stub already runs 256 bytes below `rsp`. When the CPU has the instruction,
   the group applies.
4. A JMP_RELOCATE emulation stub runs its relocated instruction between the same two `lea`s when
   that instruction can fault (record `mem_flags` bit 1).
5. Crash reports name the protected site of a stub: `(red-zone stub of 0x...)`.
6. INT3 emulation sites still trap through the exception handler, so they are not protected. Only
   CPUs without LZCNT have them (3 sites). The probe prints how many.

## When it is on

| | Windows | Linux |
|---|---|---|
| default | on | off |
| `ISS_RED_ZONE_REWRITE=1` (or `TLG_`) | on | on (tests) |
| `ISS_RED_ZONE_REWRITE=0` | off | off |

The launcher (`../run.py`, `run.py`) builds the table with `--red-zone` when protection is
wanted, even on a CPU that needs no emulation.

**Test switch (Linux):** `TLG_CLOBBER_RED_ZONE=<bytes>`.
- After each guest fault the handler resolves, it zeroes that many bytes below the interrupted
  `rsp`, the way Windows does. `1` means 128.
- Above 128 bytes, guest threads get an alternate signal stack (`SA_ONSTACK`): the kernel frame
  starts 128 bytes below `rsp`.
- The first 32 distinct guest RIPs are logged.

## Results (inFAMOUS CUSA00223 1.07, Ryzen 5700X3D, Linux, 150 s runs)

- **Sites:**
  - 6,453 instructions protected, in 5,898 patches (46 through short-jump relays);
  - 196 red-zone functions, 5 of them analysed conservatively;
  - 6,478 candidates, 1 of which is also an ISA site: an `andn`, protected because BMI1 is native;
  - not protected: 0 that use `rsp`, 0 control flow, 25 that cannot be relocated. Those 25 are in
    0x682030 and 0x682430, two copies of one function with an unbounded jump-table dispatch: the
    function stays conservative and its short sites have no safe relay.
- **Runs:**
  - `TLG_CLOBBER_RED_ZONE=1` alone: crash at 0x1cfaaf, address 0x92, after 2 Frame stats.
  - Protection + `TLG_CLOBBER_RED_ZONE=2048`: no fault, 29 Frame stats, the same count as a clean
    run. The quad writer's fault now comes from `red-zone stub of 0x1cf99d`.
  - The same with `ISS_ISA_REWRITE=sse4a` (the Intel path): no fault, 29 Frame stats.
  - Clean runs, back to back, steady state: 16.1 FPS with protection against 16.4 FPS without in one
    pair, 16.3 against 16.3 in another. Today's scene ran at ~16 FPS in every mode, against 30-36 FPS
    in the first runs of the day. The write fault rate was ~1,900/s in both modes.
  - `TLG_FAULT_WINDOW=1024` (a player workaround that does not protect anything): ~1,030 write faults/s
    instead of ~2,000, for ~4% fewer FPS.

## Porting to the other games

- `red_zone.py`, the loader code and the test switch are core code (ps4_core). Nothing in them names
  a game.
- What is game-specific:
  - the counts;
  - whether the image has `.eh_frame_hdr` tables (all PS4 SDK binaries we have seen do);
  - each port's environment prefix (`ISS_`, `GR2_`, ...);
  - the launcher policy (`../run.py`).
