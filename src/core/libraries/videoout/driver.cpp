// SPDX-FileCopyrightText: Copyright 2025-2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <thread>
#include <iterator>
#include "common/diag_log.h"
#include "common/assert.h"
#include "common/debug.h"
#include "common/path_util.h"
#include "common/thread.h"
#include "core/debug_state.h"
#include "core/emulator_settings.h"
#include "core/libraries/kernel/time.h"
#include "core/libraries/videoout/driver.h"
#include "video_core/perf_counters.h"
#include "video_core/renderer_vulkan/vk_pipeline_cache.h"
#include "core/libraries/videoout/videoout_error.h"
#include "imgui/renderer/imgui_core.h"
#include "video_core/amdgpu/liverpool.h"
#include "video_core/renderdoc.h"
#include "video_core/renderer_vulkan/vk_presenter.h"

extern std::unique_ptr<Vulkan::Presenter> presenter;
extern std::unique_ptr<AmdGpu::Liverpool> liverpool;

namespace Libraries::VideoOut {

constexpr static bool Is32BppPixelFormat(PixelFormat format) {
    switch (format) {
    case PixelFormat::A8R8G8B8Srgb:
    case PixelFormat::A8B8G8R8Srgb:
    case PixelFormat::A2R10G10B10:
    case PixelFormat::A2R10G10B10Srgb:
    case PixelFormat::A2R10G10B10Bt2020Pq:
        return true;
    default:
        return false;
    }
}

constexpr u32 PixelFormatBpp(PixelFormat pixel_format) {
    switch (pixel_format) {
    case PixelFormat::A16R16G16B16Float:
        return 8;
    default:
        return 4;
    }
}

VideoOutDriver::VideoOutDriver(u32 width, u32 height) {
    main_port.resolution.full_width = width;
    main_port.resolution.full_height = height;
    main_port.resolution.pane_width = width;
    main_port.resolution.pane_height = height;
    present_thread = std::jthread([&](std::stop_token token) { PresentThread(token); });
}

VideoOutDriver::~VideoOutDriver() = default;

int VideoOutDriver::Open(const ServiceThreadParams* params) {
    if (main_port.is_open) {
        return ORBIS_VIDEO_OUT_ERROR_RESOURCE_BUSY;
    }
    main_port.is_open = true;
    liverpool->SetVoPort(&main_port);
    return 1;
}

void VideoOutDriver::Close(s32 handle) {
    std::scoped_lock lock{mutex};

    // Mark as closed
    main_port.is_open = false;
    main_port.flip_rate = 0;
    main_port.prev_index = -1;

    // Clear port information
    std::memset(main_port.buffer_labels.data(), 0, sizeof(main_port.buffer_labels));
    std::memset(main_port.groups.data(), 0, sizeof(main_port.groups));
    std::memset(&main_port.vblank_status, 0, sizeof(main_port.vblank_status));
    main_port.flip_status = FlipStatus{};

    // Re-initialize buffers
    std::memset(main_port.buffer_slots.data(), 0, sizeof(main_port.buffer_slots));
    for (auto& buffer : main_port.buffer_slots) {
        buffer.group_index = -1;
    }

    // Clear events
    for (auto event : main_port.flip_events) {
        auto equeue = Kernel::GetEqueue(event);
        if (equeue != nullptr) {
            equeue->RemoveEvent(static_cast<u64>(OrbisVideoOutInternalEventId::Flip),
                                Kernel::OrbisKernelEvent::Filter::VideoOut);
        }
    }
    main_port.flip_events.clear();
    for (auto event : main_port.vblank_events) {
        auto equeue = Kernel::GetEqueue(event);
        if (equeue != nullptr) {
            equeue->RemoveEvent(static_cast<u64>(OrbisVideoOutInternalEventId::Vblank),
                                Kernel::OrbisKernelEvent::Filter::VideoOut);
        }
    }
    main_port.vblank_events.clear();
}

VideoOutPort* VideoOutDriver::GetPort(int handle) {
    if (handle != 1) [[unlikely]] {
        return nullptr;
    }
    return &main_port;
}

int VideoOutDriver::RegisterBuffers(VideoOutPort* port, s32 startIndex, void* const* addresses,
                                    s32 bufferNum, const BufferAttribute* attribute) {
    const s32 group_index = port->FindFreeGroup();
    if (group_index >= MaxDisplayBufferGroups) {
        return ORBIS_VIDEO_OUT_ERROR_NO_EMPTY_SLOT;
    }

    if (startIndex + bufferNum > MaxDisplayBuffers || startIndex > MaxDisplayBuffers ||
        bufferNum > MaxDisplayBuffers) {
        LOG_ERROR(Lib_VideoOut,
                  "Attempted to register too many buffers startIndex = {}, bufferNum = {}",
                  startIndex, bufferNum);
        return ORBIS_VIDEO_OUT_ERROR_INVALID_VALUE;
    }

    const s32 end_index = startIndex + bufferNum;
    if (bufferNum > 0 &&
        std::any_of(port->buffer_slots.begin() + startIndex, port->buffer_slots.begin() + end_index,
                    [](auto& buffer) { return buffer.group_index != -1; })) {
        return ORBIS_VIDEO_OUT_ERROR_SLOT_OCCUPIED;
    }

    if (attribute->reserved0 != 0 || attribute->reserved1 != 0) {
        LOG_ERROR(Lib_VideoOut, "Invalid reserved members");
        return ORBIS_VIDEO_OUT_ERROR_INVALID_VALUE;
    }
    if (attribute->aspect_ratio != 0) {
        LOG_ERROR(Lib_VideoOut, "Invalid aspect ratio = {}", attribute->aspect_ratio);
        return ORBIS_VIDEO_OUT_ERROR_INVALID_ASPECT_RATIO;
    }
    if (attribute->width > attribute->pitch_in_pixel) {
        LOG_ERROR(Lib_VideoOut, "Buffer width {} is larger than pitch {}", attribute->width,
                  attribute->pitch_in_pixel);
        return ORBIS_VIDEO_OUT_ERROR_INVALID_PITCH;
    }
    if (attribute->tiling_mode < TilingMode::Tile || attribute->tiling_mode > TilingMode::Linear) {
        LOG_ERROR(Lib_VideoOut, "Invalid tilingMode = {}",
                  static_cast<u32>(attribute->tiling_mode));
        return ORBIS_VIDEO_OUT_ERROR_INVALID_TILING_MODE;
    }

    LOG_INFO(Lib_VideoOut,
             "startIndex = {}, bufferNum = {}, pixelFormat = {}, aspectRatio = {}, "
             "tilingMode = {}, width = {}, height = {}, pitchInPixel = {}, option = {:#x}",
             startIndex, bufferNum, GetPixelFormatString(attribute->pixel_format),
             attribute->aspect_ratio, static_cast<u32>(attribute->tiling_mode), attribute->width,
             attribute->height, attribute->pitch_in_pixel, attribute->option);

    Diag::Eventf("videoout",
                 "sceVideoOutRegisterBuffers: {} buffers from index {}, {}x{} {} tiling {} pitch {} "
                 "option {:#x}",
                 bufferNum, startIndex, attribute->width, attribute->height,
                 GetPixelFormatString(attribute->pixel_format),
                 static_cast<u32>(attribute->tiling_mode), attribute->pitch_in_pixel,
                 attribute->option);

    auto& group = port->groups[group_index];
    std::memcpy(&group.attrib, attribute, sizeof(BufferAttribute));
    group.is_occupied = true;

    for (u32 i = 0; i < bufferNum; i++) {
        const uintptr_t address = reinterpret_cast<uintptr_t>(addresses[i]);
        port->buffer_slots[startIndex + i] = VideoOutBuffer{
            .group_index = group_index,
            .address_left = address,
            .address_right = 0,
        };

        // Reset flip label also when registering buffer
        port->buffer_labels[startIndex + i] = 0;
        port->SignalVoLabel();

        presenter->RegisterVideoOutSurface(group, address);
        LOG_INFO(Lib_VideoOut, "buffers[{}] = {:#x}", i + startIndex, address);
    }

    return group_index;
}

int VideoOutDriver::UnregisterBuffers(VideoOutPort* port, s32 attributeIndex) {
    if (attributeIndex >= MaxDisplayBufferGroups || !port->groups[attributeIndex].is_occupied) {
        LOG_ERROR(Lib_VideoOut, "Invalid attribute index {}", attributeIndex);
        return ORBIS_VIDEO_OUT_ERROR_INVALID_VALUE;
    }

    auto& group = port->groups[attributeIndex];
    group.is_occupied = false;

    for (auto& buffer : port->buffer_slots) {
        if (buffer.group_index != attributeIndex) {
            continue;
        }
        buffer.group_index = -1;
    }

    return ORBIS_OK;
}

int VideoOutDriver::ChangeBufferAttribute(VideoOutPort* port, s32 attributeIndex,
                                          const BufferAttribute* attribute) {
    if (attributeIndex >= MaxDisplayBufferGroups || !port->groups[attributeIndex].is_occupied) {
        LOG_ERROR(Lib_VideoOut, "Invalid attribute index {}", attributeIndex);
        return ORBIS_VIDEO_OUT_ERROR_INVALID_VALUE;
    }

    if (attribute->reserved0 != 0 || attribute->reserved1 != 0) {
        LOG_ERROR(Lib_VideoOut, "Invalid reserved members");
        return ORBIS_VIDEO_OUT_ERROR_INVALID_VALUE;
    }
    if (attribute->aspect_ratio != 0) {
        LOG_ERROR(Lib_VideoOut, "Invalid aspect ratio = {}", attribute->aspect_ratio);
        return ORBIS_VIDEO_OUT_ERROR_INVALID_ASPECT_RATIO;
    }
    if (attribute->width > attribute->pitch_in_pixel) {
        LOG_ERROR(Lib_VideoOut, "Buffer width {} is larger than pitch {}", attribute->width,
                  attribute->pitch_in_pixel);
        return ORBIS_VIDEO_OUT_ERROR_INVALID_PITCH;
    }
    if (attribute->tiling_mode < TilingMode::Tile || attribute->tiling_mode > TilingMode::Linear) {
        LOG_ERROR(Lib_VideoOut, "Invalid tilingMode = {}",
                  static_cast<u32>(attribute->tiling_mode));
        return ORBIS_VIDEO_OUT_ERROR_INVALID_TILING_MODE;
    }

    LOG_INFO(Lib_VideoOut,
             "attributeIndex = {}, pixelFormat = {}, aspectRatio = {}, "
             "tilingMode = {}, width = {}, height = {}, pitchInPixel = {}, option = {:#x}",
             attributeIndex, GetPixelFormatString(attribute->pixel_format), attribute->aspect_ratio,
             static_cast<u32>(attribute->tiling_mode), attribute->width, attribute->height,
             attribute->pitch_in_pixel, attribute->option);

    std::unique_lock lock{port->port_mutex};
    std::memcpy(&port->groups[attributeIndex].attrib, attribute, sizeof(BufferAttribute));
    return 0;
}

void VideoOutDriver::Flip(const Request& req) {
    // nhlport perf statistics: frame interval distribution of presented flips, every 5 s.
    if (Diag::Enabled()) {
        using Clock = std::chrono::steady_clock;
        static Clock::time_point last_flip{};
        static Clock::time_point window_start{};
        static u32 frames = 0, slow33 = 0, slow100 = 0;
        static double max_ms = 0.0;
        static u64 pipelines_at_start = 0, shaders_at_start = 0;
        static u64 waits_at_start = 0, rb_calls_at_start = 0, rb_bytes_at_start = 0;
        static u64 tcx_at_start = 0, tco_at_start = 0, tcd_at_start = 0;
        static u64 stall_at_start = 0, rbk_at_start = 0, rbo_at_start = 0;
        const auto now = Clock::now();
        double frame_ms = -1.0;
        if (last_flip != Clock::time_point{}) {
            const double ms = std::chrono::duration<double, std::milli>(now - last_flip).count();
            frame_ms = ms;
            ++frames;
            max_ms = std::max(max_ms, ms);
            slow33 += ms > 33.0;
            slow100 += ms > 100.0;
        } else {
            window_start = now;
            pipelines_at_start = Vulkan::NumPipelinesCompiled();
            shaders_at_start = Vulkan::NumShadersCompiled();
            waits_at_start = VideoCore::Perf::gpu_waits.load();
            rb_calls_at_start = VideoCore::Perf::readback_calls.load();
            rb_bytes_at_start = VideoCore::Perf::readback_bytes.load();
            tcx_at_start = VideoCore::Perf::tc_expand.load();
            tco_at_start = VideoCore::Perf::tc_overlap.load();
            tcd_at_start = VideoCore::Perf::tc_depth_recreate.load();
            stall_at_start = VideoCore::Perf::gpu_stall_ns.load();
            rbk_at_start = VideoCore::Perf::readback_tick_known.load();
            rbo_at_start = VideoCore::Perf::readback_tick_old.load();
        }
        last_flip = now;

        // nhlport: diagnostic log, one row per presented frame (+ a game screenshot after a marker).
        {
            u32 vo_w = 0, vo_h = 0;
            if (req.index >= 0) {
                const auto& slot = req.port->buffer_slots[req.index];
                if (slot.group_index >= 0) {
                    const auto& attrib = req.port->groups[slot.group_index].attrib;
                    vo_w = attrib.width;
                    vo_h = attrib.height;
                }
            }
            Diag::OnFramePresented(frame_ms, vo_w, vo_h,
                                   static_cast<u32>(req.port->flip_status.flip_pending_num),
                                   req.port->flip_status.count);
            if (Diag::ConsumeMarkerScreenshot() && !VideoCore::HasPendingGameOnlyScreenshot()) {
                VideoCore::RequestScreenshot(VideoCore::ScreenshotRequest::GameOnly);
            }
        }

        // nhlport: exact game frames. Creating <user>/captures/screenshot (content = N, consumed)
        // saves the next N presented frames as game-only PNGs into <user>/screenshots.
        // nhlport: captures/dumpbuffer = "hexaddr size" lines; the GPU copy of each range is dumped
        if (Diag::Enabled()) {
            static u32 dumpbuf_poll = 0;
            if (++dumpbuf_poll % 30 == 0) {
                static const std::filesystem::path trigger =
                    Common::FS::GetUserPath(Common::FS::PathType::CapturesDir) / "dumpbuffer";
                std::error_code ec;
                if (std::filesystem::exists(trigger, ec)) {
                    std::vector<std::pair<u64, u32>> reqs;
                    if (std::ifstream in{trigger}; in) {
                        std::string a;
                        u32 n = 0;
                        while (in >> a >> n) {
                            try {
                                reqs.emplace_back(std::stoull(a, nullptr, 16), n);
                            } catch (...) {
                            }
                        }
                    }
                    std::filesystem::remove(trigger, ec);
                    {
                        std::scoped_lock lk{VideoCore::Perf::dump_mutex};
                        VideoCore::Perf::dump_buffer_reqs = reqs;
                    }
                    VideoCore::Perf::dump_buffer_pending.store(u32(reqs.size()), std::memory_order_relaxed);
                    LOG_WARNING(Lib_VideoOut, "buffer dump trigger: {} ranges", reqs.size());
                }
            }
        }
        // nhlport: captures/dumpimage = hex guest addresses (one per line) of images to dump
        if (Diag::Enabled()) {
            static u32 dump_poll = 0;
            if (++dump_poll % 30 == 0) {
                static const std::filesystem::path trigger =
                    Common::FS::GetUserPath(Common::FS::PathType::CapturesDir) / "dumpimage";
                std::error_code ec;
                if (std::filesystem::exists(trigger, ec)) {
                    std::vector<u64> addrs;
                    std::vector<u32> draws;
                    if (std::ifstream in{trigger}; in) {
                        std::string line;
                        while (std::getline(in, line)) {
                            try {
                                if (!line.empty()) {
                                    const auto colon = line.find(':');
                                    addrs.push_back(std::stoull(line.substr(0, colon), nullptr, 16));
                                    draws.push_back(colon == std::string::npos
                                                        ? 0u
                                                        : u32(std::stoul(line.substr(colon + 1))));
                                }
                            } catch (...) {
                            }
                        }
                    }
                    std::filesystem::remove(trigger, ec);
                    {
                        std::scoped_lock lk{VideoCore::Perf::dump_mutex};
                        VideoCore::Perf::dump_image_addrs = addrs;
                        VideoCore::Perf::dump_image_draws = draws;
                    }
                    // Counted requests start with the next complete gpu frame.
                    VideoCore::Perf::dump_start_frame.store(
                        VideoCore::Perf::gpu_frame.load(std::memory_order_relaxed) + 1,
                        std::memory_order_relaxed);
                    VideoCore::Perf::dump_pending.store(u32(addrs.size()), std::memory_order_relaxed);
                    LOG_WARNING(Lib_VideoOut, "image dump trigger: {} addresses", addrs.size());
                }
            }
        }
        if (Diag::Enabled()) {
            static u32 shots_left = 0, shot_poll = 0;
            if (shots_left == 0 && ++shot_poll % 30 == 0) {
                static const std::filesystem::path trigger =
                    Common::FS::GetUserPath(Common::FS::PathType::CapturesDir) / "screenshot";
                std::error_code ec;
                if (std::filesystem::exists(trigger, ec)) {
                    u32 count = 1;
                    if (std::ifstream in{trigger}; in) {
                        u32 value = 0;
                        if (in >> value) {
                            count = std::clamp<u32>(value, 1, 600);
                        }
                    }
                    std::filesystem::remove(trigger, ec);
                    shots_left = count;
                    LOG_WARNING(Lib_VideoOut, "screenshot trigger: saving the next {} game frames", count);
                }
            }
            if (shots_left > 0 && !VideoCore::HasPendingGameOnlyScreenshot()) {
                VideoCore::RequestScreenshot(VideoCore::ScreenshotRequest::GameOnly);
                --shots_left;
            }
        }

        // nhlport: per-frame trace. Creating <user>/captures/frametrace (consumed) logs one line
        // per presented frame for the next 600 frames: counter deltas since the previous frame.
        if (Diag::Enabled()) {
            static u32 trace_left = 0, trace_poll = 0, trace_no = 0, draw_trace_left = 0;
            if (trace_left == 0 && ++trace_poll % 30 == 0) {
                static const std::filesystem::path trigger =
                    Common::FS::GetUserPath(Common::FS::PathType::CapturesDir) / "frametrace";
                std::error_code ec;
                if (std::filesystem::exists(trigger, ec)) {
                    u32 draw_frames = 90;
                    if (std::ifstream in{trigger}; in) {
                        u32 value = 0;
                        if (in >> value) {
                            draw_frames = std::clamp<u32>(value, 1, 600);
                        }
                    }
                    std::filesystem::remove(trigger, ec);
                    trace_left = 600;
                    draw_trace_left = draw_frames;
                    VideoCore::Perf::draw_trace.store(1, std::memory_order_relaxed);
                    LOG_WARNING(Lib_VideoOut, "frame trace: logging the next 600 frames (draw log for {})", draw_frames);
                }
            }
            // nhlport: captures/capture (consumed, content = note) = the same capture as the F1
            // hotkey, for scripts; polled every 5 flips.
            static u32 capture_poll = 0;
            if (++capture_poll % 5 == 0) {
                static const std::filesystem::path capture_trigger =
                    Common::FS::GetUserPath(Common::FS::PathType::CapturesDir) / "capture";
                std::error_code ec;
                if (std::filesystem::exists(capture_trigger, ec)) {
                    std::string note;
                    if (std::ifstream in{capture_trigger}; in) {
                        std::getline(in, note);
                    }
                    std::filesystem::remove(capture_trigger, ec);
                    Diag::ArmCapture(note.empty() ? "capture file" : note);
                }
            }
            // nhlport: hotkey capture (Diag::ArmCapture): draw log for the next N frames right away.
            if (draw_trace_left == 0) {
                if (const u32 req = VideoCore::Perf::trace_request.exchange(0, std::memory_order_relaxed);
                    req != 0) {
                    draw_trace_left = std::clamp<u32>(req, 1, 600);
                    if (trace_left == 0) {
                        trace_left = 600;
                    }
                    VideoCore::Perf::draw_trace.store(1, std::memory_order_relaxed);
                    LOG_WARNING(Lib_VideoOut, "frame trace (hotkey): draw log for the next {} frames",
                                draw_trace_left);
                }
            }
            if (trace_left > 0) {
                --trace_left;
                if (draw_trace_left > 0 && --draw_trace_left == 0) {
                    VideoCore::Perf::draw_trace.store(0, std::memory_order_relaxed);
                    // Write the buffered draw log on a helper thread so the present loop
                    // does not stall on 50+ MB of file I/O.
                    std::vector<std::string> lines;
                    {
                        std::scoped_lock lk{VideoCore::Perf::draw_log_mutex};
                        lines.swap(VideoCore::Perf::draw_log);
                    }
                    const auto path = Common::FS::GetUserPath(Common::FS::PathType::CapturesDir) /
                                      fmt::format("drawlog_{}.txt", trace_no);
                    std::thread([lines = std::move(lines), path] {
                        std::ofstream out{path, std::ios::binary};
                        for (const auto& l : lines) {
                            out << l << '\n';
                        }
                    }).detach();
                    LOG_WARNING(Lib_VideoOut, "frame trace: draw log written to {} ({} lines)",
                                path.string(), lines.size());
                }
                namespace P = VideoCore::Perf;
                const std::atomic<u64>* const counters[] = {
                    &P::draws,
                    &P::dispatches,
                    &P::draw_skip_pipeline,
                    &P::draw_skip_bind,
                    &P::tc_created,
                    &P::tc_free[P::FreePoolAlias],
                    &P::tc_free[P::FreeDepthRecreate],
                    &P::tc_free[P::FreeExpand],
                    &P::tc_free[P::FreeLeftTarget],
                    &P::tc_free[P::FreeLeftCopy],
                    &P::tc_free[P::FreeRightChance],
                    &P::tc_free[P::FreeResolveFailed],
                    &P::tc_free[P::FreeGc],
                    &P::tc_free[P::FreeUnmap],
                    &P::tc_free[P::FreeTileMode],
                    &P::tc_expand,
                    &P::tc_overlap,
                    &P::tc_incompatible,
                    &P::tc_right_view,
                    &P::tc_left_copy,
                    &P::gpu_waits,
                    &P::readback_calls,
                    &P::sched_submits,
                    &P::tc_alias_pick,
                    &P::tc_target_copy,
                };
                constexpr size_t num = std::size(counters);
                static u64 prev[num]{};
                u64 d[num];
                for (size_t i = 0; i < num; ++i) {
                    const u64 cur = counters[i]->load(std::memory_order_relaxed);
                    d[i] = cur - prev[i];
                    prev[i] = cur;
                }
                const auto wall = std::chrono::duration_cast<std::chrono::milliseconds>(
                                      std::chrono::system_clock::now().time_since_epoch())
                                      .count();
                LOG_INFO(Lib_VideoOut,
                         "frame trace #{} t={} dt={:.1f} draws {} disp {} skip-nopipe {} skip-bind {} "
                         "img +{} -[pool {} depth {} expand {} ltarget {} lcopy {} rchance {} fail {} gc {} "
                         "unmap {} tile {}] expand {} overlap {} incompat {} rview {} lcopy {} waits {} rb {} "
                         "submits {} alias {} tcopy {} live {} gpuframe {} invalskip {} streamed {}",
                         trace_no++, wall, frame_ms, d[0], d[1], d[2], d[3], d[4], d[5], d[6], d[7], d[8],
                         d[9], d[10], d[11], d[12], d[13], d[14], d[15], d[16], d[17], d[18], d[19], d[20],
                         d[21], d[22], d[23], d[24], P::tc_images.load(), P::gpu_frame.load(),
                         P::gpu_inval_skipped.load(), P::streamed_images.load());
            }
        }
        const double window_s = std::chrono::duration<double>(now - window_start).count();
        if (window_s >= 5.0 && frames > 0) {
            const u64 pipelines = Vulkan::NumPipelinesCompiled();
            const u64 shaders = Vulkan::NumShadersCompiled();
            const u64 waits = VideoCore::Perf::gpu_waits.load();
            const u64 rb_calls = VideoCore::Perf::readback_calls.load();
            const u64 rb_bytes = VideoCore::Perf::readback_bytes.load();
            LOG_INFO(Lib_VideoOut,
                     "frame stats: {:.1f} fps avg {:.1f} ms max {:.1f} ms, >33ms {} >100ms {} of {} frames, "
                     "compiled {} pipelines / {} shaders, gpu waits {} ({:.1f}/frame), readbacks {} "
                     "({} KB, stalls {:.1f} ms, tick known {} old {}), texcache expand {} overlap {} depth-recreate {} (copy skipped {}) right-view {} left-copy {} incompatible {} skipped draws: no-pipeline {} bind-fail {}, live images {} in this window",
                     frames / window_s, 1000.0 * window_s / frames, max_ms, slow33, slow100, frames,
                     pipelines - pipelines_at_start, shaders - shaders_at_start, waits - waits_at_start,
                     double(waits - waits_at_start) / frames, rb_calls - rb_calls_at_start,
                     (rb_bytes - rb_bytes_at_start) / 1024,
                     double(VideoCore::Perf::gpu_stall_ns.load() - stall_at_start) / 1e6,
                     VideoCore::Perf::readback_tick_known.load() - rbk_at_start,
                     VideoCore::Perf::readback_tick_old.load() - rbo_at_start,
                     VideoCore::Perf::tc_expand.load() - tcx_at_start,
                     VideoCore::Perf::tc_overlap.load() - tco_at_start,
                     VideoCore::Perf::tc_depth_recreate.load() - tcd_at_start,
                     VideoCore::Perf::tc_depth_skip.load(), VideoCore::Perf::tc_right_view.load(),
                     VideoCore::Perf::tc_left_copy.load(), VideoCore::Perf::tc_incompatible.load(),
                     VideoCore::Perf::draw_skip_pipeline.load(), VideoCore::Perf::draw_skip_bind.load(),
                     VideoCore::Perf::tc_images.load());
            tcx_at_start = VideoCore::Perf::tc_expand.load();
            tco_at_start = VideoCore::Perf::tc_overlap.load();
            tcd_at_start = VideoCore::Perf::tc_depth_recreate.load();
            stall_at_start = VideoCore::Perf::gpu_stall_ns.load();
            rbk_at_start = VideoCore::Perf::readback_tick_known.load();
            rbo_at_start = VideoCore::Perf::readback_tick_old.load();
            waits_at_start = waits;
            rb_calls_at_start = rb_calls;
            rb_bytes_at_start = rb_bytes;
            window_start = now;
            frames = slow33 = slow100 = 0;
            max_ms = 0.0;
            pipelines_at_start = pipelines;
            shaders_at_start = shaders;
        }
    }

    // Update HDR status before presenting.
    presenter->SetHDR(req.port->is_hdr);

    // Present the frame.
    presenter->Present(req.frame);

    // Update flip status.
    auto* port = req.port;
    {
        std::unique_lock lock{port->port_mutex};
        auto& flip_status = port->flip_status;
        flip_status.count++;
        flip_status.process_time = Libraries::Kernel::sceKernelGetProcessTime();
        flip_status.tsc = Libraries::Kernel::sceKernelReadTsc();
        flip_status.flip_arg = req.flip_arg;
        flip_status.current_buffer = req.index;
        if (req.eop) {
            --flip_status.gc_queue_num;
        }
        --flip_status.flip_pending_num;
        LOG_DEBUG(Lib_VideoOut, "flip presented: index={} arg={} eop={} count={} pending={}",
                  req.index, req.flip_arg, req.eop, flip_status.count,
                  flip_status.flip_pending_num);
    }

    // Trigger flip events for the port.
    for (auto event : port->flip_events) {
        auto equeue = Kernel::GetEqueue(event);
        if (equeue != nullptr) {
            equeue->TriggerEvent(
                static_cast<u64>(OrbisVideoOutInternalEventId::Flip),
                Kernel::OrbisKernelEvent::Filter::VideoOut,
                reinterpret_cast<void*>(static_cast<u64>(OrbisVideoOutInternalEventId::Flip) |
                                        (req.flip_arg << 16)));
        }
    }

    // Reset prev flip label
    if (port->prev_index != -1) {
        port->buffer_labels[port->prev_index] = 0;
        port->SignalVoLabel();
    }
    // save to prev buf index
    port->prev_index = req.index;
}

void VideoOutDriver::DrawBlankFrame() {
    const auto empty_frame = presenter->PrepareBlankFrame(true);
    presenter->Present(empty_frame, false, false);
}

void VideoOutDriver::DrawLastFrame() {
    const auto frame = presenter->PrepareLastFrame();
    if (frame != nullptr) {
        presenter->Present(frame, true);
    }
}

bool VideoOutDriver::SubmitFlip(VideoOutPort* port, s32 index, s64 flip_arg,
                                bool is_eop /*= false*/) {
    {
        std::unique_lock lock{port->port_mutex};
        if (index != -1 && port->flip_status.flip_pending_num > 16) {
            LOG_ERROR(Lib_VideoOut, "Flip queue is full");
            Diag::C::flip_queue_full.fetch_add(1, std::memory_order_relaxed);
            return false;
        }

        if (is_eop) {
            ++port->flip_status.gc_queue_num;
        }
        ++port->flip_status.flip_pending_num; // integral GPU and CPU pending flips counter
        port->flip_status.submit_tsc = Libraries::Kernel::sceKernelReadTsc();
    }

    if (!is_eop) {
        // Non EOP flips can arrive from any thread so ask GPU thread to perform them
        liverpool->SendCommand([=, this]() { SubmitFlipInternal(port, index, flip_arg, is_eop); });
    } else {
        SubmitFlipInternal(port, index, flip_arg, is_eop);
    }

    return true;
}

void VideoOutDriver::SubmitFlipInternal(VideoOutPort* port, s32 index, s64 flip_arg, bool is_eop) {
    Vulkan::Frame* frame;
    if (index == -1) {
        frame = presenter->PrepareBlankFrame(false);
    } else {
        const auto& buffer = port->buffer_slots[index];
        ASSERT_MSG(buffer.group_index >= 0, "Trying to flip an unregistered buffer!");
        const auto& group = port->groups[buffer.group_index];
        frame = presenter->PrepareFrame(group, buffer.address_left);
    }

    LOG_DEBUG(Lib_VideoOut, "flip submitted: index={} arg={} eop={} pending={}", index, flip_arg,
              is_eop, port->flip_status.flip_pending_num);
    std::scoped_lock lock{mutex};
    requests.push({
        .frame = frame,
        .port = port,
        .flip_arg = flip_arg,
        .index = index,
        .eop = is_eop,
    });
}

void VideoOutDriver::PresentThread(std::stop_token token) {
    const std::chrono::nanoseconds vblank_period(1000000000 /
                                                 EmulatorSettings.GetVblankFrequency());

    Common::SetCurrentThreadName("shadPS4:PresentThread");
    Common::SetCurrentThreadRealtime(vblank_period);

    Common::AccurateTimer timer{vblank_period};

    const auto receive_request = [this] -> Request {
        std::scoped_lock lk{mutex};
        if (!requests.empty()) {
            const auto request = requests.front();
            requests.pop();
            return request;
        }
        return {};
    };

    u32 trigger_poll = 0;
    while (!token.stop_requested()) {
        timer.Start();

        // nhlport: file-based RenderDoc capture trigger, checked twice a second.
        if (++trigger_poll % 30 == 0 && Diag::Enabled()) {
            VideoCore::PollCaptureTrigger();
        }

        if (DebugState.IsGuestThreadsPaused()) {
            DrawLastFrame();
            timer.End();
            continue;
        }

        // Check if it's time to take a request.
        auto& vblank_status = main_port.vblank_status;
        if (vblank_status.count % (main_port.flip_rate + 1) == 0) {
            const auto request = receive_request();
            if (!request) {
                if (timer.GetTotalWait().count() < 0) { // Dont draw too fast
                    if (!main_port.is_open) {
                        // nhlport: nothing to show before the game opens its port; the blank
                        // frame path raced with start-up and crashed inside the driver.
                    } else if (ImGui::Core::MustKeepDrawing()) {
                        DrawLastFrame();
                    }
                }
            } else {
                Flip(request);
                FRAME_END;
            }
        }

        {
            // Needs lock here as can be concurrently read by `sceVideoOutGetVblankStatus`
            std::scoped_lock lock{main_port.vo_mutex};

            // Trigger flip events for the port
            for (auto event : main_port.vblank_events) {
                auto equeue = Kernel::GetEqueue(event);
                if (equeue != nullptr) {
                    equeue->TriggerEvent(
                        static_cast<u64>(OrbisVideoOutInternalEventId::Vblank),
                        Kernel::OrbisKernelEvent::Filter::VideoOut,
                        reinterpret_cast<void*>(
                            static_cast<u64>(OrbisVideoOutInternalEventId::Vblank) |
                            (vblank_status.count << 16)));
                }
            }

            // Update vblank status
            vblank_status.count++;
            vblank_status.process_time = Libraries::Kernel::sceKernelGetProcessTime();
            vblank_status.tsc = Libraries::Kernel::sceKernelReadTsc();
            main_port.vblank_cv.notify_all();
        }

        timer.End();
    }
}

} // namespace Libraries::VideoOut
