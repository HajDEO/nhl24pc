// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <zlib.h>
#include <fmt/format.h>
#include "common/io_file.h"
#include "core/file_format/pkg.h"
#include "core/file_format/pkg_type.h"

static bool DecompressPFSC(std::span<const char> compressed_data,
                           std::span<char> decompressed_data) {
    z_stream stream{};
    if (inflateInit(&stream) != Z_OK) {
        return false;
    }
    stream.avail_in = static_cast<uInt>(compressed_data.size());
    stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(compressed_data.data()));
    stream.avail_out = static_cast<uInt>(decompressed_data.size());
    stream.next_out = reinterpret_cast<Bytef*>(decompressed_data.data());
    const int ret = inflate(&stream, Z_FINISH);
    inflateEnd(&stream);
    return ret == Z_STREAM_END || ret == Z_OK || ret == Z_BUF_ERROR;
}

static u64 GetPFSCOffset(std::span<const u8> pfs_image) {
    static constexpr u32 PfscMagic = 0x43534650;
    u32 value;
    for (u64 i = 0x20000; i + sizeof(u32) <= pfs_image.size(); i += 0x10000) {
        std::memcpy(&value, &pfs_image[i], sizeof(u32));
        if (value == PfscMagic) {
            return i;
        }
    }
    return 0;
}

PKG::PKG() = default;

PKG::~PKG() = default;

bool PKG::Open(const std::filesystem::path& filepath, std::string& failreason) {
    Common::FS::IOFile file(filepath, Common::FS::FileAccessMode::Read);
    if (!file.IsOpen()) {
        failreason = "Cannot open the file";
        return false;
    }
    pkgSize = file.GetSize();

    file.Read(pkgheader);
    if (pkgheader.magic != 0x7F434E54) {
        failreason = "Not a PS4 package (bad magic)";
        return false;
    }

    pkgFlags.clear();
    for (const auto& flag : flagNames) {
        if (isFlagSet(pkgheader.pkg_content_flags, flag.first)) {
            if (!pkgFlags.empty()) {
                pkgFlags += ", ";
            }
            pkgFlags += flag.second;
        }
    }

    // The title id is part of the content id (offset 0x40), after the 7-character prefix.
    file.Seek(0x47);
    file.Read(pkgTitleID);

    const u32 offset = pkgheader.pkg_table_entry_offset;
    const u32 n_files = pkgheader.pkg_table_entry_count;
    if (!file.Seek(offset)) {
        failreason = "Failed to seek to PKG table entry offset";
        return false;
    }

    for (u32 i = 0; i < n_files; i++) {
        PKGEntry entry{};
        file.Read(entry.id);
        file.Read(entry.filename_offset);
        file.Read(entry.flags1);
        file.Read(entry.flags2);
        file.Read(entry.offset);
        file.Read(entry.size);
        file.Seek(8, Common::FS::SeekOrigin::CurrentPosition);
        const auto current_pos = file.Tell();

        if (GetEntryNameByType(entry.id) == "param.sfo") {
            sfo.clear();
            if (!file.Seek(entry.offset)) {
                failreason = "Failed to seek to param.sfo offset";
                return false;
            }
            sfo.resize(entry.size);
            file.ReadRaw<u8>(sfo.data(), entry.size);
            file.Seek(current_pos);
        }
    }
    file.Close();
    return true;
}

