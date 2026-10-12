// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <filesystem>
#include <string>
#include "common/types.h"

namespace VideoCore {

/// Loads renderdoc dynamic library module.
void LoadRenderDoc();

/// Begins a capture if a renderdoc instance is attached.
void StartCapture();

/// Ends current renderdoc capture.
void EndCapture();

/// Triggers capturing process.
void TriggerCapture();

/// nhlport: capture when a file named "trigger" appears in the captures directory (removed
/// once consumed). Lets a script take a RenderDoc capture without keyboard focus.
void PollCaptureTrigger();

/// Sets output directory for captures
void SetOutputDir(const std::filesystem::path& path, const std::string& prefix);

/// Returns true when RenderDoc API was loaded and is usable.
bool IsRenderDocLoaded();

enum class ScreenshotRequest : u32 {
    None = 0,
    GameOnly = 1,
    WithOverlays = 2,
};

struct ScreenshotRequests {
    u32 game_only_count{};
    u32 with_overlays_count{};
};

/// Queues an in-emulator screenshot request to be consumed by the presenter.
void RequestScreenshot(ScreenshotRequest request);

/// nhlport: true while a game-only screenshot request is still waiting to be consumed.
bool HasPendingGameOnlyScreenshot();

/// Atomically consumes and returns pending "game only" screenshot request counter.
u32 ConsumeGameOnlyScreenshotRequests();

/// Atomically consumes and returns pending "with overlays" screenshot request counter.
u32 ConsumeWithOverlaysScreenshotRequests();

/// Atomically consumes and returns pending screenshot request counters.
ScreenshotRequests ConsumeScreenshotRequests();

} // namespace VideoCore
