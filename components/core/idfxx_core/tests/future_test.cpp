// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Unit tests for idfxx::future<T>
// Uses ESP-IDF Unity test framework with compile-time static_asserts

#include "idfxx/future"
#include "unity.h"

#include <atomic>
#include <chrono>
#include <memory>
#include <optional>
#include <type_traits>

using namespace idfxx;
using namespace std::chrono_literals;

// =============================================================================
// Compile-time tests (static_assert)
// =============================================================================

static_assert(std::is_default_constructible_v<future<void>>);
static_assert(std::is_copy_constructible_v<future<void>>);
static_assert(std::is_copy_assignable_v<future<void>>);
static_assert(std::is_move_constructible_v<future<void>>);
static_assert(std::is_move_assignable_v<future<void>>);
static_assert(std::is_nothrow_move_constructible_v<future<void>>);
static_assert(std::is_nothrow_move_assignable_v<future<void>>);

static_assert(std::is_default_constructible_v<future<int>>);
static_assert(std::is_copy_constructible_v<future<int>>);
static_assert(std::is_copy_assignable_v<future<int>>);
static_assert(std::is_move_constructible_v<future<int>>);
static_assert(std::is_move_assignable_v<future<int>>);
static_assert(std::is_nothrow_move_constructible_v<future<int>>);
static_assert(std::is_nothrow_move_assignable_v<future<int>>);

// =============================================================================
// Runtime tests (Unity TEST_CASE)
// =============================================================================

TEST_CASE("future default is invalid and done", "[idfxx][future]") {
    future<void> f;
    TEST_ASSERT_FALSE(f.valid());
    TEST_ASSERT_TRUE(f.done());
}

TEST_CASE("future default try_wait returns ok", "[idfxx][future]") {
    future<void> f;
    auto r = f.try_wait();
    TEST_ASSERT_TRUE(r.has_value());
}

TEST_CASE("future default try_wait_for returns ok", "[idfxx][future]") {
    future<void> f;
    auto r = f.try_wait_for(0ms);
    TEST_ASSERT_TRUE(r.has_value());
}

TEST_CASE("future<int> default try_wait returns value-initialized value", "[idfxx][future]") {
    future<int> f;
    TEST_ASSERT_FALSE(f.valid());
    TEST_ASSERT_TRUE(f.done());
    auto r = f.try_wait();
    TEST_ASSERT_TRUE(r.has_value());
    TEST_ASSERT_EQUAL(0, *r);
}

TEST_CASE("future lambda-only completes on first try_wait", "[idfxx][future]") {
    auto counter = std::make_shared<int>(0);
    future<void> f{[counter](std::optional<std::chrono::milliseconds>) -> result<void> {
        ++(*counter);
        return {};
    }};

    TEST_ASSERT_TRUE(f.valid());

    auto r = f.try_wait();
    TEST_ASSERT_TRUE(r.has_value());
    TEST_ASSERT_EQUAL(1, *counter);
}

TEST_CASE("future lambda-only done polls via waiter with 0ms", "[idfxx][future]") {
    auto observed = std::make_shared<std::optional<std::chrono::milliseconds>>();
    future<void> f{[observed](std::optional<std::chrono::milliseconds> t) -> result<void> {
        *observed = t;
        return {};
    }};

    TEST_ASSERT_TRUE(f.done());
    TEST_ASSERT_TRUE(observed->has_value());
    TEST_ASSERT_EQUAL(0, observed->value().count());
}

TEST_CASE("future lambda+done-check done does not consume waiter", "[idfxx][future]") {
    auto waiter_calls = std::make_shared<int>(0);
    auto done_calls = std::make_shared<int>(0);
    auto is_done = std::make_shared<std::atomic<bool>>(false);

    future<void> f{
        [waiter_calls, is_done](std::optional<std::chrono::milliseconds>) -> result<void> {
            ++(*waiter_calls);
            if (!is_done->load()) {
                return error(errc::timeout);
            }
            return {};
        },
        [done_calls, is_done]() noexcept -> bool {
            ++(*done_calls);
            return is_done->load();
        }
    };

    // Multiple done() calls should route to done_check, not waiter.
    TEST_ASSERT_FALSE(f.done());
    TEST_ASSERT_FALSE(f.done());
    TEST_ASSERT_FALSE(f.done());
    TEST_ASSERT_EQUAL(0, *waiter_calls);
    TEST_ASSERT_EQUAL(3, *done_calls);

    is_done->store(true);
    TEST_ASSERT_TRUE(f.done());
    TEST_ASSERT_EQUAL(0, *waiter_calls);
    TEST_ASSERT_EQUAL(4, *done_calls);
}

TEST_CASE("future try_wait_for forwards timeout to waiter", "[idfxx][future]") {
    auto observed = std::make_shared<std::optional<std::chrono::milliseconds>>();
    future<void> f{[observed](std::optional<std::chrono::milliseconds> t) -> result<void> {
        *observed = t;
        return error(errc::timeout);
    }};

    auto r = f.try_wait_for(5ms);
    TEST_ASSERT_FALSE(r.has_value());
    TEST_ASSERT_EQUAL(std::to_underlying(errc::timeout), r.error().value());
    TEST_ASSERT_TRUE(observed->has_value());
    TEST_ASSERT_EQUAL(5, observed->value().count());
}