bool PKG::Extract(const std::filesystem::path& filepath, const std::filesystem::path& extract,
                  std::string& failreason) {
    extract_path = extract;
    pkgpath = filepath;
    extractPaths.clear();
    fsTable.clear();
    iNodeBuf.clear();
    sectorMap.clear();

    Common::FS::IOFile file(filepath, Common::FS::FileAccessMode::Read);
    if (!file.IsOpen()) {
        failreason = "Cannot open the file";
        return false;
    }
    pkgSize = file.GetSize();
    file.ReadRaw<u8>(&pkgheader, sizeof(PKGHeader));

    if (pkgheader.magic != 0x7F434E54) {
        failreason = "Not a PS4 package (bad magic)";
        return false;
    }
    if (pkgheader.pkg_size > pkgSize) {
        failreason = "PKG file size is different (incomplete download?)";
        return false;
    }
    if ((pkgheader.pkg_content_size + pkgheader.pkg_content_offset) > pkgheader.pkg_size) {
        failreason = "Content size is bigger than pkg size";
        return false;
    }

    const u32 offset = pkgheader.pkg_table_entry_offset;
    const u32 n_files = pkgheader.pkg_table_entry_count;

    std::array<u8, 64> concatenated_ivkey_dk3{};
    std::array<u8, 32> seed_digest{};
    std::array<std::array<u8, 32>, 7> digest1{};
    std::array<std::array<u8, 256>, 7> key1{};
    std::array<u8, 256> imgkeydata{};

    if (!file.Seek(offset)) {
        failreason = "Failed to seek to PKG table entry offset";
        return false;
    }

    std::error_code ec;
    std::filesystem::create_directories(extract_path / "sce_sys", ec);

    for (u32 i = 0; i < n_files; i++) {
        PKGEntry entry{};
        file.Read(entry.id);
        file.Read(entry.filename_offset);
        file.Read(entry.flags1);
        file.Read(entry.flags2);
        file.Read(entry.offset);
        file.Read(entry.size);
        file.Seek(8, Common::FS::SeekOrigin::CurrentPosition);

        const auto current_pos = file.Tell();
        const auto name = GetEntryNameByType(entry.id);
        const auto out_path = name.empty() ? extract_path / "sce_sys" / std::to_string(entry.id)
                                           : extract_path / "sce_sys" / name;
        std::filesystem::create_directories(out_path.parent_path(), ec);

        if (!file.Seek(entry.offset)) {
            failreason = "Failed to seek to PKG entry offset";
            return false;
        }
        std::vector<u8> data(entry.size);
        file.ReadRaw<u8>(data.data(), entry.size);

        if (entry.id == 0x10) { // ENTRY_KEYS
            file.Seek(entry.offset);
            file.Read(seed_digest);
            for (int k = 0; k < 7; k++) {
                file.Read(digest1[k]);
            }
            for (int k = 0; k < 7; k++) {
                file.Read(key1[k]);
            }
            if (!crypto.RSA2048Decrypt(dk3_, key1[3], true)) {
                failreason = "Failed to decrypt the package key (dk3)";
                return false;
            }
        } else if (entry.id == 0x20) { // IMAGE_KEY
            std::memcpy(imgkeydata.data(), data.data(), std::min<size_t>(data.size(), imgkeydata.size()));
            std::memcpy(concatenated_ivkey_dk3.data(), &entry, sizeof(entry));
            std::memcpy(concatenated_ivkey_dk3.data() + sizeof(entry), dk3_.data(), dk3_.size());
            crypto.ivKeyHASH256(concatenated_ivkey_dk3, ivKey);
            crypto.aesCbcCfb128Decrypt(ivKey, imgkeydata, imgKey);
            if (!crypto.RSA2048Decrypt(ekpfsKey, imgKey, false)) {
                failreason = "Failed to decrypt the PFS key (not a fake-signed package?)";
                return false;
            }
        }

        {
            Common::FS::IOFile out(out_path, Common::FS::FileAccessMode::Create);
            if (!out.IsOpen()) {
                failreason = "Cannot write " + out_path.string();
                return false;
            }
            out.WriteRaw<u8>(data.data(), data.size());
        }

        // The np* entries are encrypted with the entry key; store them decrypted.
        if (entry.id == 0x400 || entry.id == 0x401 || entry.id == 0x402 || entry.id == 0x403) {
            const size_t rsize = entry.size;
            size_t msize = rsize;
            if (msize % 16 != 0) {
                msize = msize - msize % 16 + 16;
            }
            data.resize(msize);
            decNp.assign(msize, 0);
            std::array<u8, 64> concatenated_ivkey_dk3_{};
            std::memcpy(concatenated_ivkey_dk3_.data(), &entry, sizeof(entry));
            std::memcpy(concatenated_ivkey_dk3_.data() + sizeof(entry), dk3_.data(), dk3_.size());
            crypto.ivKeyHASH256(concatenated_ivkey_dk3_, ivKey);
            crypto.aesCbcCfb128Decrypt(ivKey, data, decNp);
            decNp.resize(rsize);
            Common::FS::IOFile out(out_path, Common::FS::FileAccessMode::Create);
            out.Write(decNp);
        }

        file.Seek(current_pos);
    }

    // Read the seed
    std::array<u8, 16> seed{};
    if (!file.Seek(pkgheader.pfs_image_offset + 0x370)) {
        failreason = "Failed to seek to PFS image offset";
        return false;
    }
    file.Read(seed);

    // Get data and tweak keys.
    crypto.PfsGenCryptoKey(ekpfsKey, seed, dataKey, tweakKey);
    const u64 length = static_cast<u64>(pkgheader.pfs_cache_size) * 0x2; // Seems to be ok.

    u64 num_blocks = 0;
    std::vector<u8> pfsc(length);
    if (length != 0) {
        std::vector<u8> pfs_encrypted(length);
        file.Seek(pkgheader.pfs_image_offset);
        file.Read(pfs_encrypted);
        file.Close();
        std::vector<u8> pfs_decrypted(length);
        crypto.decryptPFS(dataKey, tweakKey, pfs_encrypted, pfs_decrypted, 0);

        pfsc_offset = GetPFSCOffset(pfs_decrypted);
        if (pfsc_offset == 0) {
            failreason = "PFSC header not found in the decrypted PFS image (wrong keys?)";
            return false;
        }
        std::memcpy(pfsc.data(), pfs_decrypted.data() + pfsc_offset, length - pfsc_offset);

        PFSCHdr pfsChdr{};
        std::memcpy(&pfsChdr, pfsc.data(), sizeof(pfsChdr));
        if (pfsChdr.block_sz2 <= 0) {
            failreason = "Invalid PFSC header";
            return false;
        }
        num_blocks = static_cast<u64>(pfsChdr.data_length / pfsChdr.block_sz2);
        sectorMap.resize(num_blocks + 1); // 8 bytes, need extra 1 to get the last offset.
        for (u64 i = 0; i < num_blocks + 1; i++) {
            std::memcpy(&sectorMap[i], pfsc.data() + pfsChdr.block_offsets + i * 8, 8);
        }
    } else {
        failreason = "Package has no PFS image";
        return false;
    }

    u32 ent_size = 0;
    u32 ndinode = 0;
    int ndinode_counter = 0;
    bool dinode_reached = false;
    bool uroot_reached = false;
    std::vector<char> compressedData;
    std::vector<char> decompressedData(0x10000);

    // Get iNodes and Dirents. The metadata (super block, inodes, dirents) sits in front of the
    // file data, so the walk stops at the first block that belongs to a file, and never runs past
    // the part of the cache that was actually read (the tail of `pfsc` is zero filled).
    const u64 cached_valid = length - pfsc_offset;
    u64 first_data_block = std::numeric_limits<u64>::max();
    const bool trace = std::getenv("NHLPORT_PKG_TRACE") != nullptr;
    for (u64 i = 0; i < num_blocks; i++) {
        if (i >= first_data_block) {
            if (trace) {
                std::fprintf(stderr, "pfs: stop at block %llu (first data block)\n", (unsigned long long)i);
            }
            break;
        }
        const u64 sectorOffset = sectorMap[i];
        const u64 sectorSize = sectorMap[i + 1] - sectorOffset;
        if (sectorOffset + sectorSize > cached_valid) {
            break;
        }

        compressedData.resize(sectorSize);
        std::memcpy(compressedData.data(), pfsc.data() + sectorOffset, sectorSize);

        if (sectorSize == 0x10000) { // Uncompressed data
            std::memcpy(decompressedData.data(), compressedData.data(), 0x10000);
        } else if (sectorSize < 0x10000) { // Compressed data
            if (!DecompressPFSC(compressedData, decompressedData)) {
                failreason = fmt::format(
                    "zlib failed on a PFS metadata block (block {} of {}, offset {:#x} size {:#x}, "
                    "cached {:#x} of which valid {:#x}, pfsc at {:#x}, inodes {} ({} read), dirents {}, "
                    "first bytes {:02x} {:02x} {:02x} {:02x})",
                    i, num_blocks, sectorOffset, sectorSize, pfsc.size(), length - pfsc_offset,
                    pfsc_offset, ndinode, iNodeBuf.size(), fsTable.size(),
                    u8(compressedData[0]), u8(compressedData[1]), u8(compressedData[2]),
                    u8(compressedData[3]));
                return false;
            }
        }

        if (i == 0) {
            std::memcpy(&ndinode, decompressedData.data() + 0x30, 4); // number of folders and files
        }
        if (trace) {
            std::fprintf(stderr, "pfs: block %llu off %#llx size %#llx bytes@0x10 '%.15s' counter %d inodes %zu dirents %zu\n",
                         (unsigned long long)i, (unsigned long long)sectorOffset, (unsigned long long)sectorSize,
                         &decompressedData[0x10], ndinode_counter, iNodeBuf.size(), fsTable.size());
        }

        u64 occupied_blocks = (static_cast<u64>(ndinode) * 0xA8) / 0x10000;
        if (((static_cast<u64>(ndinode) * 0xA8) % 0x10000) != 0) {
            occupied_blocks += 1;
        }

        if (i >= 1 && i <= occupied_blocks) { // iNodes: type, file size and location.
            for (int p = 0; p + static_cast<int>(sizeof(Inode)) <= 0x10000; p += 0xA8) {
                Inode node;
                std::memcpy(&node, &decompressedData[p], sizeof(node));
                if (node.Mode == 0) {
                    break;
                }
                iNodeBuf.push_back(node);
            }
        }

        // The root / uroot entries: the inode of the folder whose dirents follow maps onto the
        // extraction folder itself.
        const std::string_view flat_path_table(&decompressedData[0x10], 15);
        if (flat_path_table == "flat_path_table") {
            uroot_reached = true;
        }

        if (uroot_reached) {
            for (int k = 0; k < 0x10000; k += ent_size) {
                Dirent dirent;
                std::memcpy(&dirent, &decompressedData[k], sizeof(dirent));
                ent_size = dirent.entsize;
                // The root listing ends with a zeroed entry (ino 0, or no entsize at all): that
                // entry maps the uroot folder onto the extraction folder and ends the root phase.
                if (dirent.ino == 0 || ent_size == 0) {
                    extractPaths[ndinode_counter] = extract_path;
                    uroot_reached = false;
                    break;
                }
                ndinode_counter++;
            }
        }

        const char dot = decompressedData[0x10];
        const std::string_view dotdot(&decompressedData[0x28], 2);
        if (dot == '.' && dotdot == "..") {
            dinode_reached = true;
        }

        // Get folder and file names.
        bool end_reached = false;
        if (dinode_reached) {
            for (int j = 0; j < 0x10000; j += ent_size) {
                Dirent dirent;
                std::memcpy(&dirent, &decompressedData[j], sizeof(dirent));
                if (dirent.ino == 0 || dirent.entsize == 0) {
                    break;
                }

                ent_size = dirent.entsize;
                auto& table = fsTable.emplace_back();
                table.name = std::string(dirent.name, std::min<s32>(dirent.namelen, 511));
                table.inode = dirent.ino;
                table.type = dirent.type;
                if (trace) {
                    std::fprintf(stderr, "pfs:   dirent ino %u type %u '%s'%s\n", dirent.ino, dirent.type,
                                 table.name.c_str(),
                                 (table.inode < iNodeBuf.size())
                                     ? fmt::format(" loc {} blocks {} size {}", iNodeBuf[table.inode].loc,
                                                   iNodeBuf[table.inode].Blocks, iNodeBuf[table.inode].Size).c_str()
                                     : "");
                }

                if (table.type == PFS_CURRENT_DIR) {
                    auto it = extractPaths.find(table.inode);
                    current_dir = it != extractPaths.end() ? it->second : extract_path;
                }
                extractPaths[table.inode] = current_dir / std::filesystem::path(table.name);

                if (table.type == PFS_FILE || table.type == PFS_DIR) {
                    if (table.type == PFS_DIR) {
                        std::filesystem::create_directories(extractPaths[table.inode], ec);
                    }
                    if (table.type == PFS_FILE && table.inode < iNodeBuf.size() &&
                        iNodeBuf[table.inode].Blocks != 0) {
                        first_data_block = std::min(first_data_block, static_cast<u64>(iNodeBuf[table.inode].loc));
                    }
                    ndinode_counter++;
                    if ((ndinode_counter + 1) >= static_cast<int>(ndinode)) { // 1 for the root
                        end_reached = true;
                    }
                }
            }
            if (end_reached) {
                break;
            }
        }
    }
    if (iNodeBuf.size() < ndinode) {
        failreason = fmt::format("PFS metadata incomplete ({} of {} inodes read)", iNodeBuf.size(), ndinode);
        return false;
    }
    return true;
}

