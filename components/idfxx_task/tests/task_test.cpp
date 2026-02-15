// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Unit tests for idfxx task
// Uses ESP-IDF Unity test framework with compile-time static_asserts

#include <idfxx/task>
#include <unity.h>

#include <atomic>
#include <chrono>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <idfxx/chrono>
#include <idfxx/memory>
#include <idfxx/sched>
#include <type_traits>

using namespace idfxx;
using namespace std::chrono_literals;

// =============================================================================
// Compile-time tests (static_assert)
// These verify correctness at compile time - if this file compiles, they pass.
// =============================================================================

// task is not default constructible
static_assert(!std::is_default_constructible_v<task>);

// task is not copyable
static_assert(!std::is_copy_constructible_v<task>);
static_assert(!std::is_copy_assignable_v<task>);

// task is not movable
static_assert(!std::is_move_constructible_v<task>);
static_assert(!std::is_move_assignable_v<task>);

// task::self is not default constructible
static_assert(!std::is_default_constructible_v<task::self>);

// task::self is not copyable
static_assert(!std::is_copy_constructible_v<task::self>);
static_assert(!std::is_copy_assignable_v<task::self>);

// =============================================================================
// Runtime tests (Unity TEST_CASE)
// =============================================================================

TEST_CASE("task::make with functional callback succeeds", "[idfxx][task]") {
    std::atomic<bool> running{false};
    auto result = task::make({.name = "test_task"}, [&running](task::self& self) {
        running.store(true);
        // Keep running so we can check state
        while (running.load()) {
            idfxx::delay(10ms);
        }
    });

    TEST_ASSERT_TRUE(result.has_value());
    TEST_ASSERT_NOT_NULL(result->get());
    TEST_ASSERT_NOT_NULL((*result)->idf_handle());
    TEST_ASSERT_EQUAL_STRING("test_task", (*result)->name().c_str());

    // Wait for task to start
    idfxx::delay(50ms);
    TEST_ASSERT_TRUE(running.load());

    // Signal task to stop
    running.store(false);
    idfxx::delay(50ms);
}

TEST_CASE("task::make with raw callback succeeds", "[idfxx][task]") {
    static std::atomic<bool> called{false};
    called.store(false);

    auto result = task::make(
        {.name = "test_task_raw"},
        [](task::self&, void* arg) {
            auto* flag = static_cast<std::atomic<bool>*>(arg);
            flag->store(true);
            // Keep running briefly
            vTaskDelay(pdMS_TO_TICKS(100));
        },
        &called
    );

    TEST_ASSERT_TRUE(result.has_value());
    TEST_ASSERT_NOT_NULL(result->get());
    TEST_ASSERT_NOT_NULL((*result)->idf_handle());

    // Wait for callback to execute
    idfxx::delay(50ms);
    TEST_ASSERT_TRUE(called.load());
}

TEST_CASE("task::config default values", "[idfxx][task]") {
    task::config cfg{};
    TEST_ASSERT_EQUAL_STRING("task", std::string{cfg.name}.c_str());
    TEST_ASSERT_EQUAL(4096, cfg.stack_size);
    TEST_ASSERT_EQUAL(5, cfg.priority);
    TEST_ASSERT_FALSE(cfg.core_affinity.has_value());
    TEST_ASSERT_TRUE(cfg.stack_mem == memory_type::internal);
}

TEST_CASE("task name is stored correctly", "[idfxx][task]") {
    auto result = task::make({.name = "my_custom_task"}, [](task::self& self) { idfxx::delay(100ms); });

    TEST_ASSERT_TRUE(result.has_value());
    TEST_ASSERT_EQUAL_STRING("my_custom_task", (*result)->name().c_str());
}

TEST_CASE("task priority can be retrieved", "[idfxx][task]") {
    auto result =
        task::make({.name = "priority_test", .priority = 7}, [](task::self& self) { idfxx::delay(100ms); });

    TEST_ASSERT_TRUE(result.has_value());
    TEST_ASSERT_EQUAL(7, (*result)->priority());
}

TEST_CASE("task stack_high_water_mark returns sensible value", "[idfxx][task]") {
    constexpr size_t stack_size = 4096;
    auto result = task::make(
        {.name = "hwm_test", .stack_size = stack_size}, [](task::self& self) { idfxx::delay(100ms); }
    );

    TEST_ASSERT_TRUE(result.has_value());
    auto& t = *result;

    idfxx::delay(50ms);
    size_t hwm = t->stack_high_water_mark();
    TEST_ASSERT_GREATER_THAN(0, hwm);
    TEST_ASSERT_LESS_THAN(stack_size, hwm);
}

TEST_CASE("task priority can be changed", "[idfxx][task]") {
    auto result =
        task::make({.name = "set_priority_test", .priority = 5}, [](task::self& self) { idfxx::delay(100ms); });

    TEST_ASSERT_TRUE(result.has_value());
    auto& t = *result;

    TEST_ASSERT_EQUAL(5, t->priority());

    auto set_result = t->try_set_priority(10);
    TEST_ASSERT_TRUE(set_result.has_value());
    TEST_ASSERT_EQUAL(10, t->priority());
}

