#!/usr/bin/env python3
"""Investigate SoundX / libSceNgs2 static contracts in Knack 2 game.elf."""
import struct
import sys
from pathlib import Path
import iced_x86
from iced_x86 import Decoder, DecoderOptions, Instruction, OpKind, Register

ROOT = Path(__file__).resolve().parent.parent
BOOT_LINKED = ROOT / 'out/knack2/game_elf/boot-linked.bin'

def load_probe_image(path):
    with open(path, 'rb') as f:
        hdr = f.read(56)
        magic, img_size, entry, ns, nr, import_count, flags = struct.unpack('<8s6Q', hdr)
        assert magic.startswith(b'G3PROBE')
        f.read(8 + 32) # procparam (8), main_tls (32)
        module_count, = struct.unpack('<Q', f.read(8))
        # Each module is 7 uint64: base, size, init, tls_address, tls_memsz, tls_filesz, tls_module
        f.seek(module_count * 56, 1)
        nb, = struct.unpack('<Q', f.read(8))
        f.seek(nb * 24, 1)
        segments = []
        for _ in range(ns):
            vaddr, memsz, sflags = struct.unpack('<3Q', f.read(24))
            segments.append((vaddr, memsz, sflags))
        names = []
        for _ in range(import_count):
            n = f.read(128).decode('ascii', errors='ignore').strip('\x00')
            names.append(n)
        relocs = []
        for _ in range(nr):
            target, kind, value, addend = struct.unpack('<4Q', f.read(32))
            relocs.append((target, kind, value, addend))
        image = f.read(img_size)
        return dict(img_size=img_size, entry=entry, segments=segments, names=names, relocs=relocs, image=image)

NAMES_MAP = {
    'u-WrYDaJA3k#libSceNgs2': 'sceNgs2SystemDestroy',
    '-TOuuAQ-buE#libSceNgs2': 'sceNgs2VoiceGetState',
    'MwmHz8pAdAo#libSceNgs2': 'sceNgs2RackGetVoiceHandle',
    'rEh728kXk3w#libSceNgs2': 'sceNgs2VoiceGetStateFlags',
    'U546k6orxQo#libSceNgs2': 'sceNgs2RackCreateWithAllocator',
    'cLV4aiT9JpA#libSceNgs2': 'sceNgs2RackCreate',
    '0eFLVCfWVds#libSceNgs2': 'sceNgs2RackQueryBufferSize',
    'i0VnXM-C9fc#libSceNgs2': 'sceNgs2SystemRender',
    'lCqD7oycmIM#libSceNgs2': 'sceNgs2RackDestroy',
    'mPYgU4oYpuY#libSceNgs2': 'sceNgs2SystemCreateWithAllocator',
    'uu94irFOGpA#libSceNgs2': 'sceNgs2VoiceControl',
}

def main():
    data = load_probe_image(BOOT_LINKED)
    image = data['image']

    ngs2_got = {}
    for r in data['relocs']:
        if r[1] != 0 and r[2] < len(data['names']):
            name = data['names'][r[2]]
            if name in NAMES_MAP:
                fn = NAMES_MAP[name]
                ngs2_got[r[0]] = (name, fn)
                print(f"GOT 0x{r[0]:x} -> {fn} ({name})")

    # Find executable segments
    exec_segs = [s for s in data['segments'] if s[2] & 1]
    print(f"\nExecutable segments ({len(exec_segs)}):")
    for s in exec_segs:
        print(f"  vaddr=0x{s[0]:x}, size=0x{s[1]:x}, flags={s[2]}")

    # Use iced_x86 to disassemble executable segments and find instructions accessing GOT addresses
    # Memory operand with RIP relative: op_kind == OpKind.MEMORY, mem_base_reg == Register.RIP
    # displacement gives target RIP address: instr.ip + instr.len + instr.mem_displacement (or instr.near_branch_target)

    # We can inspect instructions in the first executable segment (code segment of game.elf)
    code_seg = exec_segs[0]
    base_vaddr = code_seg[0]
    code_bytes = image[base_vaddr : base_vaddr + code_seg[1]]

    decoder = Decoder(64, code_bytes, ip=base_vaddr, options=DecoderOptions.NONE)

    got_targets = set(ngs2_got.keys())
    callers = {addr: [] for addr in got_targets}
    plt_entries = {}

    print("\nScanning executable code for GOT references...")
    count = 0
    for instr in decoder:
        count += 1
        # Check memory references
        target_mem = None
        if instr.op0_kind == OpKind.MEMORY and instr.memory_base == Register.RIP:
            target_mem = instr.memory_displacement
        elif instr.op1_kind == OpKind.MEMORY and instr.memory_base == Register.RIP:
            target_mem = instr.memory_displacement

        if target_mem in got_targets:
            fn = ngs2_got[target_mem][1]
            callers[target_mem].append((instr.ip, str(instr)))
            # Check if this is a PLT jump: jmp [rip + disp]
            if instr.mnemonic == iced_x86.Mnemonic.JMP:
                plt_entries[instr.ip] = fn

    print(f"Decoded {count} instructions.")
    print("\nGOT Reference Results:")
    for got_addr, refs in callers.items():
        fn = ngs2_got[got_addr][1]
        print(f"\n=== {fn} (GOT 0x{got_addr:x}) - {len(refs)} references ===")
        for ip, dis in refs:
            print(f"  0x{ip:x}: {dis}")

    print("\nPLT Entries found:")
    for plt_ip, fn in plt_entries.items():
        print(f"  0x{plt_ip:x} -> {fn}")

    # Also scan for calls to the PLT entries!
    if plt_entries:
        plt_addrs = set(plt_entries.keys())
        plt_callers = {addr: [] for addr in plt_addrs}
        decoder2 = Decoder(64, code_bytes, ip=base_vaddr, options=DecoderOptions.NONE)
        for instr in decoder2:
            if instr.mnemonic == iced_x86.Mnemonic.CALL:
                tgt = None
                if instr.op0_kind in (OpKind.NEAR_BRANCH16, OpKind.NEAR_BRANCH32, OpKind.NEAR_BRANCH64):
                    tgt = instr.near_branch_target
                if tgt in plt_addrs:
                    plt_callers[tgt].append((instr.ip, str(instr)))

        print("\nCalls to PLT entries:")
        for plt_addr, calls in plt_callers.items():
            fn = plt_entries[plt_addr]
            print(f"\nCalls to {fn} (via PLT 0x{plt_addr:x}) - {len(calls)} calls:")
            for ip, dis in calls:
                print(f"  0x{ip:x}: {dis}")

if __name__ == '__main__':
    sys.exit(main())
