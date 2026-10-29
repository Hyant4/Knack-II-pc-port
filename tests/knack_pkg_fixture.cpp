// SPDX-License-Identifier: GPL-2.0-or-later
// Generate a tiny encrypted PKG with invented bytes; no game data or game executables.
#include <cstring>
#include <fstream>
#include <iostream>
#include <vector>
#include <cryptopp/xts.h>
#include "pkg.h"
#include "io_file.h"

int main(int argc, char** argv) {
    if (argc == 3 && std::string(argv[1]) == "--seek64") {
        std::ofstream(argv[2], std::ios::binary).close();
        Common::FS::IOFile file(std::filesystem::path(argv[2]), Common::FS::FileAccessMode::Read);
        constexpr int64_t offset = 5LL * 1024 * 1024 * 1024 + 123;
        return file.Seek(offset) && file.Tell() == offset ? 0 : 1;
    }
    if (argc != 4 && !(argc == 5 && std::string(argv[4]) == "--base")) return 2;
    const std::string filename = argv[3];
    if (filename.size() > 100) return 2;
    std::ifstream metadata(argv[1], std::ios::binary);
    std::vector<u8> sfo((std::istreambuf_iterator<char>(metadata)), {});
    constexpr size_t block = 0x10000, pfsc_offset = 0x20000, pfs_size = 0x80000;
    std::vector<u8> pfs(pfs_size);
    PFSCHdr ph{};
    ph.magic = 0x43534650; ph.block_sz = block; ph.block_sz2 = block;
    ph.block_offsets = sizeof(ph); ph.data_start = block; ph.data_length = 5 * block;
    std::memcpy(pfs.data() + pfsc_offset, &ph, sizeof(ph));
    for (uint64_t i = 0; i <= 5; ++i) {
        uint64_t offset = block + i * block;
        std::memcpy(pfs.data() + pfsc_offset + sizeof(ph) + i * 8, &offset, 8);
    }
    auto logical = [&](size_t index) { return pfs.data() + pfsc_offset + block + index * block; };
    uint32_t count = 3;
    std::memcpy(logical(0) + 0x30, &count, 4);
    for (size_t i = 0; i < 3; ++i) {
        Inode inode{};
        inode.Mode = i == 2 ? 0x8000 : 0x4000;
        if (i == 2) { inode.Size = 21; inode.Blocks = 1; inode.loc = 4; }
        std::memcpy(logical(1) + i * 0xA8, &inode, sizeof(inode));
    }
    auto dirent = [](u8* out, int inode, int type, const std::string& name, int stride) {
        const int fields[4] = {inode, type, int(name.size()), stride};
        std::memcpy(out, fields, 16);
        std::memcpy(out + 16, name.data(), name.size());
    };
    dirent(logical(2), 1, PFS_DIR, "flat_path_table", 32);
    dirent(logical(3), 1, PFS_CURRENT_DIR, ".", 24);
    dirent(logical(3) + 24, 1, PFS_PARENT_DIR, "..", 24);
    dirent(logical(3) + 48, 2, PFS_FILE, filename, 16 + filename.size());
    std::memcpy(logical(4), "Synthetic PKG payload", 21);

    Crypto crypto;
    std::array<u8, 32> dk3{}, ekpfs{};
    std::array<u8, 16> seed{}, data_key{}, tweak_key{};
    dk3.fill(0x19); ekpfs.fill(0x27); seed.fill(0x31);
    crypto.PfsGenCryptoKey(ekpfs, seed, data_key, tweak_key);
    std::array<u8, 32> xts_key{};
    std::copy(data_key.begin(), data_key.end(), xts_key.begin());
    std::copy(tweak_key.begin(), tweak_key.end(), xts_key.begin() + 16);
    std::vector<u8> encrypted(pfs.size());
    for (uint64_t i = 0; i < pfs.size(); i += 0x1000) {
        std::array<u8, 16> iv{};
        uint64_t sector = i / 0x1000;
        std::memcpy(iv.data(), &sector, 8);
        CryptoPP::XTS_Mode<CryptoPP::AES>::Encryption cipher;
        cipher.SetKeyWithIV(xts_key.data(), xts_key.size(), iv.data());
        cipher.ProcessData(encrypted.data() + i, pfs.data() + i, 0x1000);
    }
    // PFS seed is stored in the clear header, outside the parsed PFSC blocks.
    std::memcpy(encrypted.data() + 0x370, seed.data(), seed.size());

    PKGHeader header{};
    header.magic = 0x7F434E54; header.pkg_table_entry_count = 4;
    header.pkg_table_entry_offset = 0x1000; header.pkg_content_flags = argc == 5 ? 0 : 0x100000;
    header.pfs_image_offset = 0x10000; header.pfs_image_size = pfs_size;
    header.pfs_cache_size = pfs_size / 2; header.pkg_size = 0x10000 + pfs_size;
    const std::string content = "UP9000-CUSA08014_00-KNACK20000000000";
    std::memcpy(header.pkg_content_id, content.data(), content.size());
    std::vector<u8> output(header.pkg_size);
    std::memcpy(output.data(), &header, sizeof(header));
    PKGEntry entries[4]{};
    entries[0].id = 0x10; entries[0].offset = 0x1100; entries[0].size = 2048;
    entries[1].id = 0x20; entries[1].offset = 0x1900; entries[1].size = 256;
    entries[2].id = 0x1000; entries[2].offset = 0x1A00; entries[2].size = sfo.size();
    entries[3].id = 0x403; entries[3].offset = 0x1C00; entries[3].size = 532;
    entries[3].flags1 = 0x80000000; entries[3].flags2 = 0x3000;
    std::memcpy(output.data() + 0x1000, entries, sizeof(entries));
    CryptoPP::AutoSeededRandomPool rng;
    CryptoPP::RSAES_PKCS1v15_Encryptor rsa1(crypto.key_pkg_derived_key3_keyset_init());
    rsa1.Encrypt(rng, dk3.data(), dk3.size(), output.data() + 0x1100 + 32 + 7 * 32 + 3 * 256);
    std::array<u8, 256> image_key{};
    CryptoPP::RSAES_PKCS1v15_Encryptor rsa2(crypto.FakeKeyset_keyset_init());
    rsa2.Encrypt(rng, ekpfs.data(), ekpfs.size(), image_key.data());
    std::array<u8, 64> hash_input{};
    std::array<u8, 32> ivkey{};
    std::memcpy(hash_input.data(), &entries[1], 32);
    std::memcpy(hash_input.data() + 32, dk3.data(), 32);
    crypto.ivKeyHASH256(hash_input, ivkey);
    CryptoPP::CBC_Mode<CryptoPP::AES>::Encryption aes;
    aes.SetKeyWithIV(ivkey.data() + 16, 16, ivkey.data());
    aes.ProcessData(output.data() + 0x1900, image_key.data(), image_key.size());
    std::memcpy(hash_input.data(), &entries[3], 32);
    crypto.ivKeyHASH256(hash_input, ivkey);
    std::array<u8, 544> np_plaintext{};
    for (size_t i = 0; i < 532; ++i) np_plaintext[i] = (i * 7) & 0xff;
    aes.SetKeyWithIV(ivkey.data() + 16, 16, ivkey.data());
    aes.ProcessData(output.data() + 0x1C00, np_plaintext.data(), np_plaintext.size());
    std::memcpy(output.data() + 0x1A00, sfo.data(), sfo.size());
    std::memcpy(output.data() + 0x10000, encrypted.data(), encrypted.size());
    std::ofstream file(argv[2], std::ios::binary);
    file.write(reinterpret_cast<const char*>(output.data()), output.size());
    return file ? 0 : 1;
}
