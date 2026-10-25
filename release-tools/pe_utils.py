import ctypes
from ctypes import wintypes
import hashlib
import struct

def pe(data):
    off = struct.unpack_from('<I', data, 0x3c)[0]
    assert data[off:off+4] == b'PE\0\0'
    count, optional_size = struct.unpack_from('<H12xH', data, off+6)
    optional = off+24
    assert struct.unpack_from('<H', data, optional)[0] == 0x20b
    table = optional+optional_size
    sections = {}
    for i in range(count):
        row = table+40*i
        name = data[row:row+8].rstrip(b'\0').decode()
        virtual_size, address, size, raw = struct.unpack_from('<IIII', data, row+8)
        sections[name] = {'address': address, 'virtual_size': virtual_size, 'raw': raw, 'size': size}
    def rva(address):
        for section in sections.values():
            if section['address'] <= address < section['address']+max(section['size'], section['virtual_size']):
                return section['raw']+address-section['address']
        raise ValueError(f'Unmapped RVA {address:x}')
    def directory(index):
        return struct.unpack_from('<II', data, optional+112+8*index)
    def string(offset):
        return data[offset:data.index(b'\0', offset)].decode()
    imports = []
    address, _ = directory(1)
    if address:
        cursor = rva(address)
        while any(data[cursor:cursor+20]):
            imports.append(string(rva(struct.unpack_from('<I', data, cursor+12)[0])))
            cursor += 20
    end = max(s['raw']+s['size'] for s in sections.values())
    resources = []
    address, _ = directory(2)
    if address:
        base = rva(address)
        def visit(relative, path):
            named, ids = struct.unpack_from('<HH', data, base+relative+12)
            for i in range(named+ids):
                name, child = struct.unpack_from('<II', data, base+relative+16+8*i)
                if name & 0x80000000:
                    pos = base+(name & 0x7fffffff)
                    length = struct.unpack_from('<H', data, pos)[0]
                    name = data[pos+2:pos+2+length*2].decode('utf-16-le')
                if child & 0x80000000:
                    visit(child & 0x7fffffff, path+[name])
                else:
                    resources.append(path+[name])
        visit(0, [])
    return {'sections': sections, 'imports': imports, 'resources': resources,
            'overlay': data[end:], 'entrypoint': struct.unpack_from('<I', data, optional+16)[0],
            'text_sha256': hashlib.sha256(data[sections['.text']['raw']:sections['.text']['raw']+sections['.text']['size']]).hexdigest()}

def remove_icons(path):
    original = path.read_bytes()
    before = pe(original)
    icons = [p for p in before['resources'] if p[0] in (3, 14)]
    if not icons:
        return {'removed': 0, 'text_sha256': before['text_sha256']}
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel.BeginUpdateResourceW.argtypes = [wintypes.LPCWSTR, wintypes.BOOL]
    kernel.BeginUpdateResourceW.restype = wintypes.HANDLE
    kernel.UpdateResourceW.argtypes = [wintypes.HANDLE, ctypes.c_void_p, ctypes.c_void_p, wintypes.WORD, ctypes.c_void_p, wintypes.DWORD]
    kernel.UpdateResourceW.restype = wintypes.BOOL
    kernel.EndUpdateResourceW.argtypes = [wintypes.HANDLE, wintypes.BOOL]
    kernel.EndUpdateResourceW.restype = wintypes.BOOL
    handle = kernel.BeginUpdateResourceW(str(path), False)
    if not handle:
        raise ctypes.WinError(ctypes.get_last_error())
    try:
        for kind, name, language in icons:
            assert isinstance(kind, int) and isinstance(name, int) and isinstance(language, int)
            if not kernel.UpdateResourceW(handle, kind, name, language, None, 0):
                raise ctypes.WinError(ctypes.get_last_error())
    except BaseException:
        kernel.EndUpdateResourceW(handle, True)
        raise
    if not kernel.EndUpdateResourceW(handle, False):
        raise ctypes.WinError(ctypes.get_last_error())
    changed = path.read_bytes()
    after = pe(changed)
    if before['overlay'] and after['overlay'] != before['overlay']:
        end = max(s['raw']+s['size'] for s in after['sections'].values())
        changed = changed[:end]+before['overlay']
        path.write_bytes(changed)
        after = pe(changed)
    assert after['text_sha256'] == before['text_sha256']
    assert after['imports'] == before['imports']
    assert after['entrypoint'] == before['entrypoint']
    assert after['overlay'] == before['overlay']
    assert not any(p[0] in (3, 14) for p in after['resources'])
    return {'removed': len(icons), 'text_sha256': after['text_sha256'], 'pyinstaller_overlay_preserved': bool(after['overlay'])}
