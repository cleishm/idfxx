// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Unit tests for idfxx timer
// Uses ESP-IDF Unity test framework with compile-time static_asserts

#include <idfxx/timer>
#include <unity.h>

#include <atomic>
#include <chrono>
#include <esp_timer.h>
#include <idfxx/sched>
#include <type_traits>

using namespace idfxx;
using namespace std::chrono_literals;

namespace {

// Poll until a flag is set, with generous timeout for QEMU virtual time reliability.
void wait_for(const std::atomic<bool>& flag) {
    for (int i = 0; i < 500 && !flag.load(); ++i) {
        idfxx::delay(10ms);
    }
}

// Poll until a counter reaches a minimum value.
void wait_for_count(const std::atomic<int>& counter, int min_count) {
    for (int i = 0; i < 500 && counter.load() < min_count; ++i) {
        idfxx::delay(10ms);
    }
}

} // namespace

// =============================================================================
// Compile-time tests (static_assert)
// These verify correctness at compile time - if this file compiles, they pass.
// =============================================================================

// timer is not default constructible
static_assert(!std::is_default_constructible_v<timer>);

// timer is not copyable
static_assert(!std::is_copy_constructible_v<timer>);
static_assert(!std::is_copy_assignable_v<timer>);

// timer is move-only
static_assert(std::is_move_constructible_v<timer>);
static_assert(std::is_move_assignable_v<timer>);

// Enum values match ESP-IDF values
static_assert(std::to_underlying(timer::dispatch_method::task) == ESP_TIMER_TASK);
#if CONFIG_ESP_TIMER_SUPPORTS_ISR_DISPATCH_METHOD
static_assert(std::to_underlying(timer::dispatch_method::isr) == ESP_TIMER_ISR);
#endif

// Clock type checks
static_assert(timer::clock::is_steady);
static_assert(std::chrono::is_clock_v<timer::clock>);

// =============================================================================
// Runtime tests (Unity TEST_CASE)
// =============================================================================

TEST_CASE("timer::make with functional callback succeeds", "[idfxx][timer]") {
    bool called = false;
    auto result = timer::make({.name = "test_timer"}, [&called]() { called = true; });

    TEST_ASSERT_TRUE(result.has_value());
    TEST_ASSERT_NOT_NULL(result->idf_handle());
}

TEST_CASE("timer::make with raw callback succeeds", "[idfxx][timer]") {
    static bool called = false;
    called = false;

    auto result = timer::make(
        {.name = "test_timer_raw"},
        [](void* arg) { *static_cast<bool*>(arg) = true; },
        &called
    );

    TEST_ASSERT_TRUE(result.has_value());
    TEST_ASSERT_NOT_NULL(result->idf_handle());
}

TEST_CASE("timer is not active after creation", "[idfxx][timer]") {
    auto result = timer::make({}, []() {});
    TEST_ASSERT_TRUE(result.has_value());
    TEST_ASSERT_FALSE(result->is_active());
}

TEST_CASE("timer::clock::now returns monotonically increasing time", "[idfxx][timer]") {
    auto t1 = timer::clock::now();
    auto t2 = timer::clock::now();
    auto t3 = timer::clock::now();

    TEST_ASSERT_TRUE(t1 <= t2);
    TEST_ASSERT_TRUE(t2 <= t3);
}

TEST_CASE("timer::config default values", "[idfxx][timer]") {
    timer::config cfg{};
    auto s = std::string{cfg.name};
    TEST_ASSERT_EQUAL_STRING("", s.c_str());
    TEST_ASSERT_EQUAL(timer::dispatch_method::task, cfg.dispatch);
    TEST_ASSERT_FALSE(cfg.skip_unhandled_events);
}

