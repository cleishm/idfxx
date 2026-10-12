// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Unit tests for idfxx timer
// Uses ESP-IDF Unity test framework with compile-time static_asserts

#include <idfxx/timer>
#include <unity.h>

#include <atomic>
#include <chrono>
#include <esp_idf_version.h>
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

// Poll until a fire timestamp has been recorded.
void wait_for_fire(const std::atomic<int64_t>& fired_at) {
    for (int i = 0; i < 500 && fired_at.load() == 0; ++i) {
        idfxx::delay(10ms);
    }
}

// Current timer::clock time in microseconds, for recording when a callback ran.
int64_t now_us() {
    return timer::clock::now().time_since_epoch().count();
}

// Microseconds from a time point to a recorded timestamp.
int32_t us_after(timer::clock::time_point from, int64_t at_us) {
    return static_cast<int32_t>(at_us - from.time_since_epoch().count());
}

// Detect the ESP-IDF 6.1 additions, which must not exist on earlier versions.
template<typename T>
concept has_stop_and_wait = requires(T& t) { t.try_stop_and_wait(); };

template<typename T>
concept has_stop_and_wait_timeout = requires(T& t) { t.try_stop_and_wait(std::chrono::milliseconds{1}); };

template<typename T>
concept has_restart_at = requires(T& t) { t.try_restart(timer::clock::time_point{}); };

template<typename T>
concept has_restart_at_period = requires(T& t) {
    t.try_restart(timer::clock::time_point{}, std::chrono::milliseconds{1});
};

template<typename T>
concept has_periodic_at = requires(T& t) {
    t.try_start_periodic(timer::clock::time_point{}, std::chrono::milliseconds{1});
};

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

// ESP-IDF 6.1 additions exist exactly when the IDF provides them
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 1, 0)
static_assert(has_stop_and_wait<timer>);
static_assert(has_stop_and_wait_timeout<timer>);
static_assert(has_restart_at<timer>);
static_assert(has_restart_at_period<timer>);
static_assert(has_periodic_at<timer>);
#else
static_assert(!has_stop_and_wait<timer>);
static_assert(!has_stop_and_wait_timeout<timer>);
static_assert(!has_restart_at<timer>);
static_assert(!has_restart_at_period<timer>);
static_assert(!has_periodic_at<timer>);
#endif

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
// Absolute-time start, restart and blocking stop tests
// =============================================================================

TEST_CASE("timer try_start_once with time_point fires no earlier than the target", "[idfxx][timer]") {
    std::atomic<int64_t> fired_at{0};
    auto result = timer::make({.name = "tp_target"}, [&fired_at]() { fired_at.store(now_us()); });
    TEST_ASSERT_TRUE(result.has_value());

    auto target = timer::clock::now() + 100ms;
    TEST_ASSERT_TRUE(result->try_start_once(target).has_value());

    wait_for_fire(fired_at);
    TEST_ASSERT_NOT_EQUAL(0, fired_at.load());

    // Fires at the target, allowing generous latency for QEMU
    auto late = us_after(target, fired_at.load());
    TEST_ASSERT_GREATER_OR_EQUAL_INT32(0, late);
    TEST_ASSERT_LESS_THAN_INT32(100000, late);
}

TEST_CASE("timer try_start_once with time_point in the past fires promptly", "[idfxx][timer]") {
    std::atomic<int64_t> fired_at{0};
    auto result = timer::make({.name = "tp_past_prompt"}, [&fired_at]() { fired_at.store(now_us()); });
    TEST_ASSERT_TRUE(result.has_value());

    auto started = timer::clock::now();
    TEST_ASSERT_TRUE(result->try_start_once(started - 1s).has_value());

    wait_for_fire(fired_at);
    TEST_ASSERT_NOT_EQUAL(0, fired_at.load());
    TEST_ASSERT_LESS_THAN_INT32(100000, us_after(started, fired_at.load()));
}

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 1, 0)

