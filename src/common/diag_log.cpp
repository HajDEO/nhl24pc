// SPDX-FileCopyrightText: Copyright 2026 nhlport
// SPDX-License-Identifier: GPL-2.0-or-later

#ifdef _WIN32
#define PSAPI_VERSION 2
#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>
#endif

#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "common/diag_log.h"
#include "common/logging/log.h"
#include "common/path_util.h"
#include "common/thread.h"
#include "video_core/perf_counters.h"

namespace Diag {

namespace {

using Clock = std::chrono::steady_clock;

constexpr u32 TimelineRing = 4096;
constexpr u32 RecentFrames = 120;
constexpr size_t WorstFrames = 24;

/// One logging call site (file:line) with its message count.
struct LogSite {
    std::string cls;
    int level;
    std::string file;
    int line;
    std::string func;
    u64 count = 0;
    u64 reported = 0; ///< count at the last summary (to list "new" messages)
    std::string first;
    std::string last;
};

/// Compact per-frame record kept for the "worst frames" list of the final summary.
struct FrameRec {
    u64 frame = 0;
    double t_s = 0;
    double dt_ms = 0;
    double pipe_ms = 0;
    double stall_ms = 0;
    double rb_wait_ms = 0;
    double gpu_busy_ms = 0;
    double gpu_thr_ms = 0;
    double jobs_ms = 0;
    u32 pipes = 0;
    u32 readbacks = 0;
    u32 img_new = 0;
    u32 inval = 0;
    u32 file_kb = 0;
    u32 scene_w = 0;
    u32 scene_h = 0;
    char flags[16]{};
};

struct MarkerRec {
    double t_s;
    u64 frame;
    u64 gpu_frame;
    std::string note;
};

/// A thread of the process as seen by the sampler.
struct ThreadSample {
    u32 tid = 0;
    std::string name;
    void* handle = nullptr;
    u64 cpu_100ns = 0; ///< cumulative kernel+user time at the last sample
    double pct = 0;    ///< CPU use over the last sampling interval (100 = one core)
    bool alive = true;
    bool generic_name = true;
};

/// Threads whose CPU time is tracked per frame (present thread), by role.
enum KeySlot : u8 { SlotGpu, SlotPresent, SlotSim, SlotRender, SlotJobs, SlotCount };

struct KeyThread {
    void* handle = nullptr;
    u32 tid = 0;
    u8 slot = 0;
    u64 last_100ns = 0;
};

/// Snapshot of every counter the per-frame row and the summaries are derived from.
struct Counters {
    u64 gpu_frame, gfx_submits, asc_submits, draws, dispatches, skip_nopipe, skip_bind, pipes,
        pipe_ns, shaders, shader_ns, gpu_waits, stall_ns, readbacks, rb_bytes, rb_wait_ns, rb_cpu_w,
        rb_cpu_r, vk_submits, lat_ns, lat_count, busy_ns, img_new, img_freed, tc_expand, tc_depth,
        tc_overlap, tc_incompat, cpu_inval, cp_inval, gpu_inval, refresh, refresh_bytes, streamed,
        snapshots, render_passes, file_opens, file_reads, file_bytes, audio_out, audio_wait_ns,
        threads_created, flip_full, swap_recreates, cs_meta_clear, cs_image_copy, cs_image_clear,
        cs_hle;
};

/// A compute dispatch kind replaced by an emulator fast path (one entry per kind + shader).
struct ShortcutRec {
    std::string kind;
    u64 hash = 0;
    u64 count = 0;
    std::string details; ///< of the first occurrence
};

Counters Snapshot() {
    namespace P = VideoCore::Perf;
    const auto ld = [](const std::atomic<u64>& a) { return a.load(std::memory_order_relaxed); };
    u64 freed = 0;
    for (const auto& f : P::tc_free) {
        freed += ld(f);
    }
    return Counters{
        .gpu_frame = ld(P::gpu_frame),
        .gfx_submits = ld(C::guest_gfx_submits),
        .asc_submits = ld(C::guest_asc_submits),
        .draws = ld(P::draws),
        .dispatches = ld(P::dispatches),
        .skip_nopipe = ld(P::draw_skip_pipeline),
        .skip_bind = ld(P::draw_skip_bind),
        .pipes = ld(C::pipeline_compiles),
        .pipe_ns = ld(C::pipeline_compile_ns),
        .shaders = ld(C::shader_compiles),
        .shader_ns = ld(C::shader_compile_ns),
        .gpu_waits = ld(P::gpu_waits),
        .stall_ns = ld(P::gpu_stall_ns),
        .readbacks = ld(P::readback_calls),
        .rb_bytes = ld(P::readback_bytes),
        .rb_wait_ns = ld(C::readback_wait_ns),
        .rb_cpu_w = ld(C::readback_cpu_write),
        .rb_cpu_r = ld(C::readback_cpu_read),
        .vk_submits = ld(C::vk_submits),
        .lat_ns = ld(C::gpu_lat_ns),
        .lat_count = ld(C::gpu_lat_count),
        .busy_ns = ld(C::gpu_busy_ns),
        .img_new = ld(P::tc_created),
        .img_freed = freed,
        .tc_expand = ld(P::tc_expand),
        .tc_depth = ld(P::tc_depth_recreate),
        .tc_overlap = ld(P::tc_overlap),
        .tc_incompat = ld(P::tc_incompatible),
        .cpu_inval = ld(C::tc_cpu_inval),
        .cp_inval = ld(C::tc_cp_inval),
        .gpu_inval = ld(C::tc_gpu_inval),
        .refresh = ld(C::tc_refresh),
        .refresh_bytes = ld(C::tc_refresh_bytes),
        .streamed = ld(P::streamed_images),
        .snapshots = ld(C::tc_snapshots),
        .render_passes = ld(C::render_passes),
        .file_opens = ld(C::file_opens),
        .file_reads = ld(C::file_reads),
        .file_bytes = ld(C::file_read_bytes),
        .audio_out = ld(C::audio_outputs),
        .audio_wait_ns = ld(C::audio_wait_ns),
        .threads_created = ld(C::guest_threads_created),
        .flip_full = ld(C::flip_queue_full),
        .swap_recreates = ld(C::swapchain_recreates),
        .cs_meta_clear = ld(C::cs_meta_clear),
        .cs_image_copy = ld(C::cs_image_copy),
        .cs_image_clear = ld(C::cs_image_clear),
        .cs_hle = ld(C::cs_hle),
    };
}

struct State {
    std::atomic<bool> enabled{false};
    std::atomic<bool> initialized{false};
    std::atomic<bool> shutdown_done{false};
    bool frames_csv = true;
    double interval_s = 10.0;
    Clock::time_point t0{};

    std::FILE* log = nullptr;
    std::FILE* frames = nullptr;
    std::FILE* threads = nullptr;
    std::timed_mutex log_mutex;

    std::mutex info_mutex;
    std::vector<std::pair<std::string, std::string>> pending_info;

    std::mutex sites_mutex;
    std::unordered_map<u64, LogSite> sites;

    // Frame statistics (present thread writes, sampler reads under frame_mutex).
    std::mutex frame_mutex;
    std::string frames_buf;
    std::vector<float> dts;
    u64 frame_no = 0;
    size_t window_begin = 0; ///< index into dts where the current summary window starts
    std::array<float, RecentFrames> recent{};
    u32 recent_pos = 0;
    float median_ms = 16.7f;
    std::vector<FrameRec> worst;
    std::vector<MarkerRec> markers;
    u32 stutters_in_window = 0;
    u32 stutter_events_in_window = 0;
    Clock::time_point last_drop_event{};
    Counters prev_frame{};
    Counters prev_summary{};
    bool have_prev_frame = false;
    u64 last_scene = 0;
    u64 last_window = 0;
    u64 prev_proc_100ns = 0;
    u64 prev_page_faults = 0;

    // GPU-thread render pass tracking of the current GPU frame.
    std::array<u64, 32> rp_sizes{};
    u32 rp_count = 0;
    u32 rp_in_frame = 0;
    u64 largest_depth = 0;
    u64 largest_any = 0;
    std::atomic<u64> scene_packed{0};
    std::atomic<u32> rt_sizes{0};
    std::atomic<u32> rp_last_frame{0};
    std::mutex res_mutex;
    std::map<u64, u64> res_hist; ///< packed size -> GPU frames
    std::map<u64, u64> res_hist_reported;

    // Compute dispatches replaced by fast paths.
    std::mutex shortcut_mutex;
    std::map<std::string, ShortcutRec> shortcuts;

    // Markers.
    std::atomic<u32> marker_flag{0};
    std::atomic<u32> marker_shots{0};
    u32 marker_count = 0;