TEST_CASE("timer start_once makes timer active", "[idfxx][timer]") {
    auto result = timer::make({.name = "active_test"}, []() {});
    TEST_ASSERT_TRUE(result.has_value());
    auto& t = *result;

    TEST_ASSERT_FALSE(t.is_active());

    auto start_result = t.try_start_once(1s);
    TEST_ASSERT_TRUE(start_result.has_value());
    TEST_ASSERT_TRUE(t.is_active());

    // Stop the timer
    auto stop_result = t.try_stop();
    TEST_ASSERT_TRUE(stop_result.has_value());
    TEST_ASSERT_FALSE(t.is_active());
}

TEST_CASE("timer start_periodic makes timer active", "[idfxx][timer]") {
    auto result = timer::make({.name = "periodic_test"}, []() {});
    TEST_ASSERT_TRUE(result.has_value());
    auto& t = *result;

    TEST_ASSERT_FALSE(t.is_active());

    auto start_result = t.try_start_periodic(100ms);
    TEST_ASSERT_TRUE(start_result.has_value());
    TEST_ASSERT_TRUE(t.is_active());

    // Check period
    auto period = t.period();
    TEST_ASSERT_EQUAL(100000, period.count()); // 100ms = 100000us

    // Stop the timer
    auto stop_result = t.try_stop();
    TEST_ASSERT_TRUE(stop_result.has_value());
    TEST_ASSERT_FALSE(t.is_active());
}

TEST_CASE("timer start_once fails when already running", "[idfxx][timer]") {
    auto result = timer::make({}, []() {});
    TEST_ASSERT_TRUE(result.has_value());
    auto& t = *result;

    // Start once
    auto start_result = t.try_start_once(1s);
    TEST_ASSERT_TRUE(start_result.has_value());

    // Try to start again - should fail
    auto second_start = t.try_start_once(1s);
    TEST_ASSERT_FALSE(second_start.has_value());
    TEST_ASSERT_EQUAL(std::to_underlying(errc::invalid_state), second_start.error().value());

    (void)t.try_stop();
}

TEST_CASE("timer stop fails when not running", "[idfxx][timer]") {
    auto result = timer::make({}, []() {});
    TEST_ASSERT_TRUE(result.has_value());
    auto& t = *result;

    // Timer is not running
    auto stop_result = t.try_stop();
    TEST_ASSERT_FALSE(stop_result.has_value());
    TEST_ASSERT_EQUAL(std::to_underlying(errc::invalid_state), stop_result.error().value());
}

TEST_CASE("timer restart works", "[idfxx][timer]") {
    auto result = timer::make({.name = "restart_test"}, []() {});
    TEST_ASSERT_TRUE(result.has_value());
    auto& t = *result;

    // Start
    auto start_result = t.try_start_once(1s);
    TEST_ASSERT_TRUE(start_result.has_value());
    TEST_ASSERT_TRUE(t.is_active());

    // Restart with different timeout
    auto restart_result = t.try_restart(500ms);
    TEST_ASSERT_TRUE(restart_result.has_value());
    TEST_ASSERT_TRUE(t.is_active());

    (void)t.try_stop();
}

TEST_CASE("timer restart starts a non-running timer", "[idfxx][timer]") {
    auto result = timer::make({.name = "restart_stopped_test"}, []() {});
    TEST_ASSERT_TRUE(result.has_value());
    auto& t = *result;

    // Timer is not running - restart should start it
    TEST_ASSERT_FALSE(t.is_active());
    auto restart_result = t.try_restart(500ms);
    TEST_ASSERT_TRUE(restart_result.has_value());
    TEST_ASSERT_TRUE(t.is_active());

    (void)t.try_stop();
}

TEST_CASE("timer one-shot callback fires", "[idfxx][timer][hw]") {
    std::atomic<bool> called{false};
    auto result = timer::make({.name = "callback_test"}, [&called]() { called.store(true); });
    TEST_ASSERT_TRUE(result.has_value());
    auto& t = *result;

    // Start with short timeout
    auto start_result = t.try_start_once(10ms);
    TEST_ASSERT_TRUE(start_result.has_value());

    // Wait for callback
    wait_for(called);

    TEST_ASSERT_TRUE(called.load());
    TEST_ASSERT_FALSE(t.is_active()); // One-shot timer stops after firing
}