TEST_CASE("timer try_restart with time_point moves the deadline", "[idfxx][timer]") {
    std::atomic<int64_t> fired_at{0};
    auto result = timer::make({.name = "restart_tp"}, [&fired_at]() { fired_at.store(now_us()); });
    TEST_ASSERT_TRUE(result.has_value());

    TEST_ASSERT_TRUE(result->try_start_once(10s).has_value());

    auto target = timer::clock::now() + 100ms;
    TEST_ASSERT_TRUE(result->try_restart(target).has_value());
    TEST_ASSERT_TRUE(result->is_active());
    TEST_ASSERT_EQUAL(0, result->period().count());

    wait_for_fire(fired_at);
    TEST_ASSERT_NOT_EQUAL(0, fired_at.load());

    auto late = us_after(target, fired_at.load());
    TEST_ASSERT_GREATER_OR_EQUAL_INT32(0, late);
    TEST_ASSERT_LESS_THAN_INT32(100000, late);
}

TEST_CASE("timer try_restart with time_point starts a non-running timer", "[idfxx][timer]") {
    std::atomic<int64_t> fired_at{0};
    auto result = timer::make({.name = "restart_tp_idle"}, [&fired_at]() { fired_at.store(now_us()); });
    TEST_ASSERT_TRUE(result.has_value());
    TEST_ASSERT_FALSE(result->is_active());

    auto target = timer::clock::now() + 100ms;
    TEST_ASSERT_TRUE(result->try_restart(target).has_value());
    TEST_ASSERT_TRUE(result->is_active());

    wait_for_fire(fired_at);
    TEST_ASSERT_NOT_EQUAL(0, fired_at.load());
    TEST_ASSERT_GREATER_OR_EQUAL_INT32(0, us_after(target, fired_at.load()));
}

TEST_CASE("timer try_restart with time_point in the past fires promptly", "[idfxx][timer]") {
    std::atomic<int64_t> fired_at{0};
    auto result = timer::make({.name = "restart_tp_past"}, [&fired_at]() { fired_at.store(now_us()); });
    TEST_ASSERT_TRUE(result.has_value());

    TEST_ASSERT_TRUE(result->try_start_once(10s).has_value());

    auto started = timer::clock::now();
    TEST_ASSERT_TRUE(result->try_restart(started - 1s).has_value());

    wait_for_fire(fired_at);
    TEST_ASSERT_NOT_EQUAL(0, fired_at.load());
    TEST_ASSERT_LESS_THAN_INT32(100000, us_after(started, fired_at.load()));
}

TEST_CASE("timer try_restart with time_point keeps the period of a periodic timer", "[idfxx][timer]") {
    std::atomic<int64_t> first_at{0};
    std::atomic<int> count{0};
    auto result = timer::make({.name = "restart_tp_keep"}, [&]() {
        if (count.fetch_add(1) == 0) {
            first_at.store(now_us());
        }
    });
    TEST_ASSERT_TRUE(result.has_value());

    TEST_ASSERT_TRUE(result->try_start_periodic(10s).has_value());

    auto target = timer::clock::now() + 100ms;
    TEST_ASSERT_TRUE(result->try_restart(target).has_value());
    TEST_ASSERT_TRUE(result->is_active());
    TEST_ASSERT_EQUAL_INT32(10000000, static_cast<int32_t>(result->period().count()));

    wait_for_fire(first_at);
    TEST_ASSERT_NOT_EQUAL(0, first_at.load());

    auto late = us_after(target, first_at.load());
    TEST_ASSERT_GREATER_OR_EQUAL_INT32(0, late);
    TEST_ASSERT_LESS_THAN_INT32(100000, late);

    // Still periodic with the original period, not yet due again
    TEST_ASSERT_TRUE(result->is_active());
    TEST_ASSERT_EQUAL_INT32(10000000, static_cast<int32_t>(result->period().count()));
    TEST_ASSERT_EQUAL(1, count.load());

    (void)result->try_stop();
}

