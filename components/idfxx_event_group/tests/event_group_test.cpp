// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Unit tests for idfxx event_group
// Uses ESP-IDF Unity test framework with compile-time static_asserts

#include <idfxx/event_group>
#include <unity.h>

#include <atomic>
#include <chrono>
#include <idfxx/chrono>
#include <idfxx/sched>
#include <idfxx/task>
#include <type_traits>

using namespace idfxx;
using namespace std::chrono_literals;

enum class test_event : uint32_t {
    event_a = 1u << 0,
    event_b = 1u << 1,
    event_c = 1u << 2,
    event_d = 1u << 3,
};
template<>
inline constexpr bool idfxx::enable_flags_operators<test_event> = true;

// =============================================================================
// Compile-time tests (static_assert)
// These verify correctness at compile time - if this file compiles, they pass.
// =============================================================================

// event_group<test_event> is not copyable
static_assert(!std::is_copy_constructible_v<event_group<test_event>>);
static_assert(!std::is_copy_assignable_v<event_group<test_event>>);

// event_group<test_event> is move-only
static_assert(std::is_move_constructible_v<event_group<test_event>>);
static_assert(std::is_move_assignable_v<event_group<test_event>>);

// =============================================================================
// Runtime tests (Unity TEST_CASE)
// =============================================================================

// =============================================================================
// Construction tests
// =============================================================================

TEST_CASE("event_group constructor succeeds", "[idfxx][event_group]") {
    event_group<test_event> eg;
    TEST_ASSERT_NOT_NULL(eg.idf_handle());
}

// =============================================================================
// Set and get tests
// =============================================================================

TEST_CASE("event_group set and get round-trip", "[idfxx][event_group]") {
    event_group<test_event> eg;

    eg.set(test_event::event_a);
    auto bits = eg.get();
    TEST_ASSERT_TRUE(bits.contains(test_event::event_a));
    TEST_ASSERT_FALSE(bits.contains(test_event::event_b));

    eg.set(test_event::event_b);
    bits = eg.get();
    TEST_ASSERT_TRUE(bits.contains(test_event::event_a));
    TEST_ASSERT_TRUE(bits.contains(test_event::event_b));
}

TEST_CASE("event_group set returns bits at time of return", "[idfxx][event_group]") {
    event_group<test_event> eg;

    auto returned = eg.set(test_event::event_a);
    TEST_ASSERT_TRUE(returned.contains(test_event::event_a));

    returned = eg.set(test_event::event_b);
    TEST_ASSERT_TRUE(returned.contains(test_event::event_a));
    TEST_ASSERT_TRUE(returned.contains(test_event::event_b));
}

// =============================================================================
// Clear tests
// =============================================================================

TEST_CASE("event_group clear returns previous bits and clears", "[idfxx][event_group]") {
    event_group<test_event> eg;

    eg.set(test_event::event_a | test_event::event_b);

    auto prev = eg.clear(test_event::event_a);
    TEST_ASSERT_TRUE(prev.contains(test_event::event_a));
    TEST_ASSERT_TRUE(prev.contains(test_event::event_b));

    auto bits = eg.get();
    TEST_ASSERT_FALSE(bits.contains(test_event::event_a));
    TEST_ASSERT_TRUE(bits.contains(test_event::event_b));
}

// =============================================================================
// Wait tests
// =============================================================================

TEST_CASE("event_group wait any satisfied immediately", "[idfxx][event_group]") {
    event_group<test_event> eg;

    eg.set(test_event::event_a);

    auto wait_result = eg.try_wait(test_event::event_a | test_event::event_b, wait_mode::any, 0ms, false);
    TEST_ASSERT_TRUE(wait_result.has_value());
    TEST_ASSERT_TRUE(wait_result->contains(test_event::event_a));
}

TEST_CASE("event_group wait all satisfied immediately", "[idfxx][event_group]") {
    event_group<test_event> eg;

    eg.set(test_event::event_a | test_event::event_b);

    auto wait_result = eg.try_wait(test_event::event_a | test_event::event_b, wait_mode::all, 0ms, false);
    TEST_ASSERT_TRUE(wait_result.has_value());
    TEST_ASSERT_TRUE(wait_result->contains(test_event::event_a));
    TEST_ASSERT_TRUE(wait_result->contains(test_event::event_b));
}

