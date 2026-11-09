// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Unit tests for idfxx::i2c::master_device
// Uses ESP-IDF Unity test framework with compile-time static_asserts

#include "idfxx/i2c/master"

#include <chrono>
#include <concepts>
#include <mutex>
#include <span>
#include <system_error>
#include <thread>
#include <type_traits>
#include <unity.h>
#include <vector>
#include <utility>

using namespace idfxx::i2c;

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

// master_device is not default constructible
static_assert(!std::is_default_constructible_v<master_device>);

// master_device is non-copyable
static_assert(!std::is_copy_constructible_v<master_device>);
static_assert(!std::is_copy_assignable_v<master_device>);

// master_device is move-only
static_assert(std::is_move_constructible_v<master_device>);
static_assert(std::is_move_assignable_v<master_device>);

// master_device::config is an aggregate with sensible defaults
static_assert(std::is_aggregate_v<master_device::config>);
static_assert(std::is_default_constructible_v<master_device::config>);

// register_address_size is a byte-sized scoped enum with distinct values
static_assert(std::is_enum_v<register_address_size>);
static_assert(!std::is_convertible_v<register_address_size, int>);
static_assert(std::is_same_v<std::underlying_type_t<register_address_size>, uint8_t>);
static_assert(register_address_size::one_byte != register_address_size::two_bytes);

// The register address size is readable from the device
template<typename D>
concept has_register_address = requires(const D& d) {
    { d.register_address() } -> std::same_as<register_address_size>;
};
static_assert(has_register_address<master_device>);

// Transfers are always synchronous, so there are no transfer-done callbacks
template<typename D>
concept has_try_register_event_callbacks = requires(D& d) { d.try_register_event_callbacks([]() { return false; }); };
template<typename D>
concept has_register_event_callbacks = requires(D& d) { d.register_event_callbacks([]() { return false; }); };
static_assert(!has_try_register_event_callbacks<master_device>);
static_assert(!has_register_event_callbacks<master_device>);

// A 2.x-style (high, low) register read is rejected instead of binding the literal 0 to the buffer pointer
template<typename D>
concept try_read_register_high_low = requires(D& d) { d.try_read_register(uint16_t{0x10}, 0, size_t{3}); };
template<typename D>
concept try_read_register_high_low_timeout = requires(D& d) {
    d.try_read_register(uint16_t{0x10}, 0, size_t{3}, std::chrono::milliseconds(100));
};
static_assert(!try_read_register_high_low<master_device>);
static_assert(!try_read_register_high_low_timeout<master_device>);

// The single-register read forms still resolve
template<typename D>
concept try_read_register_forms = requires(D& d, uint8_t* p, std::span<uint8_t> s) {
    { d.try_read_register(uint16_t{0x10}, size_t{3}) } -> std::same_as<idfxx::result<std::vector<uint8_t>>>;
    { d.try_read_register(uint16_t{0x10}, size_t{3}, std::chrono::milliseconds(100)) } -> std::same_as<idfxx::result<std::vector<uint8_t>>>;
    { d.try_read_register(0x10, 3) } -> std::same_as<idfxx::result<std::vector<uint8_t>>>;
    { d.try_read_register(uint16_t{0x10}, p, size_t{3}) } -> std::same_as<idfxx::result<void>>;
    { d.try_read_register(uint16_t{0x10}, p, size_t{3}, std::chrono::milliseconds(100)) } -> std::same_as<idfxx::result<void>>;
    { d.try_read_register(uint16_t{0x10}, s) } -> std::same_as<idfxx::result<void>>;
    { d.try_read_register(uint16_t{0x10}, s, std::chrono::milliseconds(100)) } -> std::same_as<idfxx::result<void>>;
};
static_assert(try_read_register_forms<master_device>);

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
template<typename D>
concept read_register_high_low = requires(D& d) { d.read_register(uint16_t{0x10}, 0, size_t{3}); };
template<typename D>
concept read_register_high_low_timeout = requires(D& d) {
    d.read_register(uint16_t{0x10}, 0, size_t{3}, std::chrono::milliseconds(100));
};
static_assert(!read_register_high_low<master_device>);
static_assert(!read_register_high_low_timeout<master_device>);

