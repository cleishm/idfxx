// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Unit tests for idfxx required
// Uses ESP-IDF Unity test framework with compile-time static_asserts

#include "idfxx/required"
#include "unity.h"

#include <chrono>
#include <concepts>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <type_traits>

using idfxx::required;
using namespace std::chrono_literals;

// =============================================================================
// Test types
// =============================================================================

namespace {

// A unit type whose finer-grained counterpart converts from it implicitly,
// the way a coarse unit (e.g. ohms) converts to a finer one (e.g. milliohms).
struct coarse_units {
    int count;
};

struct fine_units {
    int count;
    constexpr fine_units(coarse_units c)
        : count(c.count * 1000) {}
};

struct only_explicit {
    int count;
    constexpr explicit only_explicit(int c)
        : count(c) {}
};

struct test_config {
    required<fine_units> r0;
    required<std::chrono::microseconds> settle;
    int retries = 3;
};

// A pin-like type with a defaulted member operator== and named constants,
// the shape of idfxx::gpio and idfxx::gpio_5.
class pin {
public:
    constexpr explicit pin(int num)
        : _num(num) {}
    [[nodiscard]] constexpr int num() const noexcept { return _num; }
    [[nodiscard]] constexpr bool operator==(const pin&) const noexcept = default;

private:
    int _num;
};

inline constexpr pin pin_5{5};
inline constexpr pin pin_6{6};

// Compares equal to int but is not convertible to or from it
struct tagged {
    int value;
    friend constexpr bool operator==(const tagged& a, int b) noexcept { return a.value == b; }
};

struct not_comparable {
    int value;
};

struct pin_config {
    required<pin> cs;
    int retries = 3;
    bool operator==(const pin_config&) const = default;
};

struct address_config {
    required<uint16_t> address;
};

struct callback_config {
    required<std::move_only_function<int(int)>> callback;
};

// The `==` and `!=` expressions type-check
template<typename A, typename B>
concept eq_comparable = requires(const A& a, const B& b) {
    { a == b } -> std::same_as<bool>;
    { a != b } -> std::same_as<bool>;
};

} // namespace

// =============================================================================
// Compile-time tests (static_assert)
// These verify correctness at compile time - if this file compiles, they pass.
// =============================================================================

// No default constructor
static_assert(!std::is_default_constructible_v<required<int>>);
static_assert(!std::is_default_constructible_v<required<std::chrono::microseconds>>);

// Converts from T and from anything implicitly convertible to T
static_assert(std::is_convertible_v<int, required<int>>);
static_assert(std::is_convertible_v<std::chrono::microseconds, required<std::chrono::microseconds>>);
static_assert(std::is_convertible_v<std::chrono::milliseconds, required<std::chrono::microseconds>>);
static_assert(std::is_convertible_v<coarse_units, required<fine_units>>);

// Two-step conversion: the source converts to T, which initializes required<T>
static_assert(required<fine_units>(coarse_units{2})->count == 2000);
static_assert(required<std::chrono::microseconds>(2ms).get() == 2000us);

// No conversion from types that convert only explicitly (chrono makes lossy
// duration conversions such as microseconds -> milliseconds explicit)
static_assert(!std::is_convertible_v<int, required<only_explicit>>);
static_assert(!std::is_constructible_v<required<only_explicit>, int>);
static_assert(!std::is_convertible_v<std::chrono::microseconds, required<std::chrono::milliseconds>>);

// Narrowing conversions are rejected, as in aggregate initialization of a plain member
static_assert(!std::is_constructible_v<required<bool>, const char*>);
static_assert(!std::is_constructible_v<required<int>, double>);
static_assert(!std::is_constructible_v<required<uint8_t>, double>);