    // Probes.
    std::mutex probe_mutex;
    VramProbe vram_probe;
    GuestMemProbe guest_probe;
    WindowProbe window_probe;
    HostCacheProbe host_cache_probe;
    GpuRefreshProbe gpu_refresh_probe;
    std::thread gpu_poll;
    std::atomic<bool> gpu_poll_stop{false};

    // Audio starvation detection (present thread only).
    double last_audio_t = -1.0;
    bool audio_started = false;

    // Threads.
    std::mutex kt_mutex;
    std::vector<KeyThread> key_threads;
    std::vector<ThreadSample> thread_samples; ///< sampler-owned
    Clock::time_point last_thread_sample{};

    // GPU timeline.
    std::array<std::atomic<s64>, TimelineRing> submit_ns{};
    std::mutex tl_mutex;
    s64 last_complete_ns = 0;

    // Sampler thread.
    std::thread sampler;
    std::mutex sampler_mutex;
    std::condition_variable sampler_cv;
    bool sampler_stop = false;
    bool sampler_exited = false;
};

State& S() {
    static State state;
    return state;
}

s64 NowNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch())
        .count();
}

double Seconds(const State& s) {
    return std::chrono::duration<double>(Clock::now() - s.t0).count();
}

std::FILE* OpenFile(const std::filesystem::path& path) {
#ifdef _WIN32
    return _wfopen(path.c_str(), L"wb");
#else
    return std::fopen(path.c_str(), "wb");
#endif
}

/// Writes a line to the diag log (already formatted, without the timestamp).
void WriteLine(State& s, std::string_view line) {
    if (!s.log) {
        return;
    }
    std::unique_lock lk{s.log_mutex, std::defer_lock};
    if (!lk.try_lock_for(std::chrono::seconds(2))) {
        return;
    }
    std::fwrite(line.data(), 1, line.size(), s.log);
    std::fputc('\n', s.log);
}

void WriteStamped(State& s, const char* category, std::string_view message) {
    WriteLine(s, fmt::format("[{:9.3f}] {}: {}", Seconds(s), category, message));
}

std::string TimeString() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[64];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
    return buf;
}

constexpr const char* FramesHeader =
    "frame,t_s,dt_ms,gpu_frame,gfx_submits,asc_submits,draws,dispatches,skip_nopipe,skip_bind,"
    "pipes,pipe_ms,shaders,shader_ms,gpu_waits,stall_ms,readbacks,rb_kb,rb_wait_ms,rb_cpu_w,"
    "rb_cpu_r,vk_submits,gpu_lat_ms,gpu_lat_max_ms,gpu_busy_ms,img_new,img_freed,img_live,"
    "tc_expand,tc_depth_rec,tc_overlap,tc_incompat,cpu_inval,cp_inval,gpu_inval,refresh,"
    "refresh_kb,streamed,snapshots,scene_w,scene_h,rt_sizes,render_passes,vo_w,vo_h,win_w,win_h,"
    "flip_pending,file_reads,file_kb,audio_out,audio_wait_ms,cpu_ms,gpu_thr_ms,present_ms,sim_ms,"
    "render_ms,jobs_ms,page_faults,cs_meta_clear,cs_image_copy,cs_image_clear,cs_hle,flags";

#ifdef _WIN32
u64 FileTimeTo100ns(const FILETIME& ft) {
    return (u64(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
}

u64 ThreadCpu100ns(HANDLE h) {
    FILETIME c, e, k, u;
    if (!GetThreadTimes(h, &c, &e, &k, &u)) {
        return 0;
    }
    return FileTimeTo100ns(k) + FileTimeTo100ns(u);
}

u64 ProcessCpu100ns() {
    FILETIME c, e, k, u;
    if (!GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u)) {
        return 0;
    }
    return FileTimeTo100ns(k) + FileTimeTo100ns(u);
}

std::string ThreadDescription(HANDLE h) {
    PWSTR desc = nullptr;
    std::string name;
    if (SUCCEEDED(GetThreadDescription(h, &desc)) && desc) {
        const int len = WideCharToMultiByte(CP_UTF8, 0, desc, -1, nullptr, 0, nullptr, nullptr);
        if (len > 1) {
            name.resize(size_t(len) - 1);
            WideCharToMultiByte(CP_UTF8, 0, desc, -1, name.data(), len, nullptr, nullptr);
        }
        LocalFree(desc);
    }
    return name;
}

u8 SlotFor(std::string_view name, bool& is_key) {
    is_key = true;
    if (name == "shadPS4:GpuCommandProcessor") {
        return SlotGpu;
    }
    if (name == "shadPS4:PresentThread") {
        return SlotPresent;
    }
    if (name == "GameSimulationLoop") {
        return SlotSim;
    }
    if (name == "RenderLoop") {
        return SlotRender;
    }
    if (name.starts_with("Job") && name.size() <= 5) {
        return SlotJobs;
    }
    is_key = false;
    return 0;
}

/// Refreshes the thread list and the per-thread CPU percentages (sampler thread).
void SampleThreads(State& s) {
    const auto now = Clock::now();
    const double interval =
        s.last_thread_sample == Clock::time_point{}
            ? 0.0
            : std::chrono::duration<double>(now - s.last_thread_sample).count();
    s.last_thread_sample = now;

    const DWORD pid = GetCurrentProcessId();
    const HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap != INVALID_HANDLE_VALUE) {
        THREADENTRY32 te{};
        te.dwSize = sizeof(te);
        if (Thread32First(snap, &te)) {
            do {
                if (te.th32OwnerProcessID != pid) {
                    continue;
                }
                const auto it = std::find_if(s.thread_samples.begin(), s.thread_samples.end(),
                                             [&](const ThreadSample& t) {
                                                 return t.tid == te.th32ThreadID;
                                             });
                if (it != s.thread_samples.end()) {
                    continue;
                }
                const HANDLE h = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, te.th32ThreadID);
                if (!h) {
                    continue;
                }
                ThreadSample ts{};
                ts.tid = te.th32ThreadID;
                ts.handle = h;
                ts.name = ThreadDescription(h);
                ts.generic_name = ts.name.empty();
                if (ts.generic_name) {
                    ts.name = fmt::format("tid {}", ts.tid);
                }
                ts.cpu_100ns = ThreadCpu100ns(h);
                s.thread_samples.push_back(std::move(ts));
            } while (Thread32Next(snap, &te));
        }
        CloseHandle(snap);
    }

    for (auto& t : s.thread_samples) {
        if (!t.alive) {
            continue;
        }
        DWORD code = 0;
        if (GetExitCodeThread(t.handle, &code) && code != STILL_ACTIVE) {
            t.alive = false;
            t.pct = 0;
            continue;
        }
        if (t.generic_name) {
            // Guest threads are named shortly after creation.
            auto name = ThreadDescription(t.handle);
            if (!name.empty()) {
                t.name = std::move(name);
                t.generic_name = false;
            }
        }
        const u64 cpu = ThreadCpu100ns(t.handle);
        t.pct = interval > 0 ? double(cpu - t.cpu_100ns) / (interval * 1e7) * 100.0 : 0.0;
        t.cpu_100ns = cpu;
    }

    // Key threads for the per-frame columns.
    std::vector<KeyThread> keys;
    for (const auto& t : s.thread_samples) {
        if (!t.alive) {
            continue;
        }
        bool is_key = false;
        const u8 slot = SlotFor(t.name, is_key);
        if (!is_key) {
            continue;
        }
        KeyThread kt{};
        kt.handle = t.handle;
        kt.tid = t.tid;
        kt.slot = slot;
        kt.last_100ns = 0;
        keys.push_back(kt);
    }
    {
        std::scoped_lock lk{s.kt_mutex};
        // Keep the per-thread baselines of threads that are already tracked.
        for (auto& k : keys) {
            const auto it = std::find_if(s.key_threads.begin(), s.key_threads.end(),
                                         [&](const KeyThread& o) { return o.tid == k.tid; });
            k.last_100ns = it != s.key_threads.end() ? it->last_100ns : ThreadCpu100ns(k.handle);
        }
        s.key_threads = std::move(keys);
    }
}

/// Present thread: CPU time (ms) consumed by each key role since the previous frame.
void SampleKeyThreads(State& s, double out_ms[SlotCount]) {
    for (u32 i = 0; i < SlotCount; ++i) {
        out_ms[i] = 0;
    }
    std::scoped_lock lk{s.kt_mutex};
    for (auto& k : s.key_threads) {
        const u64 cpu = ThreadCpu100ns(k.handle);
        if (cpu > k.last_100ns) {
            out_ms[k.slot] += double(cpu - k.last_100ns) / 1e4;
        }
        k.last_100ns = cpu;
    }
}

struct ProcMem {
    u64 working_set = 0;
    u64 private_bytes = 0;
    u64 page_faults = 0;
};

