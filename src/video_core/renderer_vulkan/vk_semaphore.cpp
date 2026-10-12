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
    do {
        this_tick = gpu_tick.load(std::memory_order_acquire);
        auto [counter_result, cntr] = instance.GetDevice().getSemaphoreCounterValue(*semaphore);
        ASSERT_MSG(counter_result == vk::Result::eSuccess,
                   "Failed to get master semaphore value: {}", vk::to_string(counter_result));
        counter = cntr;
        if (counter < this_tick) {
            return;
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