TEST_CASE("event_group wait any times out with no bits set", "[idfxx][event_group]") {
    event_group<test_event> eg;

    auto wait_result = eg.try_wait(test_event::event_a, wait_mode::any, 10ms);
    TEST_ASSERT_FALSE(wait_result.has_value());
    TEST_ASSERT_EQUAL(std::to_underlying(errc::timeout), wait_result.error().value());
}

TEST_CASE("event_group wait all times out with partial bits set", "[idfxx][event_group]") {
    event_group<test_event> eg;

    eg.set(test_event::event_a);

    auto wait_result = eg.try_wait(test_event::event_a | test_event::event_b, wait_mode::all, 10ms);
    TEST_ASSERT_FALSE(wait_result.has_value());
    TEST_ASSERT_EQUAL(std::to_underlying(errc::timeout), wait_result.error().value());
}

TEST_CASE("event_group wait with clear_on_exit clears bits", "[idfxx][event_group]") {
    event_group<test_event> eg;

    eg.set(test_event::event_a | test_event::event_b);

    auto wait_result = eg.try_wait(test_event::event_a, wait_mode::any, 0ms, true);
    TEST_ASSERT_TRUE(wait_result.has_value());

    // event_a should be cleared, event_b should remain
    auto bits = eg.get();
    TEST_ASSERT_FALSE(bits.contains(test_event::event_a));
    TEST_ASSERT_TRUE(bits.contains(test_event::event_b));
}

TEST_CASE("event_group wait with clear_on_exit=false preserves bits", "[idfxx][event_group]") {
    event_group<test_event> eg;

    eg.set(test_event::event_a | test_event::event_b);

    auto wait_result = eg.try_wait(test_event::event_a, wait_mode::any, 0ms, false);
    TEST_ASSERT_TRUE(wait_result.has_value());

    // Both bits should remain set
    auto bits = eg.get();
    TEST_ASSERT_TRUE(bits.contains(test_event::event_a));
    TEST_ASSERT_TRUE(bits.contains(test_event::event_b));
}

// =============================================================================
// Blocking wait tests (multi-task)
// =============================================================================

TEST_CASE("event_group blocking wait any - other task sets bits", "[idfxx][event_group]") {
    event_group<test_event> eg;

    std::atomic<bool> received{false};

    // Waiter task: blocks waiting for event_a
    auto t = std::make_unique<task>(task::config{.name = "eg_wait"}, [&eg, &received](task::self&) {
        auto r = eg.try_wait(test_event::event_a, wait_mode::any, 500ms);
        if (r.has_value()) {
            received.store(true);
        }
    });

    // Let waiter block
    idfxx::delay(50ms);
    TEST_ASSERT_FALSE(received.load());

    // Set the bit
    eg.set(test_event::event_a);

    // Wait for waiter to complete
    idfxx::delay(100ms);
    TEST_ASSERT_TRUE(received.load());
}

TEST_CASE("event_group blocking wait all - two tasks set different bits", "[idfxx][event_group]") {
    event_group<test_event> eg;

    std::atomic<bool> received{false};

    // Waiter task: blocks waiting for both event_a and event_b
    auto waiter = std::make_unique<task>(task::config{.name = "eg_all"}, [&eg, &received](task::self&) {
        auto r = eg.try_wait(test_event::event_a | test_event::event_b, wait_mode::all, 500ms);
        if (r.has_value()) {
            received.store(true);
        }
    });

    // Task 1 sets event_a
    auto setter1 = std::make_unique<task>(task::config{.name = "eg_set1"}, [&eg](task::self&) {
        idfxx::delay(50ms);
        eg.set(test_event::event_a);
    });

    // Task 2 sets event_b
    auto setter2 = std::make_unique<task>(task::config{.name = "eg_set2"}, [&eg](task::self&) {
        idfxx::delay(100ms);
        eg.set(test_event::event_b);
    });

    // Wait for all tasks to complete
    auto join1 = waiter->try_join(1000ms);
    TEST_ASSERT_TRUE(join1.has_value());
    auto join2 = setter1->try_join(1000ms);
    TEST_ASSERT_TRUE(join2.has_value());
    auto join3 = setter2->try_join(1000ms);
    TEST_ASSERT_TRUE(join3.has_value());

    TEST_ASSERT_TRUE(received.load());
}

