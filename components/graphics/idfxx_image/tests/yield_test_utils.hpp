// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// What the tests of giving way to other tasks have in common: a task that runs only
// when the test's task gives up the CPU, and a wait that doesn't give it up.

#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <unity.h>

#include <atomic>
#include <chrono>
#include <cstdint>

namespace yield_test {

// More than the 500 ms between yields.
inline constexpr std::chrono::milliseconds past_yield_interval{700};

inline void busy_wait(std::chrono::milliseconds duration) {
    const auto start = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - start < duration) {
    }
}

// Counts while it runs: on this core, below this task and above the idle task, which a
// counter would otherwise share a one-tick yield with. It lasts only while a test acts,
// so that the test's assertions, which don't return, come after the destructor.
class background_counter {
public:
    background_counter()
        : _test_priority(uxTaskPriorityGet(nullptr)) {
        vTaskPrioritySet(nullptr, _test_priority + 2);
        const BaseType_t created = xTaskCreatePinnedToCore(
            [](void* arg) {
                auto& count = *static_cast<std::atomic<uint32_t>*>(arg);
                while (true) {
                    count.fetch_add(1, std::memory_order_relaxed);
                }
            },
            "yield_counter",
            2048,
            &_count,
            _test_priority + 1,
            &_task,
            xPortGetCoreID()
        );
        TEST_ASSERT_EQUAL(pdPASS, created);
    }

    background_counter(const background_counter&) = delete;
    background_counter& operator=(const background_counter&) = delete;

    ~background_counter() {
        vTaskDelete(_task);
        vTaskPrioritySet(nullptr, _test_priority);
    }

    [[nodiscard]] uint32_t count() const { return _count.load(std::memory_order_relaxed); }

private:
    UBaseType_t _test_priority;
    std::atomic<uint32_t> _count{0};
    TaskHandle_t _task = nullptr;
};

} // namespace yield_test
