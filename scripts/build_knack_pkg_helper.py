# SPDX-License-Identifier: GPL-2.0-or-later
"""Build a pinned headless PKGInstall adapter from local sources, outside the checkout.

No package installation or download. Supply upstream PKGInstall commit
902d14c1a3c277a586e4c0c4db0774d06bda3501 and Crypto++ 8.9 source directories.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import zipfile

REVISION = '902d14c1a3c277a586e4c0c4db0774d06bda3501'


def source_digest(directory):
    digest = hashlib.sha256()
    for path in sorted([*directory.glob('*.h'), *directory.glob('*.cpp')], key=lambda p: p.name):
        digest.update(path.name.encode() + b'\0' + path.read_bytes())
    return digest.hexdigest()


def adapt(source):
    """Apply exact, checked substitutions to an external copy of the pinned sources."""
    def edit(name, old, new, count=-1):
        path = source / name
        text = path.read_text(encoding='utf-8')
        if old not in text:
            raise ValueError(f'Unsupported upstream source: {name}: {old[:70]}')
        path.write_text(text.replace(old, new, count), encoding='utf-8')

    edit('pkg.cpp', '#include "pkg_type.h"', '#include "pkg_type.h"\n#include "guard.h"')
    start = (source / 'pkg.cpp').read_text().index('static void DecompressPFSC(')
    end = (source / 'pkg.cpp').read_text().index('\nu32 GetPFSCOffset', start)
    text = (source / 'pkg.cpp').read_text()
    text = text[:start] + '''static void DecompressPFSC(std::span<char> input, std::span<char> output) {
    uLongf length = output.size();
    require(uncompress(reinterpret_cast<Bytef*>(output.data()), &length,
                       reinterpret_cast<const Bytef*>(input.data()), input.size()) == Z_OK &&
            length == output.size(), "Invalid compressed PFS block");
}
''' + text[end:]
    (source / 'pkg.cpp').write_text(text)
    edit('pkg.cpp', 'file.Read(pkgheader);', 'file.Read(pkgheader);\n    require(pkgheader.pkg_size == pkgSize, "Truncated PKG");')
    edit('pkg.cpp', 'u32 n_files = pkgheader.pkg_table_entry_count;',
         '''u32 n_files = pkgheader.pkg_table_entry_count;
    require(n_files > 0 && n_files <= 16384, "Too many PKG entries");
    check_range(offset, uint64_t(n_files) * 32, pkgSize);''')
    edit('pkg.cpp', 'file.Read(entry.size);', '''file.Read(entry.size);
        check_range(entry.offset, entry.size, pkgSize);
        require(entry.size <= 32 * 1024 * 1024, "PKG metadata too large");''')
    edit('pkg.cpp', '            sfo.resize(entry.size);',
         '            require(entry.size <= 1024 * 1024, "SFO too large");\n            sfo.resize(entry.size);')
    edit('pkg.cpp', '            file.ReadRaw<u8>(sfo.data(), entry.size);',
         '            file.ReadRaw<u8>(sfo.data(), entry.size);\n            file.Seek(offset + (i + 1) * 32);')
    edit('pkg.cpp', 'std::array<u8, 256> imgkeydata;', 'std::array<u8, 256> imgkeydata{};')
    edit('pkg.cpp', '        auto currentPos = file.Tell();', '''        if (entry.id == 0x10) require(entry.size >= 2048, "Truncated key table");
        if (entry.id == 0x20) require(entry.size >= 256, "Truncated image key");
        auto currentPos = file.Tell();''')
    # Encrypted NP metadata declares the logical size; the physical CBC ciphertext
    # includes a complete final block. Read that block from the PKG, then trim.
    edit('pkg.cpp', '            decNp.resize(entry.size);', '''            const size_t cipher_size = (size_t(entry.size) + 15) / 16 * 16;
            check_range(entry.offset, cipher_size, pkgSize);
            decNp.resize(cipher_size);''')
    edit('pkg.cpp', '''            data.resize(entry.size);
            file.ReadRaw<u8>(data.data(), entry.size);

            std::span<u8> cipherNp(data.data(), entry.size);''', '''            data.resize(cipher_size);
            file.ReadRaw<u8>(data.data(), cipher_size);

            std::span<u8> cipherNp(data.data(), cipher_size);''')
    edit('pkg.cpp', '            out.Write(decNp);', '            decNp.resize(entry.size);\n            out.Write(decNp);')
    for name in ('dk3_', 'ivKey', 'imgKey', 'ekpfsKey', 'dataKey', 'tweakKey'):
        edit('pkg.h', name + ';', name + '{};')
    for name in ('seed_digest', 'digest1', 'key1'):
        edit('pkg.cpp', name + ';', name + '{};')
    edit('pkg.cpp', 'const u32 length = pkgheader.pfs_cache_size * 0x2;',
         '''require(pkgheader.pfs_cache_size > 0 && pkgheader.pfs_cache_size <= 128 * 1024 * 1024,
            "Unsupported PFS cache size");
    const u32 length = pkgheader.pfs_cache_size * 0x2;
    require(length % 0x1000 == 0, "Unaligned PFS cache");
    check_range(pkgheader.pfs_image_offset, length, pkgSize);''')
    edit('pkg.cpp', 'pfsc_offset = GetPFSCOffset(pfs_decrypted);',
         'pfsc_offset = GetPFSCOffset(pfs_decrypted);\n        check_range(pfsc_offset, sizeof(PFSCHdr), length);')
    edit('pkg.cpp', 'num_blocks = (int)(pfsChdr.data_length / pfsChdr.block_sz2);',
         '''require(pfsChdr.block_sz2 == 0x10000 && pfsChdr.data_length > 0 &&
                pfsChdr.data_length <= 64LL * 1024 * 1024 * 1024, "Unsupported PFS size");
        num_blocks = (int)(pfsChdr.data_length / pfsChdr.block_sz2);
        check_range(pfsChdr.block_offsets, uint64_t(num_blocks + 1) * 8, length - pfsc_offset);''')
    edit('pkg.cpp', '        compressedData.resize(sectorSize);',
         '''        require(sectorSize > 0 && sectorSize <= 0x10000, "Invalid PFS sector size");
        check_range(sectorOffset, sectorSize, length - pfsc_offset);
        compressedData.resize(sectorSize);''', count=1)
    edit('pkg.cpp', 'std::memcpy(&ndinode, decompressedData.data() + 0x30, 4);',
         '''std::memcpy(&ndinode, decompressedData.data() + 0x30, 4);
            require(ndinode > 0 && ndinode <= 200000, "Unsupported inode count");''')
    edit('pkg.cpp', 'p < 0x10000; p += 0xA8', 'p + sizeof(Inode) <= 0x10000; p += 0xA8')
    edit('pkg.cpp', 'std::memcpy(&dirent, &decompressedData[i], sizeof(dirent));',
         'read_dirent(dirent, decompressedData, i);')
    edit('pkg.cpp', 'std::memcpy(&dirent, &decompressedData[j], sizeof(dirent));',
         'read_dirent(dirent, decompressedData, j);')
    a = (source / 'pkg.cpp').read_text()
    start = a.index('                    auto parent_path = extract_path.parent_path();')
    end = a.index('                    uroot_reached = false;', start)
    a = a[:start] + '                    extractPaths[ndinode_counter] = extract_path;\n' + a[end:]
    (source / 'pkg.cpp').write_text(a)
    edit('pkg.cpp', '    // Get iNdoes and Dirents.', '    bool complete = false;\n    // Get iNdoes and Dirents.')
    edit('pkg.cpp', '    u32 ent_size = 0;', '''    for (size_t i = 0; i + 1 < sectorMap.size(); ++i) {
        require(sectorMap[i + 1] > sectorMap[i] && sectorMap[i + 1] - sectorMap[i] <= 0x10000,
                "Invalid PFS sector map");
        check_range(pkgheader.pfs_image_offset + pfsc_offset, sectorMap[i + 1], pkgSize);
    }
    u32 ent_size = 0;''')
    edit('pkg.cpp', '                if (table.type == PFS_CURRENT_DIR) {', '''                if (table.type == PFS_PARENT_DIR) continue;
                if (table.type == PFS_CURRENT_DIR) {''')
    edit('pkg.cpp', '                    current_dir = extractPaths[table.inode];',
         '                    current_dir = extractPaths.at(table.inode);\n                    continue;')
    edit('pkg.cpp', 'extractPaths[table.inode] = current_dir / std::filesystem::path(table.name);',
         '''check_component(table.name);
                require(table.type == PFS_FILE || table.type == PFS_DIR, "Unsupported inode type");
                require(!extractPaths.contains(table.inode), "Duplicate inode");
                extractPaths[table.inode] = current_dir / std::filesystem::u8path(table.name);
                check_destination(extract_path, extractPaths.at(table.inode));''')
    edit('pkg.cpp', '            if (end_reached) {\n                break;',
         '            if (end_reached) {\n                complete = true;\n                break;')
    edit('pkg.cpp', '    return true;\n}\n\nvoid PKG::ExtractFiles',
         '''    require(complete, "Incomplete PFS file table");
    uint64_t total = 0;
    std::set<std::filesystem::path> destinations;
    for (const auto& entry : fsTable) {
        if (entry.type != PFS_FILE) continue;
        const auto& node = iNodeBuf.at(entry.inode);
        require(node.Size >= 0 && node.Size <= 64LL * 1024 * 1024 * 1024, "Unsupported file size");
        require(node.Blocks == (node.Size + 0xffff) / 0x10000, "Unsupported inode block layout");
        check_range(node.loc, uint64_t(node.Blocks) + 1, sectorMap.size());
        total += node.Size;
        require(total <= 64ULL * 1024 * 1024 * 1024, "Update too large");
        require(destinations.insert(extractPaths.at(entry.inode)).second, "Duplicate output file");
    }
    require(std::filesystem::space(extract_path).available >= total + 64 * 1024 * 1024,
            "Not enough free space for unpacked update");
    std::cout << "Unpacked bytes: " << total << std::endl;
    return true;
}

void PKG::ExtractFiles''')
    edit('pkg.cpp', 'fsTable[index]', 'fsTable.at(index)')
    edit('pkg.cpp', 'iNodeBuf[inode_number]', 'iNodeBuf.at(inode_number)')
    edit('pkg.cpp', 'int bsize = iNodeBuf.at(inode_number).Size;', 'int64_t bsize = iNodeBuf.at(inode_number).Size;')
    edit('pkg.cpp', 'int size_decompressed = 0;', 'int64_t size_decompressed = 0;')
    edit('pkg.cpp', '        Common::FS::IOFile inflated;', '''        check_destination(extract_path, extractPaths.at(inode_number));
        Common::FS::IOFile inflated;''')
    edit('pkg.cpp', '            compressedData.resize(sectorSize);',
         '''            require(sectorSize > 0 && sectorSize <= 0x10000, "Invalid file block");
            check_range(previousData, sectorSize, pfs_decrypted.size());
            compressedData.resize(sectorSize);''')
    edit('pkg.cpp', '            pkgFile.Seek(fileOffset - previousData);',
         '''            require(sectorSize > 0 && sectorSize <= 0x10000, "Invalid file block size");
            const auto cipherSize = ((previousData + sectorSize + 0xfff) / 0x1000) * 0x1000;
            pfsc.resize(cipherSize);
            pfs_decrypted.resize(cipherSize);
            check_range(fileOffset - previousData, cipherSize, pkgSize);
            pkgFile.Seek(fileOffset - previousData);''')
    edit('pkg.cpp', '        inflated.Close();',
         '        require(inflated.Flush(), "Cannot flush extracted file");\n        inflated.Close();\n        require(std::filesystem::file_size(extractPaths.at(inode_number)) == bsize, "Extracted size mismatch");')
    edit('pkg.cpp', 'int sectorOffsetMask = (sectorOffset + pfsc_offset) & 0xFFFFF000;',
         'uint64_t sectorOffsetMask = (sectorOffset + pfsc_offset) & ~uint64_t(0xfff);')
    # All I/O failures reach the process exit status; short writes cannot announce success.
    # MinGW Clang does not define _MSC_VER. Its off_t/fseeko can be 32-bit;
    # PS4 base packages routinely contain file offsets beyond 4 GiB.
    edit('io_file.cpp', '#ifdef _MSC_VER\n#define fileno _fileno',
         '#ifdef _WIN32\n#define fileno _fileno')
    edit('io_file.h', '#include "types.h"', '#include "types.h"\n#include <stdexcept>')
    edit('io_file.h', 'return std::fread(data, sizeof(T), size, file);',
         'if (!file || std::fread(data, sizeof(T), size, file) != size) throw std::runtime_error("Short PKG read");\n        return size;')
    edit('io_file.h', 'return std::fread(&object, sizeof(T), 1, file) == 1;',
         'if (std::fread(&object, sizeof(T), 1, file) != 1) throw std::runtime_error("Short object read");\n        return true;')
    edit('io_file.h', 'return std::fwrite(data.data(), sizeof(T), data.size(), file);',
         'if (std::fwrite(data.data(), sizeof(T), data.size(), file) != data.size()) throw std::runtime_error("Short write");\n        return data.size();')
    edit('io_file.h', 'auto bytes = std::fwrite(data, sizeof(T), size, file);',
         'if (!file) throw std::runtime_error("Cannot open extracted file");\n        auto bytes = std::fwrite(data, sizeof(T), size, file);\n        if (bytes != size) throw std::runtime_error("Short extracted write");')
    edit('crypto.cpp', '    std::copy(decrypted.begin(), decrypted.begin() + dec_key.size(), dec_key.begin());',
         '    if (!result.isValidCoding || result.messageLength < dec_key.size()) throw std::runtime_error("Unsupported encrypted PKG key");\n    std::copy(decrypted.begin(), decrypted.begin() + dec_key.size(), dec_key.begin());')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-dir', type=Path, required=True)
    parser.add_argument('--cryptopp-source', type=Path, required=True)
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--toolchain', type=Path, default=Path(os.environ.get('LOCALAPPDATA', '')) / 'knack2-toolchain/msys64/clang64')
    parser.add_argument('--jobs', type=int, default=4)
    parser.add_argument('--selftest', action='store_true', help='Build and extract an encrypted synthetic PKG.')
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[1]
    build = args.build_dir.resolve()
    if build.is_relative_to(repo):
        raise ValueError('Build directory must be outside checkout.')
    build.mkdir(parents=True, exist_ok=True)
    source = build / 'adapted-pkginstall'
    if source.exists():
        raise ValueError('Use a fresh build directory; existing sources are preserved.')
    upstream = args.source_dir.resolve()
    if source_digest(upstream / 'src') != '08dc6d5e461c457d434ee1e7b445009eb9113a37466f72499ce9c6fbefdc0f28':
        raise ValueError('PKGInstall source differs from the pinned commit.')
    shutil.copytree(upstream / 'src', source)
    adapter = repo / 'port/launcher/knack2/pkg_helper'
    shutil.copy2(adapter / 'guard.h', source)
    shutil.copy2(adapter / 'main.cpp', source / 'helper_main.cpp')
    shutil.copy2(adapter / 'cpu_bridge.cpp', source / 'cpu_bridge.cpp')
    adapt(source)
    crypto = args.cryptopp_source.resolve()
    if source_digest(crypto) != 'e16f1d0aa7328524994a850baca9e6ed4d540e35a2eeb6391dc7bac9bab65f02':
        raise ValueError('Crypto++ source differs from the pinned 8.9.0 snapshot.')
    include = build / 'include/cryptopp'
    include.mkdir(parents=True, exist_ok=True)
    for path in crypto.glob('*.h'):
        shutil.copy2(path, include / path.name)
    tc = args.toolchain.resolve()
    env = dict(os.environ)
    env['PATH'] = str(tc / 'bin') + os.pathsep + env['PATH']
    compiler = tc / 'bin/clang++.exe'
    flags = [str(compiler), '-DCRYPTOPP_DISABLE_ASM', '-DNDEBUG', '-O1', '-std=c++23',
             '-include', 'cstring', '-include', 'stdexcept', '-include', 'windows.h',
             '-I' + str(build / 'include'), '-I' + str(tc / 'include')]
    excluded = {'pch', 'simple', 'adhoc', 'test', 'bench1', 'bench2', 'bench3', 'datatest',
                'dlltest', 'fipsalgt', *(f'validat{i}' for i in range(11)), *(f'regtest{i}' for i in range(1, 5))}
    units = [p for p in crypto.glob('*.cpp') if p.stem not in excluded]
    units += [source / (n + '.cpp') for n in ('pkg', 'pkg_type', 'crypto', 'io_file', 'nt_api', 'helper_main', 'cpu_bridge')]
    objects = build / 'objects'
    objects.mkdir(exist_ok=True)

    def compile_unit(path):
        output = objects / ((('up-' if path.parent == source else 'crypto-') + path.stem) + '.o')
        extra = ['-mxsave'] if path.stem == 'cpu_bridge' else []
        subprocess.run(flags + extra + ['-c', str(path), '-o', str(output)], env=env, check=True,
                       stdout=subprocess.DEVNULL)
        return output

    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        compiled = list(pool.map(compile_unit, units))
    output = build / 'KnackPkg.exe'
    subprocess.run([str(compiler), '-municode', '-o', str(output), *map(str, compiled),
                    '-L' + str(tc / 'lib'), '-lfmt', '-lz', '-lws2_32', '-lbcrypt'], env=env, check=True)
    # Private, self-contained staging only. No installed launcher or toolchain is overwritten.
    for name in ('libfmt-11.dll', 'libfmt-12.dll', 'zlib1.dll', 'libc++.dll', 'libunwind.dll'):
        path = tc / 'bin' / name
        if path.is_file():
            shutil.copy2(path, build / name)
    manifest = {'upstream': 'https://github.com/Muggle345/PKGInstall', 'revision': REVISION,
                'sha256': hashlib.sha256(output.read_bytes()).hexdigest(),
                'upstream_source_sha256': source_digest(upstream / 'src'),
                'cryptopp_source_sha256': source_digest(crypto),
                'adapted_sources': {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in source.iterdir() if p.is_file()}}
    shutil.copy2(crypto / 'License.txt', build / 'CryptoPP-License.txt')
    shutil.copy2(repo / 'port/gpu/shadps4/LICENSE', build / 'PKGInstall-COPYING.txt')
    (build / 'NOTICE.txt').write_text(
        'KnackPkg uses PKGInstall commit ' + REVISION + '.\n'
        'PKG/PFS code: Copyright 2024 shadPS4 Emulator Project, GPL-2.0-or-later.\n'
        'IO code: Copyright 2021 yuzu Emulator Project, GPL-2.0-or-later.\n'
        'Adapter: Knack II PC Port contributors, GPL-2.0-or-later.\n'
        'Crypto++ 8.9.0: see CryptoPP-License.txt.\n'
        'Corresponding upstream and adapted sources: helper-sources.zip.\n', encoding='utf-8')
    with zipfile.ZipFile(build / 'helper-sources.zip', 'w', compression=zipfile.ZIP_DEFLATED) as archive:
        for directory, prefix in ((upstream / 'src', 'pkginstall-upstream'),
                                  (source, 'pkginstall-adapted'), (crypto, 'cryptopp-8.9.0')):
            for path in directory.iterdir():
                if path.is_file() and (path.suffix in ('.h', '.cpp') or path.name.lower() in ('license.txt', 'readme.txt')):
                    archive.write(path, prefix + '/' + path.name)
        archive.write(__file__, 'adapter/build_knack_pkg_helper.py')
        archive.write(repo / 'tests/knack_pkg_fixture.cpp', 'adapter/knack_pkg_fixture.cpp')
        archive.write(build / 'PKGInstall-COPYING.txt', 'COPYING.txt')
    manifest['bundle_files'] = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in build.iterdir()
                                if p.is_file() and (p.suffix in ('.exe', '.dll', '.txt', '.zip'))}
    (build / 'helper-manifest.json').write_text(json.dumps(manifest, indent=2), encoding='utf-8')
    if args.selftest:
        fixture = build / 'KnackPkgFixture.exe'
        subprocess.run(flags + ['-I' + str(source), str(repo / 'tests/knack_pkg_fixture.cpp'),
                       *[str(p) for p in compiled if p.name.startswith('crypto-') or p.name in ('up-crypto.o', 'up-cpu_bridge.o', 'up-io_file.o', 'up-nt_api.o')],
                       '-L' + str(tc / 'lib'), '-lws2_32', '-lbcrypt', '-o', str(fixture)], env=env, check=True)
        subprocess.run([sys.executable, '-B', '-m', 'unittest', 'tests.test_knack2_pkg_native', '-v'],
                       cwd=repo, env=dict(env, KNACK_PKG_NATIVE_DIR=str(build)), check=True, timeout=60)
    print(json.dumps({'helper': str(output), 'sha256': manifest['sha256']}))


if __name__ == '__main__':
    main()