// =============================================================================
// Deadline tests
// =============================================================================

TEST_CASE("event_group wait_until with expired deadline returns immediately", "[idfxx][event_group]") {
    event_group<test_event> eg;

    auto past = chrono::tick_clock::now() - 100ms;
    auto wait_result = eg.try_wait_until(test_event::event_a, wait_mode::any, past);
    TEST_ASSERT_FALSE(wait_result.has_value());
    TEST_ASSERT_EQUAL(std::to_underlying(errc::timeout), wait_result.error().value());
}

// =============================================================================
// Sync tests
// =============================================================================

TEST_CASE("event_group sync - two tasks rendezvous", "[idfxx][event_group]") {
    event_group<test_event> eg;

    std::atomic<bool> task1_synced{false};
    std::atomic<bool> task2_synced{false};

    auto wait_bits = test_event::event_a | test_event::event_b;

    // Task 1: sets event_a, waits for both
    auto t1 = std::make_unique<task>(task::config{.name = "eg_sync1"}, [&eg, &task1_synced, wait_bits](task::self&) {
        auto r = eg.try_sync(test_event::event_a, wait_bits, 500ms);
        if (r.has_value()) {
            task1_synced.store(true);
        }
    });

    // Task 2: sets event_b, waits for both
    auto t2 = std::make_unique<task>(task::config{.name = "eg_sync2"}, [&eg, &task2_synced, wait_bits](task::self&) {
        auto r = eg.try_sync(test_event::event_b, wait_bits, 500ms);
        if (r.has_value()) {
            task2_synced.store(true);
        }
    });

    // Wait for both tasks
    auto join1 = t1->try_join(1000ms);
    TEST_ASSERT_TRUE(join1.has_value());
    auto join2 = t2->try_join(1000ms);
    TEST_ASSERT_TRUE(join2.has_value());

    TEST_ASSERT_TRUE(task1_synced.load());
    TEST_ASSERT_TRUE(task2_synced.load());
}

TEST_CASE("event_group sync times out", "[idfxx][event_group]") {
    event_group<test_event> eg;

    // Sync waiting for event_b, but nobody sets it
    auto sync_result =
        eg.try_sync(test_event::event_a, test_event::event_a | test_event::event_b, 10ms);
    TEST_ASSERT_FALSE(sync_result.has_value());
    TEST_ASSERT_EQUAL(std::to_underlying(errc::timeout), sync_result.error().value());
}

TEST_CASE("event_group wait_until with future deadline succeeds", "[idfxx][event_group]") {
    event_group<test_event> eg;

    eg.set(test_event::event_a);

    auto deadline = chrono::tick_clock::now() + 100ms;
    auto result = eg.try_wait_until(test_event::event_a, wait_mode::any, deadline);
    TEST_ASSERT_TRUE(result.has_value());
    TEST_ASSERT_TRUE(result->contains(test_event::event_a));
}

TEST_CASE("event_group try_sync_until succeeds", "[idfxx][event_group]") {
    event_group<test_event> eg;

    auto wait_bits = test_event::event_a | test_event::event_b;

    // Task sets event_b
    auto t = std::make_unique<task>(task::config{.name = "sync_until"}, [&eg](task::self&) {
        idfxx::delay(50ms);
        (void)eg.try_sync(test_event::event_b, test_event::event_a | test_event::event_b, 500ms);
    });

    auto deadline = chrono::tick_clock::now() + 1s;
    auto result = eg.try_sync_until(test_event::event_a, wait_bits, deadline);
    TEST_ASSERT_TRUE(result.has_value());

    auto join = t->try_join(1s);
    TEST_ASSERT_TRUE(join.has_value());
}

