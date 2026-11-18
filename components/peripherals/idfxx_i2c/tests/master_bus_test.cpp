// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Unit tests for idfxx::i2c::master_bus
// Uses ESP-IDF Unity test framework with compile-time static_asserts

#include "idfxx/i2c/master"

#include <chrono>
#include <concepts>
#include <mutex>
#include <system_error>
#include <thread>
#include <type_traits>
#include <unity.h>
#include <utility>

using namespace idfxx::i2c;
using namespace quanta::frequency_literals;

// SDA/SCL pins used by the bus-creation tests. Any two free pins work; the
// ESP32-C2 only has GPIO 0-20, so it uses a lower SDA pin than the default 21.
#ifdef CONFIG_IDF_TARGET_ESP32C2
inline constexpr auto TEST_SDA = idfxx::gpio_4;
#else
inline constexpr auto TEST_SDA = idfxx::gpio_21;
#endif
inline constexpr auto TEST_SCL = idfxx::gpio_9;

// =============================================================================
// Compile-time tests (static_assert)
// These verify correctness at compile time - if this file compiles, they pass.
// =============================================================================

// master_bus is not default constructible
static_assert(!std::is_default_constructible_v<master_bus>);

// master_bus is non-copyable
static_assert(!std::is_copy_constructible_v<master_bus>);
static_assert(!std::is_copy_assignable_v<master_bus>);

// master_bus is move-only
static_assert(std::is_move_constructible_v<master_bus>);
static_assert(std::is_move_assignable_v<master_bus>);

// master_bus::config is an aggregate; sda, scl and frequency have no defaults
static_assert(std::is_aggregate_v<master_bus::config>);
static_assert(!std::is_default_constructible_v<master_bus::config>);

// Verify port enum values
static_assert(static_cast<int>(port::i2c0) == 0);
static_assert(std::to_underlying(port::i2c0) == 0);
#if SOC_HP_I2C_NUM >= 2
static_assert(static_cast<int>(port::i2c1) == 1);
static_assert(std::to_underlying(port::i2c1) == 1);
#endif
#if SOC_LP_I2C_NUM >= 1
static_assert(static_cast<int>(port::lp_i2c0) == 2);
static_assert(std::to_underlying(port::lp_i2c0) == 2);
#endif

// DEFAULT_TIMEOUT is accessible
static_assert(DEFAULT_TIMEOUT == std::chrono::milliseconds(50));

// master_bus satisfies TimedLockable
template<typename B>
concept timed_lockable = requires(const B& b) {
    b.lock();
    { b.try_lock() } -> std::same_as<bool>;
    { b.try_lock_for(std::chrono::milliseconds(10)) } -> std::same_as<bool>;
    { b.try_lock_until(std::chrono::steady_clock::now()) } -> std::same_as<bool>;
    b.unlock();
};
static_assert(timed_lockable<master_bus>);

// Transfers are always synchronous: there is no transfer queue to configure or wait for
template<typename C>
concept has_trans_queue_depth = requires(C& c) { c.trans_queue_depth; };
static_assert(!has_trans_queue_depth<master_bus::config>);

template<typename B>
concept has_try_wait_all_done = requires(B& b) { b.try_wait_all_done(std::chrono::milliseconds(10)); };
template<typename B>
concept has_wait_all_done = requires(B& b) { b.wait_all_done(std::chrono::milliseconds(10)); };
static_assert(!has_try_wait_all_done<master_bus>);
static_assert(!has_wait_all_done<master_bus>);

// =============================================================================
// Runtime tests (Unity TEST_CASE)
// Unity asserts longjmp out of the test, so every assertion runs after the bus
// has gone out of scope; otherwise a failure would leave i2c0 allocated.
// =============================================================================

namespace {

// Returns the error of a result, or an empty error_code on success. Taking the result by value lets a bus that was
// unexpectedly created go out of scope before anything is asserted.
template<typename R>
std::error_code error_of(R result) {
    return result.has_value() ? std::error_code{} : result.error();
}

} // namespace