TEST_CASE("timer periodic callback fires multiple times", "[idfxx][timer][hw]") {
    std::atomic<int> count{0};
    auto result = timer::make({.name = "periodic_callback_test"}, [&count]() { count.fetch_add(1); });
    TEST_ASSERT_TRUE(result.has_value());
    auto& t = *result;

    // Start periodic timer
    auto start_result = t.try_start_periodic(20ms);
    TEST_ASSERT_TRUE(start_result.has_value());

    // Wait for multiple callbacks
    wait_for_count(count, 3);

    // Should have fired at least 3 times (100ms / 20ms = 5, but allow some margin)
    TEST_ASSERT_GREATER_OR_EQUAL(3, count.load());

    (void)t.try_stop();
}

TEST_CASE("timer raw callback fires", "[idfxx][timer][hw]") {
    static std::atomic<bool> called{false};
    called.store(false);

    auto result = timer::make(
        {.name = "raw_callback_test"},
        [](void* arg) { static_cast<std::atomic<bool>*>(arg)->store(true); },
        &called
    );
    TEST_ASSERT_TRUE(result.has_value());
    auto& t = *result;

    // Start with short timeout
    auto start_result = t.try_start_once(10ms);
    TEST_ASSERT_TRUE(start_result.has_value());

    // Wait for callback
    wait_for(called);

    TEST_ASSERT_TRUE(called.load());
}

TEST_CASE("timer expiry_time returns valid time", "[idfxx][timer]") {
    auto result = timer::make({.name = "expiry_test"}, []() {});
    TEST_ASSERT_TRUE(result.has_value());
    auto& t = *result;

    auto now = timer::clock::now();

    // Start timer
    auto start_result = t.try_start_once(100ms);
    TEST_ASSERT_TRUE(start_result.has_value());

    // Get expiry time
    auto expiry = t.expiry_time();

    auto expected_min = now + 90ms; // Allow some margin
    auto expected_max = now + 110ms;

    TEST_ASSERT_TRUE(expiry >= expected_min);
    TEST_ASSERT_TRUE(expiry <= expected_max);

    (void)t.try_stop();
}

TEST_CASE("timer destructor stops running timer", "[idfxx][timer]") {
    {
        auto result = timer::make({.name = "destructor_test"}, []() {});
        TEST_ASSERT_TRUE(result.has_value());

        auto start_result = result->try_start_once(1s);
        TEST_ASSERT_TRUE(start_result.has_value());
        TEST_ASSERT_TRUE(result->is_active());

        // Timer goes out of scope here and should be stopped and deleted
    }

    // If the destructor didn't work properly (stop + delete), we'd likely
    // see issues in subsequent tests or memory leaks
}

TEST_CASE("timer next_alarm returns valid time", "[idfxx][timer][hw]") {
    auto result = timer::make({.name = "next_alarm_test"}, []() {});
    TEST_ASSERT_TRUE(result.has_value());
    auto& t = *result;

    // Start timer
    auto start_result = t.try_start_once(100ms);
    TEST_ASSERT_TRUE(start_result.has_value());

    auto next = timer::next_alarm();
    auto now = timer::clock::now();

    // Next alarm should be in the future
    TEST_ASSERT_TRUE(next > now);

    (void)t.try_stop();
}

TEST_CASE("timer with empty name works", "[idfxx][timer]") {
    auto result = timer::make({}, []() {});
    TEST_ASSERT_TRUE(result.has_value());
    TEST_ASSERT_NOT_NULL(result->idf_handle());
}