// As for a plain member, an integer constant that fits is accepted (a
// non-constant or out-of-range narrowing value fails to compile)
static_assert(required<uint8_t>(200).get() == 200);
static_assert(required<uint16_t>(0x3C).get() == 0x3C);
static_assert(required<int8_t>(-128).get() == -128);
static_assert(required<uint32_t>(4'000'000'000LL).get() == 4'000'000'000u);
static_assert(required<bool>(1).get());
static_assert(!required<bool>(0).get());
static_assert(address_config{.address = 0x3C}.address == 0x3C);

// The range check applied to integer constants
static_assert(idfxx::detail::required_fits<uint8_t>(255));
static_assert(!idfxx::detail::required_fits<uint8_t>(256));
static_assert(!idfxx::detail::required_fits<uint8_t>(-1));
static_assert(idfxx::detail::required_fits<int8_t>(-128));
static_assert(!idfxx::detail::required_fits<int8_t>(128));
static_assert(!idfxx::detail::required_fits<int8_t>(-129));
static_assert(!idfxx::detail::required_fits<int32_t>(0xFFFF'FFFFu));
static_assert(idfxx::detail::required_fits<int32_t>(0x7FFF'FFFFu));
static_assert(!idfxx::detail::required_fits<uint64_t>(int64_t{-1}));
static_assert(idfxx::detail::required_fits<bool>(1));
static_assert(!idfxx::detail::required_fits<bool>(2));

// Non-narrowing integer conversions from runtime values use the ordinary constructor
static_assert([] {
    uint8_t narrow = 7;
    required<uint16_t> wide = narrow;
    return wide.get();
}() == 7);
static_assert(std::is_constructible_v<required<uint8_t>, uint8_t>);
static_assert(std::is_constructible_v<required<int>, short>);
static_assert(std::is_constructible_v<required<double>, float>);

// Two-step unit conversions still work with the non-narrowing check
static_assert(std::is_constructible_v<required<fine_units>, coarse_units>);
static_assert(std::is_constructible_v<required<std::chrono::microseconds>, std::chrono::milliseconds>);
static_assert(std::is_constructible_v<required<std::chrono::microseconds>, std::chrono::seconds>);

// Copyable and movable when T is
static_assert(std::is_copy_constructible_v<required<int>>);
static_assert(std::is_copy_assignable_v<required<int>>);
static_assert(std::is_move_constructible_v<required<int>>);
static_assert(std::is_move_assignable_v<required<int>>);
static_assert(std::is_nothrow_move_constructible_v<required<int>>);
static_assert(std::is_trivially_copyable_v<required<int>>);

// Implicit conversion is to const T& only
static_assert(std::is_convertible_v<required<int>, const int&>);
static_assert(std::is_convertible_v<required<int>&, const int&>);
static_assert(!std::is_convertible_v<required<int>&, int&>);
static_assert(!std::is_convertible_v<required<int>, int&&>);

// Accessors follow the value category of the required<T>
static_assert(std::is_same_v<decltype(std::declval<const required<int>&>().get()), const int&>);
static_assert(std::is_same_v<decltype(std::declval<required<int>&>().get()), int&>);
static_assert(std::is_same_v<decltype(std::declval<required<int>&&>().get()), int&&>);
static_assert(std::is_same_v<decltype(*std::declval<const required<int>&>()), const int&>);
static_assert(std::is_same_v<decltype(*std::declval<required<int>&>()), int&>);
static_assert(std::is_same_v<decltype(*std::declval<required<int>&&>()), int&&>);
static_assert(std::is_same_v<decltype(std::declval<const required<pin>&>().operator->()), const pin*>);
static_assert(std::is_same_v<decltype(std::declval<required<pin>&>().operator->()), pin*>);

// Move-only values
static_assert(!std::is_copy_constructible_v<required<std::unique_ptr<int>>>);
static_assert(std::is_move_constructible_v<required<std::unique_ptr<int>>>);
static_assert(std::is_move_assignable_v<required<std::unique_ptr<int>>>);
static_assert(std::is_convertible_v<std::unique_ptr<int>, required<std::unique_ptr<int>>>);
static_assert(std::is_aggregate_v<callback_config>);
static_assert(!std::is_default_constructible_v<callback_config>);
static_assert(std::is_move_constructible_v<callback_config>);
static_assert(!std::is_copy_constructible_v<callback_config>);

// Mutation through the accessors
static_assert([] {
    required<int> r = 1;
    r.get() = 2;
    *r += 3;
    return r.get();
}() == 5);

// Equality between required<T> values
static_assert(std::equality_comparable<required<int>>);
static_assert(std::equality_comparable<required<pin>>);
static_assert(!eq_comparable<required<not_comparable>, required<not_comparable>>);
static_assert(required<int>(1) == required<int>(1));
static_assert(required<int>(1) != required<int>(2));

// Equality with values T compares equal to, in both argument orders
static_assert(eq_comparable<required<pin>, pin>);
static_assert(eq_comparable<pin, required<pin>>);
static_assert(required<pin>(pin_5) == pin_5);
static_assert(pin_5 == required<pin>(pin_5));
static_assert(required<pin>(pin_5) != pin_6);
static_assert(pin_6 != required<pin>(pin_5));
static_assert(required<int>(3) == 3);
static_assert(3 == required<int>(3));
static_assert(required<int>(3) != 4L);
static_assert(required<std::chrono::microseconds>(2ms) == 2ms);
static_assert(2000us == required<std::chrono::microseconds>(2ms));

// Equality uses T's operator==, not a conversion to T
static_assert(required<tagged>(tagged{7}) == 7);
static_assert(7 == required<tagged>(tagged{7}));
static_assert(!eq_comparable<required<not_comparable>, not_comparable>);
static_assert(!eq_comparable<required<pin>, int>);

// The field of a config struct compares directly against a constant
static_assert(pin_config{.cs = pin_5}.cs == pin_5);
static_assert(pin_5 == pin_config{.cs = pin_5}.cs);

// A config with a required member supports a defaulted operator==
static_assert(pin_config{.cs = pin_5} == pin_config{.cs = pin_5});
static_assert(pin_config{.cs = pin_5} != pin_config{.cs = pin_6});
static_assert(pin_config{.cs = pin_5} != pin_config{.cs = pin_5, .retries = 4});

// An aggregate with a required member is not default-constructible
static_assert(std::is_aggregate_v<test_config>);
static_assert(!std::is_default_constructible_v<test_config>);

// Constant evaluation through a designated initializer
static_assert(test_config{.r0 = coarse_units{1}, .settle = 5ms}.r0->count == 1000);
static_assert(test_config{.r0 = coarse_units{1}, .settle = 5ms}.retries == 3);

// =============================================================================
// Runtime tests (Unity TEST_CASE)
// =============================================================================

TEST_CASE("required initializes from a designated initializer", "[idfxx][required]") {
    test_config cfg{.r0 = coarse_units{4}, .settle = 250us, .retries = 7};
    TEST_ASSERT_EQUAL_INT(4000, cfg.r0->count);
    TEST_ASSERT_EQUAL_INT(4000, cfg.r0.get().count);
    TEST_ASSERT_EQUAL_INT(4000, (*cfg.r0).count);
    TEST_ASSERT_EQUAL_INT32(250, cfg.settle->count());
    TEST_ASSERT_EQUAL_INT(7, cfg.retries);
}

TEST_CASE("required converts implicitly to const T&", "[idfxx][required]") {
    required<std::string> name = std::string("sensor");
    const std::string& ref = name;
    TEST_ASSERT_EQUAL_STRING("sensor", ref.c_str());
    TEST_ASSERT_EQUAL_size_t(6, name->size());
}

TEST_CASE("required copies and moves its value", "[idfxx][required]") {
    required<std::string> a = std::string("abc");
    required<std::string> b = a;
    TEST_ASSERT_EQUAL_STRING("abc", b->c_str());

    required<std::string> c = std::move(b);
    TEST_ASSERT_EQUAL_STRING("abc", c->c_str());

    required<std::string> d = std::string("xyz");
    d = a;
    TEST_ASSERT_EQUAL_STRING("abc", d->c_str());

    // A non-const lvalue copies rather than converting through the stored value
    required<std::string>& lvalue = a;
    required<std::string> e(lvalue);
    TEST_ASSERT_EQUAL_STRING("abc", e->c_str());
}

TEST_CASE("required allows mutation through its accessors", "[idfxx][required]") {
    required<std::string> s = std::string("abc");
    s.get() += "d";
    *s += "e";
    s->push_back('f');
    TEST_ASSERT_EQUAL_STRING("abcdef", s->c_str());
}

TEST_CASE("required holds a move-only callback", "[idfxx][required]") {
    int calls = 0;
    callback_config cfg{.callback = [&calls, offset = std::make_unique<int>(10)](int x) {
        ++calls;
        return x + *offset;
    }};
    TEST_ASSERT_TRUE(cfg.callback != nullptr);

    // Called through a mutable reference
    TEST_ASSERT_EQUAL_INT(11, cfg.callback.get()(1));
    TEST_ASSERT_EQUAL_INT(12, (*cfg.callback)(2));

    // Moved out of an rvalue
    std::move_only_function<int(int)> taken = std::move(cfg.callback).get();
    TEST_ASSERT_EQUAL_INT(13, taken(3));
    TEST_ASSERT_EQUAL_INT(3, calls);

    // The whole config moves too
    callback_config other{.callback = [](int x) { return -x; }};
    callback_config moved = std::move(other);
    TEST_ASSERT_EQUAL_INT(-4, (*std::move(moved.callback))(4));
}

TEST_CASE("required compares with values and other required", "[idfxx][required]") {
    pin_config a{.cs = pin_5};
    pin_config b{.cs = pin_5};
    TEST_ASSERT_TRUE(a.cs == pin_5);
    TEST_ASSERT_TRUE(pin_5 == a.cs);
    TEST_ASSERT_TRUE(a.cs != pin_6);
    TEST_ASSERT_TRUE(a.cs == b.cs);
    TEST_ASSERT_TRUE(a == b);

    b.cs = pin_6;
    TEST_ASSERT_TRUE(a.cs != b.cs);
    TEST_ASSERT_TRUE(a != b);
    TEST_ASSERT_EQUAL_INT(6, b.cs->num());
}