TEST_CASE("timer try_restart with time_point and period sets a new period", "[idfxx][timer]") {
    std::atomic<int64_t> first_at{0};
    std::atomic<int> count{0};
    auto result = timer::make({.name = "restart_tp_period"}, [&]() {
        if (count.fetch_add(1) == 0) {
            first_at.store(now_us());
        }
    });
    TEST_ASSERT_TRUE(result.has_value());

    TEST_ASSERT_TRUE(result->try_start_periodic(10s).has_value());

    auto target = timer::clock::now() + 100ms;
    TEST_ASSERT_TRUE(result->try_restart(target, 30ms).has_value());
    TEST_ASSERT_TRUE(result->is_active());
    TEST_ASSERT_EQUAL_INT32(30000, static_cast<int32_t>(result->period().count()));

    wait_for_count(count, 3);
    TEST_ASSERT_GREATER_OR_EQUAL(3, count.load());

    auto late = us_after(target, first_at.load());
    TEST_ASSERT_GREATER_OR_EQUAL_INT32(0, late);
    TEST_ASSERT_LESS_THAN_INT32(100000, late);

    (void)result->try_stop();
}

TEST_CASE("timer try_restart with time_point in the past keeps the period of a periodic timer", "[idfxx][timer]") {
    std::atomic<int64_t> first_at{0};
    std::atomic<int> count{0};
    auto result = timer::make({.name = "restart_tp_past_keep"}, [&]() {
        if (count.fetch_add(1) == 0) {
            first_at.store(now_us());
        }
    });
    TEST_ASSERT_TRUE(result.has_value());

    TEST_ASSERT_TRUE(result->try_start_periodic(10s).has_value());

    auto started = timer::clock::now();
    TEST_ASSERT_TRUE(result->try_restart(started - 1s).has_value());
    TEST_ASSERT_EQUAL_INT32(10000000, static_cast<int32_t>(result->period().count()));

    wait_for_fire(first_at);
    TEST_ASSERT_NOT_EQUAL(0, first_at.load());
    TEST_ASSERT_LESS_THAN_INT32(100000, us_after(started, first_at.load()));

    // Fired once promptly, then waits out the original period
    idfxx::delay(100ms);
    TEST_ASSERT_EQUAL(1, count.load());
    TEST_ASSERT_TRUE(result->is_active());
    TEST_ASSERT_EQUAL_INT32(10000000, static_cast<int32_t>(result->period().count()));

    (void)result->try_stop();
}

TEST_CASE("timer try_restart with time_point in the past and period sets the period", "[idfxx][timer]") {
    std::atomic<int64_t> first_at{0};
    std::atomic<int> count{0};
    auto result = timer::make({.name = "restart_tp_past_period"}, [&]() {
        if (count.fetch_add(1) == 0) {
            first_at.store(now_us());
        }
    });
    TEST_ASSERT_TRUE(result.has_value());

    TEST_ASSERT_TRUE(result->try_start_periodic(10s).has_value());

    auto started = timer::clock::now();
    TEST_ASSERT_TRUE(result->try_restart(started - 1s, 5s).has_value());
    TEST_ASSERT_EQUAL_INT32(5000000, static_cast<int32_t>(result->period().count()));

    wait_for_fire(first_at);
    TEST_ASSERT_NOT_EQUAL(0, first_at.load());
    TEST_ASSERT_LESS_THAN_INT32(100000, us_after(started, first_at.load()));

    idfxx::delay(100ms);
    TEST_ASSERT_EQUAL(1, count.load());
    TEST_ASSERT_EQUAL_INT32(5000000, static_cast<int32_t>(result->period().count()));

    (void)result->try_stop();
}

