// SPDX-FileCopyrightText: Copyright 2024-2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <codecvt>
#include <fstream>
#include <sstream>
#include <string>
#include <nlohmann/json.hpp>
#include <pugixml.hpp>
#include <windows.h>
#include "common/elf_info.h"
#include "common/logging/log.h"
#include "common/path_util.h"
#include "core/emulator_settings.h"
#include "core/libraries/videoout/video_out.h"
#include "core/emulator_state.h"
#include "core/file_format/psf.h"
#include "memory_patcher.h"

namespace MemoryPatcher {

EXPORT uintptr_t g_eboot_address;
uint64_t g_eboot_image_size;
std::string g_eboot_name;
std::string g_game_serial;
std::string patch_file;
bool patches_applied = false;
std::vector<patchInfo> pending_patches;

std::string toHex(u64 value, size_t byteSize) {
    std::stringstream ss;
    ss << std::hex << std::setfill('0') << std::setw(byteSize * 2) << value;
    return ss.str();
}

static bool isHexSym(const std::string& s) {
    return (s.size() >= 2 && (s[0] == '$' || s[0] == '#'));
}

static bool isHex0x(const std::string& s) {
    return (s.size() >= 3 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X'));
}

// possible prefixes == (s == `0x` || s == `#` || s == `$`) will be interpet as hex value, else is
// decimal
static int convertNumBase(const std::string& s) {
    return (isHex0x(s) || isHexSym(s)) ? 16 : 10;
}

std::string convertValueToHex(const std::string type, const std::string valueStr) {
    std::string result;

    if (type == "byte") {
        const u32 value = std::stoul(valueStr, nullptr, convertNumBase(valueStr));
        result = toHex(value, 1);
    } else if (type == "bytes16") {
        const u32 value = std::stoul(valueStr, nullptr, convertNumBase(valueStr));
        result = toHex(value, 2);
    } else if (type == "bytes32") {
        const u32 value = std::stoul(valueStr, nullptr, convertNumBase(valueStr));
        result = toHex(value, 4);
    } else if (type == "bytes64") {
        const u64 value = std::stoull(valueStr, nullptr, convertNumBase(valueStr));
        result = toHex(value, 8);
    } else if (type == "float32") {
        union {
            float f;
            uint32_t i;
        } floatUnion;
        floatUnion.f = std::stof(valueStr);
        result = toHex(std::byteswap(floatUnion.i), sizeof(floatUnion.i));
    } else if (type == "float64") {
        union {
            double d;
            uint64_t i;
        } doubleUnion;
        doubleUnion.d = std::stod(valueStr);
        result = toHex(std::byteswap(doubleUnion.i), sizeof(doubleUnion.i));
    } else if (type == "utf8") {
        std::vector<unsigned char> byteArray =
            std::vector<unsigned char>(valueStr.begin(), valueStr.end());
        byteArray.push_back('\0');
        std::stringstream ss;
        for (unsigned char c : byteArray) {
            ss << std::hex << std::setfill('0') << std::setw(2) << static_cast<int>(c);
        }
        result = ss.str();
    } else if (type == "utf16") {
        std::wstring wide_str(valueStr.size(), L'\0');
        std::mbstowcs(&wide_str[0], valueStr.c_str(), valueStr.size());
        wide_str.resize(std::wcslen(wide_str.c_str()));

        std::u16string valueStringU16;

        for (wchar_t wc : wide_str) {
            if (wc <= 0xFFFF) {
                valueStringU16.push_back(static_cast<char16_t>(wc));
            } else {
                wc -= 0x10000;
                valueStringU16.push_back(static_cast<char16_t>(0xD800 | (wc >> 10)));
                valueStringU16.push_back(static_cast<char16_t>(0xDC00 | (wc & 0x3FF)));
            }
        }

        std::vector<unsigned char> byteArray;
        // convert to little endian
        for (char16_t ch : valueStringU16) {
            unsigned char low_byte = static_cast<unsigned char>(ch & 0x00FF);
            unsigned char high_byte = static_cast<unsigned char>((ch >> 8) & 0x00FF);

            byteArray.push_back(low_byte);
            byteArray.push_back(high_byte);
        }
        byteArray.push_back('\0');
        byteArray.push_back('\0');
        std::stringstream ss;

        for (unsigned char ch : byteArray) {
            ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(ch);
        }
        result = ss.str();
    } else if (type == "bytes") {
        result = valueStr;
    } else if (type == "mask" || type == "mask_jump32") {
        result = valueStr;
    } else {
        LOG_INFO(Loader, "Error applying Patch, unknown type: {}", type);
    }
    return result;
}

void ApplyPendingPatches();

void ApplyPatchesFromXML(std::filesystem::path path) {
    pugi::xml_document doc;
    pugi::xml_parse_result result = doc.load_file(path.c_str());

    auto* param_sfo = Common::Singleton<PSF>::Instance();
    auto app_version = param_sfo->GetString("APP_VER").value_or("Unknown version");

    if (result) {
        auto patchXML = doc.child("Patch");
        for (pugi::xml_node_iterator it = patchXML.children().begin();
             it != patchXML.children().end(); ++it) {

            if (std::string(it->name()) == "Metadata") {
                if (std::string(it->attribute("isEnabled").value()) == "true") {
                    std::string currentPatchName = it->attribute("Name").value();
                    std::string metadataAppVer = it->attribute("AppVer").value();
                    std::string metadataAppElf = it->attribute("AppElf").value();
                    bool versionMatches = metadataAppVer == app_version;

                    if (metadataAppElf != g_eboot_name)
                        continue;

                    auto patchList = it->first_child();
                    for (pugi::xml_node_iterator patchLineIt = patchList.children().begin();
                         patchLineIt != patchList.children().end(); ++patchLineIt) {

                        std::string type = patchLineIt->attribute("Type").value();
                        if (!versionMatches && type != "mask" && type != "mask_jump32")
                            continue;

                        std::string address = patchLineIt->attribute("Address").value();
                        std::string patchValue = patchLineIt->attribute("Value").value();
                        std::string maskOffsetStr = patchLineIt->attribute("Offset").value();
                        std::string targetStr = "";
                        std::string sizeStr = "";
                        if (type == "mask_jump32") {
                            targetStr = patchLineIt->attribute("Target").value();
                            sizeStr = patchLineIt->attribute("Size").value();
                        } else {
                            try {
                                patchValue = convertValueToHex(type, patchValue);
                            } catch (std::exception& e) {
                                ASSERT_MSG(false,
                                           "Failed to parse patch value \"{}\" for \"{}\" in "
                                           "patch \"{}\", error: \"{}\"\n"
                                           "If the patch was working on earlier versions, then it "
                                           "was using a format that shadPS4 handled incorrectly, "
                                           "and the patch should instead be fixed.",
                                           patchValue, address, currentPatchName, e.what());
                            }
                        }

                        bool littleEndian = false;
                        if (type == "bytes16" || type == "bytes32" || type == "bytes64") {
                            littleEndian = true;
                        }

                        MemoryPatcher::PatchMask patchMask = MemoryPatcher::PatchMask::None;
                        int maskOffsetValue = 0;

                        if (type == "mask")
                            patchMask = MemoryPatcher::PatchMask::Mask;

                        if (type == "mask_jump32")
                            patchMask = MemoryPatcher::PatchMask::Mask_Jump32;

                        if ((type == "mask" || type == "mask_jump32") && !maskOffsetStr.empty()) {
                            maskOffsetValue = std::stoi(maskOffsetStr, 0, 10);
                        }

                        const patchInfo patch = {
                            .gameSerial = "*",
                            .modNameStr = currentPatchName,
                            .offsetStr = address,
                            .valueStr = patchValue,
                            .targetStr = targetStr,
                            .sizeStr = sizeStr,
                            .littleEndian = littleEndian,
                            .patchMask = patchMask,
                            .maskOffset = maskOffsetValue,
                        };
                        MemoryPatcher::PatchMemory(patch);
                    }
                }
            }
        }
    } else {
        LOG_ERROR(Loader, "Could not parse patch XML: {}", result.description());
    }
}

// nhlport: patch the game's base-mode resolution table entry to the configured height.
// NHL 24 holds a 16-byte-stride resolution table in eboot - {1920,1080} (base mode)
// followed 16 bytes later by {3840,2160} (Neo mode) - and fills its VideoOut buffer
// attribute from these values, so overwriting the base entry changes the whole render
// resolution. The module's load base varies per run, so the table is located by its
// unique signature (the 3840x2160 Neo entry 16 bytes after the base entry) inside the
// committed regions of the module rather than by a hardcoded offset. The signature IS
// the safety check: a future game update with different stock values simply won't match.
void ApplyRenderResolutionOverride() {
    if (g_eboot_address == 0) {
        return;
    }
    const int height = EmulatorSettings.GetRenderResolution();
    if (height != 1080 && height != 1440 && height != 2160) {
        return;
    }
    constexpr u64 ScanSpan = 0xE800000; // covers the module plus mapping slack
    constexpr std::array<u8, 8> BaseEntry{0x80, 0x07, 0x00, 0x00, 0x38, 0x04, 0x00, 0x00};
    constexpr std::array<u8, 8> NeoEntry{0x00, 0x0F, 0x00, 0x00, 0x70, 0x08, 0x00, 0x00};

    const u8* found = nullptr;
    uintptr_t addr = g_eboot_address;
    const uintptr_t scan_end = g_eboot_address + ScanSpan;
    while (addr < scan_end) {
        MEMORY_BASIC_INFORMATION mbi{};
        if (VirtualQuery(reinterpret_cast<void*>(addr), &mbi, sizeof(mbi)) == 0) {
            break;
        }
        const uintptr_t region_end = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
        if (mbi.State == MEM_COMMIT && region_end > g_eboot_address) {
            const u8* begin = reinterpret_cast<const u8*>(
                std::max(addr, g_eboot_address));
            const u8* stop = reinterpret_cast<const u8*>(std::min(region_end, scan_end));
            for (const u8* p = begin; p + 32 <= stop; ++p) {
                if (std::memcmp(p, BaseEntry.data(), BaseEntry.size()) == 0 &&
                    std::memcmp(p + 16, NeoEntry.data(), NeoEntry.size()) == 0) {
                    found = p;
                    break;
                }
            }
            if (found) {
                break;
            }
        }
        addr = region_end;
    }
    if (!found) {
        LOG_WARNING(Loader,
                    "Render resolution override: resolution table signature not found; "
                    "skipping (game update?)");
        return;
    }

    const u32 width = u32(height) * 16 / 9;
    const std::array<u8, 8> patched = {
        u8(width & 0xff), u8((width >> 8) & 0xff), 0, 0,
        u8(height & 0xff), u8((height >> 8) & 0xff), 0, 0};
    void* target = const_cast<u8*>(found);
    DWORD old_protect = 0;
    // nhlport: the write can straddle a page boundary; unprotect through its end.
    const size_t span = (reinterpret_cast<uintptr_t>(target) & 0xFFF) + patched.size();
    void* page_start = reinterpret_cast<void*>(
        reinterpret_cast<uintptr_t>(target) & ~uintptr_t(0xFFF));
    if (VirtualProtect(page_start, span, PAGE_EXECUTE_READWRITE, &old_protect) == 0) {
        LOG_ERROR(Loader, "Render resolution override: VirtualProtect failed at {:#x} (error {})",
                  reinterpret_cast<uintptr_t>(target), GetLastError());
        return;
    }
    std::memcpy(target, patched.data(), patched.size());
    VirtualProtect(page_start, span, old_protect, &old_protect);
    // nhlport: keep the reported VideoOut resolution consistent with the patched
    // buffers - a mismatch (game told 1080p while its buffers are 1440p) crashes the
    // engine at gameplay load.
    Libraries::VideoOut::SetInternalResolution(width, height);
    LOG_INFO(Loader, "Render resolution override: base mode now renders {}x{}", width, height);
}

void OnGameLoaded() {
    std::filesystem::path patch_dir = Common::FS::GetUserPath(Common::FS::PathType::PatchesDir);
    if (!patch_file.empty()) {

        auto file_path = (patch_dir / patch_file).native();
        if (std::filesystem::exists(patch_file)) {
            ApplyPatchesFromXML(patch_file);
        } else {
            ApplyPatchesFromXML(file_path);
        }
    } else if (EmulatorState::GetInstance()->IsAutoPatchesLoadEnabled()) {
        for (auto const& repo : std::filesystem::directory_iterator(patch_dir)) {
            if (!repo.is_directory()) {
                continue;
            }

            const auto files_json_path = repo.path() / "files.json";

            if (!std::filesystem::is_regular_file(files_json_path)) {
                LOG_ERROR(Loader, "The folder '{}' does not contain the files.json file.",
                          repo.path().filename().string());
                continue;
            }

            std::ifstream json_file{files_json_path};
            nlohmann::json available_patches;

            if (!json_file.is_open()) {
                LOG_ERROR(Loader, "Could not open patch index: {}", files_json_path.string());
                continue;
            }

            try {
                available_patches = nlohmann::json::parse(json_file);
            } catch (const nlohmann::json::parse_error& e) {
                LOG_ERROR(Loader, "Could not parse patch index {}: {}", files_json_path.string(),
                          e.what());
                continue;
            }

            std::filesystem::path game_patch_file;
            for (auto const& [filename, serials] : available_patches.items()) {
                if (std::find(serials.begin(), serials.end(), g_game_serial) != serials.end()) {
                    game_patch_file = repo.path() / filename;
                    break;
                }
            }
            if (std::filesystem::exists(game_patch_file)) {
                ApplyPatchesFromXML(game_patch_file);
            }
        }
    }
    ApplyPendingPatches();
    ApplyRenderResolutionOverride();
}

void AddPatchToQueue(const patchInfo& patchToAdd) {
    if (patches_applied) {
        PatchMemory(patchToAdd);
        return;
    }
    pending_patches.push_back(patchToAdd);
}

void ApplyPendingPatches() {
    patches_applied = true;
    for (size_t i = 0; i < pending_patches.size(); ++i) {
        const patchInfo& currentPatch = pending_patches[i];

        if (currentPatch.gameSerial != "*" && currentPatch.gameSerial != g_game_serial)
            continue;

        PatchMemory(currentPatch);
    }

    pending_patches.clear();
}

void PatchMemory(const patchInfo& patch) {
    // Send a request to modify the process memory.
    void* cheatAddress = nullptr;

    if (patch.patchMask == PatchMask::None) {
        if (patch.isOffset) {
            cheatAddress =
                reinterpret_cast<void*>(g_eboot_address + std::stoi(patch.offsetStr, 0, 16));
        } else {
            cheatAddress = reinterpret_cast<void*>(g_eboot_address +
                                                   (std::stoi(patch.offsetStr, 0, 16) - 0x400000));
        }
    }

    if (patch.patchMask == PatchMask::Mask) {
        cheatAddress = reinterpret_cast<void*>(PatternScan(patch.offsetStr) + patch.maskOffset);
    }

    if (patch.patchMask == PatchMask::Mask_Jump32) {
        int jumpSize = std::stoi(patch.sizeStr);

        constexpr int MAX_PATTERN_LENGTH = 256;
        if (jumpSize < 5) {
            LOG_ERROR(Loader, "Jump size must be at least 5 bytes");
            return;
        }
        if (jumpSize > MAX_PATTERN_LENGTH) {
            LOG_ERROR(Loader, "Jump size must be no more than {} bytes.", MAX_PATTERN_LENGTH);
            return;
        }

        // Find the base address using "Address"
        uintptr_t baseAddress = PatternScan(patch.offsetStr);
        if (baseAddress == 0) {
            LOG_ERROR(Loader, "PatternScan failed for mask_jump32 with pattern: {}",
                      patch.offsetStr);
            return;
        }
        uintptr_t patchAddress = baseAddress + patch.maskOffset;

        // Fills the original region (jumpSize bytes) with NOPs
        std::vector<u8> nopBytes(jumpSize, 0x90);
        std::memcpy(reinterpret_cast<void*>(patchAddress), nopBytes.data(), nopBytes.size());

        // Use "Target" to locate the start of the code cave
        uintptr_t jump_target = PatternScan(patch.targetStr);
        if (jump_target == 0) {
            LOG_ERROR(Loader, "PatternScan failed to Target with pattern: {}", patch.targetStr);
            return;
        }

        // Converts the Value attribute to a byte array (payload)
        std::vector<u8> payload;
        for (size_t i = 0; i < patch.valueStr.length(); i += 2) {
            std::string tempStr = patch.valueStr.substr(i, 2);
            const char* byteStr = tempStr.c_str();
            char* endPtr;
            unsigned int byteVal = std::strtoul(byteStr, &endPtr, 16);

            if (endPtr != byteStr + 2) {
                LOG_ERROR(Loader, "Invalid byte in Value: {}", patch.valueStr.substr(i, 2));
                return;
            }
            payload.push_back(static_cast<u8>(byteVal));
        }

        // Calculates the end of the code cave (where the return jump will be inserted)
        uintptr_t code_cave_end = jump_target + payload.size();

        // Write the payload to the code cave, from jump_target
        std::memcpy(reinterpret_cast<void*>(jump_target), payload.data(), payload.size());

        // Inserts the initial jump in the original region to divert to the code cave
        u8 jumpInstruction[5];
        jumpInstruction[0] = 0xE9;
        s32 relJump = static_cast<s32>(jump_target - patchAddress - 5);
        std::memcpy(&jumpInstruction[1], &relJump, sizeof(relJump));
        std::memcpy(reinterpret_cast<void*>(patchAddress), jumpInstruction,
                    sizeof(jumpInstruction));

        // Inserts jump back at the end of the code cave to resume execution after patching
        u8 jumpBack[5];
        jumpBack[0] = 0xE9;
        // Calculates the relative offset to return to the instruction immediately following the
        // overwritten region
        s32 target_return = static_cast<s32>((patchAddress + jumpSize) - (code_cave_end + 5));
        std::memcpy(&jumpBack[1], &target_return, sizeof(target_return));
        std::memcpy(reinterpret_cast<void*>(code_cave_end), jumpBack, sizeof(jumpBack));

        LOG_INFO(Loader,
                 "Applied Patch mask_jump32: {}, PatchAddress: {:#x}, JumpTarget: {:#x}, "
                 "CodeCaveEnd: {:#x}, JumpSize: {}",
                 patch.modNameStr, patchAddress, jump_target, code_cave_end, jumpSize);
        return;
    }

    if (cheatAddress == nullptr) {
        LOG_ERROR(Loader, "Failed to get address for patch {}", patch.modNameStr);
        return;
    }

    std::vector<unsigned char> bytePatch;

    for (size_t i = 0; i < patch.valueStr.length(); i += 2) {
        unsigned char byte = static_cast<unsigned char>(
            std::strtol(patch.valueStr.substr(i, 2).c_str(), nullptr, 16));

        bytePatch.push_back(byte);
    }

    if (patch.littleEndian) {
        std::reverse(bytePatch.begin(), bytePatch.end());
    }

    // nhlport: guest code pages can be mapped execute-only at patch time (some pages are
    // re-protected after the static CPU patches); unprotect the target range so patches
    // apply regardless of which page they land on.
    DWORD old_protect = 0;
    void* page_start = reinterpret_cast<void*>(
        reinterpret_cast<uintptr_t>(cheatAddress) & ~uintptr_t(0xFFF));
    const size_t span = bytePatch.size() + (reinterpret_cast<uintptr_t>(cheatAddress) -
                                            reinterpret_cast<uintptr_t>(page_start));
    if (VirtualProtect(page_start, span, PAGE_EXECUTE_READWRITE, &old_protect) == 0) {
        LOG_ERROR(Loader, "Failed to unprotect patch target {:#x} (span {}, error {})",
                  reinterpret_cast<uintptr_t>(cheatAddress), span, GetLastError());
        return;
    }
    std::memcpy(cheatAddress, bytePatch.data(), bytePatch.size());
    VirtualProtect(page_start, span, old_protect, &old_protect);

    LOG_INFO(Loader, "Applied patch: {}, Offset: {:#x}, Value: {}", patch.modNameStr,
             (uintptr_t)cheatAddress, patch.valueStr);
}

static std::vector<int32_t> PatternToByte(const std::string& pattern) {
    std::vector<int32_t> bytes;
    const char* start = pattern.data();
    const char* end = start + pattern.size();

    for (const char* current = start; current < end; ++current) {
        if (*current == '?') {
            ++current;
            if (*current == '?')
                ++current;
            bytes.push_back(-1);
        } else {
            bytes.push_back(strtoul(current, const_cast<char**>(&current), 16));
        }
    }

    return bytes;
}

uintptr_t PatternScan(const std::string& signature) {
    std::vector<int32_t> patternBytes = PatternToByte(signature);
    const auto scanBytes = static_cast<uint8_t*>((void*)g_eboot_address);

    const int32_t* sigPtr = patternBytes.data();
    const size_t sigSize = patternBytes.size();

    uint32_t foundResults = 0;
    for (uint32_t i = 0; i < g_eboot_image_size - sigSize; ++i) {
        bool found = true;
        for (uint32_t j = 0; j < sigSize; ++j) {
            if (scanBytes[i + j] != sigPtr[j] && sigPtr[j] != -1) {
                found = false;
                break;
            }
        }

        if (found) {
            foundResults++;
            return reinterpret_cast<uintptr_t>(&scanBytes[i]);
        }
    }

    return 0;
}

} // namespace MemoryPatcher
