// SPDX-FileCopyrightText: Copyright 2026 nhlport
// SPDX-License-Identifier: GPL-2.0-or-later

#include <iostream>
#include <string>
#include <fmt/format.h>
#include "core/file_format/pkg.h"
#include "core/file_format/pkg_extract.h"
#include "core/file_format/psf.h"

namespace PkgExtract {

namespace {

int Fail(const std::string& reason) {
    std::cout << "error: " << (reason.empty() ? "unknown error" : reason) << std::endl;
    return 1;
}

void Progress(u32 files_done, u32 files_total, u64 bytes_done, u64 bytes_total,
              const std::string& name) {
    std::cout << fmt::format("progress {} {} {} {} {}", files_done, files_total, bytes_done,
                             bytes_total, name)
              << std::endl;
}

} // namespace

int Run(const std::filesystem::path& pkg_path, std::optional<std::filesystem::path> target_dir,
        bool info_only) {
    PKG pkg;
    std::string reason;
    if (!pkg.Open(pkg_path, reason)) {
        return Fail(reason);
    }

    std::string title_id{pkg.GetTitleID()};
    std::string title, app_ver, version, category;
    PSF psf;
    if (!pkg.sfo.empty() && psf.Open(pkg.sfo)) {
        title = std::string(psf.GetString("TITLE").value_or(""));
        app_ver = std::string(psf.GetString("APP_VER").value_or(""));
        version = std::string(psf.GetString("VERSION").value_or(""));
        category = std::string(psf.GetString("CATEGORY").value_or(""));
        title_id = std::string(psf.GetString("TITLE_ID").value_or(title_id));
    }
    const std::string kind = category == "gd"   ? "base"
                             : category == "gp" ? "patch"
                             : category == "ac" ? "dlc"
                                                : "unknown";
    std::cout << fmt::format("pkg: content_id={} title_id={} category={} kind={} app_ver={} "
                             "version={} flags=\"{}\" size={} title=\"{}\"",
                             pkg.GetContentID(), title_id, category, kind, app_ver, version,
                             pkg.GetPkgFlags(), pkg.GetPkgSize(), title)
              << std::endl;
    if (kind != "base" && kind != "patch") {
        return Fail("only the base game (category gd) and its updates (gp) can be installed");
    }

    std::error_code ec;
    std::filesystem::path dir;
    // --extract-to names the game folder; updates go next to it into <game folder>-UPDATE,
    // the overlay the emulator mounts over /app0.
    const auto game_dir = target_dir ? *target_dir : std::filesystem::current_path(ec) / "game";
    dir = kind == "base" ? game_dir : std::filesystem::path(game_dir.string() + "-UPDATE");
    std::cout << "target: " << dir.string() << std::endl;
    if (info_only) {
        std::cout << "done" << std::endl;
        return 0;
    }
    std::filesystem::create_directories(dir, ec);
    if (ec) {
        return Fail("cannot create " + dir.string());
    }

    if (!pkg.Extract(pkg_path, dir, reason)) {
        return Fail(reason);
    }

    const u32 n = pkg.GetNumberOfFiles();
    u32 files_total = 0;
    u64 bytes_total = 0;
    for (u32 i = 0; i < n; i++) {
        if (pkg.IsFile(i)) {
            files_total++;
            bytes_total += pkg.GetFileSize(i);
        }
    }
    std::cout << fmt::format("files: {} bytes: {}", files_total, bytes_total) << std::endl;

    u32 files_done = 0;
    u64 bytes_done = 0;
    for (u32 i = 0; i < n; i++) {
        if (!pkg.IsFile(i)) {
            continue;
        }
        const std::string& name = pkg.GetFileName(i);
        const u64 file_size = pkg.GetFileSize(i);
        Progress(files_done, files_total, bytes_done, bytes_total, name);
        const bool ok = pkg.ExtractFiles(static_cast<int>(i), reason, [&](u64 done) {
            Progress(files_done, files_total, bytes_done + std::min(done, file_size), bytes_total,
                     name);
        });
        if (!ok) {
            return Fail(reason);
        }
        files_done++;
        bytes_done += file_size;
    }
    Progress(files_total, files_total, bytes_total, bytes_total, "-");
    std::cout << "done" << std::endl;
    return 0;
}

} // namespace PkgExtract
