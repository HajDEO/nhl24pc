// SPDX-FileCopyrightText: Copyright 2026 nhlport
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <utility>
#include <vector>
#include "common/types.h"

// nhlport: cheap global counters read by the periodic frame statistics (videoout driver).
namespace VideoCore::Perf {

inline std::atomic<u64> gpu_waits{0};      ///< Scheduler::Finish calls (CPU waited for the GPU)
inline std::atomic<u64> readback_calls{0}; ///< BufferCache::DownloadMemory with data
inline std::atomic<u64> readback_bytes{0}; ///< bytes copied GPU -> CPU by readbacks
inline std::atomic<u64> tc_expand{0};        ///< TextureCache::ExpandImage (image recreated larger)
inline std::atomic<u64> tc_overlap{0};       ///< TextureCache::ResolveOverlap calls
inline std::atomic<u64> tc_depth_recreate{0}; ///< ResolveDepthOverlap recreated an image (copy)
inline std::atomic<u64> tc_images{0};        ///< registered images (live)
inline std::atomic<u64> tc_depth_skip{0};    ///< depth<->colour recreations that skipped the content copy
inline std::atomic<u64> tc_stale_alias{0};   ///< (unused) stale larger images dropped
inline std::atomic<u64> tc_right_view{0};    ///< requested image served as a mip/slice view of a larger cached image
inline std::atomic<u64> tc_left_copy{0};     ///< cached image copied into a larger merged image (CopyMip)
inline std::atomic<u64> draw_skip_pipeline{0}; ///< draws/dispatches skipped: no pipeline (key refresh failed)
inline std::atomic<u64> draw_skip_bind{0};     ///< draws/dispatches skipped: BindResources failed
inline std::atomic<u64> tc_incompatible{0};    ///< FindImage: cached image format incompatible -> null image
inline std::atomic<u64> draws{0};              ///< Rasterizer::Draw / DrawIndirect calls (before filtering)
inline std::atomic<u64> dispatches{0};         ///< Rasterizer::DispatchDirect / DispatchIndirect calls
inline std::atomic<u64> tc_created{0};         ///< images registered (cumulative)
inline std::atomic<u64> sched_submits{0};      ///< Scheduler::SubmitExecution calls (ticks)
inline std::atomic<u64> gpu_stall_ns{0};       ///< time the GPU thread spent in emulator stalls (GPU waits, pipeline compiles)
inline std::atomic<u64> readback_tick_known{0}; ///< readbacks whose source submission tick was known
inline std::atomic<u64> readback_tick_old{0};   ///< ... and already submitted earlier (no flush needed)
inline std::atomic<u64> gpu_inval_skipped{0};   ///< small buffer writes into live targets not treated as image data
inline std::atomic<u64> streamed_images{0};     ///< images switched to per-frame upload (CPU streamed)
inline std::atomic<u64> tc_alias_pick{0};      ///< FindImage: several exact-match candidates at one address
inline std::atomic<u64> tc_target_copy{0};     ///< left overlap: bound target copied into the merged image
inline thread_local u32 current_pm4 = 0xffffffffu; ///< PM4 opcode being processed on this thread
inline std::atomic<u32> draw_trace{0};  ///< per-draw / image lifetime logging active (frame trace)
/// nhlport: guest addresses of images to dump once as raw files (captures/dumpimage trigger);
/// the rasterizer checks the bound images of every draw / dispatch while dump_pending != 0.
inline std::mutex dump_mutex;
inline std::vector<u64> dump_image_addrs;
inline std::vector<u32> dump_image_draws; ///< per address: 0 = first binding, N = after N-th target draw
inline std::atomic<u32> dump_pending{0};
/// nhlport: hotkey capture (F1 with diagnostics on, sdl_window.cpp): draw log for the next N frames
/// and a raw dump of every image bound to the next dispatch of the compute shader with this hash
/// (the UI composite by default).
inline std::atomic<u32> trace_request{0};
inline std::atomic<u64> dump_cs_hash{0};
inline std::atomic<u32> dump_cs_pending{0};
/// nhlport: "addr:N" requests only start counting target draws from this gpu frame on, so the
/// N-th draw is the N-th one of a frame regardless of when the trigger file was noticed.
inline std::atomic<u64> dump_start_frame{0};
/// nhlport: "addr size" lines from captures/dumpbuffer -> the GPU copy of that range is written
/// to captures/buf_<addr>_<size>_f<frame>.raw by the GPU thread.
inline std::vector<std::pair<u64, u32>> dump_buffer_reqs;
inline std::atomic<u32> dump_buffer_pending{0};

inline std::mutex draw_log_mutex;
inline std::vector<std::string> draw_log; ///< buffered draw / image lifetime lines (flushed to a file)
inline void DrawLog(std::string&& line) {
    std::scoped_lock lk{draw_log_mutex};
    draw_log.push_back(std::move(line));
}
inline std::atomic<u64> gpu_frame{0};   ///< flips processed by the GPU thread (frame index for logs)
inline thread_local u32 last_free_reason = 0xffffffffu; ///< FreeReason noted before FreeImage
inline thread_local const void* current_request = nullptr; ///< ImageInfo* being resolved (trace)
inline thread_local u32 current_binding = 0xffffffffu;    ///< BindingType being resolved (trace)
inline thread_local const char* inval_source = "?";     ///< who invalidates images from the GPU (trace)
inline std::atomic<u64> draw_index{0};                   ///< draws+dispatches logged so far (trace)

/// Why the texture cache freed an image (per-frame trace).
enum FreeReason : u32 {
    FreePoolAlias,     ///< equal address, different block size (pool reuse)
    FreeDepthRecreate, ///< depth<->colour recreation
    FreeExpand,        ///< replaced by a larger image (ExpandImage)
    FreeLeftTarget,    ///< subresource bound as target, larger image requested
    FreeLeftCopy,      ///< subresource copied into the larger image
    FreeRightChance,   ///< chance overlap inside a cached image
    FreeResolveFailed, ///< FindImage picked an image with fewer resources
    FreeGc,            ///< garbage collector
    FreeUnmap,         ///< memory unmapped
    FreeTileMode,      ///< equal address, different tiling mode
    FreeReasonCount,
};
inline std::atomic<u64> tc_free[FreeReasonCount]{};
inline void NoteFree(FreeReason reason) {
    tc_free[reason].fetch_add(1, std::memory_order_relaxed);
    last_free_reason = reason;
}

} // namespace VideoCore::Perf