TEST_CASE("task suspend and resume", "[idfxx][task]") {
    std::atomic<int> counter{0};
    std::atomic<bool> running{true};

    auto result = task::make({.name = "suspend_test"}, [&counter, &running](task::self& self) {
        while (running.load()) {
            counter.fetch_add(1);
            idfxx::delay(10ms);
        }
    });

    TEST_ASSERT_TRUE(result.has_value());
    auto& t = *result;

    // Let task run
    idfxx::delay(50ms);
    TEST_ASSERT_GREATER_THAN(0, counter.load());

    // Suspend
    auto suspend_result = t->try_suspend();
    TEST_ASSERT_TRUE(suspend_result.has_value());

    // Read counter after suspend, then verify it doesn't increase
    int count_after_suspend = counter.load();
    idfxx::delay(50ms);
    TEST_ASSERT_EQUAL(count_after_suspend, counter.load());

    // Resume
    auto resume_result = t->try_resume();
    TEST_ASSERT_TRUE(resume_result.has_value());

    // Wait and check counter increases again
    idfxx::delay(50ms);
    int count_after_resume = counter.load();
    TEST_ASSERT_GREATER_THAN(count_after_suspend, count_after_resume);

    // Cleanup
    running.store(false);
    idfxx::delay(50ms);
}

TEST_CASE("task is_completed returns true after function returns", "[idfxx][task]") {
    auto result = task::make({.name = "complete_test"}, [](task::self& self) {
        // Do some brief work and return
        idfxx::delay(10ms);
    });

    TEST_ASSERT_TRUE(result.has_value());
    auto& t = *result;

    // Initially should not be completed
    TEST_ASSERT_FALSE(t->is_completed());

    // Wait for task to complete
    idfxx::delay(100ms);

    // Now should be completed
    TEST_ASSERT_TRUE(t->is_completed());
}

TEST_CASE("task joinable returns correct state", "[idfxx][task]") {
    auto result = task::make({.name = "joinable_test"}, [](task::self& self) { idfxx::delay(100ms); });

    TEST_ASSERT_TRUE(result.has_value());
    auto& t = *result;

    // Should be joinable
    TEST_ASSERT_TRUE(t->joinable());
}

TEST_CASE("task::try_spawn creates fire-and-forget task", "[idfxx][task]") {
    static std::atomic<bool> executed{false};
    executed.store(false);

    auto result = task::try_spawn({.name = "spawn_test"}, [](task::self&) {
        executed.store(true);
        // Task cleans up automatically after returning
    });

    TEST_ASSERT_TRUE(result.has_value());

    // Wait for task to execute and complete
    idfxx::delay(100ms);
    TEST_ASSERT_TRUE(executed.load());
}

TEST_CASE("task::try_spawn with raw callback", "[idfxx][task]") {
    static std::atomic<bool> executed{false};
    executed.store(false);

    auto result = task::try_spawn(
        {.name = "spawn_raw_test"},
        [](task::self&, void* arg) {
            auto* flag = static_cast<std::atomic<bool>*>(arg);
            flag->store(true);
        },
        &executed
    );

    TEST_ASSERT_TRUE(result.has_value());

    // Wait for task to execute and complete
    idfxx::delay(100ms);
    TEST_ASSERT_TRUE(executed.load());
}

TEST_CASE("task detach releases ownership", "[idfxx][task]") {
    static std::atomic<bool> completed{false};
    completed.store(false);

    auto result = task::make({.name = "detach_test"}, [](task::self& self) {
        idfxx::delay(50ms);
        completed.store(true);
    });

    TEST_ASSERT_TRUE(result.has_value());
    auto& t = *result;

    TEST_ASSERT_TRUE(t->joinable());

    // Detach
    auto detach_result = t->try_detach();
    TEST_ASSERT_TRUE(detach_result.has_value());

    // Should no longer be joinable
    TEST_ASSERT_FALSE(t->joinable());
    TEST_ASSERT_NULL(t->idf_handle());

    // Wait for task to complete (it cleans up automatically)
    idfxx::delay(100ms);
    TEST_ASSERT_TRUE(completed.load());
}

TEST_CASE("task is_completed returns false after detach", "[idfxx][task]") {
    auto result = task::make({.name = "complete_detach"}, [](task::self& self) { idfxx::delay(100ms); });

    TEST_ASSERT_TRUE(result.has_value());
    auto& t = *result;

    // Detach the task
    auto detach_result = t->try_detach();
    TEST_ASSERT_TRUE(detach_result.has_value());

    // is_completed should return false (not crash) after detach
    TEST_ASSERT_FALSE(t->is_completed());
}

TEST_CASE("task detach after completion succeeds and clears state", "[idfxx][task]") {
    auto result = task::make({.name = "detach_after"}, [](task::self&) {
        // Return immediately
    });

    TEST_ASSERT_TRUE(result.has_value());
    auto& t = *result;

    // Wait for task to complete
    idfxx::delay(100ms);
    TEST_ASSERT_TRUE(t->is_completed());

    // Detach after completion should succeed
    auto detach_result = t->try_detach();
    TEST_ASSERT_TRUE(detach_result.has_value());
    TEST_ASSERT_FALSE(t->joinable());

    // is_completed should return false after detach (handle is nullptr)
    TEST_ASSERT_FALSE(t->is_completed());
}

