// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Unit tests for idfxx queue
// Uses ESP-IDF Unity test framework with compile-time static_asserts

#include <idfxx/queue>
#include <unity.h>

#include <atomic>
#include <chrono>
#include <idfxx/chrono>
#include <idfxx/memory>
#include <idfxx/sched>
#include <idfxx/task>
#include <string>
#include <type_traits>

using namespace idfxx;
using namespace std::chrono_literals;

struct point {
    int x;
    int y;
};

struct sensor_data {
    uint32_t id;
    float value;
};

// =============================================================================
// Compile-time tests (static_assert)
// These verify correctness at compile time - if this file compiles, they pass.
// =============================================================================

// queue<int> is not default constructible
static_assert(!std::is_default_constructible_v<queue<int>>);

// queue<int> is not copyable
static_assert(!std::is_copy_constructible_v<queue<int>>);
static_assert(!std::is_copy_assignable_v<queue<int>>);

// queue<int> is move-only
static_assert(std::is_move_constructible_v<queue<int>>);
static_assert(std::is_move_assignable_v<queue<int>>);

// queue<std::string> is rejected at compile time (not trivially copyable)
static_assert(!std::is_trivially_copyable_v<std::string>);
// Note: queue<std::string> would fail the requires clause

// =============================================================================
// Runtime tests (Unity TEST_CASE)
// =============================================================================

// =============================================================================
// Construction tests
// =============================================================================

TEST_CASE("queue::make succeeds", "[idfxx][queue]") {
    auto result = queue<int>::make(10);
    TEST_ASSERT_TRUE(result.has_value());
    TEST_ASSERT_NOT_NULL(result->idf_handle());
}

TEST_CASE("queue::make with length 0 returns invalid_arg", "[idfxx][queue]") {
    auto result = queue<int>::make(0);
    TEST_ASSERT_FALSE(result.has_value());
    TEST_ASSERT_EQUAL(std::to_underlying(errc::invalid_arg), result.error().value());
}

TEST_CASE("queue::make with struct type", "[idfxx][queue]") {
    static_assert(std::is_trivially_copyable_v<point>);

    auto result = queue<point>::make(5);
    TEST_ASSERT_TRUE(result.has_value());
}

// =============================================================================
// Send and receive tests
// =============================================================================

TEST_CASE("queue send and receive round-trip", "[idfxx][queue]") {
    auto result = queue<int>::make(10);
    TEST_ASSERT_TRUE(result.has_value());
    auto& q = *result;

    auto send_result = q.try_send(42);
    TEST_ASSERT_TRUE(send_result.has_value());

    auto recv_result = q.try_receive(0ms);
    TEST_ASSERT_TRUE(recv_result.has_value());
    TEST_ASSERT_EQUAL(42, *recv_result);
}

TEST_CASE("queue maintains FIFO order", "[idfxx][queue]") {
    auto result = queue<int>::make(10);
    TEST_ASSERT_TRUE(result.has_value());
    auto& q = *result;

    TEST_ASSERT_TRUE(q.try_send(1).has_value());
    TEST_ASSERT_TRUE(q.try_send(2).has_value());
    TEST_ASSERT_TRUE(q.try_send(3).has_value());

    TEST_ASSERT_EQUAL(1, q.try_receive(0ms).value());
    TEST_ASSERT_EQUAL(2, q.try_receive(0ms).value());
    TEST_ASSERT_EQUAL(3, q.try_receive(0ms).value());
}

TEST_CASE("queue send_to_front puts item at front", "[idfxx][queue]") {
    auto result = queue<int>::make(10);
    TEST_ASSERT_TRUE(result.has_value());
    auto& q = *result;

    TEST_ASSERT_TRUE(q.try_send(1).has_value());
    TEST_ASSERT_TRUE(q.try_send(2).has_value());
    TEST_ASSERT_TRUE(q.try_send_to_front(99).has_value());

    TEST_ASSERT_EQUAL(99, q.try_receive(0ms).value());
    TEST_ASSERT_EQUAL(1, q.try_receive(0ms).value());
    TEST_ASSERT_EQUAL(2, q.try_receive(0ms).value());
}

