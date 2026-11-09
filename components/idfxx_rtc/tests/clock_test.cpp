// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Unit tests for idfxx_rtc
// Uses ESP-IDF Unity test framework with compile-time static_asserts

#include "idfxx/error"
#include "idfxx/rtc/clock"
#include "unity.h"

#include <chrono>
#include <optional>
#include <type_traits>
#include <utility>

// `clock` is also the C library's ::clock(), so the class is always named in full.
using rtc_clock_base = idfxx::rtc::clock;

namespace {

// A clock that holds its time in memory, to exercise the public wrappers.
class fake_clock final : public rtc_clock_base {
public:
    fake_clock() = default;
    fake_clock(fake_clock&&) noexcept = default;
    fake_clock& operator=(fake_clock&&) noexcept = default;

    bool valid = false;
    bool transfer_fails = false;
    std::chrono::sys_seconds time{};
    int set_calls = 0;
    int now_calls = 0;

private:
    idfxx::result<std::optional<std::chrono::sys_seconds>> do_now() override {
        ++now_calls;
        if (transfer_fails) {
            return idfxx::error(idfxx::errc::timeout);
        }
        if (!valid) {
            return std::nullopt;
        }
        return time;
    }

    idfxx::result<void> do_set_time(std::chrono::sys_seconds t) override {
        ++set_calls;
        time = t;
        valid = true;
        return {};
    }
};

} // namespace

// =============================================================================
// Compile-time tests (static_assert)
// These verify correctness at compile time - if this file compiles, they pass.
// =============================================================================

// The clock base is abstract — no concrete chip details leak in.
static_assert(std::is_abstract_v<rtc_clock_base>);
static_assert(std::has_virtual_destructor_v<rtc_clock_base>);

// The base is non-copyable so concrete drivers inherit that property.
static_assert(!std::is_copy_constructible_v<rtc_clock_base>);
static_assert(!std::is_copy_assignable_v<rtc_clock_base>);

// Moves are protected: only drivers can move, and a driver can't be sliced through a base reference.
static_assert(!std::is_move_constructible_v<rtc_clock_base>);
static_assert(!std::is_move_assignable_v<rtc_clock_base>);
static_assert(std::is_move_constructible_v<fake_clock>);
static_assert(!std::is_copy_constructible_v<fake_clock>);

// The public interface speaks UTC seconds
static_assert(std::is_same_v<
              decltype(std::declval<rtc_clock_base&>().try_now()),
              idfxx::result<std::optional<std::chrono::sys_seconds>>>);
static_assert(std::is_same_v<
              decltype(std::declval<rtc_clock_base&>().try_set_time(std::chrono::sys_seconds{})),
              idfxx::result<void>>);

// =============================================================================
// Runtime tests (Unity TEST_CASE)
// =============================================================================

TEST_CASE("rtc::clock forwards to the driver hooks", "[idfxx][rtc]") {
    using namespace std::chrono;
    fake_clock fake;
    rtc_clock_base& rtc = fake;

    // A time that is not valid reads as empty, not as an error
    auto stale = rtc.try_now();
    TEST_ASSERT_TRUE(stale.has_value());
    TEST_ASSERT_FALSE(stale->has_value());

    const sys_seconds t = sys_days{2026y / November / 9} + 12h + 30min;
    TEST_ASSERT_TRUE(rtc.try_set_time(t).has_value());
    TEST_ASSERT_EQUAL_INT32(1, fake.set_calls);

    auto now = rtc.try_now();
    TEST_ASSERT_TRUE(now.has_value());
    TEST_ASSERT_TRUE(now->has_value());
    TEST_ASSERT_TRUE(**now == t);
}

TEST_CASE("rtc::clock try_now passes the hook's result through", "[idfxx][rtc]") {
    using namespace std::chrono;
    fake_clock fake;
    rtc_clock_base& rtc = fake;

    // An empty time from the hook means the time is not valid
    auto now = rtc.try_now();
    TEST_ASSERT_TRUE(now.has_value());
    TEST_ASSERT_FALSE(now->has_value());
    TEST_ASSERT_EQUAL_INT32(1, fake.now_calls);

    // A time from the hook is returned as is
    fake.valid = true;
    fake.time = sys_days{2099y / December / 31} + 23h + 59min + 59s;
    now = rtc.try_now();
    TEST_ASSERT_TRUE(now.has_value());
    TEST_ASSERT_TRUE(now->has_value());
    TEST_ASSERT_TRUE(**now == fake.time);
    TEST_ASSERT_EQUAL_INT32(2, fake.now_calls);

    // An error is passed through
    fake.transfer_fails = true;
    now = rtc.try_now();
    TEST_ASSERT_FALSE(now.has_value());
    TEST_ASSERT_TRUE(now.error() == idfxx::errc::timeout);
    TEST_ASSERT_EQUAL_INT32(3, fake.now_calls);
}

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
TEST_CASE("rtc::clock throwing forms", "[idfxx][rtc]") {
    using namespace std::chrono;
    fake_clock fake;
    rtc_clock_base& rtc = fake;

    // A time that is not valid doesn't throw
    TEST_ASSERT_FALSE(rtc.now().has_value());

    const sys_seconds t = sys_days{2000y / January / 1};
    rtc.set_time(t);
    auto now = rtc.now();
    TEST_ASSERT_TRUE(now.has_value());
    TEST_ASSERT_TRUE(*now == t);

    fake.transfer_fails = true;
    bool threw = false;
    try {
        (void)rtc.now();
    } catch (const std::system_error& e) {
        threw = e.code() == idfxx::errc::timeout;
    }
    TEST_ASSERT_TRUE(threw);
}
#endif