TEST_CASE("timer try_restart with time_point and period on a one-shot ignores the period", "[idfxx][timer]") {
    std::atomic<int64_t> fired_at{0};
    std::atomic<int> count{0};
    auto result = timer::make({.name = "restart_tp_oneshot"}, [&]() {
        fired_at.store(now_us());
        count.fetch_add(1);
    });
    TEST_ASSERT_TRUE(result.has_value());

    TEST_ASSERT_TRUE(result->try_start_once(10s).has_value());

    auto target = timer::clock::now() + 100ms;
    TEST_ASSERT_TRUE(result->try_restart(target, 20ms).has_value());
    TEST_ASSERT_TRUE(result->is_active());
    TEST_ASSERT_EQUAL(0, result->period().count());

    wait_for_fire(fired_at);
    TEST_ASSERT_NOT_EQUAL(0, fired_at.load());
    TEST_ASSERT_GREATER_OR_EQUAL_INT32(0, us_after(target, fired_at.load()));

    // Fired exactly once
    idfxx::delay(100ms);
    TEST_ASSERT_EQUAL(1, count.load());
    TEST_ASSERT_FALSE(result->is_active());
}

TEST_CASE("timer try_start_periodic with time_point fires first at the target", "[idfxx][timer]") {
    std::atomic<int64_t> first_at{0};
    std::atomic<int> count{0};
    auto result = timer::make({.name = "periodic_tp"}, [&]() {
        if (count.fetch_add(1) == 0) {
            first_at.store(now_us());
        }
    });
    TEST_ASSERT_TRUE(result.has_value());

    auto target = timer::clock::now() + 200ms;
    TEST_ASSERT_TRUE(result->try_start_periodic(target, 50ms).has_value());
    TEST_ASSERT_TRUE(result->is_active());
    TEST_ASSERT_EQUAL_INT32(50000, static_cast<int32_t>(result->period().count()));

    wait_for_count(count, 3);
    TEST_ASSERT_GREATER_OR_EQUAL(3, count.load());

    auto late = us_after(target, first_at.load());
    TEST_ASSERT_GREATER_OR_EQUAL_INT32(0, late);
    TEST_ASSERT_LESS_THAN_INT32(100000, late);

    (void)result->try_stop();
}

TEST_CASE("timer try_start_periodic with time_point in the past fires promptly", "[idfxx][timer]") {
    std::atomic<int64_t> first_at{0};
    std::atomic<int> count{0};
    auto result = timer::make({.name = "periodic_tp_past"}, [&]() {
        if (count.fetch_add(1) == 0) {
            first_at.store(now_us());
        }
    });
    TEST_ASSERT_TRUE(result.has_value());

    auto started = timer::clock::now();
    TEST_ASSERT_TRUE(result->try_start_periodic(started - 1s, 1s).has_value());

    wait_for_count(count, 1);
    TEST_ASSERT_GREATER_OR_EQUAL(1, count.load());
    TEST_ASSERT_LESS_THAN_INT32(100000, us_after(started, first_at.load()));
    TEST_ASSERT_TRUE(result->is_active());

    (void)result->try_stop();
}

TEST_CASE("timer try_start_periodic with time_point fails when already running", "[idfxx][timer]") {
    auto result = timer::make({.name = "periodic_tp_busy"}, []() {});
    TEST_ASSERT_TRUE(result.has_value());

    TEST_ASSERT_TRUE(result->try_start_once(10s).has_value());

    auto r = result->try_start_periodic(timer::clock::now() + 1s, 100ms);
    TEST_ASSERT_FALSE(r.has_value());
    TEST_ASSERT_EQUAL(std::to_underlying(errc::invalid_state), r.error().value());
}

TEST_CASE("timer::try_start_periodic static with time_point and functional callback", "[idfxx][timer]") {
    std::atomic<int> count{0};
    auto result = timer::try_start_periodic(
        {.name = "static_periodic_tp"}, timer::clock::now() + 20ms, 20ms, [&count]() { count.fetch_add(1); }
    );
    TEST_ASSERT_TRUE(result.has_value());
    TEST_ASSERT_TRUE(result->is_active());

    wait_for_count(count, 3);
    TEST_ASSERT_GREATER_OR_EQUAL(3, count.load());
}