TEST_CASE("task detach on already detached task fails", "[idfxx][task]") {
    auto result = task::make({.name = "double_detach"}, [](task::self& self) { idfxx::delay(100ms); });

    TEST_ASSERT_TRUE(result.has_value());
    auto& t = *result;

    // First detach should succeed
    auto detach_result = t->try_detach();
    TEST_ASSERT_TRUE(detach_result.has_value());

    // Second detach should fail
    auto second_detach = t->try_detach();
    TEST_ASSERT_FALSE(second_detach.has_value());
    TEST_ASSERT_EQUAL(std::to_underlying(errc::invalid_state), second_detach.error().value());
}

TEST_CASE("task operations fail after detach", "[idfxx][task]") {
    auto result = task::make({.name = "ops_after_detach"}, [](task::self& self) { idfxx::delay(100ms); });

    TEST_ASSERT_TRUE(result.has_value());
    auto& t = *result;

    // Detach
    auto detach_result = t->try_detach();
    TEST_ASSERT_TRUE(detach_result.has_value());

    // Operations should fail
    TEST_ASSERT_FALSE(t->try_suspend().has_value());
    TEST_ASSERT_FALSE(t->try_resume().has_value());
    TEST_ASSERT_FALSE(t->try_set_priority(5).has_value());
}

TEST_CASE("task operations fail after completion", "[idfxx][task]") {
    auto result = task::make({.name = "ops_after_complete"}, [](task::self& self) {
        // Return immediately
    });

    TEST_ASSERT_TRUE(result.has_value());
    auto& t = *result;

    // Wait for task to complete
    idfxx::delay(100ms);
    TEST_ASSERT_TRUE(t->is_completed());

    // Operations should fail with invalid_state
    auto suspend_result = t->try_suspend();
    TEST_ASSERT_FALSE(suspend_result.has_value());
    TEST_ASSERT_EQUAL(std::to_underlying(errc::invalid_state), suspend_result.error().value());

    auto resume_result = t->try_resume();
    TEST_ASSERT_FALSE(resume_result.has_value());
    TEST_ASSERT_EQUAL(std::to_underlying(errc::invalid_state), resume_result.error().value());

    auto priority_result = t->try_set_priority(5);
    TEST_ASSERT_FALSE(priority_result.has_value());
    TEST_ASSERT_EQUAL(std::to_underlying(errc::invalid_state), priority_result.error().value());

    // priority() should return 0
    TEST_ASSERT_EQUAL(0, t->priority());

    // resume_from_isr should return false
    TEST_ASSERT_FALSE(t->resume_from_isr());
}

TEST_CASE("task detach succeeds after completion", "[idfxx][task]") {
    auto result = task::make({.name = "detach_complete"}, [](task::self& self) {
        // Return immediately
    });

    TEST_ASSERT_TRUE(result.has_value());
    auto& t = *result;

    // Wait for task to complete
    idfxx::delay(100ms);
    TEST_ASSERT_TRUE(t->is_completed());

    // Detach should succeed even after completion
    auto detach_result = t->try_detach();
    TEST_ASSERT_TRUE(detach_result.has_value());
    TEST_ASSERT_FALSE(t->joinable());
}

// =============================================================================
// Join tests
// =============================================================================

TEST_CASE("try_join on completed task succeeds immediately", "[idfxx][task][join]") {
    auto result = task::make({.name = "join_completed"}, [](task::self&) {
        idfxx::delay(10ms);
    });

    TEST_ASSERT_TRUE(result.has_value());
    auto& t = *result;

    // Wait for task to complete
    idfxx::delay(100ms);
    TEST_ASSERT_TRUE(t->is_completed());

    // Join on already-completed task should succeed immediately
    auto join_result = t->try_join();
    TEST_ASSERT_TRUE(join_result.has_value());
    TEST_ASSERT_FALSE(t->joinable());
}

TEST_CASE("try_join blocks until completion", "[idfxx][task][join]") {
    std::atomic<bool> completed{false};

    auto result = task::make({.name = "join_block"}, [&completed](task::self&) {
        idfxx::delay(100ms);
        completed.store(true);
    });

    TEST_ASSERT_TRUE(result.has_value());
    auto& t = *result;

    TEST_ASSERT_FALSE(completed.load());

    // Join should block until task completes
    auto join_result = t->try_join();
    TEST_ASSERT_TRUE(join_result.has_value());
    TEST_ASSERT_TRUE(completed.load());
    TEST_ASSERT_FALSE(t->joinable());
}

TEST_CASE("try_join with timeout succeeds", "[idfxx][task][join]") {
    auto result = task::make({.name = "join_timeout_ok"}, [](task::self&) {
        idfxx::delay(50ms);
    });

    TEST_ASSERT_TRUE(result.has_value());
    auto& t = *result;

    // Join with generous timeout should succeed
    auto join_result = t->try_join(500ms);
    TEST_ASSERT_TRUE(join_result.has_value());
    TEST_ASSERT_FALSE(t->joinable());
}