ProcMem ProcessMemory() {
    PROCESS_MEMORY_COUNTERS_EX pmc{};
    pmc.cb = sizeof(pmc);
    ProcMem m{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc),
                             sizeof(pmc))) {
        m.working_set = pmc.WorkingSetSize;
        m.private_bytes = pmc.PrivateUsage;
        m.page_faults = pmc.PageFaultCount;
    }
    return m;
}

std::string SystemSummary() {
    MEMORYSTATUSEX ms{};
    ms.dwLength = sizeof(ms);
    GlobalMemoryStatusEx(&ms);
    SYSTEM_INFO si{};
    GetSystemInfo(&si);
    return fmt::format("logical cpus {}, physical RAM {} MB ({} MB free at start)",
                       si.dwNumberOfProcessors, ms.ullTotalPhys >> 20, ms.ullAvailPhys >> 20);
}
#else
void SampleThreads(State&) {}
void SampleKeyThreads(State&, double out_ms[SlotCount]) {
    for (u32 i = 0; i < SlotCount; ++i) {
        out_ms[i] = 0;
    }
}
u64 ProcessCpu100ns() {
    return 0;
}
struct ProcMem {
    u64 working_set = 0;
    u64 private_bytes = 0;
    u64 page_faults = 0;
};
ProcMem ProcessMemory() {
    return {};
}
std::string SystemSummary() {
    return "n/a";
}
#endif

std::string Dim(u64 packed) {
    return fmt::format("{}x{}", u32(packed >> 32), u32(packed & 0xffffffffu));
}

/// Percentile of a sorted vector (p in 0..100).
float Pct(const std::vector<float>& sorted, double p) {
    if (sorted.empty()) {
        return 0.f;
    }
    const size_t idx = std::min(sorted.size() - 1, size_t(p / 100.0 * double(sorted.size())));
    return sorted[idx];
}

std::string ThreadTable(State& s, size_t max_rows) {
    std::vector<const ThreadSample*> rows;
    for (const auto& t : s.thread_samples) {
        if (t.alive && t.pct >= 0.5) {
            rows.push_back(&t);
        }
    }
    std::sort(rows.begin(), rows.end(),
              [](const ThreadSample* a, const ThreadSample* b) { return a->pct > b->pct; });
    double total = 0;
    for (const auto& t : s.thread_samples) {
        total += t.alive ? t.pct : 0;
    }
    std::string out = fmt::format("threads (total {:.0f}%):", total);
    for (size_t i = 0; i < rows.size() && i < max_rows; ++i) {
        out += fmt::format(" {} {:.0f}%", rows[i]->name, rows[i]->pct);
    }
    return out;
}

std::string MemoryLine(State& s, const ProcMem& pm, u64 pf_delta, double interval) {
    std::string out = fmt::format("memory: working set {} MB, private {} MB, page faults +{} ({:.0f}/s)",
                                  pm.working_set >> 20, pm.private_bytes >> 20, pf_delta,
                                  interval > 0 ? double(pf_delta) / interval : 0.0);
    VramProbe vram;
    GuestMemProbe guest;
    HostCacheProbe host;
    {
        std::scoped_lock lk{s.probe_mutex};
        vram = s.vram_probe;
        guest = s.guest_probe;
        host = s.host_cache_probe;
    }
    if (vram) {
        const auto [used, budget] = vram();
        out += fmt::format(" | vram {} / {} MB", used >> 20, budget >> 20);
    }
    if (guest) {
        const auto [direct, flex_total, flex_free] = guest();
        out += fmt::format(" | guest direct {} / {} MB, flexible {} / {} MB",
                           C::dmem_allocated.load(std::memory_order_relaxed) >> 20, direct >> 20,
                           (flex_total - flex_free) >> 20, flex_total >> 20);
    }
    if (host) {
        const auto [bytes, images] = host();
        out += fmt::format(" | texture cache {} MB in {} images", bytes >> 20, images);
    }
    return out;
}

/// Messages whose count grew since the last summary, most frequent first.
std::string NewWarnings(State& s, size_t max_rows, bool final) {
    std::vector<LogSite*> rows;
    {
        std::scoped_lock lk{s.sites_mutex};
        for (auto& [key, site] : s.sites) {
            if (final ? site.count > 0 : site.count > site.reported) {
                rows.push_back(&site);
            }
        }
        std::sort(rows.begin(), rows.end(), [final](const LogSite* a, const LogSite* b) {
            return final ? a->count > b->count
                         : (a->count - a->reported) > (b->count - b->reported);
        });
        std::string out;
        for (size_t i = 0; i < rows.size() && i < max_rows; ++i) {
            const auto& r = *rows[i];
            const u64 n = final ? r.count : r.count - r.reported;
            const char* lvl = r.level >= 5 ? "CRIT" : r.level == 4 ? "ERR" : r.level == 3 ? "WARN" : "info";
            out += fmt::format("\n    {:>7}x {} [{}] {}:{} {}: {}", n, lvl, r.cls, r.file, r.line,
                               r.func, final && r.last != r.first ? r.first + "  ...  " + r.last : r.last);
        }
        for (auto* r : rows) {
            r->reported = r->count;
        }
        if (out.empty()) {
            return final ? "no warnings" : "no new warnings";
        }
        return fmt::format("{} message sites:{}", rows.size(), out);
    }
}

std::string ResolutionHistogram(State& s, bool delta) {
    std::scoped_lock lk{s.res_mutex};
    std::vector<std::pair<u64, u64>> rows;
    u64 total = 0;
    for (const auto& [size, frames] : s.res_hist) {
        const u64 n = delta ? frames - s.res_hist_reported[size] : frames;
        if (n > 0) {
            rows.emplace_back(size, n);
            total += n;
        }
        if (delta) {
            s.res_hist_reported[size] = frames;
        }
    }
    std::sort(rows.begin(), rows.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });
    std::string out;
    for (size_t i = 0; i < rows.size() && i < 8; ++i) {
        out += fmt::format("{}{} {:.0f}%", i ? ", " : "", Dim(rows[i].first),
                           total ? 100.0 * double(rows[i].second) / double(total) : 0.0);
    }
    if (rows.size() > 8) {
        out += fmt::format(" (+{} more)", rows.size() - 8);
    }
    return out.empty() ? "none" : out;
}

void FlushFrames(State& s) {
    std::string buf;
    {
        std::scoped_lock lk{s.frame_mutex};
        buf.swap(s.frames_buf);
    }
    if (!buf.empty() && s.frames) {
        std::fwrite(buf.data(), 1, buf.size(), s.frames);
        std::fflush(s.frames);
    }
}