template<typename D>
concept read_register_forms = requires(D& d, uint8_t* p, std::span<uint8_t> s) {
    { d.read_register(uint16_t{0x10}, size_t{3}) } -> std::same_as<std::vector<uint8_t>>;
    { d.read_register(uint16_t{0x10}, size_t{3}, std::chrono::milliseconds(100)) } -> std::same_as<std::vector<uint8_t>>;
    { d.read_register(0x10, 3) } -> std::same_as<std::vector<uint8_t>>;
    { d.read_register(uint16_t{0x10}, p, size_t{3}) } -> std::same_as<void>;
    { d.read_register(uint16_t{0x10}, p, size_t{3}, std::chrono::milliseconds(100)) } -> std::same_as<void>;
    { d.read_register(uint16_t{0x10}, s) } -> std::same_as<void>;
    { d.read_register(uint16_t{0x10}, s, std::chrono::milliseconds(100)) } -> std::same_as<void>;
};
static_assert(read_register_forms<master_device>);
#endif

// =============================================================================
// Runtime tests (Unity TEST_CASE)
// Unity asserts longjmp out of the test, so every assertion runs after the bus
// and device have gone out of scope; otherwise a failure would leave i2c0
// allocated.
// =============================================================================

namespace {

// Returns the error of a result, or an empty error_code on success.
template<typename R>
std::error_code error_of(const R& result) {
    return result.has_value() ? std::error_code{} : result.error();
}

} // namespace