TEST_CASE("try_join with timeout expires", "[idfxx][task][join]") {
    auto result = task::make({.name = "join_timeout_fail"}, [](task::self&) {
        idfxx::delay(5000ms);
    });

    TEST_ASSERT_TRUE(result.has_value());
    auto& t = *result;

    // Join with short timeout should fail
    auto join_result = t->try_join(50ms);
    TEST_ASSERT_FALSE(join_result.has_value());
    TEST_ASSERT_EQUAL(std::to_underlying(errc::timeout), join_result.error().value());

    // Task should remain joinable after timeout
    TEST_ASSERT_TRUE(t->joinable());

    // Detach to avoid blocking in destructor
    auto detach_result = t->try_detach();
    TEST_ASSERT_TRUE(detach_result.has_value());
}

TEST_CASE("try_join on detached task fails", "[idfxx][task][join]") {
    auto result = task::make({.name = "join_detached"}, [](task::self&) {
        idfxx::delay(100ms);
    });

    TEST_ASSERT_TRUE(result.has_value());
    auto& t = *result;

    // Detach first
    auto detach_result = t->try_detach();
    TEST_ASSERT_TRUE(detach_result.has_value());

    // Join should fail
    auto join_result = t->try_join();
    TEST_ASSERT_FALSE(join_result.has_value());
    TEST_ASSERT_EQUAL(std::to_underlying(errc::invalid_state), join_result.error().value());
}

TEST_CASE("double try_join fails", "[idfxx][task][join]") {
    auto result = task::make({.name = "double_join"}, [](task::self&) {
        idfxx::delay(10ms);
    });

    TEST_ASSERT_TRUE(result.has_value());
    auto& t = *result;

    // First join should succeed
    auto join_result = t->try_join();
    TEST_ASSERT_TRUE(join_result.has_value());
    TEST_ASSERT_FALSE(t->joinable());

    // Second join should fail
    auto second_join = t->try_join();
    TEST_ASSERT_FALSE(second_join.has_value());
    TEST_ASSERT_EQUAL(std::to_underlying(errc::invalid_state), second_join.error().value());
}

TEST_CASE("destructor auto-joins task", "[idfxx][task][join]") {
    std::atomic<bool> completed{false};

    {
        auto result = task::make({.name = "dtor_join"}, [&completed](task::self&) {
            idfxx::delay(100ms);
            completed.store(true);
        });

        TEST_ASSERT_TRUE(result.has_value());
        // Destructor should block until task completes
    }

    // After block exits (destructor joined), task should have completed
    TEST_ASSERT_TRUE(completed.load());
}

TEST_CASE("try_join timeout then detach", "[idfxx][task][join]") {
    auto result = task::make({.name = "join_then_detach"}, [](task::self&) {
        idfxx::delay(5000ms);
    });

    TEST_ASSERT_TRUE(result.has_value());
    auto& t = *result;

    // Join with short timeout should fail
    auto join_result = t->try_join(10ms);
    TEST_ASSERT_FALSE(join_result.has_value());
    TEST_ASSERT_EQUAL(std::to_underlying(errc::timeout), join_result.error().value());

    // Detach should succeed
    auto detach_result = t->try_detach();
    TEST_ASSERT_TRUE(detach_result.has_value());
    TEST_ASSERT_FALSE(t->joinable());
}

// =============================================================================
// Stop signaling tests
// =============================================================================

TEST_CASE("stop_requested initially false", "[idfxx][task][stop]") {
    std::atomic<bool> self_stop{true};

    auto result = task::make({.name = "stop_init"}, [&self_stop](task::self& self) {
        self_stop.store(self.stop_requested());
        idfxx::delay(100ms);
    });

    TEST_ASSERT_TRUE(result.has_value());
    auto& t = *result;

    idfxx::delay(50ms);
    TEST_ASSERT_FALSE(t->stop_requested());
    TEST_ASSERT_FALSE(self_stop.load());
}

TEST_CASE("request_stop sets flag and returns true", "[idfxx][task][stop]") {
    auto result = task::make({.name = "stop_set"}, [](task::self&) { idfxx::delay(500ms); });

    TEST_ASSERT_TRUE(result.has_value());
    auto& t = *result;

    TEST_ASSERT_FALSE(t->stop_requested());

    bool was_new = t->request_stop();
    TEST_ASSERT_TRUE(was_new);
    TEST_ASSERT_TRUE(t->stop_requested());
}

TEST_CASE("request_stop is idempotent", "[idfxx][task][stop]") {
    auto result = task::make({.name = "stop_idem"}, [](task::self&) { idfxx::delay(500ms); });

    TEST_ASSERT_TRUE(result.has_value());
    auto& t = *result;

    bool first = t->request_stop();
    TEST_ASSERT_TRUE(first);

    bool second = t->request_stop();
    TEST_ASSERT_FALSE(second);

    TEST_ASSERT_TRUE(t->stop_requested());
}