void WriteSummary(State& s, double interval) {
    const Counters cur = Snapshot();
    std::string frames_part;
    {
        std::scoped_lock lk{s.frame_mutex};
        const size_t n = s.dts.size() - s.window_begin;
        if (n > 0) {
            std::vector<float> win(s.dts.begin() + s.window_begin, s.dts.end());
            std::sort(win.begin(), win.end());
            double sum = 0;
            u32 slow33 = 0, slow100 = 0;
            for (const float d : win) {
                sum += d;
                slow33 += d > 33.4f;
                slow100 += d > 100.f;
            }
            const float p99 = Pct(win, 99.0);
            frames_part = fmt::format(
                "{} frames, {:.1f} fps (avg {:.1f} ms, median {:.1f}, 99% {:.1f}, max {:.1f}; "
                "1% low {:.1f} fps), >33 ms {}, >100 ms {}, stutters {}",
                n, n / sum * 1000.0, sum / n, Pct(win, 50.0), p99, win.back(),
                1000.0 / std::max(0.001f, p99), slow33, slow100, s.stutters_in_window);
        } else {
            frames_part = "no frames presented";
        }
        s.stutters_in_window = 0;
        s.stutter_events_in_window = 0;
        s.window_begin = s.dts.size();
    }
    const Counters& p = s.prev_summary;
    const auto d = [&](u64 Counters::*m) { return cur.*m - p.*m; };
    const double frames = std::max<double>(1.0, double(d(&Counters::gpu_frame)));
    const double lat_avg = d(&Counters::lat_count)
                               ? double(d(&Counters::lat_ns)) / double(d(&Counters::lat_count)) / 1e6
                               : 0.0;
    std::string line = fmt::format(
        "summary {:.0f} s: {} | gpu frames {} ({:.0f} draws + {:.0f} dispatches, {:.1f} render passes, "
        "{:.1f} gfx + {:.1f} asc submits per frame) | gpu busy {:.0f}% latency {:.1f} ms | "
        "compiles {} pipelines ({:.0f} ms) {} shaders ({:.0f} ms) | gpu waits {} stall {:.0f} ms | "
        "readbacks {} ({} KB, wait {:.0f} ms; cpu-write {} cpu-read {}) | texcache +{} -{} live {} "
        "expand {} depth-recreate {} overlap {} incompatible {} | inval cpu {} cp {} gpu {} uploads {} "
        "({} KB) streamed {} snapshots {} | dropped draws: no-pipeline {} bind-fail {} | compute "
        "fast paths: meta-clear {} image-copy {} image-clear {} hle {} | "
        "files: opens {} reads {} ({} KB) | audio: outputs {} (thread blocked {:.0f} ms) | threads +{} | "
        "flip queue full {} swapchain recreated {} | scene {}",
        interval, frames_part, d(&Counters::gpu_frame), double(d(&Counters::draws)) / frames,
        double(d(&Counters::dispatches)) / frames, double(d(&Counters::render_passes)) / frames,
        double(d(&Counters::gfx_submits)) / frames, double(d(&Counters::asc_submits)) / frames,
        interval > 0 ? 100.0 * double(d(&Counters::busy_ns)) / (interval * 1e9) : 0.0, lat_avg,
        d(&Counters::pipes), double(d(&Counters::pipe_ns)) / 1e6, d(&Counters::shaders),
        double(d(&Counters::shader_ns)) / 1e6, d(&Counters::gpu_waits),
        double(d(&Counters::stall_ns)) / 1e6, d(&Counters::readbacks), d(&Counters::rb_bytes) >> 10,
        double(d(&Counters::rb_wait_ns)) / 1e6, d(&Counters::rb_cpu_w), d(&Counters::rb_cpu_r),
        d(&Counters::img_new), d(&Counters::img_freed),
        VideoCore::Perf::tc_images.load(std::memory_order_relaxed), d(&Counters::tc_expand),
        d(&Counters::tc_depth), d(&Counters::tc_overlap), d(&Counters::tc_incompat),
        d(&Counters::cpu_inval), d(&Counters::cp_inval), d(&Counters::gpu_inval),
        d(&Counters::refresh), d(&Counters::refresh_bytes) >> 10, d(&Counters::streamed),
        d(&Counters::snapshots), d(&Counters::skip_nopipe), d(&Counters::skip_bind),
        d(&Counters::cs_meta_clear), d(&Counters::cs_image_copy), d(&Counters::cs_image_clear),
        d(&Counters::cs_hle), d(&Counters::file_opens), d(&Counters::file_reads),
        d(&Counters::file_bytes) >> 10,
        d(&Counters::audio_out), double(d(&Counters::audio_wait_ns)) / 1e6,
        d(&Counters::threads_created), d(&Counters::flip_full), d(&Counters::swap_recreates),
        ResolutionHistogram(s, true));
    s.prev_summary = cur;
    WriteStamped(s, "summary", line);
}

void SamplerThread() {
    Common::SetCurrentThreadName("nhlport:Diag");
    State& s = S();
    Clock::time_point last_summary = Clock::now();
    u64 prev_pf = ProcessMemory().page_faults;
    Clock::time_point last_mem = Clock::now();
    const std::filesystem::path mark_trigger =
        Common::FS::GetUserPath(Common::FS::PathType::CapturesDir) / "diagmark";
    u32 tick = 0;
    while (true) {
        {
            std::unique_lock lk{s.sampler_mutex};
            if (s.sampler_cv.wait_for(lk, std::chrono::seconds(1), [&] { return s.sampler_stop; })) {
                break;
            }
        }
        ++tick;
        FlushFrames(s);
        SampleThreads(s);
        if (s.threads) {
            const double t = Seconds(s);
            std::string rows;
            for (const auto& th : s.thread_samples) {
                if (th.alive && th.pct >= 0.5) {
                    rows += fmt::format("{:.1f},{},\"{}\",{:.1f}\n", t, th.tid, th.name, th.pct);
                }
            }
            if (!rows.empty()) {
                std::fwrite(rows.data(), 1, rows.size(), s.threads);
            }
            if (tick % 5 == 0) {
                std::fflush(s.threads);
            }
        }
        // User marker through a file: the content becomes the note.
        {
            std::error_code ec;
            if (std::filesystem::exists(mark_trigger, ec)) {
                std::string note;
                if (std::ifstream in{mark_trigger}; in) {
                    std::getline(in, note);
                }
                std::filesystem::remove(mark_trigger, ec);
                Marker(note.empty() ? "file trigger" : note);
            }
        }
        const auto now = Clock::now();
        const double since = std::chrono::duration<double>(now - last_summary).count();
        if (since >= s.interval_s) {
            last_summary = now;
            WriteSummary(s, since);
            WriteStamped(s, "summary", ThreadTable(s, 12));
            const ProcMem pm = ProcessMemory();
            const double mem_interval = std::chrono::duration<double>(now - last_mem).count();
            WriteStamped(s, "summary", MemoryLine(s, pm, pm.page_faults - prev_pf, mem_interval));
            prev_pf = pm.page_faults;
            last_mem = now;
            WriteStamped(s, "summary", NewWarnings(s, 10, false));
        }
        if (s.log && tick % 2 == 0) {
            std::unique_lock lk{s.log_mutex, std::defer_lock};
            if (lk.try_lock_for(std::chrono::milliseconds(200))) {
                std::fflush(s.log);
            }
        }
    }
    {
        std::scoped_lock lk{s.sampler_mutex};
        s.sampler_exited = true;
    }
    s.sampler_cv.notify_all();
}

/// Observes GPU completions between draws: without it a completion is only noticed when the GPU
/// thread next refreshes the timeline, which inflates the latency / busy estimate while it idles.
void GpuPollThread() {
    Common::SetCurrentThreadName("nhlport:DiagGpu");
    State& s = S();
    while (!s.gpu_poll_stop.load(std::memory_order_relaxed)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        GpuRefreshProbe probe;
        {
            std::scoped_lock lk{s.probe_mutex};
            probe = s.gpu_refresh_probe;
        }
        if (probe) {
            probe();
        }
    }
}

