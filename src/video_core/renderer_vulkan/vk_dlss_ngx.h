// SPDX-FileCopyrightText: Copyright 2026 IFreemz
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <memory>
#include <string>
#include <vector>

#include "common/types.h"
#include "video_core/renderer_vulkan/vk_common.h"

namespace Vulkan {

// Loads the optional shadps4_dlss.dll bridge next to the executable. shadPS4 itself contains no
// NVIDIA code; without the bridge (or on unsupported GPUs) rendering stays unchanged.
// Callers own synchronization and must drain recorded work before releasing a feature.
class DlssNgx {
public:
    // True when the bridge and nvngx_dlss.dll are present and DLSS is not disabled.
    static bool Present();
    // nhlport: initialize the CUDA driver before the emulator reserves the PS4
    // address space. NVIDIA's Vulkan driver initializes CUDA during vkCreateDevice
    // when VK_NVX_binary_import is enabled, and that initialization needs free
    // virtual address space the reservations would otherwise consume.
    static void PreWarmCuda();
    static std::unique_ptr<DlssNgx> Create();
    ~DlssNgx();

    void AppendInstanceExtensions(std::vector<const char*>& enabled);
    void AppendDeviceExtensions(vk::Instance instance, vk::PhysicalDevice physical,
                                std::vector<const char*>& enabled);
    void Initialize(vk::Instance instance, vk::PhysicalDevice physical, vk::Device device);
    void Shutdown();
    bool IsAvailable() const;

    struct Resource {
        vk::Image image;
        vk::ImageView view;
        vk::ImageSubresourceRange range;
        vk::Format format;
        vk::Extent2D extent;
    };

    // 0 DLAA, 1 Quality, 2 Balanced, 3 Performance, 4 Ultra Performance.
    static int QualityForScale(float output_over_input);
    struct FeatureDesc {
        u32 input_width, input_height, output_width, output_height;
        int quality;
        bool depth_inverted;
        u32 preset; // NVSDK_NGX_DLSS_Hint_Render_Preset value, 0 = driver default
        bool hdr;   // linear HDR colour (automatic exposure)
    };
    // Releases an existing feature first; the caller must have drained the GPU if one exists.
    bool CreateFeature(vk::CommandBuffer command, const FeatureDesc& desc);
    bool HasFeature(const FeatureDesc& desc) const;
    struct EvalDesc {
        float jitter_x, jitter_y; // render pixels, same space as the motion vectors
        bool reset;
        float frame_ms;
    };
    bool Evaluate(vk::CommandBuffer command, const Resource& color, const Resource& depth,
                  const Resource& motion, const Resource& output, const EvalDesc& eval);
    void ReleaseFeatureAfterGpuDrain();

private:
    DlssNgx();
    struct Impl;
    std::unique_ptr<Impl> impl;
};

} // namespace Vulkan
