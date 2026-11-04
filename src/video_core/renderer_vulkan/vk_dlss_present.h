// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <chrono>
#include <optional>

#include "video_core/renderer_vulkan/vk_common.h"
#include "video_core/renderer_vulkan/vk_dlss_ngx.h"
#include "video_core/texture_cache/image.h"

namespace Vulkan {

class Instance;
class Runtime;

// nhlport: presenter-stage DLSS upscaler. When the NGX context is available it replaces
// the FSR upscale: the game's final frame (render resolution) is fed to the DLSS
// SuperResolution feature together with a static depth plane and zeroed motion vectors,
// and the DLSS output at display resolution continues into the post-process pass.
// Motion vectors from the game can be added later; the resource plumbing is identical.
class DlssPresentUpscaler {
public:
    explicit DlssPresentUpscaler(const Instance& instance, Runtime& runtime);
    ~DlssPresentUpscaler();

    bool Active() const {
        return active;
    }

    // Upscales the game's frame (render resolution) to frame_size. The game image is
    // blitted to an RGBA8 intermediate first: NGX requires plain 8/16-bit color I/O,
    // while the game outputs A2R10G10B10. Returns the output view at frame resolution,
    // or VK_NULL_HANDLE when DLSS did not run (the caller then falls back to FSR).
    vk::ImageView Render(vk::CommandBuffer cmdbuf, VideoCore::Image& game_image,
                         vk::Format view_format, vk::Extent2D game_size,
                         vk::Extent2D frame_size, bool is_hdr);

    // The GPU must have finished all recorded work before this is called.
    void ReleaseFeature();

private:
    struct Owned {
        VideoCore::UniqueImage image{};
        vk::ImageView view{};        // storage/sample view handed to NGX (identity swizzle)
        vk::ImageView present_view{}; // sRGB view for the post-process pass (output only)
        vk::Format format{vk::Format::eUndefined};
        vk::Extent2D extent{};
    };
    void DestroyOwned(Owned& owned);

    void EnsureInputs(vk::CommandBuffer cmdbuf, vk::Extent2D game_size);
    void EnsureOutput(vk::CommandBuffer cmdbuf, vk::Extent2D frame_size, vk::Format view_format);

    const Instance& instance;
    Runtime& runtime;
    DlssNgx* dlss{};
    bool active{false};

    Owned color_in{};
    Owned depth{};
    Owned motion{};
    Owned output{};
    vk::ImageLayout output_layout{vk::ImageLayout::eUndefined};
    vk::Extent2D feature_input{};
    vk::Extent2D feature_output{};
    bool feature_valid{false};
    bool first_evaluate{true};
    std::chrono::steady_clock::time_point last_evaluate{};
};

} // namespace Vulkan