void WriteFinalSummary(State& s, const char* reason) {
    const double session_s = Seconds(s);
    WriteLine(s, "");
    WriteStamped(s, "final", fmt::format("session ended ({}), {:.1f} s", reason, session_s));
    {
        std::scoped_lock lk{s.frame_mutex};
        std::vector<float> all = s.dts;
        if (!all.empty()) {
            std::sort(all.begin(), all.end());
            double sum = 0;
            u32 slow33 = 0, slow50 = 0, slow100 = 0;
            for (const float d : all) {
                sum += d;
                slow33 += d > 33.4f;
                slow50 += d > 50.f;
                slow100 += d > 100.f;
            }
            const float p99 = Pct(all, 99.0), p999 = Pct(all, 99.9);
            WriteStamped(
                s, "final",
                fmt::format("frames {}: {:.1f} fps average (avg {:.2f} ms), median {:.2f} ms, 90% {:.2f}, "
                            "99% {:.2f}, 99.9% {:.2f}, max {:.1f} ms; 1% low {:.1f} fps, 0.1% low {:.1f} "
                            "fps; >33 ms {} ({:.1f}%), >50 ms {}, >100 ms {}",
                            all.size(), all.size() / sum * 1000.0, sum / all.size(), Pct(all, 50.0),
                            Pct(all, 90.0), p99, p999, all.back(), 1000.0 / std::max(0.001f, p99),
                            1000.0 / std::max(0.001f, p999), slow33,
                            100.0 * slow33 / all.size(), slow50, slow100));
            constexpr float edges[] = {8.4f, 12.6f, 16.8f, 20.f, 25.f, 33.4f, 50.f, 100.f, 250.f};
            std::string hist = "frame time histogram:";
            size_t pos = 0;
            float lo = 0.f;
            for (const float hi : edges) {
                size_t n = 0;
                while (pos < all.size() && all[pos] <= hi) {
                    ++pos;
                    ++n;
                }
                hist += fmt::format(" {:.0f}-{:.0f}ms {:.1f}%", lo, hi, 100.0 * n / all.size());
                lo = hi;
            }
            hist += fmt::format(" >250ms {:.1f}%", 100.0 * (all.size() - pos) / all.size());
            WriteStamped(s, "final", hist);
        }
        if (!s.worst.empty()) {
            std::vector<FrameRec> worst = s.worst;
            std::sort(worst.begin(), worst.end(),
                      [](const FrameRec& a, const FrameRec& b) { return a.dt_ms > b.dt_ms; });
            std::string out = "longest frames (frame @t: dt | compiles | gpu stall | readbacks | "
                              "images | invalidations | file | gpu busy / gpu thread / jobs | flags):";
            for (const auto& w : worst) {
                out += fmt::format(
                    "\n    #{} @{:.1f}s: {:.1f} ms | {} ({:.1f} ms) | {:.1f} ms | {} ({:.1f} ms) | +{} | "
                    "{} | {} KB | {:.1f} / {:.1f} / {:.1f} ms | {} {}",
                    w.frame, w.t_s, w.dt_ms, w.pipes, w.pipe_ms, w.stall_ms, w.readbacks,
                    w.rb_wait_ms, w.img_new, w.inval, w.file_kb, w.gpu_busy_ms, w.gpu_thr_ms,
                    w.jobs_ms, w.flags, Dim((u64(w.scene_w) << 32) | w.scene_h));
            }
            WriteStamped(s, "final", out);
        }
        if (!s.markers.empty()) {
            std::string out = fmt::format("{} user markers:", s.markers.size());
            for (const auto& m : s.markers) {
                out += fmt::format("\n    @{:.1f}s frame #{} (gpu frame {}): {}", m.t_s, m.frame,
                                   m.gpu_frame, m.note);
            }
            WriteStamped(s, "final", out);
        }
    }
    const Counters c = Snapshot();
    WriteStamped(
        s, "final",
        fmt::format("totals: gpu frames {} | compiles {} pipelines ({:.0f} ms) {} shaders ({:.0f} ms) | "
                    "gpu waits {} stall {:.0f} ms | readbacks {} ({} MB, wait {:.0f} ms; cpu-write {} "
                    "cpu-read {}) | texcache images +{} -{} live {} expand {} depth-recreate {} "
                    "overlap {} incompatible {} | inval cpu {} cp {} gpu {} uploads {} ({} MB) "
                    "streamed {} snapshots {} | dropped draws no-pipeline {} bind-fail {} | compute "
                    "fast paths: meta-clear {} image-copy {} image-clear {} hle {} | files: "
                    "opens {} reads {} ({} MB) | audio outputs {} (thread blocked {:.0f} ms) | guest threads {} | "
                    "flip queue full {} swapchain recreated {} | gpu busy {:.0f}% of session, "
                    "avg submit latency {:.1f} ms",
                    c.gpu_frame, c.pipes, double(c.pipe_ns) / 1e6, c.shaders,
                    double(c.shader_ns) / 1e6, c.gpu_waits, double(c.stall_ns) / 1e6, c.readbacks,
                    c.rb_bytes >> 20, double(c.rb_wait_ns) / 1e6, c.rb_cpu_w, c.rb_cpu_r, c.img_new,
                    c.img_freed, VideoCore::Perf::tc_images.load(std::memory_order_relaxed),
                    c.tc_expand, c.tc_depth, c.tc_overlap, c.tc_incompat, c.cpu_inval, c.cp_inval,
                    c.gpu_inval, c.refresh, c.refresh_bytes >> 20, c.streamed, c.snapshots,
                    c.skip_nopipe, c.skip_bind, c.cs_meta_clear, c.cs_image_copy,
                    c.cs_image_clear, c.cs_hle, c.file_opens, c.file_reads, c.file_bytes >> 20,
                    c.audio_out, double(c.audio_wait_ns) / 1e6, c.threads_created, c.flip_full,
                    c.swap_recreates, session_s > 0 ? 100.0 * double(c.busy_ns) / (session_s * 1e9) : 0.0,
                    c.lat_count ? double(c.lat_ns) / double(c.lat_count) / 1e6 : 0.0));
    {
        namespace P = VideoCore::Perf;
        const char* names[] = {"pool-alias", "depth-recreate", "expand", "left-target", "left-copy",
                               "right-chance", "resolve-failed", "gc", "unmap", "tile-mode"};
        std::string out = "texture cache frees by reason:";
        for (u32 i = 0; i < P::FreeReasonCount; ++i) {
            out += fmt::format(" {} {}", names[i], P::tc_free[i].load(std::memory_order_relaxed));
        }
        WriteStamped(s, "final", out);
    }
    {
        std::scoped_lock lk{s.shortcut_mutex};
        if (!s.shortcuts.empty()) {
            std::vector<const ShortcutRec*> recs;
            for (const auto& [key, rec] : s.shortcuts) {
                recs.push_back(&rec);
            }
            std::sort(recs.begin(), recs.end(), [](const ShortcutRec* a, const ShortcutRec* b) {
                return a->count > b->count;
            });
            std::string out = fmt::format(
                "compute dispatches replaced by emulator fast paths ({} shader/target pairs; "
                "these are intentional, not bind failures):",
                recs.size());
            for (const ShortcutRec* rec : recs) {
                out += fmt::format("\n    {:>8}x {} cs {:#x}: {}", rec->count, rec->kind,
                                   rec->hash, rec->details);
            }
            WriteStamped(s, "final", out);
        }
    }
    WriteStamped(s, "final", fmt::format("scene resolutions (share of gpu frames): {}",
                                         ResolutionHistogram(s, false)));
    {
        // Session-wide CPU per thread from the cumulative thread times.
        std::vector<std::pair<std::string, double>> rows;
        double total = 0;
        for (const auto& t : s.thread_samples) {
            const double pct = session_s > 0 ? double(t.cpu_100ns) / (session_s * 1e7) * 100.0 : 0.0;
            total += pct;
            if (pct >= 1.0) {
                rows.emplace_back(t.name, pct);
            }
        }
        std::sort(rows.begin(), rows.end(),
                  [](const auto& a, const auto& b) { return a.second > b.second; });
        std::string out = fmt::format("cpu per thread over the session (total {:.0f}%):", total);
        for (size_t i = 0; i < rows.size() && i < 20; ++i) {
            out += fmt::format(" {} {:.0f}%", rows[i].first, rows[i].second);
        }
        WriteStamped(s, "final", out);
    }
    const ProcMem pm = ProcessMemory();
    WriteStamped(s, "final", MemoryLine(s, pm, pm.page_faults, session_s));
    WriteStamped(s, "final", NewWarnings(s, 40, true));
}

} // namespace

bool Enabled() {
    return S().enabled.load(std::memory_order_relaxed);
}

void SetSessionInfo(std::string key, std::string value) {
    State& s = S();
    if (s.initialized.load(std::memory_order_acquire)) {
        WriteStamped(s, "info", fmt::format("{}: {}", key, value));
        return;
    }
    std::scoped_lock lk{s.info_mutex};
    s.pending_info.emplace_back(std::move(key), std::move(value));
}

void Init() {
    State& s = S();
    if (s.initialized.load(std::memory_order_acquire)) {
        return;
    }
    const auto env_on = [](const char* name, bool def) {
        const char* v = std::getenv(name);
        if (!v || !*v) {
            return def;
        }
        return !(v[0] == '0' || v[0] == 'n' || v[0] == 'N' || v[0] == 'f' || v[0] == 'F');
    };
    // Release builds run without the diagnostic log and the capture triggers; NHLPORT_DIAG=1
    // turns them on (nhl24_diag.log, frames CSV, captures/* triggers, F1 marker).
    if (!env_on("NHLPORT_DIAG", false)) {
        LOG_INFO(Config, "nhlport diagnostics off (set NHLPORT_DIAG=1 to enable)");
        return;
    }
    s.frames_csv = env_on("NHLPORT_DIAG_FRAMES", true);
    if (const char* v = std::getenv("NHLPORT_DIAG_INTERVAL"); v && *v) {
        s.interval_s = std::clamp(std::atof(v), 1.0, 600.0);
    }
    const auto dir = Common::FS::GetUserPath(Common::FS::PathType::LogDir);
    s.log = OpenFile(dir / "nhl24_diag.log");
    if (!s.log) {
        LOG_ERROR(Config, "nhlport diag log: cannot open {}", (dir / "nhl24_diag.log").string());
        return;
    }
    if (s.frames_csv) {
        s.frames = OpenFile(dir / "nhl24_frames.csv");
        if (s.frames) {
            std::fputs(FramesHeader, s.frames);
            std::fputc('\n', s.frames);
        }
    }
    s.threads = OpenFile(dir / "nhl24_threads.csv");
    if (s.threads) {
        std::fputs("t_s,tid,name,cpu_pct\n", s.threads);
    }
    s.t0 = Clock::now();
    s.worst.reserve(WorstFrames + 1);
    s.dts.reserve(1 << 16);
    s.prev_proc_100ns = ProcessCpu100ns();
    s.prev_page_faults = ProcessMemory().page_faults;

    WriteLine(s, "# NHL24 PC diagnostic log");
    WriteLine(s, fmt::format("# started {} (t = 0.000 s); summaries every {:.0f} s; per-frame rows in "
                             "nhl24_frames.csv ({}); thread CPU in nhl24_threads.csv",
                             TimeString(), s.interval_s, s.frames_csv ? "on" : "off"));
    WriteLine(s, fmt::format("# host: {}", SystemSummary()));
    {
        std::scoped_lock lk{s.info_mutex};
        for (const auto& [k, v] : s.pending_info) {
            WriteLine(s, fmt::format("# {}: {}", k, v));
        }
        s.pending_info.clear();
    }
    static const char* const env_names[] = {
        "NHLPORT_GPU_CLOCK",   "NHLPORT_READBACK_DRAIN", "NHLPORT_STREAMED",
        "NHLPORT_MIP_MEMO",    "NHLPORT_GPU_INVAL_SMALL", "NHLPORT_META_ERASE",
        "NHLPORT_WRITEDATA_FAULT", "NHLPORT_VTX_KILL",   "NHLPORT_DIAG_FRAMES",
        "NHLPORT_DIAG_INTERVAL",
    };
    std::string env_line;
    for (const char* name : env_names) {
        if (const char* v = std::getenv(name); v && *v) {
            env_line += fmt::format(" {}={}", name, v);
        }
    }
    WriteLine(s, fmt::format("# environment overrides:{}", env_line.empty() ? " none" : env_line));
    WriteLine(s, "# frame flags: S stutter (dt > 2.5x median or > 100 ms), C pipeline/shader compile, "
                 "W gpu stall > 2 ms, R scene resolution changed, X >= 20 images created, "
                 "I >= 50 invalidations, F >= 512 KB file reads, G gpu busy >= 90% of frame, "
                 "T gpu thread CPU >= 90% of frame (work or spinning), D draws dropped, P >= 200 page faults, "
                 "A no audio output for > 100 ms, Q flip backlog >= 3, M user marker");
    WriteLine(s, "");
    if (s.log) {
        std::fflush(s.log);
    }
    if (s.frames) {
        std::fflush(s.frames);
    }
    if (s.threads) {
        std::fflush(s.threads);
    }
    s.initialized.store(true, std::memory_order_release);
    s.enabled.store(true, std::memory_order_release);
    s.sampler = std::thread(SamplerThread);
    s.gpu_poll = std::thread(GpuPollThread);
    LOG_INFO(Config, "nhlport diag log: {}", (dir / "nhl24_diag.log").string());
}