TEST_CASE("master_device::make with valid bus succeeds", "[idfxx][i2c][master_device]") {
    bool bus_created = false;
    bool device_created = false;
    uint16_t address = 0;
    {
        auto bus_result = master_bus::make(port::i2c0, TEST_SDA, TEST_SCL, quanta::frequency::kilohertz(100));
        if (bus_result.has_value()) {
            bus_created = true;
            // Create a device (0x50 is common EEPROM address, may not be present)
            auto device_result = master_device::make(*bus_result, 0x50);
            if (device_result.has_value()) {
                device_created = true;
                address = device_result->address();
            }
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(bus_created, "Failed to create I2C bus");
    TEST_ASSERT_TRUE(device_created);
    TEST_ASSERT_EQUAL(0x50, address);
}

TEST_CASE("master_device::make with config succeeds", "[idfxx][i2c][master_device]") {
    bool bus_created = false;
    bool device_created = false;
    uint16_t address = 0;
    {
        auto bus_result = master_bus::make(port::i2c0, TEST_SDA, TEST_SCL, quanta::frequency::kilohertz(100));
        if (bus_result.has_value()) {
            bus_created = true;
            auto device_result = master_device::make(*bus_result, 0x50, {
                .scl_speed = quanta::frequency::kilohertz(400),
            });
            if (device_result.has_value()) {
                device_created = true;
                address = device_result->address();
            }
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(bus_created, "Failed to create I2C bus");
    TEST_ASSERT_TRUE(device_created);
    TEST_ASSERT_EQUAL(0x50, address);
}

TEST_CASE("master_device::make with config default scl_speed uses bus frequency", "[idfxx][i2c][master_device]") {
    bool bus_created = false;
    bool device_created = false;
    uint16_t address = 0;
    {
        auto bus_result = master_bus::make(port::i2c0, TEST_SDA, TEST_SCL, quanta::frequency::kilohertz(100));
        if (bus_result.has_value()) {
            bus_created = true;
            // scl_speed left unset means use bus frequency
            auto device_result = master_device::make(*bus_result, 0x50, {});
            if (device_result.has_value()) {
                device_created = true;
                address = device_result->address();
            }
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(bus_created, "Failed to create I2C bus");
    TEST_ASSERT_TRUE(device_created);
    TEST_ASSERT_EQUAL(0x50, address);
}

TEST_CASE("master_device::config defaults leave optional settings unset", "[idfxx][i2c][master_device]") {
    master_device::config cfg{};
    TEST_ASSERT_FALSE(cfg.scl_speed.has_value());
    TEST_ASSERT_FALSE(cfg.scl_wait_us.has_value());
}

TEST_CASE("master_device::config defaults to one-byte register addresses", "[idfxx][i2c][master_device]") {
    master_device::config cfg{};
    TEST_ASSERT_TRUE(cfg.register_address == register_address_size::one_byte);
}

TEST_CASE("master_device::make with zero scl_speed fails", "[idfxx][i2c][master_device]") {
    bool bus_created = false;
    std::error_code ec;
    {
        auto bus_result = master_bus::make(port::i2c0, TEST_SDA, TEST_SCL, quanta::frequency::kilohertz(100));
        if (bus_result.has_value()) {
            bus_created = true;
            ec = error_of(master_device::make(*bus_result, 0x50, {
                .scl_speed = quanta::frequency::hertz(0),
            }));
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(bus_created, "Failed to create I2C bus");
    TEST_ASSERT_TRUE_MESSAGE(ec == idfxx::errc::invalid_arg, ec.message().c_str());
}

TEST_CASE("master_device::make with zero scl_wait_us fails", "[idfxx][i2c][master_device]") {
    bool bus_created = false;
    std::error_code ec;
    {
        auto bus_result = master_bus::make(port::i2c0, TEST_SDA, TEST_SCL, quanta::frequency::kilohertz(100));
        if (bus_result.has_value()) {
            bus_created = true;
            ec = error_of(master_device::make(*bus_result, 0x50, {
                .scl_wait_us = 0,
            }));
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(bus_created, "Failed to create I2C bus");
    TEST_ASSERT_TRUE_MESSAGE(ec == idfxx::errc::invalid_arg, ec.message().c_str());
}

TEST_CASE("master_device register_address accessor reflects the config", "[idfxx][i2c][master_device]") {
    bool device_created = false;
    register_address_size default_size = register_address_size::two_bytes;
    register_address_size two_byte_size = register_address_size::one_byte;
    {
        auto bus_result = master_bus::make(port::i2c0, TEST_SDA, TEST_SCL, quanta::frequency::kilohertz(100));
        if (bus_result.has_value()) {
            auto default_device = master_device::make(*bus_result, 0x50);
            auto two_byte_device = master_device::make(*bus_result, 0x51, {
                .register_address = register_address_size::two_bytes,
            });
            if (default_device.has_value() && two_byte_device.has_value()) {
                device_created = true;
                default_size = default_device->register_address();
                two_byte_size = two_byte_device->register_address();
            }
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(device_created, "Failed to create I2C bus or devices");
    TEST_ASSERT_TRUE(default_size == register_address_size::one_byte);
    TEST_ASSERT_TRUE(two_byte_size == register_address_size::two_bytes);
}

TEST_CASE("master_device transmit API compiles", "[idfxx][i2c][master_device]") {
    // This test verifies that the transmit API exists at compile time
    // Actual transmission would require hardware device
    bool device_created = false;
    {
        auto bus_result = master_bus::make(port::i2c0, TEST_SDA, TEST_SCL, quanta::frequency::kilohertz(100));
        if (bus_result.has_value()) {
            auto device_result = master_device::make(*bus_result, 0x50);
            if (device_result.has_value()) {
                device_created = true;
                auto& device = *device_result;

                // Verify API exists (will likely fail without actual device)
                std::vector<uint8_t> data{0x01, 0x02, 0x03};
                [[maybe_unused]] auto result1 = device.try_transmit(data);
                [[maybe_unused]] auto result2 = device.try_transmit(data.data(), data.size());
                [[maybe_unused]] auto result3 = device.try_transmit(data, std::chrono::milliseconds(100));
            }
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(device_created, "Failed to create I2C bus or device");
}

TEST_CASE("master_device receive API compiles", "[idfxx][i2c][master_device]") {
    bool device_created = false;
    {
        auto bus_result = master_bus::make(port::i2c0, TEST_SDA, TEST_SCL, quanta::frequency::kilohertz(100));
        if (bus_result.has_value()) {
            auto device_result = master_device::make(*bus_result, 0x50);
            if (device_result.has_value()) {
                device_created = true;
                auto& device = *device_result;

                // Verify API exists
                std::vector<uint8_t> buffer(10);
                [[maybe_unused]] auto result1 = device.try_receive(buffer);
                [[maybe_unused]] auto result2 = device.try_receive(buffer.data(), buffer.size());
                [[maybe_unused]] auto result3 = device.try_receive(buffer, std::chrono::milliseconds(100));
            }
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(device_created, "Failed to create I2C bus or device");
}

TEST_CASE("master_device write_register API compiles", "[idfxx][i2c][master_device]") {
    bool device_created = false;
    {
        auto bus_result = master_bus::make(port::i2c0, TEST_SDA, TEST_SCL, quanta::frequency::kilohertz(100));
        if (bus_result.has_value()) {
            auto device_result = master_device::make(*bus_result, 0x50);
            if (device_result.has_value()) {
                device_created = true;
                auto& device = *device_result;

                // Verify register API exists
                std::vector<uint8_t> data{0xAB, 0xCD};
                [[maybe_unused]] auto result1 = device.try_write_register(0x0010, data);
                [[maybe_unused]] auto result2 = device.try_write_register(0x0010, data.data(), data.size());
                [[maybe_unused]] auto result3 = device.try_write_register(0x0010, data, std::chrono::milliseconds(100));
            }
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(device_created, "Failed to create I2C bus or device");
}

TEST_CASE("master_device read_register API compiles", "[idfxx][i2c][master_device]") {
    bool device_created = false;
    {
        auto bus_result = master_bus::make(port::i2c0, TEST_SDA, TEST_SCL, quanta::frequency::kilohertz(100));
        if (bus_result.has_value()) {
            auto device_result = master_device::make(*bus_result, 0x50);
            if (device_result.has_value()) {
                device_created = true;
                auto& device = *device_result;

                // Verify register API exists
                std::vector<uint8_t> buffer(10);
                [[maybe_unused]] auto result1 = device.try_read_register(0x0010, buffer);
                [[maybe_unused]] auto result2 = device.try_read_register(0x0010, buffer.data(), buffer.size());
                [[maybe_unused]] auto result3 = device.try_read_register(0x0010, buffer, std::chrono::milliseconds(100));
            }
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(device_created, "Failed to create I2C bus or device");
}

TEST_CASE("master_device write_registers API compiles", "[idfxx][i2c][master_device]") {
    bool device_created = false;
    {
        auto bus_result = master_bus::make(port::i2c0, TEST_SDA, TEST_SCL, quanta::frequency::kilohertz(100));
        if (bus_result.has_value()) {
            auto device_result = master_device::make(*bus_result, 0x50);
            if (device_result.has_value()) {
                device_created = true;
                auto& device = *device_result;

                // Verify multi-register write API exists
                std::vector<uint16_t> registers{0x0010, 0x0011, 0x0012};
                std::vector<uint8_t> data{0xAB, 0xCD, 0xEF};
                [[maybe_unused]] auto result1 = device.try_write_registers(registers, data);
                [[maybe_unused]] auto result2 = device.try_write_registers(registers, data.data(), data.size());
                [[maybe_unused]] auto result3 = device.try_write_registers(registers, data, std::chrono::milliseconds(10));
            }
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(device_created, "Failed to create I2C bus or device");
}

TEST_CASE("master_device bus accessor works", "[idfxx][i2c][master_device]") {
    bool device_created = false;
    port bus_port = static_cast<port>(-1);
    {
        auto bus_result = master_bus::make(port::i2c0, TEST_SDA, TEST_SCL, quanta::frequency::kilohertz(100));
        if (bus_result.has_value()) {
            auto device_result = master_device::make(*bus_result, 0x50);
            if (device_result.has_value()) {
                device_created = true;
                bus_port = device_result->bus().port();
            }
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(device_created, "Failed to create I2C bus or device");
    TEST_ASSERT_EQUAL(std::to_underlying(port::i2c0), std::to_underlying(bus_port));
}

TEST_CASE("master_device one-byte register address rejects registers above 0xFF", "[idfxx][i2c][master_device]") {
    bool device_created = false;
    std::error_code read_ec, write_ec, multi_ec;
    {
        auto bus_result = master_bus::make(port::i2c0, TEST_SDA, TEST_SCL, quanta::frequency::kilohertz(100));
        if (bus_result.has_value()) {
            // The default config uses one-byte register addresses
            auto device_result = master_device::make(*bus_result, 0x50);
            if (device_result.has_value()) {
                device_created = true;
                auto& device = *device_result;

                std::vector<uint8_t> data{0xAB};
                std::vector<uint16_t> registers{0x0010, 0x0100};
                read_ec = error_of(device.try_read_register(0x100, 1));
                write_ec = error_of(device.try_write_register(0x100, data));
                multi_ec = error_of(device.try_write_registers(registers, data));
            }
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(device_created, "Failed to create I2C bus or device");
    TEST_ASSERT_TRUE_MESSAGE(read_ec == idfxx::errc::invalid_arg, read_ec.message().c_str());
    TEST_ASSERT_TRUE_MESSAGE(write_ec == idfxx::errc::invalid_arg, write_ec.message().c_str());
    TEST_ASSERT_TRUE_MESSAGE(multi_ec == idfxx::errc::invalid_arg, multi_ec.message().c_str());
}

TEST_CASE("master_device read_register with an empty buffer fails with invalid_arg", "[idfxx][i2c][master_device]") {
    bool device_created = false;
    std::error_code zero_ec, null_ec;
    {
        auto bus_result = master_bus::make(port::i2c0, TEST_SDA, TEST_SCL, quanta::frequency::kilohertz(100));
        if (bus_result.has_value()) {
            auto device_result = master_device::make(*bus_result, 0x50);
            if (device_result.has_value()) {
                device_created = true;
                auto& device = *device_result;
                zero_ec = error_of(device.try_read_register(0x10, size_t{0}));
                null_ec = error_of(device.try_read_register(0x10, nullptr, size_t{3}));
            }
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(device_created, "Failed to create I2C bus or device");
    TEST_ASSERT_TRUE_MESSAGE(zero_ec == idfxx::errc::invalid_arg, zero_ec.message().c_str());
    TEST_ASSERT_TRUE_MESSAGE(null_ec == idfxx::errc::invalid_arg, null_ec.message().c_str());
}

TEST_CASE("master_device write_register with a null buffer fails with invalid_arg", "[idfxx][i2c][master_device]") {
    bool device_created = false;
    std::error_code ec;
    {
        auto bus_result = master_bus::make(port::i2c0, TEST_SDA, TEST_SCL, quanta::frequency::kilohertz(100));
        if (bus_result.has_value()) {
            auto device_result = master_device::make(*bus_result, 0x50);
            if (device_result.has_value()) {
                device_created = true;
                ec = error_of(device_result->try_write_register(0x10, nullptr, size_t{3}));
            }
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(device_created, "Failed to create I2C bus or device");
    TEST_ASSERT_TRUE_MESSAGE(ec == idfxx::errc::invalid_arg, ec.message().c_str());
}

TEST_CASE("master_device transfer the device doesn't acknowledge fails with invalid_response", "[idfxx][i2c][master_device]") {
    // No device is present at 0x50, so the transfer is either NACKed or times out on the bus; the driver reports
    // both the same way.
    bool device_created = false;
    std::error_code ec;
    {
        auto bus_result = master_bus::make(port::i2c0, TEST_SDA, TEST_SCL, quanta::frequency::kilohertz(100));
        if (bus_result.has_value()) {
            auto device_result = master_device::make(*bus_result, 0x50);
            if (device_result.has_value()) {
                device_created = true;
                std::vector<uint8_t> data{0xAB};
                ec = error_of(device_result->try_transmit(data));
            }
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(device_created, "Failed to create I2C bus or device");
    TEST_ASSERT_TRUE_MESSAGE(static_cast<bool>(ec), "Transmit to an absent device succeeded");
    TEST_ASSERT_TRUE_MESSAGE(ec == idfxx::errc::invalid_response, ec.message().c_str());
}

TEST_CASE("master_device transfer fails with timeout while another task holds the bus", "[idfxx][i2c][master_device]") {
    bool device_created = false;
    std::error_code transmit_ec, read_ec;
    {
        auto bus_result = master_bus::make(port::i2c0, TEST_SDA, TEST_SCL, quanta::frequency::kilohertz(100));
        if (bus_result.has_value()) {
            auto& bus = *bus_result;
            auto device_result = master_device::make(bus, 0x50);
            if (device_result.has_value()) {
                device_created = true;
                auto& device = *device_result;
                std::lock_guard<master_bus> hold(bus);
                std::thread contender([&]() {
                    std::vector<uint8_t> data{0xAB};
                    uint8_t buffer[2];
                    transmit_ec = error_of(device.try_transmit(data, std::chrono::milliseconds(10)));
                    read_ec = error_of(device.try_read_register(0x10, buffer, sizeof(buffer), std::chrono::milliseconds(10)));
                });
                contender.join();
            }
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(device_created, "Failed to create I2C bus or device");
    TEST_ASSERT_TRUE_MESSAGE(transmit_ec == idfxx::errc::timeout, transmit_ec.message().c_str());
    TEST_ASSERT_TRUE_MESSAGE(read_ec == idfxx::errc::timeout, read_ec.message().c_str());
}

TEST_CASE("master_device change_address rejects an address that doesn't fit the addressing mode", "[idfxx][i2c][master_device]") {
    // The address is checked before the driver sees it, so the bus stays usable: a later transfer reports the
    // missing device rather than timing out.
    bool device_created = false;
    std::error_code change_ec, transmit_ec;
    uint16_t address = 0;
    {
        auto bus_result = master_bus::make(port::i2c0, TEST_SDA, TEST_SCL, quanta::frequency::kilohertz(100));
        if (bus_result.has_value()) {
            auto device_result = master_device::make(*bus_result, 0x50);
            if (device_result.has_value()) {
                device_created = true;
                auto& device = *device_result;
                change_ec = error_of(device.try_change_address(0x80));
                address = device.address();
                std::vector<uint8_t> data{0xAB};
                transmit_ec = error_of(device.try_transmit(data));
            }
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(device_created, "Failed to create I2C bus or device");
    TEST_ASSERT_TRUE_MESSAGE(change_ec == idfxx::errc::invalid_arg, change_ec.message().c_str());
    TEST_ASSERT_EQUAL(0x50, address);
    TEST_ASSERT_TRUE_MESSAGE(transmit_ec == idfxx::errc::invalid_response, transmit_ec.message().c_str());
}

TEST_CASE("master_device change_address updates the address", "[idfxx][i2c][master_device]") {
    bool device_created = false;
    std::error_code ec;
    uint16_t address = 0;
    {
        auto bus_result = master_bus::make(port::i2c0, TEST_SDA, TEST_SCL, quanta::frequency::kilohertz(100));
        if (bus_result.has_value()) {
            auto device_result = master_device::make(*bus_result, 0x50);
            if (device_result.has_value()) {
                device_created = true;
                ec = error_of(device_result->try_change_address(0x51));
                address = device_result->address();
            }
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(device_created, "Failed to create I2C bus or device");
    TEST_ASSERT_FALSE_MESSAGE(static_cast<bool>(ec), ec.message().c_str());
    TEST_ASSERT_EQUAL(0x51, address);
}