TEST_CASE("master_bus::make with null GPIO returns error", "[idfxx][i2c][master_bus]") {
    auto ec = error_of(master_bus::make(port::i2c0, idfxx::gpio::nc(), idfxx::gpio::nc(), 100_kHz));
    TEST_ASSERT_TRUE_MESSAGE(static_cast<bool>(ec), "Bus with unconnected pins was created");
    TEST_ASSERT_EQUAL(std::to_underlying(idfxx::errc::invalid_arg), ec.value());
}

TEST_CASE("master_bus::make with config and null GPIO returns error", "[idfxx][i2c][master_bus]") {
    auto ec = error_of(master_bus::make(port::i2c0, {
        .sda = idfxx::gpio::nc(),
        .scl = idfxx::gpio::nc(),
        .frequency = 100_kHz,
    }));
    TEST_ASSERT_TRUE_MESSAGE(static_cast<bool>(ec), "Bus with unconnected pins was created");
    TEST_ASSERT_EQUAL(std::to_underlying(idfxx::errc::invalid_arg), ec.value());
}

TEST_CASE("master_bus::make with config and zero frequency returns error", "[idfxx][i2c][master_bus]") {
    auto ec = error_of(master_bus::make(port::i2c0, {
        .sda = TEST_SDA,
        .scl = TEST_SCL,
        .frequency = 0_Hz,
    }));
    TEST_ASSERT_TRUE_MESSAGE(static_cast<bool>(ec), "Bus with zero frequency was created");
    TEST_ASSERT_EQUAL(std::to_underlying(idfxx::errc::invalid_arg), ec.value());
}