void Flush() {
    State& s = S();
    if (!s.initialized.load(std::memory_order_acquire)) {
        return;
    }
    FlushFrames(s);
    std::unique_lock lk{s.log_mutex, std::defer_lock};
    if (lk.try_lock_for(std::chrono::seconds(1))) {
        if (s.log) {
            std::fflush(s.log);
        }
    }
    if (s.threads) {
        std::fflush(s.threads);
    }
}

void Shutdown(const char* reason) {
    State& s = S();
    if (!s.initialized.load(std::memory_order_acquire) || s.shutdown_done.exchange(true)) {
        return;
    }
    s.enabled.store(false, std::memory_order_release);
    s.gpu_poll_stop.store(true, std::memory_order_relaxed);
    if (s.gpu_poll.joinable()) {
        s.gpu_poll.join();
    }
    {
        std::unique_lock lk{s.sampler_mutex};
        s.sampler_stop = true;
        s.sampler_cv.notify_all();
        // Wait briefly for the sampler; on a crash path it may be stuck, so do not block forever.
        s.sampler_cv.wait_for(lk, std::chrono::seconds(3), [&] { return s.sampler_exited; });
    }
    if (s.sampler.joinable()) {
        if (s.sampler_exited) {
            s.sampler.join();
        } else {
            s.sampler.detach();
        }
    }
    FlushFrames(s);
    WriteFinalSummary(s, reason);
    std::unique_lock lk{s.log_mutex, std::defer_lock};
    const bool locked = lk.try_lock_for(std::chrono::seconds(2));
    if (s.log) {
        std::fflush(s.log);
        if (locked) {
            std::fclose(s.log);
            s.log = nullptr;
        }
    }
    if (s.frames) {
        std::fclose(s.frames);
        s.frames = nullptr;
    }
    if (s.threads) {
        std::fclose(s.threads);
        s.threads = nullptr;
    }
}

void Event(const char* category, std::string message) {
    State& s = S();
    if (!s.enabled.load(std::memory_order_relaxed)) {
        return;
    }
    WriteStamped(s, category, message);
}

void Marker(std::string note) {
    State& s = S();
    if (!s.enabled.load(std::memory_order_relaxed)) {
        return;
    }
    u64 frame = 0;
    {
        std::scoped_lock lk{s.frame_mutex};
        frame = s.frame_no;
        s.markers.push_back(MarkerRec{Seconds(s), frame,
                                   VideoCore::Perf::gpu_frame.load(std::memory_order_relaxed), note});
        ++s.marker_count;
    }
    s.marker_flag.store(1, std::memory_order_relaxed);
    s.marker_shots.fetch_add(1, std::memory_order_relaxed);
    WriteStamped(s, "marker",
                 fmt::format("#{} at frame {} (gpu frame {}): {}", s.marker_count, frame,
                             VideoCore::Perf::gpu_frame.load(std::memory_order_relaxed), note));
}

static const std::vector<u64>& CaptureDispatchHashes() {
    static const std::vector<u64> hashes = [] {
        std::vector<u64> v;
        if (const char* e = std::getenv("NHLPORT_DIAG_F1_CS"); e && *e) {
            std::string list{e};
            size_t pos = 0;
            while (pos <= list.size()) {
                const size_t comma = list.find(',', pos);
                const std::string item = list.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
                if (!item.empty()) {
                    v.push_back(std::strtoull(item.c_str(), nullptr, 16));
                }
                if (comma == std::string::npos) {
                    break;
                }
                pos = comma + 1;
            }
        } else {
            v = {0x2c569595ull, 0x5bb1bf02ull};
        }
        return v;
    }();
    return hashes;
}

bool CaptureMatchesDispatch(u64 pgm_hash) {
    if (VideoCore::Perf::dump_cs_pending.load(std::memory_order_relaxed) == 0) {
        return false;
    }
    const auto& hashes = CaptureDispatchHashes();
    return std::find(hashes.begin(), hashes.end(), pgm_hash) != hashes.end();
}

void ArmCapture(std::string note) {
    State& s = S();
    if (!s.enabled.load(std::memory_order_relaxed)) {
        return;
    }
    Marker(std::move(note));
    VideoCore::Perf::trace_request.store(3, std::memory_order_relaxed);
    if (!CaptureDispatchHashes().empty()) {
        VideoCore::Perf::dump_cs_pending.store(1, std::memory_order_relaxed);
    }
    std::string list;
    for (const u64 h : CaptureDispatchHashes()) {
        list += fmt::format("{}{:#x}", list.empty() ? "" : ",", h);
    }
    WriteStamped(s, "capture",
                 fmt::format("draw log 3 frames + images of cs [{}] (gpu frame {})", list,
                             VideoCore::Perf::gpu_frame.load(std::memory_order_relaxed)));
}

bool ConsumeMarkerScreenshot() {
    State& s = S();
    u32 n = s.marker_shots.load(std::memory_order_relaxed);
    while (n > 0) {
        if (s.marker_shots.compare_exchange_weak(n, n - 1, std::memory_order_relaxed)) {
            return true;
        }
    }
    return false;
}

void OnGpuFrameBoundary() {
    State& s = S();
    if (!s.enabled.load(std::memory_order_relaxed)) {
        return;
    }
    const u64 scene = s.largest_depth ? s.largest_depth : s.largest_any;
    s.scene_packed.store(scene, std::memory_order_relaxed);
    s.rt_sizes.store(s.rp_count, std::memory_order_relaxed);
    s.rp_last_frame.store(s.rp_in_frame, std::memory_order_relaxed);
    if (scene != 0) {
        std::scoped_lock lk{s.res_mutex};
        ++s.res_hist[scene];
    }
    s.rp_count = 0;
    s.rp_in_frame = 0;
    s.largest_depth = 0;
    s.largest_any = 0;
}

void NoteRenderPass(u32 width, u32 height, bool has_depth) {
    State& s = S();
    C::render_passes.fetch_add(1, std::memory_order_relaxed);
    if (!s.enabled.load(std::memory_order_relaxed)) {
        return;
    }
    ++s.rp_in_frame;
    const u64 packed = (u64(width) << 32) | height;
    const u64 area = u64(width) * height;
    if (has_depth && area > (u64(s.largest_depth >> 32) * u32(s.largest_depth))) {
        s.largest_depth = packed;
    }
    if (area > (u64(s.largest_any >> 32) * u32(s.largest_any))) {
        s.largest_any = packed;
    }
    for (u32 i = 0; i < s.rp_count; ++i) {
        if (s.rp_sizes[i] == packed) {
            return;
        }
    }
    if (s.rp_count < s.rp_sizes.size()) {
        s.rp_sizes[s.rp_count++] = packed;
    }
}

void NoteComputeShortcut(const char* kind, u64 shader_hash, u64 target, std::string details) {
    State& s = S();
    if (!s.enabled.load(std::memory_order_relaxed)) {
        return;
    }
    bool first = false;
    u64 count = 0;
    {
        std::scoped_lock lk{s.shortcut_mutex};
        auto [it, is_new] =
            s.shortcuts.try_emplace(fmt::format("{} {:#x} {:#x}", kind, shader_hash, target));
        ShortcutRec& rec = it->second;
        if (is_new) {
            rec.kind = kind;
            rec.hash = shader_hash;
            rec.details = details;
            first = true;
        }
        count = ++rec.count;
    }
    if (first) {
        WriteStamped(s, "compute-shortcut",
                     fmt::format("{} cs {:#x} (gpu frame {}): {}", kind, shader_hash,
                                 VideoCore::Perf::gpu_frame.load(std::memory_order_relaxed),
                                 details));
    }
    (void)count;
}

