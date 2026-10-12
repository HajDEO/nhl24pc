// SPDX-FileCopyrightText: Copyright 2026 nhlport
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// nhlport diagnostic log ("diag"): a second, structured log next to shad_log.txt that records as
// much as possible about one game session so that glitches, resolution problems and stutters can
// be correlated after the fact:
//
//   <user>/log/nhl24_diag.log     timestamped events + periodic summaries + final summary
//   <user>/log/nhl24_frames.csv   one row per presented frame (counters, resolution, CPU/GPU time)
//   <user>/log/nhl24_threads.csv  per-second CPU usage of every thread of the process
//
// Enabled by default; NHLPORT_DIAG=0 turns everything off, NHLPORT_DIAG_FRAMES=0 skips the
// per-frame CSV, NHLPORT_DIAG_INTERVAL=<seconds> changes the summary period (default 10).
// A user marker ("I saw a glitch now") comes from the hotkey (hotkey_diag_mark, F1 by default)
// or from writing a note into <user>/captures/diagmark.

#include <atomic>
#include <functional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <fmt/format.h>
#include "common/types.h"

namespace Diag {

/// Cheap global counters incremented by the emulator subsystems; the present thread turns them
/// into per-frame deltas. All are cumulative since process start.
namespace C {
inline std::atomic<u64> guest_gfx_submits{0};   ///< Liverpool::SubmitGfx (game command buffers)
inline std::atomic<u64> guest_asc_submits{0};   ///< Liverpool::SubmitAsc (compute queues)
inline std::atomic<u64> render_passes{0};       ///< Rasterizer::BeginRendering calls
inline std::atomic<u64> pipeline_compiles{0};   ///< graphics + compute pipelines created
inline std::atomic<u64> pipeline_compile_ns{0}; ///< time spent creating them (GPU thread)
inline std::atomic<u64> shader_compiles{0};     ///< shader modules translated (recompiler + SPIR-V)
inline std::atomic<u64> shader_compile_ns{0};
inline std::atomic<u64> readback_wait_ns{0};    ///< time the GPU thread waited for readback copies
inline std::atomic<u64> readback_cpu_write{0};  ///< readbacks caused by a CPU write fault
inline std::atomic<u64> readback_cpu_read{0};   ///< readbacks caused by a CPU read fault
inline std::atomic<u64> tc_cpu_inval{0};        ///< texture cache: images hit by CPU page faults
inline std::atomic<u64> tc_cp_inval{0};         ///< texture cache: images hit by inline PM4 writes
inline std::atomic<u64> tc_gpu_inval{0};        ///< texture cache: images marked GpuDirty
inline std::atomic<u64> tc_refresh{0};          ///< images (re)uploaded from guest memory
inline std::atomic<u64> tc_refresh_bytes{0};
inline std::atomic<u64> tc_snapshots{0};        ///< streamed-image snapshots taken
inline std::atomic<u64> file_opens{0};          ///< guest file opens
inline std::atomic<u64> file_reads{0};          ///< guest read calls (read/pread/readv/aio)
inline std::atomic<u64> file_read_bytes{0};
inline std::atomic<u64> audio_outputs{0};       ///< sceAudioOutOutput calls with data
inline std::atomic<u64> audio_samples{0};
inline std::atomic<u64> audio_wait_ns{0};       ///< time guest threads blocked waiting for the mixer
inline std::atomic<u64> guest_threads_created{0};
inline std::atomic<u64> guest_threads_live{0};
inline std::atomic<u64> dmem_allocated{0};      ///< direct memory allocated by the game (bytes)
inline std::atomic<u64> flip_queue_full{0};     ///< sceVideoOutSubmitFlip rejected (queue full)
inline std::atomic<u64> gpu_lat_ns{0};          ///< submit -> completion latency sum (draw queue)
inline std::atomic<u64> gpu_lat_count{0};
inline std::atomic<u64> gpu_lat_max_ns{0};      ///< max latency since the last frame (reset per frame)
inline std::atomic<u64> gpu_busy_ns{0};         ///< estimated GPU busy time (draw queue)
inline std::atomic<u64> vk_submits{0};          ///< queue submits of the draw scheduler
inline std::atomic<u64> swapchain_recreates{0};
inline std::atomic<u64> cs_meta_clear{0};  ///< dispatches replaced by a metadata clear
inline std::atomic<u64> cs_image_copy{0};  ///< dispatches replaced by an image copy
inline std::atomic<u64> cs_image_clear{0}; ///< dispatches replaced by an image clear
inline std::atomic<u64> cs_hle{0};         ///< dispatches executed by a HLE shader implementation
} // namespace C

/// Whether the diagnostic log is active (cheap check for callers that build strings).
bool Enabled();

/// Opens the files and starts the sampler thread. Needs the user directory (log path).
void Init();

/// Writes the final summary and closes the files. Safe to call several times / from any thread.
void Shutdown(const char* reason);

/// Flushes the files (crash paths).
void Flush();

/// Records a key/value line in the session header (GPU model, config, ...). Can be called
/// before Init (buffered) or after (written as an "info" event).
void SetSessionInfo(std::string key, std::string value);

/// Timestamped free-form event, e.g. Event("videoout", "flip rate 1").
void Event(const char* category, std::string message);

template <typename... Args>
void Eventf(const char* category, fmt::format_string<Args...> format, Args&&... args) {
    if (Enabled()) {
        Event(category, fmt::format(format, std::forward<Args>(args)...));
    }
}

/// User marker: the user saw something (glitch, flicker, stutter). The next frame row is flagged.
void Marker(std::string note);
/// Marker + screenshot + draw log of the next 3 frames + raw dump of the images bound to the UI
/// composite dispatch (NHLPORT_DIAG_F1_CS = hex shader hash, default 0x2c569595, 0 = no dump).
/// Raised by the F1 hotkey and by the file <user>/captures/capture (content = note).
void ArmCapture(std::string note);
/// True while a capture is pending and the compute shader is one of the composite shaders whose
/// bound images the capture dumps (NHLPORT_DIAG_F1_CS = comma separated hex hashes; default: the
/// NHL 24 UI composites 2c569595 and 5bb1bf02).
bool CaptureMatchesDispatch(u64 pgm_hash);

/// True once per marker: the present thread then requests a game screenshot for correlation.
bool ConsumeMarkerScreenshot();

/// Present thread: one presented frame. dt_ms < 0 for the first frame.
void OnFramePresented(double dt_ms, u32 vo_width, u32 vo_height, u32 flip_pending,
                      u64 flip_count);

/// GPU thread: a flip packet was processed = the game finished one GPU frame.
void OnGpuFrameBoundary();

/// GPU thread: a render pass begins with this framebuffer size (depth target bound or not).
void NoteRenderPass(u32 width, u32 height, bool has_depth);

/// GPU thread: a compute dispatch was replaced by an emulator fast path (kind = "meta-clear",
/// "image-copy", "image-clear"); logged once per (shader, target address), counted for the
/// final summary.
void NoteComputeShortcut(const char* kind, u64 shader_hash, u64 target, std::string details);

/// GPU thread: a pipeline / shader module was compiled.
void NotePipelineCompile(bool compute, u64 hash, double ms);
void NoteShaderCompile(std::string_view stage, u64 hash, double ms, bool permutation);

/// Logging backend: every log message above Debug passes through here (counted per call site).
void NoteLog(std::string_view log_class, int level, const char* file, int line, const char* func,
             std::string_view message);

/// Submit -> completion timeline of the draw scheduler (GPU latency / busy estimate).
void OnGpuSubmit(u64 tick);
void OnGpuComplete(u64 from_tick, u64 to_tick);

/// Probes polled by the sampler thread / the present thread.
using VramProbe = std::function<std::pair<u64, u64>()>; ///< (used bytes, budget bytes)
using GuestMemProbe = std::function<std::tuple<u64, u64, u64>()>; ///< (direct total, flexible total, flexible free)
using WindowProbe = std::function<std::pair<u32, u32>()>;         ///< window size in pixels
using HostCacheProbe = std::function<std::pair<u64, u64>()>;      ///< (texture cache bytes, live images)
using GpuRefreshProbe = std::function<void()>; ///< refreshes the draw queue's timeline semaphore
void SetVramProbe(VramProbe probe);
void SetGuestMemProbe(GuestMemProbe probe);
void SetWindowProbe(WindowProbe probe);
void SetHostCacheProbe(HostCacheProbe probe);
/// Polled every 2 ms by a diag thread so GPU completions are observed promptly (busy estimate).
void SetGpuRefreshProbe(GpuRefreshProbe probe);

} // namespace Diag
