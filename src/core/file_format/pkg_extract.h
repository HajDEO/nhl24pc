// SPDX-FileCopyrightText: Copyright 2026 nhlport
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <filesystem>
#include <optional>

namespace PkgExtract {

/// nhl24.exe --extract-pkg <file> [--extract-to <game folder>]: installs a PS4 package (base
/// game -> the game folder, default <exe dir>/game; update -> <game folder>-UPDATE) and reports on stdout,
/// one line each: "pkg: ...", "target: <dir>", "files: <n> bytes: <total>",
/// "progress <files done> <files> <bytes done> <bytes> <name>", "done" or "error: <reason>".
/// Returns the process exit code.
/// info_only (--pkg-info): print the "pkg:" and "target:" lines and exit without writing.
int Run(const std::filesystem::path& pkg_path, std::optional<std::filesystem::path> target_dir,
        bool info_only);

} // namespace PkgExtract