TEST_CASE("event_group try_sync_until times out with past deadline", "[idfxx][event_group]") {
    event_group<test_event> eg;

    auto past = chrono::tick_clock::now();
    auto result = eg.try_sync_until(test_event::event_a, test_event::event_a | test_event::event_b, past);
    TEST_ASSERT_FALSE(result.has_value());
    TEST_ASSERT_EQUAL(std::to_underlying(errc::timeout), result.error().value());
}

// =============================================================================
// Destructor test
// =============================================================================

TEST_CASE("event_group destructor cleans up", "[idfxx][event_group]") {
    {
        event_group<test_event> eg;

        eg.set(test_event::event_a | test_event::event_b);

        // Event group goes out of scope with bits still set
    }

    // If we get here without crashing, cleanup worked
    idfxx::delay(10ms);
}

// =============================================================================
// Exception-based API tests
// =============================================================================

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS

TEST_CASE("event_group wait succeeds via exception API", "[idfxx][event_group]") {
    event_group<test_event> eg;

    eg.set(test_event::event_a);

    auto bits = eg.wait(test_event::event_a, wait_mode::any, 0ms);
    TEST_ASSERT_TRUE(bits.contains(test_event::event_a));
}

TEST_CASE("event_group wait throws on timeout", "[idfxx][event_group]") {
    event_group<test_event> eg;

    bool threw = false;
    try {
        (void)eg.wait(test_event::event_a, wait_mode::any, 0ms);
    } catch (const std::system_error& e) {
        threw = true;
        TEST_ASSERT_EQUAL(std::to_underlying(errc::timeout), e.code().value());
    }
    TEST_ASSERT_TRUE(threw);
}

TEST_CASE("event_group wait_until succeeds via exception API", "[idfxx][event_group]") {
    event_group<test_event> eg;

    eg.set(test_event::event_b);

    auto deadline = idfxx::chrono::tick_clock::now() + 100ms;
    auto bits = eg.wait_until(test_event::event_b, wait_mode::any, deadline);
    TEST_ASSERT_TRUE(bits.contains(test_event::event_b));
}

TEST_CASE("event_group wait_until throws on deadline", "[idfxx][event_group]") {
    event_group<test_event> eg;

    bool threw = false;
    try {
        auto deadline = idfxx::chrono::tick_clock::now(); // already past
        (void)eg.wait_until(test_event::event_a, wait_mode::any, deadline);
    } catch (const std::system_error& e) {
        threw = true;
        TEST_ASSERT_EQUAL(std::to_underlying(errc::timeout), e.code().value());
    }
    TEST_ASSERT_TRUE(threw);
}

#endif // CONFIG_COMPILER_CXX_EXCEPTIONS

// =============================================================================
// Moved-from state tests
// =============================================================================

TEST_CASE("moved-from event_group returns defaults", "[idfxx][event_group]") {
    event_group<test_event> eg;
    eg.set(test_event::event_a);

    auto moved = std::move(eg);

    // moved-from object: simple operations return default values
    TEST_ASSERT_NULL(eg.idf_handle());
    TEST_ASSERT_TRUE(eg.get().empty());
    TEST_ASSERT_TRUE(eg.set(test_event::event_b).empty());
    TEST_ASSERT_TRUE(eg.clear(test_event::event_a).empty());

    // result-based wait on moved-from returns invalid_state
    auto wait_result = eg.try_wait(test_event::event_a, wait_mode::any, 0ms);
    TEST_ASSERT_FALSE(wait_result.has_value());
    TEST_ASSERT_EQUAL(std::to_underlying(errc::invalid_state), wait_result.error().value());

    // Moved-to object should work normally
    TEST_ASSERT_NOT_NULL(moved.idf_handle());
    TEST_ASSERT_FALSE(moved.get().empty());
}

TEST_CASE("event_group move assignment cleans up target", "[idfxx][event_group]") {
    event_group<test_event> eg1;
    event_group<test_event> eg2;

    eg1.set(test_event::event_a);
    eg2.set(test_event::event_b);

    // Move assignment should clean up target
    eg1 = std::move(eg2);

    TEST_ASSERT_NOT_NULL(eg1.idf_handle());
    TEST_ASSERT_NULL(eg2.idf_handle());
    TEST_ASSERT_TRUE(eg1.get().contains(test_event::event_b));
}