TEST_CASE("request_stop on detached task returns false", "[idfxx][task][stop]") {
    auto result = task::make({.name = "stop_detach"}, [](task::self&) { idfxx::delay(100ms); });

    TEST_ASSERT_TRUE(result.has_value());
    auto& t = *result;

    auto detach_result = t->try_detach();
    TEST_ASSERT_TRUE(detach_result.has_value());

    TEST_ASSERT_FALSE(t->request_stop());
    TEST_ASSERT_FALSE(t->stop_requested());
}

TEST_CASE("self::stop_requested reflects external request", "[idfxx][task][stop]") {
    std::atomic<bool> self_stop_before{true};
    std::atomic<bool> self_stop_after{false};

    auto result = task::make({.name = "stop_self"}, [&](task::self& self) {
        self_stop_before.store(self.stop_requested());
        // Wait for external request_stop
        idfxx::delay(100ms);
        self_stop_after.store(self.stop_requested());
        idfxx::delay(100ms);
    });

    TEST_ASSERT_TRUE(result.has_value());
    auto& t = *result;

    idfxx::delay(50ms);
    TEST_ASSERT_FALSE(self_stop_before.load());

    t->request_stop();

    idfxx::delay(100ms);
    TEST_ASSERT_TRUE(self_stop_after.load());
}

TEST_CASE("task loop exits on stop_requested", "[idfxx][task][stop]") {
    std::atomic<int> iterations{0};

    auto result = task::make({.name = "stop_loop"}, [&iterations](task::self& self) {
        while (!self.stop_requested()) {
            iterations.fetch_add(1);
            idfxx::delay(10ms);
        }
    });

    TEST_ASSERT_TRUE(result.has_value());
    auto& t = *result;

    // Let it run a bit
    idfxx::delay(50ms);
    TEST_ASSERT_GREATER_THAN(0, iterations.load());

    // Request stop and join
    t->request_stop();
    auto join_result = t->try_join(500ms);
    TEST_ASSERT_TRUE(join_result.has_value());
}

TEST_CASE("destructor requests stop before joining", "[idfxx][task][stop]") {
    std::atomic<bool> exited{false};

    {
        auto result = task::make({.name = "stop_dtor"}, [&exited](task::self& self) {
            while (!self.stop_requested()) {
                idfxx::delay(10ms);
            }
            exited.store(true);
        });

        TEST_ASSERT_TRUE(result.has_value());

        // Let the task start running
        idfxx::delay(50ms);
        TEST_ASSERT_FALSE(exited.load());

        // Destructor fires here — should request_stop() then join
    }

    TEST_ASSERT_TRUE(exited.load());
}

// =============================================================================
// Static utility method tests
// =============================================================================

TEST_CASE("task::current_handle returns valid handle", "[idfxx][task]") {
    TaskHandle_t handle = task::current_handle();
    TEST_ASSERT_NOT_NULL(handle);
}

TEST_CASE("task::current_name returns non-empty", "[idfxx][task]") {
    std::string name = task::current_name();
    TEST_ASSERT_FALSE(name.empty());
}

TEST_CASE("delay_until provides periodic timing", "[idfxx][task]") {
    auto next = idfxx::chrono::tick_clock::now() + 20ms;
    int iterations = 0;

    // Run for a few iterations
    while (iterations < 5) {
        idfxx::delay_until(next);
        next += 20ms;
        iterations++;
    }

    // If we get here without issues, delay_until is working
    TEST_ASSERT_EQUAL(5, iterations);
}

TEST_CASE("task destructor cleans up properly", "[idfxx][task]") {
    // This test mainly verifies no crashes/leaks occur
    {
        auto result = task::make({.name = "destructor_test"}, [](task::self& self) { idfxx::delay(100ms); });

        TEST_ASSERT_TRUE(result.has_value());
        // Task goes out of scope and should be deleted
    }

    // Small delay to let cleanup complete
    idfxx::delay(50ms);

    // If we get here without crashing, cleanup worked
}

TEST_CASE("task with core affinity", "[idfxx][task]") {
    std::atomic<bool> executed{false};

    auto result = task::make(
        {.name = "core_affinity_test", .core_affinity = core_id::core_0},
        [&executed](task::self& self) {
            executed.store(true);
            idfxx::delay(50ms);
        }
    );

    TEST_ASSERT_TRUE(result.has_value());

    // Wait for execution
    idfxx::delay(100ms);
    TEST_ASSERT_TRUE(executed.load());
}

TEST_CASE("task with explicit internal stack memory", "[idfxx][task]") {
    std::atomic<bool> executed{false};

    auto result = task::make(
        {.name = "internal_stack", .stack_mem = memory_type::internal},
        [&executed](task::self&) {
            executed.store(true);
            idfxx::delay(50ms);
        }
    );

    TEST_ASSERT_TRUE(result.has_value());

    idfxx::delay(100ms);
    TEST_ASSERT_TRUE(executed.load());
}

#if CONFIG_SPIRAM
TEST_CASE("task with spiram stack memory", "[idfxx][task]") {
    std::atomic<bool> executed{false};

    auto result = task::make(
        {.name = "spiram_stack", .stack_size = 8192, .stack_mem = memory_type::spiram},
        [&executed](task::self&) {
            executed.store(true);
            idfxx::delay(50ms);
        }
    );

    TEST_ASSERT_TRUE(result.has_value());

    idfxx::delay(100ms);
    TEST_ASSERT_TRUE(executed.load());
}
#endif