TEST_CASE("master_bus::make with config succeeds", "[idfxx][i2c][master_bus]") {
    bool created = false;
    quanta::frequency::hertz frequency{0};
    {
        auto result = master_bus::make(port::i2c0, {
            .sda = TEST_SDA,
            .scl = TEST_SCL,
            .frequency = 400_kHz,
        });
        if (result.has_value()) {
            created = true;
            frequency = result->frequency();
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(created, "Failed to create I2C bus with config");
    TEST_ASSERT_EQUAL(400000, frequency.count());
}

TEST_CASE("master_bus::make with config custom settings succeeds", "[idfxx][i2c][master_bus]") {
    bool created = false;
    quanta::frequency::hertz frequency{0};
    {
        auto result = master_bus::make(port::i2c0, {
            .sda = TEST_SDA,
            .scl = TEST_SCL,
            .frequency = 100_kHz,
            .glitch_ignore_cnt = 5,
            .enable_internal_pullup = false,
        });
        if (result.has_value()) {
            created = true;
            frequency = result->frequency();
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(created, "Failed to create I2C bus with custom config");
    TEST_ASSERT_EQUAL(100000, frequency.count());
}

TEST_CASE("master_bus::make on a port already in use fails with invalid_state", "[idfxx][i2c][master_bus]") {
    // The driver's error passes through rather than being reported as invalid_arg
    bool created = false;
    std::error_code ec;
    {
        auto first = master_bus::make(port::i2c0, TEST_SDA, TEST_SCL, 100_kHz);
        if (first.has_value()) {
            created = true;
            auto second = master_bus::make(port::i2c0, TEST_SDA, TEST_SCL, 100_kHz);
            if (!second.has_value()) {
                ec = second.error();
            }
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(created, "Failed to create I2C bus");
    TEST_ASSERT_TRUE_MESSAGE(static_cast<bool>(ec), "Second bus on the same port was created");
    TEST_ASSERT_TRUE_MESSAGE(ec == idfxx::errc::invalid_state, ec.message().c_str());
}

TEST_CASE("master_bus scan_devices returns vector", "[idfxx][i2c][master_bus]") {
    bool created = false;
    size_t count = 0;
    {
        auto bus_result = master_bus::make(port::i2c0, TEST_SDA, TEST_SCL, 100_kHz);
        if (bus_result.has_value()) {
            created = true;
            // Scan for devices (may return empty if no devices connected)
            count = bus_result->scan_devices().size();
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(created, "Failed to create I2C bus - check GPIO pins are valid for your hardware");
    TEST_ASSERT_TRUE(count <= 120); // Valid I2C addresses: 0x08-0x77
}

TEST_CASE("master_bus try_probe with invalid address returns error", "[idfxx][i2c][master_bus]") {
    bool created = false;
    bool probed = false;
    {
        auto bus_result = master_bus::make(port::i2c0, TEST_SDA, TEST_SCL, 100_kHz);
        if (bus_result.has_value()) {
            created = true;
            // Address 0x00 is reserved, so probing it should fail
            probed = bus_result->try_probe(0x00).has_value();
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(created, "Failed to create I2C bus");
    TEST_ASSERT_FALSE(probed);
}

TEST_CASE("master_bus try_probe of an absent device fails with not_found", "[idfxx][i2c][master_bus][hw]") {
    // Needs a real bus with pull-ups: QEMU's I2C model times out the probe on the bus instead of NACKing it
    bool created = false;
    std::error_code ec;
    {
        auto bus_result = master_bus::make(port::i2c0, TEST_SDA, TEST_SCL, 100_kHz);
        if (bus_result.has_value()) {
            created = true;
            auto probe_result = bus_result->try_probe(0x50);
            if (!probe_result.has_value()) {
                ec = probe_result.error();
            }
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(created, "Failed to create I2C bus");
    TEST_ASSERT_TRUE_MESSAGE(static_cast<bool>(ec), "Probe of an absent device succeeded");
    TEST_ASSERT_TRUE_MESSAGE(ec == idfxx::errc::not_found, ec.message().c_str());
}

TEST_CASE("master_bus try_probe fails with timeout while another task holds the bus", "[idfxx][i2c][master_bus]") {
    bool created = false;
    std::error_code ec;
    {
        auto bus_result = master_bus::make(port::i2c0, TEST_SDA, TEST_SCL, 100_kHz);
        if (bus_result.has_value()) {
            created = true;
            auto& bus = *bus_result;
            std::lock_guard<master_bus> hold(bus);
            std::thread contender([&]() {
                auto probe_result = bus.try_probe(0x50, std::chrono::milliseconds(10));
                if (!probe_result.has_value()) {
                    ec = probe_result.error();
                }
            });
            contender.join();
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(created, "Failed to create I2C bus");
    TEST_ASSERT_TRUE_MESSAGE(ec == idfxx::errc::timeout, ec.message().c_str());
}

TEST_CASE("master_bus is lockable", "[idfxx][i2c][master_bus]") {
    bool created = false;
    bool owned = false;
    bool timed_owned = false;
    bool deadline_owned = false;
    {
        auto bus_result = master_bus::make(port::i2c0, TEST_SDA, TEST_SCL, 100_kHz);
        if (bus_result.has_value()) {
            created = true;
            auto& bus = *bus_result;

            // Verify lock/unlock methods exist and work
            bus.lock();
            bus.unlock();

            // Verify try_lock exists
            if (bus.try_lock()) {
                bus.unlock();
            }

            // Verify works with std::lock_guard
            {
                std::lock_guard<master_bus> lock(bus);
            }

            // Verify works with std::unique_lock
            {
                std::unique_lock<master_bus> lock(bus);
                owned = lock.owns_lock();
            }

            // Verify the timed forms
            {
                std::unique_lock<master_bus> lock(bus, std::chrono::milliseconds(10));
                timed_owned = lock.owns_lock();
            }
            {
                std::unique_lock<master_bus> lock(bus, std::chrono::steady_clock::now() + std::chrono::milliseconds(10));
                deadline_owned = lock.owns_lock();
            }
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(created, "Failed to create I2C bus");
    TEST_ASSERT_TRUE(owned);
    TEST_ASSERT_TRUE(timed_owned);
    TEST_ASSERT_TRUE(deadline_owned);
}

TEST_CASE("master_bus try_lock_for times out while another task holds the bus", "[idfxx][i2c][master_bus]") {
    bool created = false;
    bool acquired = true;
    {
        auto bus_result = master_bus::make(port::i2c0, TEST_SDA, TEST_SCL, 100_kHz);
        if (bus_result.has_value()) {
            created = true;
            auto& bus = *bus_result;
            std::lock_guard<master_bus> hold(bus);
            std::thread contender([&]() {
                acquired = bus.try_lock_for(std::chrono::milliseconds(10));
                if (acquired) {
                    bus.unlock();
                }
            });
            contender.join();
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(created, "Failed to create I2C bus");
    TEST_ASSERT_FALSE(acquired);
}

TEST_CASE("master_bus frequency accessor works", "[idfxx][i2c][master_bus]") {
    bool created = false;
    quanta::frequency::hertz frequency{0};
    {
        auto bus_result = master_bus::make(port::i2c0, TEST_SDA, TEST_SCL, 400_kHz);
        if (bus_result.has_value()) {
            created = true;
            frequency = bus_result->frequency();
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(created, "Failed to create I2C bus");
    TEST_ASSERT_EQUAL(400000, frequency.count());
}

TEST_CASE("master_bus port accessor works", "[idfxx][i2c][master_bus]") {
    bool created = false;
    port bus_port = static_cast<port>(-1);
    {
        auto bus_result = master_bus::make(port::i2c0, TEST_SDA, TEST_SCL, 100_kHz);
        if (bus_result.has_value()) {
            created = true;
            bus_port = bus_result->port();
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(created, "Failed to create I2C bus");
    TEST_ASSERT_EQUAL(port::i2c0, bus_port);
    TEST_ASSERT_EQUAL(std::to_underlying(port::i2c0), std::to_underlying(bus_port));
}

// =============================================================================
// to_string tests
// =============================================================================

TEST_CASE("to_string(port) outputs correct names", "[idfxx][i2c][master_bus]") {
    auto s = idfxx::to_string(port::i2c0);
    TEST_ASSERT_EQUAL_STRING("I2C0", s.c_str());
#if SOC_HP_I2C_NUM >= 2
    s = idfxx::to_string(port::i2c1);
    TEST_ASSERT_EQUAL_STRING("I2C1", s.c_str());
#endif
#if SOC_LP_I2C_NUM >= 1
    s = idfxx::to_string(port::lp_i2c0);
    TEST_ASSERT_EQUAL_STRING("LP_I2C0", s.c_str());
#endif
}

TEST_CASE("to_string(port) handles unknown values", "[idfxx][i2c][master_bus]") {
    auto unknown = static_cast<port>(99);
    auto s = idfxx::to_string(unknown);
    TEST_ASSERT_EQUAL_STRING("unknown(99)", s.c_str());
}

// =============================================================================
// Formatter tests
// =============================================================================

#ifdef CONFIG_IDFXX_STD_FORMAT
static_assert(std::formattable<port, char>);

TEST_CASE("i2c::port formatter outputs correct names", "[idfxx][i2c][master_bus]") {
    auto s = std::format("{}", port::i2c0);
    TEST_ASSERT_EQUAL_STRING("I2C0", s.c_str());
#if SOC_HP_I2C_NUM >= 2
    s = std::format("{}", port::i2c1);
    TEST_ASSERT_EQUAL_STRING("I2C1", s.c_str());
#endif
#if SOC_LP_I2C_NUM >= 1
    s = std::format("{}", port::lp_i2c0);
    TEST_ASSERT_EQUAL_STRING("LP_I2C0", s.c_str());
#endif
}

TEST_CASE("i2c::port formatter handles unknown values", "[idfxx][i2c][master_bus]") {
    auto unknown = static_cast<port>(99);
    auto s = std::format("{}", unknown);
    TEST_ASSERT_EQUAL_STRING("unknown(99)", s.c_str());
}
#endif // CONFIG_IDFXX_STD_FORMAT
