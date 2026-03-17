// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Unit tests for idfxx event
// Uses ESP-IDF Unity test framework with compile-time static_asserts

#include "idfxx/event"
#include "unity.h"

#include <atomic>
#include <chrono>
#include <esp_event.h>
#include <idfxx/sched>
#include <tuple>
#include <type_traits>

using namespace std::chrono_literals;

using namespace idfxx;

// =============================================================================
// Test event definitions
// =============================================================================

enum class test_event_id : int32_t {
    event_a = 0,
    event_b = 1,
    event_c = 2,
};

IDFXX_EVENT_DEFINE_BASE(test_events, test_event_id);

struct test_event_data {
    int value;

    static test_event_data from_opaque(const void* data) {
        return *static_cast<const test_event_data*>(data);
    }
};

inline constexpr event<test_event_id, test_event_data> test_data_event{test_event_id::event_a};
inline constexpr event<test_event_id> test_void_event_b{test_event_id::event_b};
inline constexpr event<test_event_id> test_void_event_c{test_event_id::event_c};

// =============================================================================
// Compile-time tests (static_assert)
// These verify correctness at compile time - if this file compiles, they pass.
// =============================================================================

// event_base is trivially copyable (just a pointer)
static_assert(std::is_trivially_copyable_v<event_base<test_event_id>>);

// event_base can be constructed from esp_event_base_t
static_assert(std::is_constructible_v<event_base<test_event_id>, esp_event_base_t>);

// event_base is NOT constructible from string types (no more string API)
static_assert(!std::is_constructible_v<event_base<test_event_id>, std::string>);
static_assert(!std::is_constructible_v<event_base<test_event_id>, std::string_view>);

// Verify macro-defined base works
static_assert(test_events.idf_base() != nullptr);

// test_event_data is trivially copyable
static_assert(std::is_trivially_copyable_v<test_event_data>);

// event is constexpr constructible
static_assert(test_data_event.id == test_event_id::event_a);
static_assert(test_void_event_b.id == test_event_id::event_b);

// ADL base lookup works
static_assert(event_base_lookup<test_event_id>().idf_base() == test_events.idf_base());

// event data concepts
static_assert(receivable_event_data<test_event_data>);
static_assert(event_data<test_event_data>);

// listener_handle is default constructible
static_assert(std::is_default_constructible_v<event_loop::listener_handle>);

// unique_listener_handle is move-only
static_assert(std::is_move_constructible_v<event_loop::unique_listener_handle>);
static_assert(std::is_move_assignable_v<event_loop::unique_listener_handle>);
static_assert(!std::is_copy_constructible_v<event_loop::unique_listener_handle>);
static_assert(!std::is_copy_assignable_v<event_loop::unique_listener_handle>);

// event_loop is move-only
static_assert(!std::is_copy_constructible_v<event_loop>);
static_assert(!std::is_copy_assignable_v<event_loop>);
static_assert(std::is_move_constructible_v<event_loop>);
static_assert(std::is_move_assignable_v<event_loop>);

// user_event_loop is move-only
static_assert(!std::is_copy_constructible_v<user_event_loop>);
static_assert(!std::is_copy_assignable_v<user_event_loop>);
static_assert(std::is_move_constructible_v<user_event_loop>);
static_assert(std::is_move_assignable_v<user_event_loop>);

// =============================================================================
// Runtime tests (Unity TEST_CASE)
// =============================================================================

TEST_CASE("user_event_loop::make creates loop successfully", "[idfxx][event]") {
    auto result = user_event_loop::make();
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_NOT_NULL(result.value().idf_handle());
}

TEST_CASE("event_loop::make with task creates loop", "[idfxx][event]") {
    auto result = event_loop::make({.name = "test_events", .stack_size = 2048, .priority = 5}, 16);
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_NOT_NULL(result.value().idf_handle());

    // Give task time to start
    idfxx::delay(10ms);
}

TEST_CASE("user_event_loop posts and receives typed events", "[idfxx][event]") {
    auto loop = event_loop::make({.name = "test_loop"}).value();
    std::atomic<int> counter{0};

    auto handle_result = loop.try_listener_add(test_void_event_b, [&counter]() { counter++; });
    TEST_ASSERT_TRUE(handle_result);

    // Post event
    auto post_result = loop.try_post(test_void_event_b);
    TEST_ASSERT_TRUE(post_result);

    // Wait for event to be processed
    idfxx::delay(50ms);

    TEST_ASSERT_EQUAL(1, counter.load());
}