TEST_CASE("task is_completed returns true for raw function pointer tasks", "[idfxx][task]") {
    auto result = task::make(
        {.name = "raw_complete"},
        [](task::self&, void*) {
            vTaskDelay(pdMS_TO_TICKS(10));
            // Return after brief delay
        },
        nullptr
    );

    TEST_ASSERT_TRUE(result.has_value());
    auto& t = *result;

    // Initially should not be completed
    TEST_ASSERT_FALSE(t->is_completed());

    // Wait for task to complete
    idfxx::delay(100ms);

    // is_completed should return true after task function returns
    TEST_ASSERT_TRUE(t->is_completed());
}

TEST_CASE("task detach works for raw function pointer tasks", "[idfxx][task]") {
    static std::atomic<bool> completed{false};
    completed.store(false);

    auto result = task::make(
        {.name = "raw_detach"},
        [](task::self&, void* arg) {
            auto* flag = static_cast<std::atomic<bool>*>(arg);
            idfxx::delay(50ms);
            flag->store(true);
        },
        &completed
    );

    TEST_ASSERT_TRUE(result.has_value());
    auto& t = *result;

    TEST_ASSERT_TRUE(t->joinable());

    // Detach
    auto detach_result = t->try_detach();
    TEST_ASSERT_TRUE(detach_result.has_value());

    // Should no longer be joinable
    TEST_ASSERT_FALSE(t->joinable());
    TEST_ASSERT_NULL(t->idf_handle());

    // Wait for task to complete (it cleans up automatically)
    idfxx::delay(100ms);
    TEST_ASSERT_TRUE(completed.load());
}

// =============================================================================
// task::self tests
// =============================================================================

TEST_CASE("task::self suspend and resume from another task", "[idfxx][task][self]") {
    std::atomic<bool> suspended{false};
    std::atomic<bool> resumed{false};

    auto result = task::make({.name = "self_suspend"}, [&suspended, &resumed](task::self& self) {
        suspended.store(true);
        self.suspend();
        // After resume, we get here
        resumed.store(true);
    });

    TEST_ASSERT_TRUE(result.has_value());
    auto& t = *result;

    // Wait for task to suspend itself
    idfxx::delay(50ms);
    TEST_ASSERT_TRUE(suspended.load());
    TEST_ASSERT_FALSE(resumed.load());

    // Resume the task
    auto resume_result = t->try_resume();
    TEST_ASSERT_TRUE(resume_result.has_value());

    // Wait for task to continue
    idfxx::delay(50ms);
    TEST_ASSERT_TRUE(resumed.load());
}

TEST_CASE("task::self priority round-trip", "[idfxx][task][self]") {
    std::atomic<unsigned int> initial_prio{0};
    std::atomic<unsigned int> changed_prio{0};

    auto result = task::make({.name = "self_prio", .priority = 7}, [&](task::self& self) {
        initial_prio.store(self.priority());
        self.set_priority(12);
        changed_prio.store(self.priority());
        idfxx::delay(100ms);
    });

    TEST_ASSERT_TRUE(result.has_value());

    // Wait for task to complete its priority operations
    idfxx::delay(50ms);
    TEST_ASSERT_EQUAL(7, initial_prio.load());
    TEST_ASSERT_EQUAL(12, changed_prio.load());
}

TEST_CASE("task::self stack_high_water_mark returns sensible value", "[idfxx][task][self]") {
    std::atomic<size_t> hwm{0};

    auto result = task::make({.name = "self_hwm"}, [&hwm](task::self& self) {
        hwm.store(self.stack_high_water_mark());
        idfxx::delay(100ms);
    });

    TEST_ASSERT_TRUE(result.has_value());

    idfxx::delay(50ms);
    TEST_ASSERT_GREATER_THAN(0, hwm.load());
}

TEST_CASE("task::self name matches configured name", "[idfxx][task][self]") {
    static std::string observed_name;

    auto result = task::make({.name = "self_name_test"}, [](task::self& self) {
        observed_name = self.name();
        idfxx::delay(100ms);
    });

    TEST_ASSERT_TRUE(result.has_value());

    // Wait for task to read its name
    idfxx::delay(50ms);
    TEST_ASSERT_EQUAL_STRING("self_name_test", observed_name.c_str());
}

TEST_CASE("task::self delay works", "[idfxx][task][self]") {
    std::atomic<bool> completed{false};

    auto result = task::make({.name = "self_delay"}, [&completed](task::self& self) {
        idfxx::delay(50ms);
        completed.store(true);
    });

    TEST_ASSERT_TRUE(result.has_value());

    // Wait for task to complete
    idfxx::delay(100ms);
    TEST_ASSERT_TRUE(completed.load());
}

TEST_CASE("task::self idf_handle is non-null", "[idfxx][task][self]") {
    static TaskHandle_t observed_handle = nullptr;

    auto result = task::make({.name = "self_handle"}, [](task::self& self) {
        observed_handle = self.idf_handle();
        idfxx::delay(100ms);
    });

    TEST_ASSERT_TRUE(result.has_value());

    // Wait for task to read its handle
    idfxx::delay(50ms);
    TEST_ASSERT_NOT_NULL(observed_handle);

    // The handle from self should match the task object's handle
    TEST_ASSERT_EQUAL_PTR((*result)->idf_handle(), observed_handle);
}