TEST_CASE("future try_wait passes nullopt for indefinite wait", "[idfxx][future]") {
    auto observed = std::make_shared<std::optional<std::chrono::milliseconds>>(std::chrono::milliseconds{42});
    future<void> f{[observed](std::optional<std::chrono::milliseconds> t) -> result<void> {
        *observed = t;
        return {};
    }};

    auto r = f.try_wait();
    TEST_ASSERT_TRUE(r.has_value());
    TEST_ASSERT_FALSE(observed->has_value());
}

TEST_CASE("future try_wait_for rounds up sub-millisecond durations", "[idfxx][future]") {
    auto observed = std::make_shared<std::optional<std::chrono::milliseconds>>();
    future<void> f{[observed](std::optional<std::chrono::milliseconds> t) -> result<void> {
        *observed = t;
        return {};
    }};

    (void)f.try_wait_for(std::chrono::microseconds(1001));
    TEST_ASSERT_TRUE(observed->has_value());
    TEST_ASSERT_EQUAL(2, observed->value().count());
}

TEST_CASE("future copies share completion state", "[idfxx][future]") {
    auto completed = std::make_shared<std::atomic<bool>>(false);

    future<void> f1{
        [completed](std::optional<std::chrono::milliseconds>) -> result<void> {
            completed->store(true);
            return {};
        },
        [completed]() noexcept -> bool { return completed->load(); }
    };
    auto f2 = f1;

    TEST_ASSERT_TRUE(f1.valid());
    TEST_ASSERT_TRUE(f2.valid());
    TEST_ASSERT_FALSE(f1.done());
    TEST_ASSERT_FALSE(f2.done());

    auto r = f1.try_wait();
    TEST_ASSERT_TRUE(r.has_value());
    TEST_ASSERT_TRUE(f1.done());
    TEST_ASSERT_TRUE(f2.done());
}

TEST_CASE("future last copy releases captured resource", "[idfxx][future]") {
    auto alive = std::make_shared<int>(0);

    struct guard {
        std::shared_ptr<int> counter;
        explicit guard(std::shared_ptr<int> c)
            : counter(std::move(c)) {
            ++(*counter);
        }
        guard(guard&& o) noexcept
            : counter(std::move(o.counter)) {}
        guard& operator=(guard&&) = delete;
        guard(const guard&) = delete;
        guard& operator=(const guard&) = delete;
        ~guard() {
            if (counter) {
                --(*counter);
            }
        }
    };

    {
        future<void> f1{[g = guard{alive}](std::optional<std::chrono::milliseconds>) mutable -> result<void> {
            return {};
        }};
        TEST_ASSERT_EQUAL(1, *alive);

        auto f2 = f1;
        TEST_ASSERT_EQUAL(1, *alive); // only one guard instance despite the copy
    } // both copies destroyed — guard fires

    TEST_ASSERT_EQUAL(0, *alive);
}

TEST_CASE("future<int> returns computed value", "[idfxx][future]") {
    future<int> f{[](std::optional<std::chrono::milliseconds>) -> result<int> { return 42; }};

    TEST_ASSERT_TRUE(f.valid());
    auto r = f.try_wait();
    TEST_ASSERT_TRUE(r.has_value());
    TEST_ASSERT_EQUAL(42, *r);
}

TEST_CASE("future<int> propagates error from waiter", "[idfxx][future]") {
    future<int> f{[](std::optional<std::chrono::milliseconds>) -> result<int> { return error(errc::timeout); }};

    auto r = f.try_wait_for(1ms);
    TEST_ASSERT_FALSE(r.has_value());
    TEST_ASSERT_EQUAL(std::to_underlying(errc::timeout), r.error().value());
}

TEST_CASE("future move leaves source invalid", "[idfxx][future]") {
    future<void> f1{[](std::optional<std::chrono::milliseconds>) -> result<void> { return {}; }};
    TEST_ASSERT_TRUE(f1.valid());

    future<void> f2 = std::move(f1);
    TEST_ASSERT_TRUE(f2.valid());
    TEST_ASSERT_FALSE(f1.valid()); // NOLINT: intentional use-after-move
    TEST_ASSERT_TRUE(f1.done());   // invalid future is always done
}

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS

TEST_CASE("future wait returns value on success", "[idfxx][future]") {
    future<int> f{[](std::optional<std::chrono::milliseconds>) -> result<int> { return 7; }};
    TEST_ASSERT_EQUAL(7, f.wait());
}

TEST_CASE("future wait_for throws on timeout", "[idfxx][future]") {
    future<int> f{[](std::optional<std::chrono::milliseconds>) -> result<int> { return error(errc::timeout); }};
    bool threw = false;
    try {
        (void)f.wait_for(1ms);
    } catch (const std::system_error& e) {
        threw = true;
        TEST_ASSERT_EQUAL(std::to_underlying(errc::timeout), e.code().value());
    }
    TEST_ASSERT_TRUE(threw);
}

TEST_CASE("future<void> wait returns without value", "[idfxx][future]") {
    future<void> f{[](std::optional<std::chrono::milliseconds>) -> result<void> { return {}; }};
    f.wait();
    // Reached here — no throw.
}

#endif // CONFIG_COMPILER_CXX_EXCEPTIONS