TEST_CASE("queue send to full queue times out", "[idfxx][queue]") {
    auto result = queue<int>::make(2);
    TEST_ASSERT_TRUE(result.has_value());
    auto& q = *result;

    TEST_ASSERT_TRUE(q.try_send(1).has_value());
    TEST_ASSERT_TRUE(q.try_send(2).has_value());

    auto send_result = q.try_send(3, 10ms);
    TEST_ASSERT_FALSE(send_result.has_value());
    TEST_ASSERT_EQUAL(std::to_underlying(errc::timeout), send_result.error().value());
}

TEST_CASE("queue receive from empty queue times out", "[idfxx][queue]") {
    auto result = queue<int>::make(10);
    TEST_ASSERT_TRUE(result.has_value());
    auto& q = *result;

    auto recv_result = q.try_receive(10ms);
    TEST_ASSERT_FALSE(recv_result.has_value());
    TEST_ASSERT_EQUAL(std::to_underlying(errc::timeout), recv_result.error().value());
}

TEST_CASE("queue send and receive struct type", "[idfxx][queue]") {
    auto result = queue<sensor_data>::make(5);
    TEST_ASSERT_TRUE(result.has_value());
    auto& q = *result;

    TEST_ASSERT_TRUE(q.try_send({.id = 1, .value = 3.14f}).has_value());
    TEST_ASSERT_TRUE(q.try_send({.id = 2, .value = 2.72f}).has_value());

    auto item1 = q.try_receive(0ms);
    TEST_ASSERT_TRUE(item1.has_value());
    TEST_ASSERT_EQUAL(1, item1->id);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 3.14f, item1->value);

    auto item2 = q.try_receive(0ms);
    TEST_ASSERT_TRUE(item2.has_value());
    TEST_ASSERT_EQUAL(2, item2->id);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 2.72f, item2->value);
}

// =============================================================================
// Peek tests
// =============================================================================

TEST_CASE("queue peek returns item without removing", "[idfxx][queue]") {
    auto result = queue<int>::make(10);
    TEST_ASSERT_TRUE(result.has_value());
    auto& q = *result;

    TEST_ASSERT_TRUE(q.try_send(42).has_value());

    auto peek_result = q.try_peek(0ms);
    TEST_ASSERT_TRUE(peek_result.has_value());
    TEST_ASSERT_EQUAL(42, *peek_result);

    // Item should still be in the queue
    TEST_ASSERT_EQUAL(1, q.size());

    auto recv_result = q.try_receive(0ms);
    TEST_ASSERT_TRUE(recv_result.has_value());
    TEST_ASSERT_EQUAL(42, *recv_result);
}

TEST_CASE("queue peek on empty queue times out", "[idfxx][queue]") {
    auto result = queue<int>::make(10);
    TEST_ASSERT_TRUE(result.has_value());
    auto& q = *result;

    auto peek_result = q.try_peek(10ms);
    TEST_ASSERT_FALSE(peek_result.has_value());
    TEST_ASSERT_EQUAL(std::to_underlying(errc::timeout), peek_result.error().value());
}

// =============================================================================
// Overwrite tests
// =============================================================================

TEST_CASE("queue overwrite on single-item queue", "[idfxx][queue]") {
    auto result = queue<int>::make(1);
    TEST_ASSERT_TRUE(result.has_value());
    auto& q = *result;

    q.overwrite(10);
    TEST_ASSERT_EQUAL(1, q.size());

    q.overwrite(20);
    TEST_ASSERT_EQUAL(1, q.size());

    q.overwrite(30);
    TEST_ASSERT_EQUAL(1, q.size());

    auto recv_result = q.try_receive(0ms);
    TEST_ASSERT_TRUE(recv_result.has_value());
    TEST_ASSERT_EQUAL(30, *recv_result);
}

// =============================================================================
// Query and reset tests
// =============================================================================