TEST_CASE("yield does not crash", "[idfxx][task][self]") {
    std::atomic<bool> completed{false};

    auto result = task::make({.name = "self_yield"}, [&completed](task::self&) {
        yield();
        yield();
        yield();
        completed.store(true);
    });

    TEST_ASSERT_TRUE(result.has_value());

    // Wait for task to complete
    idfxx::delay(50ms);
    TEST_ASSERT_TRUE(completed.load());
}

TEST_CASE("task::self delay_until provides periodic timing", "[idfxx][task][self]") {
    std::atomic<int> iterations{0};

    auto result = task::make({.name = "self_delay_until"}, [&iterations](task::self&) {
        auto next = idfxx::chrono::tick_clock::now() + 20ms;
        for (int i = 0; i < 5; ++i) {
            idfxx::delay_until(next);
            next += 20ms;
            iterations.fetch_add(1);
        }
    });

    TEST_ASSERT_TRUE(result.has_value());

    // Wait for task to complete all iterations
    idfxx::delay(200ms);
    TEST_ASSERT_EQUAL(5, iterations.load());
}

TEST_CASE("task::self is_detached returns false for owned task", "[idfxx][task][self]") {
    std::atomic<bool> detached{false};
    std::atomic<bool> checked{false};

    auto result = task::make({.name = "self_not_detached"}, [&detached, &checked](task::self& self) {
        detached.store(self.is_detached());
        checked.store(true);
        idfxx::delay(100ms);
    });

    TEST_ASSERT_TRUE(result.has_value());

    // Wait for task to check
    idfxx::delay(50ms);
    TEST_ASSERT_TRUE(checked.load());
    TEST_ASSERT_FALSE(detached.load());
}

TEST_CASE("task::self is_detached returns true after detach", "[idfxx][task][self]") {
    std::atomic<bool> detached_before{true};
    std::atomic<bool> detached_after{false};

    auto result = task::make({.name = "self_detached"}, [&detached_before, &detached_after](task::self& self) {
        detached_before.store(self.is_detached());
        // Wait for detach to happen (generous delay for QEMU timing imprecision)
        idfxx::delay(300ms);
        detached_after.store(self.is_detached());
    });

    TEST_ASSERT_TRUE(result.has_value());
    auto& t = *result;

    // Wait for task to check initial state
    idfxx::delay(50ms);
    TEST_ASSERT_FALSE(detached_before.load());

    // Detach the task
    auto detach_result = t->try_detach();
    TEST_ASSERT_TRUE(detach_result.has_value());

    // Wait for task to check again after detach
    idfxx::delay(350ms);
    TEST_ASSERT_TRUE(detached_after.load());
}

TEST_CASE("task::self is_detached returns true for spawned task", "[idfxx][task][self]") {
    static std::atomic<bool> detached{false};
    static std::atomic<bool> checked{false};
    detached.store(false);
    checked.store(false);

    auto result = task::try_spawn({.name = "self_spawn_detach"}, [](task::self& self) {
        detached.store(self.is_detached());
        checked.store(true);
    });

    TEST_ASSERT_TRUE(result.has_value());

    // Wait for task to execute
    idfxx::delay(100ms);
    TEST_ASSERT_TRUE(checked.load());
    TEST_ASSERT_TRUE(detached.load());
}

// =============================================================================
// Notification tests (wait/notify)
// =============================================================================

TEST_CASE("wait/notify wakes a waiting task", "[idfxx][task][notify]") {
    std::atomic<bool> waiting{false};
    std::atomic<bool> woke{false};

    auto result = task::make({.name = "wait_notify"}, [&waiting, &woke](task::self& self) {
        waiting.store(true);
        self.wait();
        woke.store(true);
    });

    TEST_ASSERT_TRUE(result.has_value());
    auto& t = *result;

    // Wait for task to enter wait()
    idfxx::delay(50ms);
    TEST_ASSERT_TRUE(waiting.load());
    TEST_ASSERT_FALSE(woke.load());

    // Notify the task
    auto notify_result = t->try_notify();
    TEST_ASSERT_TRUE(notify_result.has_value());

    // Wait for task to wake
    idfxx::delay(50ms);
    TEST_ASSERT_TRUE(woke.load());
}

TEST_CASE("take returns accumulated notification count", "[idfxx][task][notify]") {
    std::atomic<bool> ready{false};
    std::atomic<uint32_t> count{0};

    auto result = task::make({.name = "take_count"}, [&ready, &count](task::self& self) {
        ready.store(true);
        count.store(self.take());
    });

    TEST_ASSERT_TRUE(result.has_value());
    auto& t = *result;

    // Wait for task to signal ready
    idfxx::delay(50ms);
    TEST_ASSERT_TRUE(ready.load());

    // Send multiple notifications before the task takes them
    t->try_notify();
    t->try_notify();
    t->try_notify();

    // Wait for task to take and complete
    idfxx::delay(50ms);
    TEST_ASSERT_GREATER_OR_EQUAL(1, count.load());
}