TEST_CASE("timer with custom dispatch method", "[idfxx][timer]") {
    // Note: ISR dispatch may not be available on all ESP-IDF configurations
    auto result =
        timer::make({.name = "dispatch_test", .dispatch = timer::dispatch_method::task}, [](void*) {}, nullptr);
    TEST_ASSERT_TRUE(result.has_value());
}

TEST_CASE("timer with skip_unhandled_events", "[idfxx][timer]") {
    auto result = timer::make({.name = "skip_test", .skip_unhandled_events = true}, []() {});
    TEST_ASSERT_TRUE(result.has_value());
}

// =============================================================================
// Duration conversion tests
// =============================================================================

TEST_CASE("timer accepts various duration types", "[idfxx][timer]") {
    auto result = timer::make({}, []() {});
    TEST_ASSERT_TRUE(result.has_value());
    auto& t = *result;

    // Milliseconds
    TEST_ASSERT_TRUE(t.try_start_once(100ms).has_value());
    (void)t.try_stop();

    // Seconds
    TEST_ASSERT_TRUE(t.try_start_once(1s).has_value());
    (void)t.try_stop();

    // Microseconds
    TEST_ASSERT_TRUE(t.try_start_once(std::chrono::microseconds(50000)).has_value());
    (void)t.try_stop();

    // Mixed arithmetic
    TEST_ASSERT_TRUE(t.try_start_once(500ms + 500ms).has_value());
    (void)t.try_stop();
}

// =============================================================================
// ISR-compatible method tests
// =============================================================================

TEST_CASE("timer start_once_isr", "[idfxx][timer]") {
    auto result = timer::make({.name = "isr_test"}, []() {});
    TEST_ASSERT_TRUE(result.has_value());
    auto& t = *result;

    auto err = t.try_start_once_isr(100000); // 100ms
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_TRUE(t.is_active());

    err = t.try_stop_isr();
    TEST_ASSERT_EQUAL(ESP_OK, err);
}

TEST_CASE("timer start_periodic_isr", "[idfxx][timer]") {
    auto result = timer::make({.name = "periodic_isr_test"}, []() {});
    TEST_ASSERT_TRUE(result.has_value());
    auto& t = *result;

    auto err = t.try_start_periodic_isr(50000); // 50ms
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_TRUE(t.is_active());

    err = t.try_stop_isr();
    TEST_ASSERT_EQUAL(ESP_OK, err);
}

TEST_CASE("timer restart_isr", "[idfxx][timer]") {
    auto result = timer::make({.name = "restart_isr_test"}, []() {});
    TEST_ASSERT_TRUE(result.has_value());
    auto& t = *result;

    auto err = t.try_start_once_isr(1000000); // 1s
    TEST_ASSERT_EQUAL(ESP_OK, err);

    err = t.try_restart_isr(500000); // 500ms
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_TRUE(t.is_active());

    err = t.try_stop_isr();
    TEST_ASSERT_EQUAL(ESP_OK, err);
}

TEST_CASE("timer restart_isr starts a non-running timer", "[idfxx][timer]") {
    auto result = timer::make({.name = "restart_isr_stopped"}, []() {});
    TEST_ASSERT_TRUE(result.has_value());
    auto& t = *result;

    // Timer is not running - restart should start it
    TEST_ASSERT_FALSE(t.is_active());
    auto err = t.try_restart_isr(500000); // 500ms
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_TRUE(t.is_active());

    (void)t.try_stop_isr();
}

TEST_CASE("timer stop_isr fails when not running", "[idfxx][timer]") {
    auto result = timer::make({.name = "stop_isr_test"}, []() {});
    TEST_ASSERT_TRUE(result.has_value());
    auto& t = *result;

    auto err = t.try_stop_isr();
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, err);
}