TEST_CASE("queue size/available/empty/full track state", "[idfxx][queue]") {
    auto result = queue<int>::make(3);
    TEST_ASSERT_TRUE(result.has_value());
    auto& q = *result;

    // Initially empty
    TEST_ASSERT_EQUAL(0, q.size());
    TEST_ASSERT_EQUAL(3, q.available());
    TEST_ASSERT_TRUE(q.empty());
    TEST_ASSERT_FALSE(q.full());

    // Add one item
    TEST_ASSERT_TRUE(q.try_send(1).has_value());
    TEST_ASSERT_EQUAL(1, q.size());
    TEST_ASSERT_EQUAL(2, q.available());
    TEST_ASSERT_FALSE(q.empty());
    TEST_ASSERT_FALSE(q.full());

    // Fill up
    TEST_ASSERT_TRUE(q.try_send(2).has_value());
    TEST_ASSERT_TRUE(q.try_send(3).has_value());
    TEST_ASSERT_EQUAL(3, q.size());
    TEST_ASSERT_EQUAL(0, q.available());
    TEST_ASSERT_FALSE(q.empty());
    TEST_ASSERT_TRUE(q.full());

    // Remove one
    TEST_ASSERT_TRUE(q.try_receive(0ms).has_value());
    TEST_ASSERT_EQUAL(2, q.size());
    TEST_ASSERT_EQUAL(1, q.available());
    TEST_ASSERT_FALSE(q.empty());
    TEST_ASSERT_FALSE(q.full());
}

TEST_CASE("queue reset empties the queue", "[idfxx][queue]") {
    auto result = queue<int>::make(10);
    TEST_ASSERT_TRUE(result.has_value());
    auto& q = *result;

    TEST_ASSERT_TRUE(q.try_send(1).has_value());
    TEST_ASSERT_TRUE(q.try_send(2).has_value());
    TEST_ASSERT_TRUE(q.try_send(3).has_value());
    TEST_ASSERT_EQUAL(3, q.size());

    q.reset();
    TEST_ASSERT_EQUAL(0, q.size());
    TEST_ASSERT_TRUE(q.empty());

    // Should be able to receive nothing
    auto recv_result = q.try_receive(0ms);
    TEST_ASSERT_FALSE(recv_result.has_value());
}

// =============================================================================
// Timeout and deadline tests
// =============================================================================

TEST_CASE("queue blocking send succeeds when consumer frees space", "[idfxx][queue]") {
    auto q_result = queue<int>::make(1);
    TEST_ASSERT_TRUE(q_result.has_value());
    auto& q = *q_result;

    // Fill the queue
    TEST_ASSERT_TRUE(q.try_send(1).has_value());

    std::atomic<bool> sent{false};

    // Producer task: blocks trying to send to a full queue
    auto t = std::make_unique<task>(task::config{.name = "q_producer"}, [&q, &sent](task::self&) {
        auto r = q.try_send(2, 500ms);
        if (r.has_value()) {
            sent.store(true);
        }
    });

    // Let producer block
    idfxx::delay(50ms);
    TEST_ASSERT_FALSE(sent.load());

    // Consume item to free space
    auto recv = q.try_receive(0ms);
    TEST_ASSERT_TRUE(recv.has_value());
    TEST_ASSERT_EQUAL(1, *recv);

    // Wait for producer to complete
    idfxx::delay(100ms);
    TEST_ASSERT_TRUE(sent.load());

    // Receive the item the producer sent
    auto recv2 = q.try_receive(0ms);
    TEST_ASSERT_TRUE(recv2.has_value());
    TEST_ASSERT_EQUAL(2, *recv2);
}

TEST_CASE("queue blocking receive succeeds when producer sends", "[idfxx][queue]") {
    auto q_result = queue<int>::make(10);
    TEST_ASSERT_TRUE(q_result.has_value());
    auto& q = *q_result;

    std::atomic<int> received{0};

    // Consumer task: blocks trying to receive from an empty queue
    auto t = std::make_unique<task>(task::config{.name = "q_consumer"}, [&q, &received](task::self&) {
        auto r = q.try_receive(500ms);
        if (r.has_value()) {
            received.store(*r);
        }
    });

    // Let consumer block
    idfxx::delay(50ms);
    TEST_ASSERT_EQUAL(0, received.load());

    // Send an item
    TEST_ASSERT_TRUE(q.try_send(99).has_value());

    // Wait for consumer to receive
    idfxx::delay(100ms);
    TEST_ASSERT_EQUAL(99, received.load());
}