TEST_CASE("timer::try_start_periodic static with time_point and raw callback", "[idfxx][timer]") {
    static std::atomic<int> count{0};
    count.store(0);

    auto result = timer::try_start_periodic(
        {.name = "static_periodic_tp_raw"}, timer::clock::now() + 20ms, 20ms,
        [](void* arg) { static_cast<std::atomic<int>*>(arg)->fetch_add(1); }, &count
    );
    TEST_ASSERT_TRUE(result.has_value());
    TEST_ASSERT_TRUE(result->is_active());

    wait_for_count(count, 3);
    TEST_ASSERT_GREATER_OR_EQUAL(3, count.load());
}

TEST_CASE("timer try_stop_and_wait with timeout waits for a running callback", "[idfxx][timer]") {
    std::atomic<bool> callback_started{false};
    std::atomic<bool> callback_completed{false};
    auto result = timer::make({.name = "stop_wait"}, [&]() {
        callback_started.store(true);
        idfxx::delay(100ms);
        callback_completed.store(true);
    });
    TEST_ASSERT_TRUE(result.has_value());

    TEST_ASSERT_TRUE(result->try_start_periodic(10ms).has_value());
    wait_for(callback_started);
    TEST_ASSERT_TRUE(callback_started.load());

    auto r = result->try_stop_and_wait(2s);
    TEST_ASSERT_TRUE(r.has_value());
    TEST_ASSERT_TRUE(callback_completed.load());
    TEST_ASSERT_FALSE(result->is_active());
}

TEST_CASE("timer try_stop_and_wait without timeout waits for a running callback", "[idfxx][timer]") {
    std::atomic<bool> callback_started{false};
    std::atomic<bool> callback_completed{false};
    auto result = timer::make({.name = "stop_wait_forever"}, [&]() {
        callback_started.store(true);
        idfxx::delay(100ms);
        callback_completed.store(true);
    });
    TEST_ASSERT_TRUE(result.has_value());

    TEST_ASSERT_TRUE(result->try_start_periodic(10ms).has_value());
    wait_for(callback_started);
    TEST_ASSERT_TRUE(callback_started.load());

    auto r = result->try_stop_and_wait();
    TEST_ASSERT_TRUE(r.has_value());
    TEST_ASSERT_TRUE(callback_completed.load());
    TEST_ASSERT_FALSE(result->is_active());
}

TEST_CASE("timer try_stop_and_wait with timeout times out on a long callback", "[idfxx][timer]") {
    std::atomic<bool> callback_started{false};
    std::atomic<bool> callback_completed{false};
    auto result = timer::make({.name = "stop_timeout"}, [&]() {
        callback_started.store(true);
        idfxx::delay(500ms);
        callback_completed.store(true);
    });
    TEST_ASSERT_TRUE(result.has_value());

    TEST_ASSERT_TRUE(result->try_start_once(10ms).has_value());
    wait_for(callback_started);
    TEST_ASSERT_TRUE(callback_started.load());

    auto r = result->try_stop_and_wait(20ms);
    TEST_ASSERT_FALSE(r.has_value());
    TEST_ASSERT_EQUAL(std::to_underlying(errc::timeout), r.error().value());
    TEST_ASSERT_FALSE(callback_completed.load());

    // The callback is still running, so the timer is still reported active
    TEST_ASSERT_TRUE(result->is_active());

    wait_for(callback_completed);
    TEST_ASSERT_TRUE(callback_completed.load());
}