void NotePipelineCompile(bool compute, u64 hash, double ms) {
    C::pipeline_compiles.fetch_add(1, std::memory_order_relaxed);
    C::pipeline_compile_ns.fetch_add(u64(ms * 1e6), std::memory_order_relaxed);
    State& s = S();
    if (!s.enabled.load(std::memory_order_relaxed)) {
        return;
    }
    WriteStamped(s, "compile",
                 fmt::format("{} pipeline {:#x} {:.1f} ms (gpu frame {})",
                             compute ? "compute" : "graphics", hash, ms,
                             VideoCore::Perf::gpu_frame.load(std::memory_order_relaxed)));
}

void NoteShaderCompile(std::string_view stage, u64 hash, double ms, bool permutation) {
    C::shader_compiles.fetch_add(1, std::memory_order_relaxed);
    C::shader_compile_ns.fetch_add(u64(ms * 1e6), std::memory_order_relaxed);
    State& s = S();
    if (!s.enabled.load(std::memory_order_relaxed)) {
        return;
    }
    WriteStamped(s, "compile",
                 fmt::format("{} shader {:#x}{} {:.1f} ms", stage, hash,
                             permutation ? " (permutation)" : "", ms));
}

void NoteLog(std::string_view log_class, int level, const char* file, int line, const char* func,
             std::string_view message) {
    State& s = S();
    if (!s.enabled.load(std::memory_order_relaxed)) {
        return;
    }
    // Warnings and errors from everywhere; renderer info lines too (they describe cache events).
    if (level < 3 && !(level == 2 && log_class.starts_with("Render"))) {
        return;
    }
    const u64 key = reinterpret_cast<u64>(file) ^ (u64(u32(line)) << 40);
    std::scoped_lock lk{s.sites_mutex};
    auto [it, is_new] = s.sites.try_emplace(key);
    LogSite& site = it->second;
    if (is_new) {
        site.cls = std::string(log_class);
        site.level = level;
        const std::string_view path{file};
        const auto slash = path.find_last_of("/\\");
        site.file = std::string(slash == std::string_view::npos ? path : path.substr(slash + 1));
        site.line = line;
        site.func = func ? func : "";
        site.first = std::string(message.substr(0, 200));
    }
    ++site.count;
    if (site.count <= 1 || (site.count & 15) == 0) {
        site.last = std::string(message.substr(0, 200));
    }
}

void OnGpuSubmit(u64 tick) {
    C::vk_submits.fetch_add(1, std::memory_order_relaxed);
    State& s = S();
    if (!s.enabled.load(std::memory_order_relaxed)) {
        return;
    }
    s.submit_ns[tick % TimelineRing].store(NowNs(), std::memory_order_relaxed);
}

void OnGpuComplete(u64 from_tick, u64 to_tick) {
    State& s = S();
    if (!s.enabled.load(std::memory_order_relaxed) || to_tick <= from_tick) {
        return;
    }
    if (to_tick - from_tick > TimelineRing) {
        from_tick = to_tick - TimelineRing;
    }
    const s64 now = NowNs();
    std::scoped_lock lk{s.tl_mutex};
    for (u64 t = from_tick + 1; t <= to_tick; ++t) {
        const s64 submit = s.submit_ns[t % TimelineRing].exchange(0, std::memory_order_relaxed);
        if (submit == 0 || submit > now) {
            continue;
        }
        const u64 lat = u64(now - submit);
        C::gpu_lat_ns.fetch_add(lat, std::memory_order_relaxed);
        C::gpu_lat_count.fetch_add(1, std::memory_order_relaxed);
        u64 cur_max = C::gpu_lat_max_ns.load(std::memory_order_relaxed);
        while (lat > cur_max &&
               !C::gpu_lat_max_ns.compare_exchange_weak(cur_max, lat, std::memory_order_relaxed)) {
        }
        const s64 busy_from = std::max(submit, s.last_complete_ns);
        if (now > busy_from) {
            C::gpu_busy_ns.fetch_add(u64(now - busy_from), std::memory_order_relaxed);
        }
        s.last_complete_ns = now;
    }
}

void SetVramProbe(VramProbe probe) {
    State& s = S();
    std::scoped_lock lk{s.probe_mutex};
    s.vram_probe = std::move(probe);
}

void SetGuestMemProbe(GuestMemProbe probe) {
    State& s = S();
    std::scoped_lock lk{s.probe_mutex};
    s.guest_probe = std::move(probe);
}

void SetWindowProbe(WindowProbe probe) {
    State& s = S();
    std::scoped_lock lk{s.probe_mutex};
    s.window_probe = std::move(probe);
}

void SetHostCacheProbe(HostCacheProbe probe) {
    State& s = S();
    std::scoped_lock lk{s.probe_mutex};
    s.host_cache_probe = std::move(probe);
}

void SetGpuRefreshProbe(GpuRefreshProbe probe) {
    State& s = S();
    std::scoped_lock lk{s.probe_mutex};
    s.gpu_refresh_probe = std::move(probe);
}