TEST_CASE("queue send_until with expired deadline returns immediately", "[idfxx][queue]") {
    auto result = queue<int>::make(1);
    TEST_ASSERT_TRUE(result.has_value());
    auto& q = *result;

    // Fill the queue
    TEST_ASSERT_TRUE(q.try_send(1).has_value());

    // Use a deadline in the past
    auto past = chrono::tick_clock::now() - 100ms;
    auto send_result = q.try_send_until(2, past);
    TEST_ASSERT_FALSE(send_result.has_value());
    TEST_ASSERT_EQUAL(std::to_underlying(errc::timeout), send_result.error().value());
}

TEST_CASE("queue receive_until with expired deadline returns immediately", "[idfxx][queue]") {
    auto result = queue<int>::make(10);
    TEST_ASSERT_TRUE(result.has_value());
    auto& q = *result;

    auto past = chrono::tick_clock::now() - 100ms;
    auto recv_result = q.try_receive_until(past);
    TEST_ASSERT_FALSE(recv_result.has_value());
    TEST_ASSERT_EQUAL(std::to_underlying(errc::timeout), recv_result.error().value());
}

TEST_CASE("queue send_to_front_until with expired deadline returns immediately", "[idfxx][queue]") {
    auto result = queue<int>::make(1);
    TEST_ASSERT_TRUE(result.has_value());
    auto& q = *result;

    TEST_ASSERT_TRUE(q.try_send(1).has_value());

    auto past = chrono::tick_clock::now() - 100ms;
    auto send_result = q.try_send_to_front_until(2, past);
    TEST_ASSERT_FALSE(send_result.has_value());
    TEST_ASSERT_EQUAL(std::to_underlying(errc::timeout), send_result.error().value());
}

TEST_CASE("queue peek_until with expired deadline returns immediately", "[idfxx][queue]") {
    auto result = queue<int>::make(10);
    TEST_ASSERT_TRUE(result.has_value());
    auto& q = *result;

    auto past = chrono::tick_clock::now() - 100ms;
    auto peek_result = q.try_peek_until(past);
    TEST_ASSERT_FALSE(peek_result.has_value());
    TEST_ASSERT_EQUAL(std::to_underlying(errc::timeout), peek_result.error().value());
}

// =============================================================================
// Multi-task producer-consumer tests
// =============================================================================

TEST_CASE("queue producer-consumer across tasks", "[idfxx][queue]") {
    auto q_result = queue<int>::make(5);
    TEST_ASSERT_TRUE(q_result.has_value());
    auto& q = *q_result;

    constexpr int num_items = 20;
    std::atomic<int> sum{0};

    // Producer task
    auto producer = std::make_unique<task>(task::config{.name = "q_prod"}, [&q](task::self&) {
        for (int i = 1; i <= num_items; ++i) {
            auto r = q.try_send(i, 500ms);
            (void)r;
        }
    });

    // Consumer task
    auto consumer = std::make_unique<task>(task::config{.name = "q_cons"}, [&q, &sum](task::self& self) {
        for (int i = 0; i < num_items; ++i) {
            auto r = q.try_receive(500ms);
            if (r.has_value()) {
                sum.fetch_add(*r);
            }
        }
    });

    // Wait for both tasks to finish
    auto join1 = producer->try_join(5000ms);
    TEST_ASSERT_TRUE(join1.has_value());

    auto join2 = consumer->try_join(5000ms);
    TEST_ASSERT_TRUE(join2.has_value());

    // Sum of 1..20 = 210
    TEST_ASSERT_EQUAL(210, sum.load());
}

// =============================================================================
// Destructor test
// =============================================================================

