# SPDX-License-Identifier: GPL-2.0-or-later
"""Convert a local original game icon and brand a NEW Windows runtime copy.

Never executes the runtime, overwrites an existing output, or changes source code.
All non-resource PE sections and loader contracts must remain identical.
"""
import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import uuid
import xml.etree.ElementTree as ET

VERSION_STRINGS = {
    'CompanyName': 'Knack II PC Port contributors',
    'FileDescription': 'Knack II PC Port Runtime',
    'FileVersion': '0.0.0.0',
    'InternalName': 'knack2-runtime',
    'OriginalFilename': 'knack2-runtime.exe',
    'ProductName': 'Knack II PC Port',
    'ProductVersion': 'Local experimental build',
    'LegalCopyright': 'Upstream authors credited in README.md and LICENSE files.',
}


def digest(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def pe_contract(path):
    data = Path(path).read_bytes()
    if data[:2] != b'MZ' or len(data) < 64:
        raise ValueError('Expected a Windows PE runtime')
    pe, = struct.unpack_from('<I', data, 60)
    if data[pe:pe+4] != b'PE\0\0':
        raise ValueError('Invalid PE signature')
    machine, count = struct.unpack_from('<HH', data, pe+4)
    optional_size, characteristics = struct.unpack_from('<HH', data, pe+20)
    optional = pe+24
    if machine != 0x8664 or struct.unpack_from('<H', data, optional)[0] != 0x20b:
        raise ValueError('Expected AMD64 PE32+')
    if struct.unpack_from('<II', data, optional+112+4*8) != (0, 0):
        raise ValueError('Signed binaries require a separate signing workflow')
    sections = {}
    for index in range(count):
        pos = optional+optional_size+index*40
        name = data[pos:pos+8].rstrip(b'\0').decode('ascii')
        virtual_size, address, raw_size, offset = struct.unpack_from('<IIII', data, pos+8)
        if offset+raw_size > len(data):
            raise ValueError('PE section exceeds the file')
        if name != '.rsrc':
            sections[name] = {'address': address, 'virtual_size': virtual_size,
                              'raw_size': raw_size, 'flags': struct.unpack_from('<I', data, pos+36)[0],
                              'sha256': hashlib.sha256(data[offset:offset+raw_size]).hexdigest()}
    # Exclude only the resource directory and sizes/checksum derived from it.
    directories = [list(struct.unpack_from('<II', data, optional+112+i*8))
                   for i in range(16) if i != 2]
    symbol_offset, symbol_count = struct.unpack_from('<II', data, pe+12)
    symbol_data = b''
    if symbol_offset:
        strings = symbol_offset+symbol_count*18
        if strings+4 > len(data):
            raise ValueError('COFF symbols exceed the file')
        string_size, = struct.unpack_from('<I', data, strings)
        if string_size < 4 or strings+string_size > len(data):
            raise ValueError('COFF string table exceeds the file')
        symbol_data = data[symbol_offset:strings+string_size]
    return {'machine': machine, 'characteristics': characteristics,
            'entrypoint': struct.unpack_from('<I', data, optional+16)[0],
            'image_base': struct.unpack_from('<Q', data, optional+24)[0],
            'subsystem_and_dll_flags': list(struct.unpack_from('<HH', data, optional+68)),
            'directories_except_resources': directories, 'sections_except_resources': sections,
            'coff_symbol_count': symbol_count,
            'coff_symbols_and_strings_sha256': hashlib.sha256(symbol_data).hexdigest()}


def _block(key, value=b'', children=(), kind=1):
    def align(value):
        return value+b'\0'*((-len(value)) % 4)
    text = key.encode('utf-16-le')+b'\0\0'
    result = align(b'\0'*6+text)+value
    for child in children:
        result = align(result)+child
    length = len(value)//2 if kind else len(value)
    return struct.pack('<HHH', len(result), length, kind)+result[6:]


def version_resource():
    fixed = struct.pack('<13I', 0xfeef04bd, 0x10000, 0, 0, 0, 0, 0x3f, 2, 0x40004, 1, 0, 0, 0)
    strings = [_block(k, v.encode('utf-16-le')+b'\0\0') for k, v in VERSION_STRINGS.items()]
    return _block('VS_VERSION_INFO', fixed, [
        _block('StringFileInfo', children=[_block('040904B0', children=strings)]),
        _block('VarFileInfo', children=[_block('Translation', struct.pack('<HH', 0x409, 1200), kind=0)])
    ], kind=0)


def icon_resources(ico):
    data = Path(ico).read_bytes()
    reserved, kind, count = struct.unpack_from('<HHH', data)
    if reserved or kind != 1 or not 1 <= count <= 32:
        raise ValueError('Invalid icon directory')
    group, images = bytearray(struct.pack('<HHH', 0, 1, count)), {}
    for index in range(count):
        width, height, colors, zero, planes, bits, size, offset = struct.unpack_from('<BBBBHHII', data, 6+16*index)
        payload = data[offset:offset+size]
        if len(payload) != size or not payload.startswith(b'\x89PNG\r\n\x1a\n'):
            raise ValueError('Expected complete PNG icon frames')
        images[index+1] = payload
        group += struct.pack('<BBBBHHIH', width, height, colors, zero, planes, bits, size, index+1)
    return bytes(group), images


def convert_original_icon(source, directory):
    os.environ.setdefault('QT_QPA_PLATFORM', 'offscreen')
    from PySide6.QtCore import QByteArray, QBuffer, QIODevice, Qt
    from PySide6.QtGui import QGuiApplication, QImage
    app = QGuiApplication.instance() or QGuiApplication([])
    original = QImage(str(source))
    if original.isNull() or original.width() != original.height():
        raise ValueError('Expected a square original game icon')
    target = Path(directory)
    # Keep the icon within the existing PE resource span; never move executable RVAs.
    sizes = [16, 24, 32, 48, 64, 128]
    frames = []
    for size in sizes:
        image = original.scaled(size, size, Qt.IgnoreAspectRatio, Qt.SmoothTransformation)
        array = QByteArray()
        buffer = QBuffer(array)
        buffer.open(QIODevice.WriteOnly)
        if not image.save(buffer, 'PNG'):
            raise OSError('Cannot encode icon frame')
        frames.append(bytes(array))
    offset = 6+16*len(frames)
    header = bytearray(struct.pack('<HHH', 0, 1, len(frames)))
    for size, frame in zip(sizes, frames):
        header += struct.pack('<BBBBHHII', size % 256, size % 256, 0, 0, 1, 32, len(frame), offset)
        offset += len(frame)
    target.mkdir(parents=True, exist_ok=True)
    (target/'Knack.ico').write_bytes(header+b''.join(frames))
    bitmap = original.scaled(256, 256, Qt.IgnoreAspectRatio, Qt.SmoothTransformation)
    if not bitmap.convertToFormat(QImage.Format_RGB32).save(str(target/'Knack.bmp'), 'BMP'):
        raise OSError('Cannot encode SDL window icon')


def _api():
    if os.name != 'nt':
        raise OSError('Windows resource APIs required')
    from ctypes import wintypes as w
    k = ctypes.WinDLL('kernel32', use_last_error=True)
    for name, args, result in [
        ('LoadLibraryExW', [w.LPCWSTR, w.HANDLE, w.DWORD], w.HMODULE),
        ('FreeLibrary', [w.HMODULE], w.BOOL),
        ('FindResourceExW', [w.HMODULE, ctypes.c_void_p, ctypes.c_void_p, w.WORD], w.HANDLE),
        ('LoadResource', [w.HMODULE, w.HANDLE], w.HANDLE),
        ('LockResource', [w.HANDLE], ctypes.c_void_p),
        ('SizeofResource', [w.HMODULE, w.HANDLE], w.DWORD),
        ('BeginUpdateResourceW', [w.LPCWSTR, w.BOOL], w.HANDLE),
        ('UpdateResourceW', [w.HANDLE, ctypes.c_void_p, ctypes.c_void_p, w.WORD, ctypes.c_void_p, w.DWORD], w.BOOL),
        ('EndUpdateResourceW', [w.HANDLE, w.BOOL], w.BOOL),
    ]:
        fn = getattr(k, name); fn.argtypes = args; fn.restype = result
    return k, w


def _resource_arg(value):
    return value if isinstance(value, int) else ctypes.cast(ctypes.c_wchar_p(value), ctypes.c_void_p)


def read_resources(path):
    k, w = _api()
    # Data mapping only: no entrypoint, DLL initializers or dependencies are executed.
    handle = k.LoadLibraryExW(str(Path(path).resolve()), None, 0x40 | 0x20)
    if not handle:
        raise ctypes.WinError(ctypes.get_last_error())
    resources, errors = {}, []
    def identifier(pointer):
        return pointer if pointer <= 0xffff else ctypes.wstring_at(pointer)
    types_cb = ctypes.WINFUNCTYPE(w.BOOL, w.HMODULE, ctypes.c_void_p, w.LPARAM)
    names_cb = ctypes.WINFUNCTYPE(w.BOOL, w.HMODULE, ctypes.c_void_p, ctypes.c_void_p, w.LPARAM)
    langs_cb = ctypes.WINFUNCTYPE(w.BOOL, w.HMODULE, ctypes.c_void_p, ctypes.c_void_p, w.WORD, w.LPARAM)
    @langs_cb
    def language(module, resource_type, name, lang, parameter):
        try:
            info = k.FindResourceExW(module, resource_type, name, lang)
            pointer = k.LockResource(k.LoadResource(module, info))
            size = k.SizeofResource(module, info)
            resources[(identifier(resource_type), identifier(name), lang)] = ctypes.string_at(pointer, size)
            return True
        except Exception as error:
            errors.append(error); return False
    @names_cb
    def name(module, resource_type, resource_name, parameter):
        return k.EnumResourceLanguagesW(module, resource_type, resource_name, language, 0)
    @types_cb
    def resource_type(module, kind, parameter):
        return k.EnumResourceNamesW(module, kind, name, 0)
    k.EnumResourceTypesW.argtypes = [w.HMODULE, types_cb, w.LPARAM]
    k.EnumResourceNamesW.argtypes = [w.HMODULE, ctypes.c_void_p, names_cb, w.LPARAM]
    k.EnumResourceLanguagesW.argtypes = [w.HMODULE, ctypes.c_void_p, ctypes.c_void_p, langs_cb, w.LPARAM]
    try:
        if not k.EnumResourceTypesW(handle, resource_type, 0) or errors:
            raise errors[0] if errors else ctypes.WinError(ctypes.get_last_error())
    finally:
        k.FreeLibrary(handle)
    return resources


def _manifest_contract(data):
    root = ET.fromstring(data.rstrip(b'\0'))
    return [(e.tag, sorted(e.attrib.items()), (e.text or '').strip()) for e in root.iter()
            if not e.tag.endswith('assemblyIdentity')]


def _resource_extent(path):
    data = Path(path).read_bytes()
    pe, = struct.unpack_from('<I', data, 60)
    count, = struct.unpack_from('<H', data, pe+6)
    size, = struct.unpack_from('<H', data, pe+20)
    alignment, = struct.unpack_from('<I', data, pe+24+32)
    for index in range(count):
        pos = pe+24+size+40*index
        if data[pos:pos+8].rstrip(b'\0') == b'.rsrc':
            virtual_size, = struct.unpack_from('<I', data, pos+8)
            return virtual_size, alignment
    raise ValueError('Missing resource section')


def _restore_overlay(source, destination):
    # UpdateResource drops the COFF overlay produced by LLVM/MinGW. Restore it
    # verbatim and adjust only its file pointer; executable section bytes/RVAs stay fixed.
    original = Path(source).read_bytes()
    derived = bytearray(Path(destination).read_bytes())
    def layout(data):
        pe, = struct.unpack_from('<I', data, 60)
        count, = struct.unpack_from('<H', data, pe+6)
        size, = struct.unpack_from('<H', data, pe+20)
        end = max(sum(struct.unpack_from('<II', data, pe+24+size+index*40+16))
                  for index in range(count))
        return pe, end
    source_pe, source_end = layout(original)
    target_pe, target_end = layout(derived)
    symbols, count = struct.unpack_from('<II', original, source_pe+12)
    if symbols and symbols < source_end:
        raise ValueError('COFF symbols are not in the source overlay')
    derived = derived[:target_end]+original[source_end:]
    if symbols:
        struct.pack_into('<II', derived, target_pe+12, target_end+symbols-source_end, count)
    checksum = ctypes.WinDLL('imagehlp', use_last_error=True).CheckSumMappedFile
    checksum.argtypes = [ctypes.c_void_p, ctypes.c_uint32, ctypes.POINTER(ctypes.c_uint32), ctypes.POINTER(ctypes.c_uint32)]
    checksum.restype = ctypes.c_void_p
    buffer = ctypes.create_string_buffer(bytes(derived))
    old, new = ctypes.c_uint32(), ctypes.c_uint32()
    if not checksum(buffer, len(derived), ctypes.byref(old), ctypes.byref(new)):
        raise ctypes.WinError(ctypes.get_last_error())
    struct.pack_into('<I', derived, target_pe+24+64, new.value)
    Path(destination).write_bytes(derived)


def brand_runtime(source, destination, ico, manifest, expected_sha256):
    source, output = Path(source).resolve(), Path(destination).resolve()
    if source == output or output.exists():
        raise ValueError('Branding requires a new output; existing binaries are preserved')
    # A rejected/partial artifact never receives the executable's selected filename.
    destination = output.with_name(output.name+'.branding-pending-'+uuid.uuid4().hex)
    source_sha = digest(source)
    if source_sha != expected_sha256:
        raise ValueError('Runtime differs from the expected artifact')
    contract, resources = pe_contract(source), read_resources(source)
    manifest_data = Path(manifest).read_bytes()
    old_manifests = [data for (kind, _, _), data in resources.items() if kind == 24]
    if not old_manifests or any(_manifest_contract(data) != _manifest_contract(manifest_data) for data in old_manifests):
        raise ValueError('Manifest capabilities changed; refuse branding')
    group, images = icon_resources(ico)
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, destination)
    k, w = _api()
    update = k.BeginUpdateResourceW(str(destination), False)
    if not update:
        raise ctypes.WinError(ctypes.get_last_error())
    def put(kind, name, lang, data):
        buffer = ctypes.create_string_buffer(data) if data is not None else None
        if not k.UpdateResourceW(update, _resource_arg(kind), _resource_arg(name), lang,
                                 ctypes.cast(buffer, ctypes.c_void_p) if buffer else None,
                                 len(data) if data is not None else 0):
            raise ctypes.WinError(ctypes.get_last_error())
    try:
        for kind, name, lang in resources:
            if kind in (3, 14, 16, 24):
                put(kind, name, lang, None)
        for name, data in images.items():
            put(3, name, 0, data)
        put(14, 1, 0, group)
        put(16, 1, 0, version_resource())
        put(24, 1, 0, manifest_data)
    except BaseException:
        k.EndUpdateResourceW(update, True)
        raise
    if not k.EndUpdateResourceW(update, False):
        raise ctypes.WinError(ctypes.get_last_error())
    old_size, alignment = _resource_extent(source)
    new_size, _ = _resource_extent(destination)
    old_span = (old_size+alignment-1)//alignment*alignment
    new_span = (new_size+alignment-1)//alignment*alignment
    if new_span < old_span:
        # Windows otherwise shifts later .reloc/DWARF RVAs when old icons shrink.
        # Reserve the same virtual span with inert RCDATA, not inherited artwork.
        padding = old_span-new_size-1024
        if padding <= 0:
            raise ValueError('Insufficient room for resource reservation')
        update = k.BeginUpdateResourceW(str(destination), False)
        if not update:
            raise ctypes.WinError(ctypes.get_last_error())
        try:
            put(10, 65534, 0, b'\0'*padding)
        except BaseException:
            k.EndUpdateResourceW(update, True)
            raise
        if not k.EndUpdateResourceW(update, False):
            raise ctypes.WinError(ctypes.get_last_error())
    _restore_overlay(source, destination)
    after = read_resources(destination)
    if pe_contract(destination) != contract:
        raise ValueError('Non-resource PE contract changed; derived runtime must not be used')
    for key, data in resources.items():
        if key[0] not in (3, 14, 16, 24) and after.get(key) != data:
            raise ValueError('Unrelated resource changed')
    if digest(source) != source_sha:
        raise ValueError('Source runtime changed during branding')
    result_sha = digest(destination)
    destination.rename(output)
    return {'source': str(source), 'source_sha256': source_sha, 'runtime': str(output),
            'runtime_sha256': result_sha, 'non_resource_contract_identical': True,
            'manifest_capabilities_identical': True, 'pe_contract': contract,
            'resource_count_before': len(resources), 'resource_count_after': len(after)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--expected-sha256', required=True)
    parser.add_argument('--report', type=Path, required=True)
    parser.add_argument('--icon-source', type=Path, required=True, help='original sce_sys/icon0.png from your local dump')
    args = parser.parse_args()
    from .identity import identity_assets
    ui = identity_assets()
    convert_original_icon(args.icon_source, ui)
    manifest = Path(__file__).resolve().parents[2]/'packaging/windows/tlg-probe.manifest'
    result = brand_runtime(args.source, args.output, ui/'Knack.ico', manifest, args.expected_sha256)
    result.update(icon_source=str(args.icon_source.resolve()), icon_source_sha256=digest(args.icon_source))
    args.report.write_text(json.dumps(result, indent=2), encoding='utf-8')


if __name__ == '__main__':
    main()