TEST_CASE("user_event_loop listener receives any event from base", "[idfxx][event]") {
    auto loop = event_loop::make({.name = "test_loop"}).value();

    std::atomic<int> counter{0};

    // Register for any event from test_events base
    auto handle_result =
        loop.try_listener_add(test_events, [&counter](event_base<test_event_id>, test_event_id id, void*) {
            counter++;
        });
    TEST_ASSERT_TRUE(handle_result);

    // Post different events
    TEST_ASSERT_TRUE(loop.try_post(test_data_event, test_event_data{0}));
    TEST_ASSERT_TRUE(loop.try_post(test_void_event_b));
    TEST_ASSERT_TRUE(loop.try_post(test_void_event_c));

    idfxx::delay(50ms);

    TEST_ASSERT_EQUAL(3, counter.load());
}

TEST_CASE("user_event_loop typed event with data", "[idfxx][event]") {
    auto loop = event_loop::make({.name = "test_loop"}).value();

    std::atomic<int> received_value{0};

    auto handle_result =
        loop.try_listener_add(test_data_event, [&received_value](const test_event_data& data) {
            received_value = data.value;
        });
    TEST_ASSERT_TRUE(handle_result);

    TEST_ASSERT_TRUE(loop.try_post(test_data_event, test_event_data{42}));

    idfxx::delay(50ms);

    TEST_ASSERT_EQUAL(42, received_value.load());
}

TEST_CASE("user_event_loop listener removal", "[idfxx][event]") {
    auto loop = event_loop::make({.name = "test_loop"}).value();

    std::atomic<int> counter{0};

    auto handle = loop.try_listener_add(test_void_event_b, [&counter]() { counter++; }).value();

    // Post and verify
    TEST_ASSERT_TRUE(loop.try_post(test_void_event_b));
    idfxx::delay(50ms);
    TEST_ASSERT_EQUAL(1, counter.load());

    // Remove listener
    auto remove_result = loop.try_listener_remove(handle);
    TEST_ASSERT_TRUE(remove_result);

    // Post again - should not increment
    TEST_ASSERT_TRUE(loop.try_post(test_void_event_b));
    idfxx::delay(50ms);
    TEST_ASSERT_EQUAL(1, counter.load());
}

TEST_CASE("unique_listener_handle RAII cleanup", "[idfxx][event]") {
    auto loop = event_loop::make({.name = "test_loop"}).value();

    std::atomic<int> counter{0};

    {
        event_loop::unique_listener_handle unique_handle{
            loop.try_listener_add(test_void_event_b, [&counter]() { counter++; }).value()};

        TEST_ASSERT_TRUE(loop.try_post(test_void_event_b));
        idfxx::delay(50ms);
        TEST_ASSERT_EQUAL(1, counter.load());

        // unique_handle goes out of scope here, removing listener
    }

    // Post again - should not increment since listener was removed
    TEST_ASSERT_TRUE(loop.try_post(test_void_event_b));
    idfxx::delay(50ms);
    TEST_ASSERT_EQUAL(1, counter.load());
}

TEST_CASE("unique_listener_handle move semantics", "[idfxx][event]") {
    auto loop = event_loop::make({.name = "test_loop"}).value();

    std::atomic<int> counter{0};

    event_loop::unique_listener_handle handle1{
        loop.try_listener_add(test_void_event_b, [&counter]() { counter++; }).value()};

    // Move to new handle
    event_loop::unique_listener_handle handle2 = std::move(handle1);

    // Original should be empty
    TEST_ASSERT_FALSE(static_cast<bool>(handle1));
    TEST_ASSERT_TRUE(static_cast<bool>(handle2));

    TEST_ASSERT_TRUE(loop.try_post(test_void_event_b));
    idfxx::delay(50ms);
    TEST_ASSERT_EQUAL(1, counter.load());
}

TEST_CASE("unique_listener_handle release", "[idfxx][event]") {
    auto loop = event_loop::make({.name = "test_loop"}).value();

    std::atomic<int> counter{0};

    event_loop::listener_handle raw_handle;

    {
        event_loop::unique_listener_handle unique_handle{
            loop.try_listener_add(test_void_event_b, [&counter]() { counter++; }).value()};

        raw_handle = unique_handle.release();
        TEST_ASSERT_FALSE(static_cast<bool>(unique_handle));
    }

    // Listener should still be active after unique_handle destroyed
    TEST_ASSERT_TRUE(loop.try_post(test_void_event_b));
    idfxx::delay(50ms);
    TEST_ASSERT_EQUAL(1, counter.load());

    // Clean up manually
    std::ignore = loop.try_listener_remove(raw_handle);
}