TEST_CASE("queue destructor cleans up with items in queue", "[idfxx][queue]") {
    {
        auto result = queue<int>::make(10);
        TEST_ASSERT_TRUE(result.has_value());
        auto& q = *result;

        TEST_ASSERT_TRUE(q.try_send(1).has_value());
        TEST_ASSERT_TRUE(q.try_send(2).has_value());
        TEST_ASSERT_TRUE(q.try_send(3).has_value());

        // Queue goes out of scope with items still in it
    }

    // If we get here without crashing, cleanup worked
    idfxx::delay(10ms);
}

// =============================================================================
// Storage memory tests
// =============================================================================

TEST_CASE("queue::make with explicit internal storage", "[idfxx][queue]") {
    auto result = queue<int>::make(10, memory::capabilities::dram);
    TEST_ASSERT_TRUE(result.has_value());
    auto& q = *result;
    TEST_ASSERT_TRUE(q.try_send(42).has_value());
    auto recv = q.try_receive(0ms);
    TEST_ASSERT_TRUE(recv.has_value());
    TEST_ASSERT_EQUAL(42, *recv);
}

#if CONFIG_SPIRAM

TEST_CASE("queue::make with spiram storage", "[idfxx][queue]") {
    auto result = queue<int>::make(10, memory::capabilities::spiram);
    TEST_ASSERT_TRUE(result.has_value());
    auto& q = *result;

    // Verify basic send/receive works with PSRAM-backed queue
    TEST_ASSERT_TRUE(q.try_send(42).has_value());
    TEST_ASSERT_TRUE(q.try_send(99).has_value());

    auto recv1 = q.try_receive(0ms);
    TEST_ASSERT_TRUE(recv1.has_value());
    TEST_ASSERT_EQUAL(42, *recv1);

    auto recv2 = q.try_receive(0ms);
    TEST_ASSERT_TRUE(recv2.has_value());
    TEST_ASSERT_EQUAL(99, *recv2);
}

#endif // CONFIG_SPIRAM

// =============================================================================
// Deadline-based operation tests
// =============================================================================

TEST_CASE("try_send_until with future deadline succeeds", "[idfxx][queue]") {
    auto result = queue<int>::make(5);
    TEST_ASSERT_TRUE(result.has_value());
    auto& q = *result;

    auto deadline = idfxx::chrono::tick_clock::now() + 100ms;
    auto send_result = q.try_send_until(42, deadline);
    TEST_ASSERT_TRUE(send_result.has_value());

    auto recv_result = q.try_receive(0ms);
    TEST_ASSERT_TRUE(recv_result.has_value());
    TEST_ASSERT_EQUAL(42, *recv_result);
}

TEST_CASE("try_receive_until with future deadline succeeds", "[idfxx][queue]") {
    auto result = queue<int>::make(5);
    TEST_ASSERT_TRUE(result.has_value());
    auto& q = *result;

    (void)q.try_send(99, 0ms);

    auto deadline = idfxx::chrono::tick_clock::now() + 100ms;
    auto recv_result = q.try_receive_until(deadline);
    TEST_ASSERT_TRUE(recv_result.has_value());
    TEST_ASSERT_EQUAL(99, *recv_result);
}

TEST_CASE("try_receive_until with past deadline returns timeout", "[idfxx][queue]") {
    auto result = queue<int>::make(5);
    TEST_ASSERT_TRUE(result.has_value());
    auto& q = *result;

    auto deadline = idfxx::chrono::tick_clock::now(); // already past
    auto recv_result = q.try_receive_until(deadline);
    TEST_ASSERT_FALSE(recv_result.has_value());
    TEST_ASSERT_EQUAL(std::to_underlying(errc::timeout), recv_result.error().value());
}

TEST_CASE("try_send_until on full queue with past deadline returns timeout", "[idfxx][queue]") {
    auto result = queue<int>::make(1);
    TEST_ASSERT_TRUE(result.has_value());
    auto& q = *result;

    (void)q.try_send(1, 0ms);

    auto deadline = idfxx::chrono::tick_clock::now(); // already past
    auto send_result = q.try_send_until(2, deadline);
    TEST_ASSERT_FALSE(send_result.has_value());
    TEST_ASSERT_EQUAL(std::to_underlying(errc::timeout), send_result.error().value());
}