void OnFramePresented(double dt_ms, u32 vo_width, u32 vo_height, u32 flip_pending,
                      u64 flip_count) {
    State& s = S();
    if (!s.enabled.load(std::memory_order_relaxed)) {
        return;
    }
    const Counters cur = Snapshot();
    const u64 proc_100ns = ProcessCpu100ns();
    const ProcMem pm = ProcessMemory();
    double key_ms[SlotCount];
    SampleKeyThreads(s, key_ms);
    if (!s.have_prev_frame || dt_ms < 0) {
        s.prev_frame = cur;
        s.prev_summary = cur;
        s.have_prev_frame = true;
        s.prev_proc_100ns = proc_100ns;
        s.prev_page_faults = pm.page_faults;
        return;
    }
    const Counters p = s.prev_frame; // copy: prev_frame is overwritten below
    const auto d = [&](u64 Counters::*m) { return cur.*m - p.*m; };

    const double t_s = Seconds(s);
    const double pipe_ms = double(d(&Counters::pipe_ns)) / 1e6;
    const double shader_ms = double(d(&Counters::shader_ns)) / 1e6;
    const double stall_ms = double(d(&Counters::stall_ns)) / 1e6;
    const double rb_wait_ms = double(d(&Counters::rb_wait_ns)) / 1e6;
    const u64 lat_count = d(&Counters::lat_count);
    const double lat_ms = lat_count ? double(d(&Counters::lat_ns)) / double(lat_count) / 1e6 : 0.0;
    const double lat_max_ms =
        double(C::gpu_lat_max_ns.exchange(0, std::memory_order_relaxed)) / 1e6;
    const double busy_ms = double(d(&Counters::busy_ns)) / 1e6;
    const u64 inval = d(&Counters::cpu_inval) + d(&Counters::cp_inval) + d(&Counters::gpu_inval);
    const u64 file_kb = d(&Counters::file_bytes) >> 10;
    const double audio_wait_ms = double(d(&Counters::audio_wait_ns)) / 1e6;
    const double cpu_ms = proc_100ns > s.prev_proc_100ns ? double(proc_100ns - s.prev_proc_100ns) / 1e4 : 0.0;
    const u64 pf_delta = pm.page_faults > s.prev_page_faults ? pm.page_faults - s.prev_page_faults : 0;
    const u64 scene = s.scene_packed.load(std::memory_order_relaxed);
    const u32 scene_w = u32(scene >> 32), scene_h = u32(scene & 0xffffffffu);
    const u32 rt_sizes = s.rt_sizes.load(std::memory_order_relaxed);
    const u32 rp = s.rp_last_frame.load(std::memory_order_relaxed);
    const u64 skipped = d(&Counters::skip_nopipe) + d(&Counters::skip_bind);
    u32 win_w = 0, win_h = 0;
    {
        WindowProbe probe;
        {
            std::scoped_lock lk{s.probe_mutex};
            probe = s.window_probe;
        }
        if (probe) {
            std::tie(win_w, win_h) = probe();
        }
    }
    const bool marker = s.marker_flag.exchange(0, std::memory_order_relaxed) != 0;

    s.prev_frame = cur;
    s.prev_proc_100ns = proc_100ns;
    s.prev_page_faults = pm.page_faults;

    u64 frame_no;
    bool scene_changed = false;
    bool stutter = false;
    bool report_stutter = false;
    bool report_drop = false;
    float median;
    {
        std::scoped_lock lk{s.frame_mutex};
        frame_no = ++s.frame_no;
        s.dts.push_back(float(dt_ms));
        s.recent[s.recent_pos++ % RecentFrames] = float(dt_ms);
        if (s.recent_pos % 30 == 0) {
            std::array<float, RecentFrames> tmp = s.recent;
            const size_t n = std::min<size_t>(s.recent_pos, RecentFrames);
            std::nth_element(tmp.begin(), tmp.begin() + n / 2, tmp.begin() + n);
            s.median_ms = tmp[n / 2];
        }
        median = s.median_ms;
        stutter = dt_ms > 100.0 || (dt_ms > 25.0 && dt_ms > 2.5 * median);
        if (stutter) {
            ++s.stutters_in_window;
            report_stutter = s.stutter_events_in_window++ < 40;
        }
        scene_changed = scene != s.last_scene;
        if (scene_changed) {
            const u64 old = s.last_scene;
            s.last_scene = scene;
            WriteStamped(s, "resolution",
                         fmt::format("scene {} -> {} (frame {}, gpu frame {}, {} render target sizes)",
                                     old ? Dim(old) : "none", scene ? Dim(scene) : "none", frame_no,
                                     cur.gpu_frame, rt_sizes));
        }
        const u64 window = (u64(win_w) << 32) | win_h;
        if (window != s.last_window) {
            s.last_window = window;
            WriteStamped(s, "window", fmt::format("window {}x{}, video out {}x{}", win_w, win_h,
                                                  vo_width, vo_height));
        }
        if (skipped) {
            const auto now = Clock::now();
            if (now - s.last_drop_event > std::chrono::seconds(5)) {
                s.last_drop_event = now;
                report_drop = true;
            }
        }
    }

    char flags[16];
    u32 nf = 0;
    const auto flag = [&](char c) {
        if (nf < sizeof(flags) - 1) {
            flags[nf++] = c;
        }
    };
    if (stutter) flag('S');
    if (d(&Counters::pipes) || d(&Counters::shaders)) flag('C');
    if (stall_ms > 2.0) flag('W');
    if (scene_changed) flag('R');
    if (d(&Counters::img_new) >= 20) flag('X');
    if (inval >= 50) flag('I');
    if (file_kb >= 512) flag('F');
    if (busy_ms >= 0.9 * dt_ms) flag('G');
    if (key_ms[SlotGpu] >= 0.9 * dt_ms) flag('T');
    if (skipped) flag('D');
    if (pf_delta >= 200) flag('P');
    if (d(&Counters::audio_out) > 0) {
        s.last_audio_t = t_s;
        s.audio_started = true;
    }
    if (s.audio_started && t_s - s.last_audio_t > 0.1) flag('A');
    if (flip_pending >= 3) flag('Q');
    if (marker) flag('M');
    flags[nf] = '\0';

    if (s.frames_csv) {
        std::string row;
        row.reserve(320);
        fmt::format_to(std::back_inserter(row),
                       "{},{:.3f},{:.2f},{},{},{},{},{},{},{},{},{:.2f},{},{:.2f},{},{:.2f},{},{},{:.2f},{},"
                       "{},{},{:.2f},{:.2f},{:.2f},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},"
                       "{},{},{},{},{},{},{},{:.2f},{:.2f},{:.2f},{:.2f},{:.2f},{:.2f},{:.2f},{},{},{},"
                       "{},{},{}\n",
                       frame_no, t_s, dt_ms, cur.gpu_frame, d(&Counters::gfx_submits),
                       d(&Counters::asc_submits), d(&Counters::draws), d(&Counters::dispatches),
                       d(&Counters::skip_nopipe), d(&Counters::skip_bind), d(&Counters::pipes), pipe_ms,
                       d(&Counters::shaders), shader_ms, d(&Counters::gpu_waits), stall_ms,
                       d(&Counters::readbacks), d(&Counters::rb_bytes) >> 10, rb_wait_ms,
                       d(&Counters::rb_cpu_w), d(&Counters::rb_cpu_r), d(&Counters::vk_submits), lat_ms,
                       lat_max_ms, busy_ms, d(&Counters::img_new), d(&Counters::img_freed),
                       VideoCore::Perf::tc_images.load(std::memory_order_relaxed), d(&Counters::tc_expand),
                       d(&Counters::tc_depth), d(&Counters::tc_overlap), d(&Counters::tc_incompat),
                       d(&Counters::cpu_inval), d(&Counters::cp_inval), d(&Counters::gpu_inval),
                       d(&Counters::refresh), d(&Counters::refresh_bytes) >> 10, d(&Counters::streamed),
                       d(&Counters::snapshots), scene_w, scene_h, rt_sizes, rp, vo_width, vo_height,
                       win_w, win_h, flip_pending, d(&Counters::file_reads), file_kb,
                       d(&Counters::audio_out), audio_wait_ms, cpu_ms, key_ms[SlotGpu],
                       key_ms[SlotPresent], key_ms[SlotSim], key_ms[SlotRender], key_ms[SlotJobs],
                       pf_delta, d(&Counters::cs_meta_clear), d(&Counters::cs_image_copy),
                       d(&Counters::cs_image_clear), d(&Counters::cs_hle), flags);
        std::scoped_lock lk{s.frame_mutex};
        s.frames_buf += row;
    }

    // Worst-frame list and stutter events.
    FrameRec rec{};
    rec.frame = frame_no;
    rec.t_s = t_s;
    rec.dt_ms = dt_ms;
    rec.pipe_ms = pipe_ms + shader_ms;
    rec.stall_ms = stall_ms;
    rec.rb_wait_ms = rb_wait_ms;
    rec.gpu_busy_ms = busy_ms;
    rec.gpu_thr_ms = key_ms[SlotGpu];
    rec.jobs_ms = key_ms[SlotJobs];
    rec.pipes = u32(d(&Counters::pipes) + d(&Counters::shaders));
    rec.readbacks = u32(d(&Counters::readbacks));
    rec.img_new = u32(d(&Counters::img_new));
    rec.inval = u32(inval);
    rec.file_kb = u32(file_kb);
    rec.scene_w = scene_w;
    rec.scene_h = scene_h;
    std::memcpy(rec.flags, flags, sizeof(rec.flags));
    {
        std::scoped_lock lk{s.frame_mutex};
        if (s.worst.size() < WorstFrames) {
            s.worst.push_back(rec);
        } else {
            auto min_it = std::min_element(s.worst.begin(), s.worst.end(),
                                           [](const FrameRec& a, const FrameRec& b) {
                                               return a.dt_ms < b.dt_ms;
                                           });
            if (min_it->dt_ms < dt_ms) {
                *min_it = rec;
            }
        }
    }
    if (report_stutter) {
        WriteStamped(s, "stutter",
                     fmt::format("frame {} {:.1f} ms (median {:.1f}): compiles {} ({:.1f} ms), gpu stall "
                                 "{:.1f} ms ({} waits), readbacks {} ({} KB, wait {:.1f} ms), images +{} "
                                 "-{}, inval cpu {} cp {} gpu {}, uploads {} ({} KB), file {} KB, audio "
                                 "wait {:.1f} ms, gpu busy {:.1f} ms lat {:.1f}/{:.1f} ms, cpu {:.1f} ms "
                                 "(gpu thread {:.1f}, present {:.1f}, sim {:.1f}, render {:.1f}, jobs "
                                 "{:.1f}), page faults {}, flip pending {}, scene {}, flags {}",
                                 frame_no, dt_ms, median, rec.pipes, rec.pipe_ms, stall_ms,
                                 d(&Counters::gpu_waits), rec.readbacks, d(&Counters::rb_bytes) >> 10,
                                 rb_wait_ms, rec.img_new, d(&Counters::img_freed),
                                 d(&Counters::cpu_inval), d(&Counters::cp_inval),
                                 d(&Counters::gpu_inval), d(&Counters::refresh),
                                 d(&Counters::refresh_bytes) >> 10, file_kb, audio_wait_ms, busy_ms,
                                 lat_ms, lat_max_ms, cpu_ms, key_ms[SlotGpu], key_ms[SlotPresent],
                                 key_ms[SlotSim], key_ms[SlotRender], key_ms[SlotJobs], pf_delta,
                                 flip_pending, Dim(scene), flags));
    }
    if (report_drop) {
        WriteStamped(s, "glitch",
                     fmt::format("draws dropped in frame {}: no-pipeline {} bind-fail {} (of {} draws + "
                                 "{} dispatches); such frames are missing geometry or effects",
                                 frame_no, d(&Counters::skip_nopipe), d(&Counters::skip_bind),
                                 d(&Counters::draws), d(&Counters::dispatches)));
    }
    if (marker) {
        WriteStamped(s, "marker",
                     fmt::format("frame {} after marker: {:.1f} ms, scene {}, flags {}", frame_no,
                                 dt_ms, Dim(scene), flags));
    }
    (void)flip_count;
}

} // namespace Diag