TEST_CASE("timer start_once_isr fails when already running", "[idfxx][timer]") {
    auto result = timer::make({.name = "double_start_isr"}, []() {});
    TEST_ASSERT_TRUE(result.has_value());
    auto& t = *result;

    auto err = t.try_start_once_isr(1000000);
    TEST_ASSERT_EQUAL(ESP_OK, err);

    err = t.try_start_once_isr(1000000);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, err);

    (void)t.try_stop_isr();
}

// =============================================================================
// Name accessor tests
// =============================================================================

TEST_CASE("timer name returns configured name", "[idfxx][timer]") {
    auto result = timer::make({.name = "my_timer"}, []() {});
    TEST_ASSERT_TRUE(result.has_value());
    TEST_ASSERT_EQUAL_STRING("my_timer", result->name().c_str());
}

TEST_CASE("timer name returns empty string when unnamed", "[idfxx][timer]") {
    auto result = timer::make({}, []() {});
    TEST_ASSERT_TRUE(result.has_value());
    TEST_ASSERT_EQUAL_STRING("", result->name().c_str());
}

// =============================================================================
// Static factory+start method tests
// =============================================================================

TEST_CASE("timer::try_start_once static with functional callback", "[idfxx][timer][hw]") {
    std::atomic<bool> called{false};
    auto result = timer::try_start_once({.name = "static_once"}, 10ms, [&called]() { called.store(true); });

    TEST_ASSERT_TRUE(result.has_value());
    TEST_ASSERT_NOT_NULL(result->idf_handle());
    TEST_ASSERT_TRUE(result->is_active());

    // Wait for callback
    wait_for(called);

    TEST_ASSERT_TRUE(called.load());
    TEST_ASSERT_FALSE(result->is_active()); // One-shot timer stops after firing
}

TEST_CASE("timer::try_start_once static with raw callback", "[idfxx][timer][hw]") {
    static std::atomic<bool> called{false};
    called.store(false);

    auto result = timer::try_start_once(
        {.name = "static_once_raw"}, 10ms,
        [](void* arg) { static_cast<std::atomic<bool>*>(arg)->store(true); }, &called
    );

    TEST_ASSERT_TRUE(result.has_value());
    TEST_ASSERT_NOT_NULL(result->idf_handle());
    TEST_ASSERT_TRUE(result->is_active());

    // Wait for callback
    wait_for(called);

    TEST_ASSERT_TRUE(called.load());
}

TEST_CASE("timer::try_start_periodic static with functional callback", "[idfxx][timer][hw]") {
    std::atomic<int> count{0};
    auto result = timer::try_start_periodic({.name = "static_periodic"}, 20ms, [&count]() { count.fetch_add(1); });

    TEST_ASSERT_TRUE(result.has_value());
    TEST_ASSERT_NOT_NULL(result->idf_handle());
    TEST_ASSERT_TRUE(result->is_active());

    // Check period
    auto period = result->period();
    TEST_ASSERT_EQUAL(20000, period.count()); // 20ms = 20000us

    // Wait for multiple callbacks
    wait_for_count(count, 3);

    TEST_ASSERT_GREATER_OR_EQUAL(3, count.load());

    (void)result->try_stop();
}

TEST_CASE("timer::try_start_periodic static with raw callback", "[idfxx][timer][hw]") {
    static std::atomic<int> count{0};
    count.store(0);

    auto result = timer::try_start_periodic(
        {.name = "static_periodic_raw"}, 20ms,
        [](void* arg) { static_cast<std::atomic<int>*>(arg)->fetch_add(1); }, &count
    );

    TEST_ASSERT_TRUE(result.has_value());
    TEST_ASSERT_NOT_NULL(result->idf_handle());
    TEST_ASSERT_TRUE(result->is_active());

    // Wait for multiple callbacks
    wait_for_count(count, 3);

    TEST_ASSERT_GREATER_OR_EQUAL(3, count.load());

    (void)result->try_stop();
}

