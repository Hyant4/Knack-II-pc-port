import struct

with open('out/knack2/game_elf/boot-linked.bin', 'rb') as f:
    f.seek(8)
    def r64(): return struct.unpack('<Q', f.read(8))[0]
    size, entry, ns, nr, import_count, caps = [r64() for _ in range(6)]
    procparam = r64()
    main_tls = [r64() for _ in range(4)]
    module_count = r64()
    for _ in range(module_count): [r64() for _ in range(7)]
    nb = r64()
    for _ in range(nb): r64(); r64(); r64()
    segments = [(r64(), r64(), r64()) for _ in range(ns)]
    names = [f.read(128).split(b'\0')[0].decode('latin1') for _ in range(import_count)]

import_map = {}
for file in ['port/src/import_names.inc', 'port/src/knack_import_names.inc']:
    for line in open(file):
        line = line.strip()
        if line.startswith('{') and line.endswith('},'):
            parts = [p.strip(' "\t') for p in line[1:-2].split(',')]
            if len(parts) == 2:
                import_map[parts[0]] = parts[1]

font_exports = set()
for line in open('port/src/runtime_gr2_font.c'):
    line = line.strip()
    if line.startswith('{"sceFont') and ',' in line:
        sym = line.split('"')[1]
        font_exports.add(sym)

font_imports = [(i, n) for i, n in enumerate(names) if 'libSceFont' in n]
for idx, nid in font_imports:
    sym = import_map.get(nid, 'UNKNOWN')
    resolved = 'RESOLVED' if sym in font_exports else 'MISSING'
    print(f'[{idx:03d}] {nid} -> {sym} : {resolved}')