bool PKG::ExtractFiles(int index, std::string& failreason, const Progress& progress) {
    const u32 inode_number = fsTable[index].inode;
    const u32 inode_type = fsTable[index].type;
    if (inode_type != PFS_FILE) {
        return true;
    }
    if (inode_number >= iNodeBuf.size()) {
        failreason = "Inode out of range for " + fsTable[index].name;
        return false;
    }
    const u64 sector_loc = iNodeBuf[inode_number].loc;
    const u64 nblocks = iNodeBuf[inode_number].Blocks;
    const u64 bsize = static_cast<u64>(iNodeBuf[inode_number].Size);
    if (sector_loc + nblocks + 1 > sectorMap.size()) {
        failreason = "Sector map out of range for " + fsTable[index].name;
        return false;
    }

    const auto out_path = extractPaths[inode_number];
    std::error_code ec;
    std::filesystem::create_directories(out_path.parent_path(), ec);
    Common::FS::IOFile inflated(out_path, Common::FS::FileAccessMode::Create);
    if (!inflated.IsOpen()) {
        failreason = "Cannot write " + out_path.string();
        return false;
    }
    Common::FS::IOFile pkgFile(pkgpath, Common::FS::FileAccessMode::Read);
    if (!pkgFile.IsOpen()) {
        failreason = "Cannot reopen the package";
        return false;
    }

    u64 size_decompressed = 0;
    std::vector<char> compressedData;
    std::vector<char> decompressedData(0x10000);
    const u64 pfsc_buf_size = 0x11000; // extra 0x1000 for the sector alignment
    std::vector<u8> pfsc(pfsc_buf_size);
    std::vector<u8> pfs_decrypted(pfsc_buf_size);

    for (u64 j = 0; j < nblocks; j++) {
        const u64 sectorOffset = sectorMap[sector_loc + j]; // offset into the PFSC image
        const u64 sectorSize = sectorMap[sector_loc + j + 1] - sectorOffset;
        const u64 fileOffset = pkgheader.pfs_image_offset + pfsc_offset + sectorOffset;
        const u64 currentSector1 = (pfsc_offset + sectorOffset) / 0x1000; // XTS block size
        const u64 sectorOffsetMask = (sectorOffset + pfsc_offset) & ~u64{0xFFF};
        const u64 previousData = (sectorOffset + pfsc_offset) - sectorOffsetMask;
        if (sectorSize > 0x10000 || previousData + sectorSize > pfsc_buf_size) {
            failreason = "Corrupt PFS sector in " + fsTable[index].name;
            return false;
        }

        pkgFile.Seek(fileOffset - previousData);
        pkgFile.Read(pfsc);
        crypto.decryptPFS(dataKey, tweakKey, pfsc, pfs_decrypted, currentSector1);

        compressedData.resize(sectorSize);
        std::memcpy(compressedData.data(), pfs_decrypted.data() + previousData, sectorSize);

        if (sectorSize == 0x10000) { // Uncompressed data
            std::memcpy(decompressedData.data(), compressedData.data(), 0x10000);
        } else if (!DecompressPFSC(compressedData, decompressedData)) {
            failreason = "zlib failed in " + fsTable[index].name;
            return false;
        }

        size_decompressed += 0x10000;
        u64 write_size = decompressedData.size();
        if (j == nblocks - 1 && size_decompressed > bsize) {
            // Drop the zero padding at the end of the file.
            write_size = decompressedData.size() - (size_decompressed - bsize);
        }
        if (inflated.WriteRaw<u8>(decompressedData.data(), write_size) != write_size) {
            failreason = "Write failed for " + fsTable[index].name + " (disk full?)";
            return false;
        }
        if (progress && (j % 64 == 63 || j == nblocks - 1)) {
            progress(std::min(size_decompressed, bsize));
        }
    }
    if (nblocks == 0 && progress) {
        progress(0);
    }
    return true;
}