TEST_CASE("timer from static factory cleans up on destruction", "[idfxx][timer]") {
    std::atomic<bool> called{false};
    {
        auto result = timer::try_start_once({.name = "static_cleanup"}, 1s, [&called]() { called.store(true); });
        TEST_ASSERT_TRUE(result.has_value());
        TEST_ASSERT_TRUE(result->is_active());

        // Timer goes out of scope here - should be stopped and deleted
    }

    // Callback should not have fired (1s timeout, destroyed immediately)
    TEST_ASSERT_FALSE(called.load());
}

// =============================================================================
// Destruction race condition tests
// =============================================================================

// =============================================================================
// Time_point overload tests
// =============================================================================

TEST_CASE("timer try_start_once with time_point fires at correct time", "[idfxx][timer][hw]") {
    std::atomic<bool> called{false};
    auto result = timer::make({.name = "tp_once"}, [&called]() { called.store(true); });
    TEST_ASSERT_TRUE(result.has_value());
    auto& t = *result;

    // Start at now + 50ms
    auto target = timer::clock::now() + 50ms;
    auto start_result = t.try_start_once(target);
    TEST_ASSERT_TRUE(start_result.has_value());
    TEST_ASSERT_TRUE(t.is_active());

    // Should not have fired yet
    idfxx::delay(10ms);
    TEST_ASSERT_FALSE(called.load());

    // Wait for it to fire
    wait_for(called);
    TEST_ASSERT_TRUE(called.load());
    TEST_ASSERT_FALSE(t.is_active());
}

TEST_CASE("timer try_start_once with time_point in the past fires immediately", "[idfxx][timer][hw]") {
    std::atomic<bool> called{false};
    auto result = timer::make({.name = "tp_past"}, [&called]() { called.store(true); });
    TEST_ASSERT_TRUE(result.has_value());
    auto& t = *result;

    // Start at a time in the past
    auto past = timer::clock::now() - 1s;
    auto start_result = t.try_start_once(past);
    TEST_ASSERT_TRUE(start_result.has_value());

    // Should fire very quickly
    wait_for(called);
    TEST_ASSERT_TRUE(called.load());
}

TEST_CASE("timer::try_start_once static with time_point and functional callback", "[idfxx][timer][hw]") {
    std::atomic<bool> called{false};
    auto target = timer::clock::now() + 10ms;
    auto result = timer::try_start_once({.name = "static_tp_func"}, target, [&called]() { called.store(true); });

    TEST_ASSERT_TRUE(result.has_value());
    TEST_ASSERT_NOT_NULL(result->idf_handle());
    TEST_ASSERT_TRUE(result->is_active());

    // Wait for callback
    wait_for(called);

    TEST_ASSERT_TRUE(called.load());
    TEST_ASSERT_FALSE(result->is_active());
}

TEST_CASE("timer::try_start_once static with time_point and raw callback", "[idfxx][timer][hw]") {
    static std::atomic<bool> called{false};
    called.store(false);

    auto target = timer::clock::now() + 10ms;
    auto result = timer::try_start_once(
        {.name = "static_tp_raw"}, target,
        [](void* arg) { static_cast<std::atomic<bool>*>(arg)->store(true); }, &called
    );

    TEST_ASSERT_TRUE(result.has_value());
    TEST_ASSERT_NOT_NULL(result->idf_handle());
    TEST_ASSERT_TRUE(result->is_active());

    // Wait for callback
    wait_for(called);

    TEST_ASSERT_TRUE(called.load());
}

// =============================================================================
// Destruction race condition tests
// =============================================================================

TEST_CASE("timer destruction waits for one-shot callback to complete", "[idfxx][timer][hw]") {
    std::atomic<bool> callback_started{false};
    std::atomic<bool> callback_completed{false};

    {
        auto result = timer::make({.name = "race_oneshot"}, [&]() {
            callback_started.store(true);
            idfxx::delay(100ms);
            callback_completed.store(true);
        });
        TEST_ASSERT_TRUE(result.has_value());

        auto start_result = result->try_start_once(10ms);
        TEST_ASSERT_TRUE(start_result.has_value());

        // Wait for callback to begin executing
        wait_for(callback_started);
        TEST_ASSERT_TRUE(callback_started.load());

        // Timer is destroyed here while callback is still running (sleeping for 100ms)
    }

    // If destruction waited properly, the callback must have completed
    TEST_ASSERT_TRUE(callback_completed.load());
}