// =============================================================================
// Exception-based API tests
// =============================================================================

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS

TEST_CASE("queue constructor succeeds", "[idfxx][queue]") {
    queue<int> q(5);
    TEST_ASSERT_NOT_NULL(q.idf_handle());
    TEST_ASSERT_EQUAL(0, q.size());
}

TEST_CASE("queue constructor throws on zero length", "[idfxx][queue]") {
    bool threw = false;
    try {
        queue<int> q(0);
        (void)q;
    } catch (const std::system_error& e) {
        threw = true;
        TEST_ASSERT_EQUAL(std::to_underlying(errc::invalid_arg), e.code().value());
    }
    TEST_ASSERT_TRUE(threw);
}

TEST_CASE("queue send and receive via exception API", "[idfxx][queue]") {
    queue<int> q(5);

    q.send(42);
    q.send(99);

    TEST_ASSERT_EQUAL(42, q.receive(0ms));
    TEST_ASSERT_EQUAL(99, q.receive(0ms));
}

TEST_CASE("queue receive throws on empty with timeout", "[idfxx][queue]") {
    queue<int> q(5);

    bool threw = false;
    try {
        (void)q.receive(0ms);
    } catch (const std::system_error& e) {
        threw = true;
        TEST_ASSERT_EQUAL(std::to_underlying(errc::timeout), e.code().value());
    }
    TEST_ASSERT_TRUE(threw);
}

TEST_CASE("queue send_to_front via exception API", "[idfxx][queue]") {
    queue<int> q(5);

    q.send(1);
    q.send(2);
    q.send_to_front(0);

    TEST_ASSERT_EQUAL(0, q.receive(0ms));
    TEST_ASSERT_EQUAL(1, q.receive(0ms));
    TEST_ASSERT_EQUAL(2, q.receive(0ms));
}

TEST_CASE("queue send with timeout via exception API", "[idfxx][queue]") {
    queue<int> q(2);
    q.send(1);
    q.send(2);

    // Queue is full - send with zero timeout should throw
    bool threw = false;
    try {
        q.send(3, 0ms);
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

TEST_CASE("moved-from queue returns invalid_state", "[idfxx][queue]") {
    auto result = queue<int>::make(5);
    TEST_ASSERT_TRUE(result.has_value());

    auto moved = std::move(*result);

    // moved-from object should return invalid_state
    auto send_result = result->try_send(42, 0ms);
    TEST_ASSERT_FALSE(send_result.has_value());
    TEST_ASSERT_EQUAL(std::to_underlying(errc::invalid_state), send_result.error().value());

    auto recv_result = result->try_receive(0ms);
    TEST_ASSERT_FALSE(recv_result.has_value());
    TEST_ASSERT_EQUAL(std::to_underlying(errc::invalid_state), recv_result.error().value());

    // Simple accessors return defaults
    TEST_ASSERT_EQUAL(0, result->size());
    TEST_ASSERT_EQUAL(0, result->available());
    TEST_ASSERT_TRUE(result->empty());
    TEST_ASSERT_FALSE(result->full());
    TEST_ASSERT_NULL(result->idf_handle());

    // Moved-to object should work normally
    TEST_ASSERT_NOT_NULL(moved.idf_handle());
    TEST_ASSERT_TRUE(moved.try_send(42, 0ms).has_value());
}

TEST_CASE("queue move assignment cleans up target", "[idfxx][queue]") {
    auto r1 = queue<int>::make(5);
    TEST_ASSERT_TRUE(r1.has_value());

    auto r2 = queue<int>::make(3);
    TEST_ASSERT_TRUE(r2.has_value());

    // Move assignment should clean up target and transfer source
    *r1 = std::move(*r2);

    TEST_ASSERT_NOT_NULL(r1->idf_handle());
    TEST_ASSERT_NULL(r2->idf_handle());
}
