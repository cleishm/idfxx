// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#include "idfxx/sched"

#include <idfxx/chrono>

#include <cassert>
#include <cstdint>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <rom/ets_sys.h>

namespace {

constexpr auto busy_wait_threshold = std::chrono::milliseconds{10};

[[nodiscard]] bool in_isr_context() noexcept {
    return xPortInIsrContext() != 0;
}

} // anonymous namespace

namespace idfxx {

template<>
void delay(const std::chrono::microseconds& duration) {
    if (duration <= std::chrono::microseconds::zero()) {
        return;
    }

    assert(!in_isr_context() && "delay() must not be called from ISR context");

    if (duration < busy_wait_threshold) {
        // Busy-wait path with microsecond precision
        auto us = duration.count();
        // Handle uint32_t overflow for very long delays
        while (us > UINT32_MAX) {
            ets_delay_us(UINT32_MAX);
            us -= UINT32_MAX;
        }
        if (us > 0) {
            ets_delay_us(static_cast<uint32_t>(us));
        }
    } else {
        // Scheduler-friendly path
        vTaskDelay(chrono::ticks(duration));
    }
}

void IRAM_ATTR yield_from_isr(bool higher_priority_task_woken) noexcept {
    portYIELD_FROM_ISR(higher_priority_task_woken);
}

} // namespace idfxx