TEST_CASE("timer destruction waits for periodic callback to complete", "[idfxx][timer][hw]") {
    std::atomic<bool> callback_started{false};
    std::atomic<bool> callback_completed{false};

    {
        auto result = timer::make({.name = "race_periodic"}, [&]() {
            callback_started.store(true);
            idfxx::delay(100ms);
            callback_completed.store(true);
        });
        TEST_ASSERT_TRUE(result.has_value());

        auto start_result = result->try_start_periodic(10ms);
        TEST_ASSERT_TRUE(start_result.has_value());

        // Wait for callback to begin executing
        wait_for(callback_started);
        TEST_ASSERT_TRUE(callback_started.load());

        // Timer is destroyed here while callback is still running
    }

    // If destruction waited properly, the callback must have completed
    TEST_ASSERT_TRUE(callback_completed.load());
}

// =============================================================================
// Moved-from state tests
// =============================================================================

TEST_CASE("moved-from timer returns invalid_state", "[idfxx][timer]") {
    auto result = timer::make({.name = "move_test"}, []() {});
    TEST_ASSERT_TRUE(result.has_value());

    auto moved = std::move(*result);

    // moved-from object should return invalid_state
    auto start_result = result->try_start_once(100ms);
    TEST_ASSERT_FALSE(start_result.has_value());
    TEST_ASSERT_EQUAL(std::to_underlying(errc::invalid_state), start_result.error().value());

    auto stop_result = result->try_stop();
    TEST_ASSERT_FALSE(stop_result.has_value());
    TEST_ASSERT_EQUAL(std::to_underlying(errc::invalid_state), stop_result.error().value());

    auto restart_result = result->try_restart(100ms);
    TEST_ASSERT_FALSE(restart_result.has_value());
    TEST_ASSERT_EQUAL(std::to_underlying(errc::invalid_state), restart_result.error().value());

    // Simple accessors return defaults
    TEST_ASSERT_NULL(result->idf_handle());
    TEST_ASSERT_FALSE(result->is_active());
    TEST_ASSERT_EQUAL(0, result->period().count());

    // Moved-to object should work normally
    TEST_ASSERT_NOT_NULL(moved.idf_handle());
}

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS

TEST_CASE("timer constructor with functional callback succeeds", "[idfxx][timer]") {
    timer t({.name = "ctor_test"}, []() {});
    TEST_ASSERT_NOT_NULL(t.idf_handle());
}

TEST_CASE("timer start_once throws on already-running timer", "[idfxx][timer]") {
    timer t({.name = "throw_test"}, []() {});

    t.start_once(1s);
    TEST_ASSERT_TRUE(t.is_active());

    bool threw = false;
    try {
        t.start_once(1s);
    } catch (const std::system_error& e) {
        threw = true;
        TEST_ASSERT_EQUAL(std::to_underlying(errc::invalid_state), e.code().value());
    }
    TEST_ASSERT_TRUE(threw);

    t.stop();
}

TEST_CASE("timer stop throws when not running", "[idfxx][timer]") {
    timer t({.name = "stop_throw"}, []() {});

    bool threw = false;
    try {
        t.stop();
    } catch (const std::system_error& e) {
        threw = true;
        TEST_ASSERT_EQUAL(std::to_underlying(errc::invalid_state), e.code().value());
    }
    TEST_ASSERT_TRUE(threw);
}