TEST_CASE("user_event_loop manual dispatch without task", "[idfxx][event]") {
    // Create loop without dedicated task
    auto loop = user_event_loop::make(16).value();

    std::atomic<int> counter{0};

    auto handle_result = loop.try_listener_add(test_void_event_b, [&counter]() { counter++; });
    TEST_ASSERT_TRUE(handle_result);

    // Post event
    TEST_ASSERT_TRUE(loop.try_post(test_void_event_b));

    // Counter should still be 0 since no dispatch yet
    TEST_ASSERT_EQUAL(0, counter.load());

    // Manually dispatch
    TEST_ASSERT_TRUE(loop.try_run(100ms));

    TEST_ASSERT_EQUAL(1, counter.load());
}

TEST_CASE("event_loop::system create and destroy", "[idfxx][event]") {
    auto create_result = event_loop::try_create_system();
    TEST_ASSERT_TRUE(create_result);

    auto destroy_result = event_loop::try_destroy_system();
    TEST_ASSERT_TRUE(destroy_result);
}

TEST_CASE("event_loop::system posts and receives events", "[idfxx][event]") {
    auto create_result = event_loop::try_create_system();
    TEST_ASSERT_TRUE(create_result);

    std::atomic<int> counter{0};

    event_loop& sys = event_loop::system();
    auto handle_result = sys.try_listener_add(test_void_event_b, [&counter]() { counter++; });
    TEST_ASSERT_TRUE(handle_result);

    TEST_ASSERT_TRUE(sys.try_post(test_void_event_b));
    idfxx::delay(50ms);

    TEST_ASSERT_EQUAL(1, counter.load());

    std::ignore = sys.try_listener_remove(handle_result.value());
    std::ignore = event_loop::try_destroy_system();
}

TEST_CASE("multiple listeners for same event", "[idfxx][event]") {
    auto loop = event_loop::make({.name = "test_loop"}, 32).value();

    std::atomic<int> counter1{0};
    std::atomic<int> counter2{0};

    event_loop::unique_listener_handle handle1{
        loop.try_listener_add(test_void_event_b, [&counter1]() { counter1++; }).value()};

    event_loop::unique_listener_handle handle2{
        loop.try_listener_add(test_void_event_b, [&counter2]() { counter2++; }).value()};

    TEST_ASSERT_TRUE(loop.try_post(test_void_event_b));
    idfxx::delay(50ms);

    TEST_ASSERT_EQUAL(1, counter1.load());
    TEST_ASSERT_EQUAL(1, counter2.load());
}

TEST_CASE("event_loop::task_config default values", "[idfxx][event]") {
    event_loop::task_config task_cfg{.name = "test"};
    TEST_ASSERT_EQUAL(2048, task_cfg.stack_size);
    TEST_ASSERT_EQUAL(5u, task_cfg.priority.value());
    TEST_ASSERT_FALSE(task_cfg.core_affinity.has_value());
}

TEST_CASE("generic function works with both loop types", "[idfxx][event]") {
    // A function that accepts event_loop& should work with both system and user loops

    auto post_and_count = [](event_loop& loop) -> int {
        std::atomic<int> counter{0};

        auto handle = loop.try_listener_add(test_void_event_b, [&counter]() { counter++; }).value();

        std::ignore = loop.try_post(test_void_event_b);
        idfxx::delay(50ms);

        auto result = counter.load();
        std::ignore = loop.try_listener_remove(handle);
        return result;
    };

    // Test with user-created loop
    auto user_loop = event_loop::make({.name = "test_loop"}).value();
    int user_count = post_and_count(user_loop);
    TEST_ASSERT_EQUAL(1, user_count);

    // Test with system loop
    TEST_ASSERT_TRUE(event_loop::try_create_system());
    int system_count = post_and_count(event_loop::system());
    TEST_ASSERT_EQUAL(1, system_count);
    std::ignore = event_loop::try_destroy_system();
}

TEST_CASE("moved-from event_loop returns invalid_state", "[idfxx][event]") {
    auto loop = event_loop::make({.name = "test"}).value();
    auto moved = std::move(loop);
    auto result = loop.try_post(test_void_event_b);
    TEST_ASSERT_FALSE(result);
}