TEST_CASE("wait_for returns false on timeout", "[idfxx][task][notify]") {
    std::atomic<bool> timed_out{false};

    auto result = task::make({.name = "wait_for_to"}, [&timed_out](task::self& self) {
        bool got = self.wait_for(50ms);
        timed_out.store(!got);
    });

    TEST_ASSERT_TRUE(result.has_value());

    // Don't send any notification — let it time out
    idfxx::delay(200ms);
    TEST_ASSERT_TRUE(timed_out.load());
}

TEST_CASE("wait_for returns true on notification", "[idfxx][task][notify]") {
    std::atomic<bool> ready{false};
    std::atomic<bool> got_notification{false};

    auto result = task::make({.name = "wait_for_ok"}, [&ready, &got_notification](task::self& self) {
        ready.store(true);
        bool got = self.wait_for(500ms);
        got_notification.store(got);
    });

    TEST_ASSERT_TRUE(result.has_value());
    auto& t = *result;

    // Wait for task to start waiting
    idfxx::delay(50ms);
    TEST_ASSERT_TRUE(ready.load());

    // Send notification
    t->try_notify();

    // Wait for task to process
    idfxx::delay(50ms);
    TEST_ASSERT_TRUE(got_notification.load());
}

TEST_CASE("take_for returns 0 on timeout", "[idfxx][task][notify]") {
    std::atomic<uint32_t> count{99};

    auto result = task::make({.name = "take_for_to"}, [&count](task::self& self) {
        count.store(self.take_for(50ms));
    });

    TEST_ASSERT_TRUE(result.has_value());

    // Don't send any notification — let it time out
    idfxx::delay(200ms);
    TEST_ASSERT_EQUAL(0, count.load());
}

TEST_CASE("take_for returns count on notification", "[idfxx][task][notify]") {
    std::atomic<bool> ready{false};
    std::atomic<uint32_t> count{0};

    auto result = task::make({.name = "take_for_ok"}, [&ready, &count](task::self& self) {
        ready.store(true);
        count.store(self.take_for(500ms));
    });

    TEST_ASSERT_TRUE(result.has_value());
    auto& t = *result;

    // Wait for task to start waiting
    idfxx::delay(50ms);
    TEST_ASSERT_TRUE(ready.load());

    // Send multiple notifications
    t->try_notify();
    t->try_notify();

    // Wait for task to process
    idfxx::delay(50ms);
    TEST_ASSERT_GREATER_OR_EQUAL(1, count.load());
}

TEST_CASE("destructor wakes a task blocked in wait", "[idfxx][task][notify]") {
    std::atomic<bool> exited{false};

    {
        auto result = task::make({.name = "dtor_wait"}, [&exited](task::self& self) {
            while (!self.stop_requested()) {
                self.wait();
            }
            exited.store(true);
        });

        TEST_ASSERT_TRUE(result.has_value());

        // Let the task start and enter wait()
        idfxx::delay(50ms);
        TEST_ASSERT_FALSE(exited.load());

        // Destructor fires here — should request_stop() + notify to wake + join
    }

    TEST_ASSERT_TRUE(exited.load());
}

TEST_CASE("notify fails on detached task", "[idfxx][task][notify]") {
    auto result = task::make({.name = "notify_detach"}, [](task::self&) { idfxx::delay(100ms); });

    TEST_ASSERT_TRUE(result.has_value());
    auto& t = *result;

    auto detach_result = t->try_detach();
    TEST_ASSERT_TRUE(detach_result.has_value());

    auto notify_result = t->try_notify();
    TEST_ASSERT_FALSE(notify_result.has_value());
    TEST_ASSERT_EQUAL(std::to_underlying(errc::invalid_state), notify_result.error().value());
}

TEST_CASE("notify fails on completed task", "[idfxx][task][notify]") {
    auto result = task::make({.name = "notify_complete"}, [](task::self&) {
        // Return immediately
    });

    TEST_ASSERT_TRUE(result.has_value());
    auto& t = *result;

    // Wait for task to complete
    idfxx::delay(100ms);
    TEST_ASSERT_TRUE(t->is_completed());

    auto notify_result = t->try_notify();
    TEST_ASSERT_FALSE(notify_result.has_value());
    TEST_ASSERT_EQUAL(std::to_underlying(errc::invalid_state), notify_result.error().value());
}

TEST_CASE("wait returns immediately when stop requested", "[idfxx][task][notify]") {
    std::atomic<bool> wait_returned{false};

    auto result = task::make({.name = "wait_stop"}, [&wait_returned](task::self& self) {
        // Wait for stop to be requested before calling wait
        while (!self.stop_requested()) {
            idfxx::delay(10ms);
        }
        // This should return immediately since stop is already requested
        self.wait();
        wait_returned.store(true);
    });

    TEST_ASSERT_TRUE(result.has_value());
    auto& t = *result;

    idfxx::delay(50ms);
    t->request_stop();

    // Wait for task to complete
    idfxx::delay(100ms);
    TEST_ASSERT_TRUE(wait_returned.load());
}