TEST_CASE("timer try_stop_and_wait succeeds on an idle timer", "[idfxx][timer]") {
    auto result = timer::make({.name = "stop_idle"}, []() {});
    TEST_ASSERT_TRUE(result.has_value());

    // Not running
    TEST_ASSERT_TRUE(result->try_stop_and_wait(10ms).has_value());
    TEST_ASSERT_TRUE(result->try_stop_and_wait().has_value());

    // Armed, callback not running
    TEST_ASSERT_TRUE(result->try_start_once(10s).has_value());
    TEST_ASSERT_TRUE(result->try_stop_and_wait(0ms).has_value());
    TEST_ASSERT_FALSE(result->is_active());

    TEST_ASSERT_TRUE(result->try_start_once(10s).has_value());
    TEST_ASSERT_TRUE(result->try_stop_and_wait().has_value());
    TEST_ASSERT_FALSE(result->is_active());
}

TEST_CASE("timer try_stop_and_wait from its own callback returns immediately", "[idfxx][timer]") {
    std::atomic<bool> stop_ok{false};
    std::atomic<int> count{0};
    timer* self = nullptr;
    auto result = timer::make({.name = "stop_self"}, [&]() {
        stop_ok.store(self->try_stop_and_wait().has_value());
        count.fetch_add(1);
    });
    TEST_ASSERT_TRUE(result.has_value());
    self = &*result;

    TEST_ASSERT_TRUE(result->try_start_periodic(20ms).has_value());
    wait_for_count(count, 1);
    TEST_ASSERT_TRUE(stop_ok.load());

    // The periodic timer stopped itself after one invocation
    for (int i = 0; i < 500 && result->is_active(); ++i) {
        idfxx::delay(10ms);
    }
    TEST_ASSERT_FALSE(result->is_active());
    idfxx::delay(100ms);
    TEST_ASSERT_EQUAL(1, count.load());
}

#endif // ESP_IDF_VERSION >= 6.1.0

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

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 1, 0)

TEST_CASE("timer restart with time_point succeeds via exception API", "[idfxx][timer]") {
    timer t({.name = "exc_restart_tp"}, []() {});

    t.start_once(1s);
    t.restart(timer::clock::now() + 500ms);
    TEST_ASSERT_TRUE(t.is_active());

    t.restart(timer::clock::now() + 500ms, 100ms);
    TEST_ASSERT_TRUE(t.is_active());
    t.stop_and_wait();
    TEST_ASSERT_FALSE(t.is_active());
}

TEST_CASE("timer start_periodic with time_point and stop_and_wait via exception API", "[idfxx][timer]") {
    timer t({.name = "exc_periodic_tp"}, []() {});

    t.start_periodic(timer::clock::now() + 1s, 100ms);
    TEST_ASSERT_TRUE(t.is_active());
    t.stop_and_wait(100ms);
    TEST_ASSERT_FALSE(t.is_active());
}

TEST_CASE("timer stop_and_wait with timeout throws on a long callback", "[idfxx][timer]") {
    std::atomic<bool> callback_started{false};
    std::atomic<bool> callback_completed{false};
    timer t({.name = "exc_stop_timeout"}, [&]() {
        callback_started.store(true);
        idfxx::delay(500ms);
        callback_completed.store(true);
    });

    t.start_once(10ms);
    wait_for(callback_started);
    TEST_ASSERT_TRUE(callback_started.load());

    bool threw = false;
    try {
        t.stop_and_wait(20ms);
    } catch (const std::system_error& e) {
        threw = true;
        TEST_ASSERT_EQUAL(std::to_underlying(errc::timeout), e.code().value());
    }
    TEST_ASSERT_TRUE(threw);

    wait_for(callback_completed);
    TEST_ASSERT_TRUE(callback_completed.load());
}

TEST_CASE("timer::start_periodic static factory with time_point via exception API", "[idfxx][timer]") {
    std::atomic<int> count{0};
    auto t = timer::start_periodic(
        {.name = "exc_periodic_tp_static"}, timer::clock::now() + 20ms, 20ms, [&count]() { count.fetch_add(1); }
    );

    TEST_ASSERT_TRUE(t.is_active());
    wait_for_count(count, 3);
    TEST_ASSERT_GREATER_OR_EQUAL(3, count.load());
}

#endif // ESP_IDF_VERSION >= 6.1.0

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
