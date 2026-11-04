// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <limits>
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_semaphore.h"

#include "common/assert.h"
#include "common/diag_log.h"
#include "common/logging/log.h"
#include "common/thread.h"

namespace Vulkan {

constexpr u64 WAIT_TIMEOUT = std::numeric_limits<u64>::max();

Semaphore::Semaphore(const Instance& instance_) : instance{instance_} {
    const vk::StructureChain semaphore_chain = {
        vk::SemaphoreCreateInfo{},
        vk::SemaphoreTypeCreateInfo{
            .semaphoreType = vk::SemaphoreType::eTimeline,
            .initialValue = 0,
        },
    };
    auto [semaphore_result, sem] =
        instance.GetDevice().createSemaphoreUnique(semaphore_chain.get());
    ASSERT_MSG(semaphore_result == vk::Result::eSuccess, "Failed to create master semaphore: {}",
               vk::to_string(semaphore_result));
    semaphore = std::move(sem);
}

Semaphore::~Semaphore() = default;

void Semaphore::Refresh() {
    u64 this_tick{};
    u64 counter{};
    u32 retries = 0;
    do {
        this_tick = gpu_tick.load(std::memory_order_acquire);
        auto [counter_result, cntr] = instance.GetDevice().getSemaphoreCounterValue(*semaphore);
        ASSERT_MSG(counter_result == vk::Result::eSuccess,
                   "Failed to get master semaphore value: {}", vk::to_string(counter_result));
        counter = cntr;
        if (counter < this_tick) {
            return;
        }
        // nhlport: during the Neo-mode texture storm the driver reported a garbage
        // counter (0xffffffffffffffff) while the highest tick we ever handed to a
        // submit was 0x171. Accepting it poisoned gpu_tick and voided all GPU
        // synchronization (the GPU thread then spun at 100% CPU). Signaled values are
        // always < current_tick, so anything above is bogus - ignore it.
        const u64 max_issued = current_tick.load(std::memory_order_acquire) - 1;
        if (counter > max_issued) {
            static std::atomic<u32> bogus_logs{0};
            if (bogus_logs.fetch_add(1, std::memory_order_relaxed) < 10) {
                LOG_WARNING(Render_Vulkan,
                            "Semaphore::Refresh: counter {:#x} exceeds last issued tick "
                            "{:#x}; ignoring bogus GPU counter",
                            counter, max_issued);
            }
            return;
        }
        // nhlport: the GPU thread was observed spinning at 100% CPU inside Refresh during
        // the Neo-mode texture storm; report the tick dynamics when the CAS keeps failing.
        if (++retries == 1000) {
            LOG_WARNING(Render_Vulkan,
                        "Semaphore::Refresh retried {} times (gpu_tick {:#x}, counter {:#x})",
                        retries, this_tick, counter);
        }
    } while (!gpu_tick.compare_exchange_weak(this_tick, counter, std::memory_order_release,
                                             std::memory_order_relaxed));
    if (diag_tracked && counter > this_tick) {
        Diag::OnGpuComplete(this_tick, counter);
    }
}

void Semaphore::Wait(u64 tick) {
    // No need to wait if the GPU is ahead of the tick
    if (IsFree(tick)) {
        return;
    }
    // Update the GPU tick and try again
    Refresh();
    if (IsFree(tick)) {
        return;
    }

    // If none of the above is hit, fallback to a regular wait
    const vk::SemaphoreWaitInfo wait_info = {
        .semaphoreCount = 1,
        .pSemaphores = &semaphore.get(),
        .pValues = &tick,
    };

    // nhlport: wait in slices so a GPU-side stall is reported (tick numbers + thread) instead of
    // freezing silently; a lost device is a hard error rather than an endless loop.
    constexpr u64 SliceNs = 2000000000ULL;
    u32 waited_s = 0;
    while (true) {
        const auto result = instance.GetDevice().waitSemaphores(&wait_info, SliceNs);
        if (result == vk::Result::eSuccess) {
            break;
        }
        ASSERT_MSG(result == vk::Result::eTimeout, "Timeline semaphore wait failed: {}",
                   vk::to_string(result));
        waited_s += 2;
        Refresh();
        LOG_WARNING(Render_Vulkan, "timeline wait {}s: tick {} known {} current {} thread {}",
                    waited_s, tick, KnownGpuTick(), CurrentTick(), Common::GetCurrentThreadName());
    }
    Refresh();
}

} // namespace Vulkan
