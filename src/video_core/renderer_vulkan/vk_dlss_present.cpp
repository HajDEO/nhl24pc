// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "video_core/renderer_vulkan/vk_dlss_present.h"

#include <algorithm>
#include <cstdlib>
#include <string_view>

#include "common/logging/log.h"
#include "core/emulator_settings.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_runtime.h"
#include "video_core/texture_cache/image.h"

namespace Vulkan {

namespace {

// NGX writes shader-RGBA into the output storage image, so the output memory must be
// RGBA-ordered regardless of the game's byte order. The view then uses the RGBA-order
// equivalent of the game's view format (same compatibility class, mutable format flag).
vk::Format OutputViewFormatForGameView(vk::Format view_format) {
    switch (view_format) {
    case vk::Format::eB8G8R8A8Srgb:
    case vk::Format::eR8G8B8A8Srgb:
        return vk::Format::eR8G8B8A8Srgb;
    case vk::Format::eB8G8R8A8Unorm:
    case vk::Format::eR8G8B8A8Unorm:
        return vk::Format::eR8G8B8A8Unorm;
    default:
        return view_format;
    }
}

} // namespace

DlssPresentUpscaler::DlssPresentUpscaler(const Instance& instance, Runtime& runtime)
    : instance{instance}, runtime{runtime} {
    dlss = instance.GetDlssNgx();
    if (!dlss || !dlss->IsAvailable()) {
        return;
    }
    // nhlport: the config setting GPU.dlss_enabled controls the presenter upscaler;
    // NHL_DLSS_PRESENT=0 still forces it off for debugging.
    if (!EmulatorSettings.IsDlssEnabled()) {
        LOG_INFO(Render_Vulkan, "[DLSS] disabled by config (GPU.dlss_enabled=false)");
        return;
    }
    if (const char* env = std::getenv("NHL_DLSS_PRESENT"); env && std::string_view{env} == "0") {
        LOG_WARNING(Render_Vulkan, "[DLSS] NHL_DLSS_PRESENT=0: presenter upscaling disabled");
        return;
    }
    active = true;
    LOG_INFO(Render_Vulkan, "[DLSS] Presenter upscaling enabled");
}

DlssPresentUpscaler::~DlssPresentUpscaler() {
    DestroyOwned(color_in);
    DestroyOwned(depth);
    DestroyOwned(motion);
    DestroyOwned(output);
}

void DlssPresentUpscaler::DestroyOwned(Owned& owned) {
    const vk::Device device = instance.GetDevice();
    if (owned.view) {
        device.destroyImageView(owned.view);
        owned.view = VK_NULL_HANDLE;
    }
    if (owned.present_view) {
        device.destroyImageView(owned.present_view);
        owned.present_view = VK_NULL_HANDLE;
    }
    owned.image.Destroy();
    owned.image = VideoCore::UniqueImage{};
}

void DlssPresentUpscaler::ReleaseFeature() {
    if (feature_valid && dlss) {
        dlss->ReleaseFeatureAfterGpuDrain();
    }
    feature_valid = false;
}

void DlssPresentUpscaler::EnsureInputs(vk::CommandBuffer cmdbuf, vk::Extent2D game_size) {
    if (depth.image && depth.extent == game_size) {
        return;
    }
    DestroyOwned(color_in);
    DestroyOwned(depth);
    DestroyOwned(motion);
    feature_valid = false;

    // RGBA8 intermediate the game frame is blitted into before DLSS consumes it.
    color_in.format = vk::Format::eR8G8B8A8Unorm;
    color_in.extent = game_size;
    color_in.image = VideoCore::UniqueImage{instance.GetDevice(), instance.GetAllocator()};
    color_in.image.Create(vk::ImageCreateInfo{
        .imageType = vk::ImageType::e2D,
        .format = color_in.format,
        .extent = {game_size.width, game_size.height, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = vk::SampleCountFlagBits::e1,
        .tiling = vk::ImageTiling::eOptimal,
        .usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst,
    });
    {
        const auto [view_result, view] = instance.GetDevice().createImageView(
            vk::ImageViewCreateInfo{
                .image = color_in.image,
                .viewType = vk::ImageViewType::e2D,
                .format = color_in.format,
                .subresourceRange{
                    .aspectMask = vk::ImageAspectFlagBits::eColor,
                    .levelCount = 1,
                    .layerCount = 1,
                },
            });
        ASSERT_MSG(view_result == vk::Result::eSuccess,
                   "Failed to create DLSS color view: {}", vk::to_string(view_result));
        color_in.view = view;
    }

    // Static depth plane at the far plane: without game depth there is no reprojection
    // information, which matches the zeroed motion vectors.
    depth.format = vk::Format::eD32Sfloat;
    depth.extent = game_size;
    depth.image = VideoCore::UniqueImage{instance.GetDevice(), instance.GetAllocator()};
    depth.image.Create(vk::ImageCreateInfo{
        .imageType = vk::ImageType::e2D,
        .format = depth.format,
        .extent = {game_size.width, game_size.height, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = vk::SampleCountFlagBits::e1,
        .tiling = vk::ImageTiling::eOptimal,
        .usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst,
    });
    {
        const auto [view_result, view] = instance.GetDevice().createImageView(
            vk::ImageViewCreateInfo{
                .image = depth.image,
                .viewType = vk::ImageViewType::e2D,
                .format = depth.format,
                .subresourceRange{
                    .aspectMask = vk::ImageAspectFlagBits::eDepth,
                    .levelCount = 1,
                    .layerCount = 1,
                },
            });
        ASSERT_MSG(view_result == vk::Result::eSuccess, "Failed to create DLSS depth view: {}",
                   vk::to_string(view_result));
        depth.view = view;
    }

    motion.format = vk::Format::eR16G16Sfloat;
    motion.extent = game_size;
    motion.image = VideoCore::UniqueImage{instance.GetDevice(), instance.GetAllocator()};
    motion.image.Create(vk::ImageCreateInfo{
        .imageType = vk::ImageType::e2D,
        .format = motion.format,
        .extent = {game_size.width, game_size.height, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = vk::SampleCountFlagBits::e1,
        .tiling = vk::ImageTiling::eOptimal,
        .usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst,
    });
    {
        const auto [view_result, view] = instance.GetDevice().createImageView(
            vk::ImageViewCreateInfo{
                .image = motion.image,
                .viewType = vk::ImageViewType::e2D,
                .format = motion.format,
                .subresourceRange{
                    .aspectMask = vk::ImageAspectFlagBits::eColor,
                    .levelCount = 1,
                    .layerCount = 1,
                },
            });
        ASSERT_MSG(view_result == vk::Result::eSuccess, "Failed to create DLSS motion view: {}",
                   vk::to_string(view_result));
        motion.view = view;
    }

    // Clear once (depth = far plane, motion = zero) and leave both in shader-read.
    const vk::ImageSubresourceRange color_range{
        .aspectMask = vk::ImageAspectFlagBits::eColor,
        .levelCount = 1,
        .layerCount = 1,
    };
    const vk::ImageSubresourceRange depth_range{
        .aspectMask = vk::ImageAspectFlagBits::eDepth,
        .levelCount = 1,
        .layerCount = 1,
    };
    const vk::ImageMemoryBarrier2 to_clear[]{
        vk::ImageMemoryBarrier2{
            .srcStageMask = vk::PipelineStageFlagBits2::eTopOfPipe,
            .dstStageMask = vk::PipelineStageFlagBits2::eTransfer,
            .dstAccessMask = vk::AccessFlagBits2::eTransferWrite,
            .oldLayout = vk::ImageLayout::eUndefined,
            .newLayout = vk::ImageLayout::eTransferDstOptimal,
            .image = motion.image,
            .subresourceRange = color_range,
        },
        vk::ImageMemoryBarrier2{
            .srcStageMask = vk::PipelineStageFlagBits2::eTopOfPipe,
            .dstStageMask = vk::PipelineStageFlagBits2::eTransfer,
            .dstAccessMask = vk::AccessFlagBits2::eTransferWrite,
            .oldLayout = vk::ImageLayout::eUndefined,
            .newLayout = vk::ImageLayout::eTransferDstOptimal,
            .image = depth.image,
            .subresourceRange = depth_range,
        },
    };
    cmdbuf.pipelineBarrier2(vk::DependencyInfo{
        .imageMemoryBarrierCount = 2,
        .pImageMemoryBarriers = to_clear,
    });
    const vk::ClearColorValue motion_clear{std::array<float, 4>{0.0f, 0.0f, 0.0f, 1.0f}};
    cmdbuf.clearColorImage(motion.image, vk::ImageLayout::eTransferDstOptimal, motion_clear,
                           color_range);
    const vk::ClearDepthStencilValue depth_clear{.depth = 1.0f, .stencil = 0};
    cmdbuf.clearDepthStencilImage(depth.image, vk::ImageLayout::eTransferDstOptimal, depth_clear,
                                  depth_range);
    const vk::ImageMemoryBarrier2 to_read[]{
        vk::ImageMemoryBarrier2{
            .srcStageMask = vk::PipelineStageFlagBits2::eTransfer,
            .srcAccessMask = vk::AccessFlagBits2::eTransferWrite,
            .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
            .dstAccessMask = vk::AccessFlagBits2::eShaderRead,
            .oldLayout = vk::ImageLayout::eTransferDstOptimal,
            .newLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
            .image = motion.image,
            .subresourceRange = color_range,
        },
        vk::ImageMemoryBarrier2{
            .srcStageMask = vk::PipelineStageFlagBits2::eTransfer,
            .srcAccessMask = vk::AccessFlagBits2::eTransferWrite,
            .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
            .dstAccessMask = vk::AccessFlagBits2::eShaderRead,
            .oldLayout = vk::ImageLayout::eTransferDstOptimal,
            .newLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
            .image = depth.image,
            .subresourceRange = depth_range,
        },
    };
    cmdbuf.pipelineBarrier2(vk::DependencyInfo{
        .imageMemoryBarrierCount = 2,
        .pImageMemoryBarriers = to_read,
    });
}

void DlssPresentUpscaler::EnsureOutput(vk::CommandBuffer cmdbuf, vk::Extent2D frame_size,
                                       vk::Format view_format) {
    const vk::Format storage_format = OutputViewFormatForGameView(view_format);
    if (output.image && output.extent == frame_size && output.format == storage_format) {
        return;
    }
    DestroyOwned(output);
    feature_valid = false;

    output.format = storage_format;
    output.extent = frame_size;
    output.image = VideoCore::UniqueImage{instance.GetDevice(), instance.GetAllocator()};
    output.image.Create(vk::ImageCreateInfo{
        .flags = vk::ImageCreateFlagBits::eMutableFormat,
        .imageType = vk::ImageType::e2D,
        .format = output.format,
        .extent = {frame_size.width, frame_size.height, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = vk::SampleCountFlagBits::e1,
        .tiling = vk::ImageTiling::eOptimal,
        .usage = vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled,
    });
    {
        // Storage view for NGX: identity swizzle, storage-capable (non-sRGB) format.
        const auto [view_result, view] = instance.GetDevice().createImageView(
            vk::ImageViewCreateInfo{
                .image = output.image,
                .viewType = vk::ImageViewType::e2D,
                .format = storage_format,
                .subresourceRange{
                    .aspectMask = vk::ImageAspectFlagBits::eColor,
                    .levelCount = 1,
                    .layerCount = 1,
                },
            });
        ASSERT_MSG(view_result == vk::Result::eSuccess, "Failed to create DLSS output view: {}",
                   vk::to_string(view_result));
        output.view = view;

        // Present view for the post-process pass: raw UNORM (pp_pass performs the
        // sRGB decode itself, matching the FSR path), alpha forced to one.
        const auto [present_result, present] = instance.GetDevice().createImageView(
            vk::ImageViewCreateInfo{
                .image = output.image,
                .viewType = vk::ImageViewType::e2D,
                .format = storage_format,
                .components{
                    .a = vk::ComponentSwizzle::eOne,
                },
                .subresourceRange{
                    .aspectMask = vk::ImageAspectFlagBits::eColor,
                    .levelCount = 1,
                    .layerCount = 1,
                },
            });
        ASSERT_MSG(present_result == vk::Result::eSuccess,
                   "Failed to create DLSS present view: {}", vk::to_string(present_result));
        output.present_view = present;
    }
    output_layout = vk::ImageLayout::eUndefined;
}

vk::ImageView DlssPresentUpscaler::Render(vk::CommandBuffer cmdbuf, VideoCore::Image& game_image,
                                          vk::Format view_format, vk::Extent2D game_size,
                                          vk::Extent2D frame_size, bool is_hdr) {
    if (!active || game_size.width == 0 || game_size.height == 0 || frame_size.width == 0 ||
        frame_size.height == 0) {
        return VK_NULL_HANDLE;
    }

    EnsureInputs(cmdbuf, game_size);
    EnsureOutput(cmdbuf, frame_size, view_format);
    if (!feature_valid) {
        const float scale = game_size.height > 0
                                ? static_cast<float>(frame_size.height) /
                                      static_cast<float>(game_size.height)
                                : 1.0f;
        // GPU.dlss_preset: NVSDK_NGX_DLSS_Hint_Render_Preset value (9 = J, transformer);
        // NHL_DLSS_PRESET overrides for debugging.
        u32 preset = static_cast<u32>(std::clamp(EmulatorSettings.GetDlssPreset(), 0, 11));
        if (const char* env = std::getenv("NHL_DLSS_PRESET")) {
            preset = static_cast<u32>(std::atoi(env));
        }
        const DlssNgx::FeatureDesc desc{
            .input_width = game_size.width,
            .input_height = game_size.height,
            .output_width = frame_size.width,
            .output_height = frame_size.height,
            .quality = DlssNgx::QualityForScale(scale),
            .depth_inverted = false,
            .preset = preset,
            .hdr = is_hdr,
        };
        if (!dlss->CreateFeature(cmdbuf, desc)) {
            LOG_WARNING(Render_Vulkan, "[DLSS] feature creation failed; falling back to FSR");
            active = false;
            return VK_NULL_HANDLE;
        }
        feature_valid = true;
        first_evaluate = true;
        LOG_INFO(Render_Vulkan, "[DLSS] evaluating {}x{} -> {}x{} (preset {})", game_size.width,
                 game_size.height, frame_size.width, frame_size.height, preset);
    }

    const vk::ImageSubresourceRange range{
        .aspectMask = vk::ImageAspectFlagBits::eColor,
        .levelCount = 1,
        .layerCount = 1,
    };
    // Blit the game frame (A2R10G10B10) into the RGBA8 intermediate: NGX needs plain
    // 8/16-bit color I/O, and the blit performs the format conversion.
    runtime.Transit(&game_image, vk::ImageLayout::eTransferSrcOptimal,
                    vk::PipelineStageFlagBits2::eTransfer, vk::AccessFlagBits2::eTransferRead);
    runtime.FlushBarriers();
    const vk::ImageMemoryBarrier2 color_in_to_dst{
        .srcStageMask = vk::PipelineStageFlagBits2::eTopOfPipe,
        .dstStageMask = vk::PipelineStageFlagBits2::eTransfer,
        .dstAccessMask = vk::AccessFlagBits2::eTransferWrite,
        .oldLayout = vk::ImageLayout::eUndefined,
        .newLayout = vk::ImageLayout::eTransferDstOptimal,
        .image = color_in.image,
        .subresourceRange = range,
    };
    cmdbuf.pipelineBarrier2(vk::DependencyInfo{
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &color_in_to_dst,
    });
    const std::array<vk::Offset3D, 2> src_offsets{
        vk::Offset3D{0, 0, 0},
        vk::Offset3D{static_cast<s32>(game_size.width), static_cast<s32>(game_size.height), 1}};
    const vk::ImageBlit blit_region{
        .srcSubresource{
            .aspectMask = vk::ImageAspectFlagBits::eColor,
            .mipLevel = 0,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
        .srcOffsets{src_offsets},
        .dstSubresource{
            .aspectMask = vk::ImageAspectFlagBits::eColor,
            .mipLevel = 0,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
        .dstOffsets{src_offsets},
    };
    cmdbuf.blitImage(game_image.GetImage(), vk::ImageLayout::eTransferSrcOptimal, color_in.image,
                     vk::ImageLayout::eTransferDstOptimal, {blit_region}, vk::Filter::eLinear);
    // Restore the game image for the sampled/FSR-fallback paths.
    runtime.Transit(&game_image, vk::ImageLayout::eShaderReadOnlyOptimal,
                    vk::PipelineStageFlagBits2::eFragmentShader, vk::AccessFlagBits2::eShaderRead);
    runtime.FlushBarriers();
    const vk::ImageMemoryBarrier2 color_in_to_read{
        .srcStageMask = vk::PipelineStageFlagBits2::eTransfer,
        .srcAccessMask = vk::AccessFlagBits2::eTransferWrite,
        .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
        .dstAccessMask = vk::AccessFlagBits2::eShaderRead,
        .oldLayout = vk::ImageLayout::eTransferDstOptimal,
        .newLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
        .image = color_in.image,
        .subresourceRange = range,
    };
    cmdbuf.pipelineBarrier2(vk::DependencyInfo{
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &color_in_to_read,
    });

    // The output is storage-written by NGX (general layout), then sampled by the
    // post-process pass (shader-read).
    const vk::ImageMemoryBarrier2 to_general{
        .srcStageMask = vk::PipelineStageFlagBits2::eAllCommands,
        .srcAccessMask = vk::AccessFlagBits2::eMemoryWrite | vk::AccessFlagBits2::eMemoryRead,
        .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
        .dstAccessMask = vk::AccessFlagBits2::eShaderWrite,
        .oldLayout = output_layout,
        .newLayout = vk::ImageLayout::eGeneral,
        .image = output.image,
        .subresourceRange = range,
    };
    cmdbuf.pipelineBarrier2(vk::DependencyInfo{
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &to_general,
    });

    const auto now = std::chrono::steady_clock::now();
    float frame_ms = 16.6f;
    if (last_evaluate.time_since_epoch().count() != 0) {
        frame_ms = std::chrono::duration<float, std::milli>(now - last_evaluate).count();
        frame_ms = std::clamp(frame_ms, 1.0f, 100.0f);
    }
    last_evaluate = now;

    const DlssNgx::Resource color{
        .view = color_in.view,
        .range = range,
        .format = color_in.format,
        .extent = color_in.extent,
    };
    const DlssNgx::Resource depth_res{
        .view = depth.view,
        .range = vk::ImageSubresourceRange{
            .aspectMask = vk::ImageAspectFlagBits::eDepth,
            .levelCount = 1,
            .layerCount = 1,
        },
        .format = depth.format,
        .extent = depth.extent,
    };
    const DlssNgx::Resource motion_res{
        .view = motion.view,
        .range = range,
        .format = motion.format,
        .extent = motion.extent,
    };
    const DlssNgx::Resource output_res{
        .view = output.view,
        .range = range,
        .format = output.format,
        .extent = output.extent,
    };

    const bool reset = first_evaluate;
    const bool ok = dlss->Evaluate(cmdbuf, color, depth_res, motion_res, output_res,
                                   {.jitter_x = 0.0f,
                                    .jitter_y = 0.0f,
                                    .reset = reset,
                                    .frame_ms = frame_ms});
    first_evaluate = false;
    if (!ok) {
        LOG_WARNING(Render_Vulkan, "[DLSS] evaluate failed; falling back to FSR next frame");
        return VK_NULL_HANDLE;
    }

    const vk::ImageMemoryBarrier2 to_read{
        .srcStageMask = vk::PipelineStageFlagBits2::eComputeShader,
        .srcAccessMask = vk::AccessFlagBits2::eShaderWrite,
        .dstStageMask = vk::PipelineStageFlagBits2::eFragmentShader,
        .dstAccessMask = vk::AccessFlagBits2::eShaderRead,
        .oldLayout = vk::ImageLayout::eGeneral,
        .newLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
        .image = output.image,
        .subresourceRange = range,
    };
    cmdbuf.pipelineBarrier2(vk::DependencyInfo{
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &to_read,
    });
    output_layout = vk::ImageLayout::eShaderReadOnlyOptimal;
    return output.present_view;
}

} // namespace Vulkan