TEST_CASE("timer start_periodic and stop succeed via exception API", "[idfxx][timer]") {
    timer t({.name = "exc_periodic"}, []() {});

    t.start_periodic(100ms);
    TEST_ASSERT_TRUE(t.is_active());
    t.stop();
    TEST_ASSERT_FALSE(t.is_active());
}

TEST_CASE("timer restart succeeds via exception API", "[idfxx][timer]") {
    timer t({.name = "exc_restart"}, []() {});

    t.start_once(1s);
    t.restart(500ms);
    TEST_ASSERT_TRUE(t.is_active());
    t.stop();
}

TEST_CASE("timer::start_once static factory with exception API", "[idfxx][timer][hw]") {
    std::atomic<bool> called{false};
    auto t = timer::start_once({.name = "exc_static"}, 10ms, [&called]() { called.store(true); });

    TEST_ASSERT_TRUE(t.is_active());
    wait_for(called);
    TEST_ASSERT_TRUE(called.load());
}

TEST_CASE("timer::start_periodic static factory with exception API", "[idfxx][timer][hw]") {
    std::atomic<int> count{0};
    auto t = timer::start_periodic({.name = "exc_periodic_static"}, 20ms, [&count]() { count.fetch_add(1); });

    TEST_ASSERT_TRUE(t.is_active());
    wait_for_count(count, 3);
    TEST_ASSERT_GREATER_OR_EQUAL(3, count.load());
    t.stop();
}

#endif // CONFIG_COMPILER_CXX_EXCEPTIONS

// =============================================================================
// Multiple concurrent timer tests
// =============================================================================

TEST_CASE("multiple timers running concurrently", "[idfxx][timer][hw]") {
    std::atomic<int> count_a{0};
    std::atomic<int> count_b{0};
    std::atomic<int> count_c{0};

    auto ra = timer::make({.name = "concurrent_a"}, [&count_a]() { count_a.fetch_add(1); });
    auto rb = timer::make({.name = "concurrent_b"}, [&count_b]() { count_b.fetch_add(1); });
    auto rc = timer::make({.name = "concurrent_c"}, [&count_c]() { count_c.fetch_add(1); });
    TEST_ASSERT_TRUE(ra.has_value());
    TEST_ASSERT_TRUE(rb.has_value());
    TEST_ASSERT_TRUE(rc.has_value());

    // Start all three with different periods
    TEST_ASSERT_TRUE(ra->try_start_periodic(20ms).has_value());
    TEST_ASSERT_TRUE(rb->try_start_periodic(30ms).has_value());
    TEST_ASSERT_TRUE(rc->try_start_periodic(40ms).has_value());

    // All should be active
    TEST_ASSERT_TRUE(ra->is_active());
    TEST_ASSERT_TRUE(rb->is_active());
    TEST_ASSERT_TRUE(rc->is_active());

    // Wait for callbacks
    for (int i = 0; i < 500 && (count_a.load() < 5 || count_b.load() < 4 || count_c.load() < 3); ++i) {
        idfxx::delay(10ms);
    }

    // All should have fired multiple times
    TEST_ASSERT_GREATER_OR_EQUAL(5, count_a.load());
    TEST_ASSERT_GREATER_OR_EQUAL(4, count_b.load());
    TEST_ASSERT_GREATER_OR_EQUAL(3, count_c.load());

    (void)ra->try_stop();
    (void)rb->try_stop();
    (void)rc->try_stop();
}

TEST_CASE("timer move assignment cleans up target", "[idfxx][timer]") {
    auto r1 = timer::make({.name = "target"}, []() {});
    TEST_ASSERT_TRUE(r1.has_value());

    auto r2 = timer::make({.name = "source"}, []() {});
    TEST_ASSERT_TRUE(r2.has_value());

    // Move assignment should clean up target and transfer source
    *r1 = std::move(*r2);

    TEST_ASSERT_NOT_NULL(r1->idf_handle());
    TEST_ASSERT_NULL(r2->idf_handle());
}
