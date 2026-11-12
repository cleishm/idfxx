// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Unit tests for idfxx::i2c::master_device
// Uses ESP-IDF Unity test framework with compile-time static_asserts

#include "idfxx/i2c/master"

#include <array>
#include <bit>
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

// A 2.x-style (high, low, size) register read doesn't compile
template<typename D>
concept try_read_register_high_low = requires(D& d) { d.try_read_register(uint16_t{0x10}, 0, size_t{3}); };
template<typename D>
concept try_read_register_high_low_timeout = requires(D& d) {
    d.try_read_register(uint16_t{0x10}, 0, size_t{3}, std::chrono::milliseconds(100));
};
static_assert(!try_read_register_high_low<master_device>);
static_assert(!try_read_register_high_low_timeout<master_device>);

// The single-register read forms resolve: into any writable byte buffer, or a fixed-size array
template<typename D>
concept try_read_register_forms = requires(D& d, uint8_t* p, std::span<uint8_t> s, std::array<std::byte, 4>& b) {
    { d.try_read_register(uint16_t{0x10}, s) } -> std::same_as<idfxx::result<void>>;
    { d.try_read_register(uint16_t{0x10}, s, std::chrono::milliseconds(100)) } -> std::same_as<idfxx::result<void>>;
    { d.try_read_register(uint16_t{0x10}, b) } -> std::same_as<idfxx::result<void>>;
    { d.try_read_register(uint16_t{0x10}, {p, size_t{3}}) } -> std::same_as<idfxx::result<void>>;
    { d.try_read_register(uint16_t{0x10}, {p, size_t{3}}, std::chrono::milliseconds(100)) } -> std::same_as<idfxx::result<void>>;
    { d.template try_read_register<3>(uint16_t{0x10}) } -> std::same_as<idfxx::result<std::array<uint8_t, 3>>>;
    { d.template try_read_register<3>(uint16_t{0x10}, std::chrono::milliseconds(100)) } -> std::same_as<idfxx::result<std::array<uint8_t, 3>>>;
};
static_assert(try_read_register_forms<master_device>);

// A size is not a buffer: there is no allocating read, and a zero-byte fixed-size read doesn't compile
template<typename D>
concept try_read_register_sized = requires(D& d) { d.try_read_register(0x10, 3); };
template<typename D>
concept try_read_register_sized_timeout = requires(D& d) {
    d.try_read_register(uint16_t{0x10}, size_t{3}, std::chrono::milliseconds(100));
};
template<typename D>
concept try_read_register_zero = requires(D& d) { d.template try_read_register<0>(uint16_t{0x10}); };
template<typename D>
concept try_receive_sized = requires(D& d) { d.try_receive(size_t{4}); };
static_assert(!try_read_register_sized<master_device>);
static_assert(!try_read_register_sized_timeout<master_device>);
static_assert(!try_read_register_zero<master_device>);
static_assert(!try_receive_sized<master_device>);

// Byte buffers are passed as views, so pointer-and-length arguments don't resolve; {ptr, len} does
template<typename D>
concept try_transmit_pointer_length = requires(D& d, const uint8_t* p) { d.try_transmit(p, size_t{2}); };
template<typename D>
concept try_receive_pointer_length = requires(D& d, uint8_t* p) { d.try_receive(p, size_t{2}); };
template<typename D>
concept try_write_register_pointer_length = requires(D& d, const uint8_t* p) {
    d.try_write_register(uint16_t{0x10}, p, size_t{2});
};
static_assert(!try_transmit_pointer_length<master_device>);
static_assert(!try_receive_pointer_length<master_device>);
static_assert(!try_write_register_pointer_length<master_device>);

// Transmit and register writes take any byte buffer, literal bytes, or several buffers at once
template<typename D>
concept try_write_forms = requires(
    D& d,
    const uint8_t* p,
    std::vector<uint8_t>& v,
    std::array<std::byte, 2>& b,
    std::array<idfxx::byte_view, 2>& views,
    std::span<const uint16_t> regs
) {
    { d.try_transmit(v) } -> std::same_as<idfxx::result<void>>;
    { d.try_transmit(b, std::chrono::milliseconds(100)) } -> std::same_as<idfxx::result<void>>;
    { d.try_transmit({p, size_t{2}}) } -> std::same_as<idfxx::result<void>>;
    { d.try_transmit({0x01, 0x02}) } -> std::same_as<idfxx::result<void>>;
    { d.try_transmit({0x01}, std::chrono::milliseconds(100)) } -> std::same_as<idfxx::result<void>>;
    { d.try_transmit(v, b) } -> std::same_as<idfxx::result<void>>;
    { d.try_transmit(views) } -> std::same_as<idfxx::result<void>>;
    { d.try_transmit(views, std::chrono::milliseconds(100)) } -> std::same_as<idfxx::result<void>>;
    { d.try_write_register(uint16_t{0x10}, b) } -> std::same_as<idfxx::result<void>>;
    { d.try_write_register(uint16_t{0x10}, {0x01, 0x02}) } -> std::same_as<idfxx::result<void>>;
    { d.try_write_register(uint16_t{0x10}, {0x01}, std::chrono::milliseconds(100)) } -> std::same_as<idfxx::result<void>>;
    { d.try_write_registers(regs, b) } -> std::same_as<idfxx::result<void>>;
    { d.try_write_registers(regs, {0x00}, std::chrono::milliseconds(100)) } -> std::same_as<idfxx::result<void>>;
    { d.try_write_registers({0x10, 0x11}, v) } -> std::same_as<idfxx::result<void>>;
    { d.try_write_registers({0x10, 0x11}, {0x00, 0x01}) } -> std::same_as<idfxx::result<void>>;
    { d.try_write_registers({0x10, 0x11}, {0x00, 0x01}, std::chrono::milliseconds(100)) } -> std::same_as<idfxx::result<void>>;
};
static_assert(try_write_forms<master_device>);

// Receives fill any writable byte buffer, or return a fixed-size array
template<typename D>
concept try_receive_forms = requires(D& d, std::vector<uint8_t>& v, std::array<std::byte, 2>& b) {
    { d.try_receive(v) } -> std::same_as<idfxx::result<void>>;
    { d.try_receive(b, std::chrono::milliseconds(100)) } -> std::same_as<idfxx::result<void>>;
    { d.template try_receive<2>() } -> std::same_as<idfxx::result<std::array<uint8_t, 2>>>;
    { d.template try_receive<2>(std::chrono::milliseconds(100)) } -> std::same_as<idfxx::result<std::array<uint8_t, 2>>>;
};
static_assert(try_receive_forms<master_device>);

// Text and temporaries are not byte buffers
template<typename D>
concept try_transmit_chars = requires(D& d, std::array<char, 2>& c) { d.try_transmit(c); };
template<typename D>
concept try_receive_temporary = requires(D& d) { d.try_receive(std::array<uint8_t, 2>{}); };
static_assert(!try_transmit_chars<master_device>);
static_assert(!try_receive_temporary<master_device>);

// Custom operations hold views of the caller's buffers
static_assert(std::is_same_v<decltype(operation::write_data), idfxx::byte_view>);
static_assert(std::is_same_v<decltype(operation::read_data), idfxx::byte_span>);

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
concept read_register_forms = requires(D& d, uint8_t* p, std::span<uint8_t> s, std::array<std::byte, 4>& b) {
    { d.read_register(uint16_t{0x10}, s) } -> std::same_as<void>;
    { d.read_register(uint16_t{0x10}, s, std::chrono::milliseconds(100)) } -> std::same_as<void>;
    { d.read_register(uint16_t{0x10}, b) } -> std::same_as<void>;
    { d.read_register(uint16_t{0x10}, {p, size_t{3}}) } -> std::same_as<void>;
    { d.read_register(uint16_t{0x10}, {p, size_t{3}}, std::chrono::milliseconds(100)) } -> std::same_as<void>;
    { d.template read_register<3>(uint16_t{0x10}) } -> std::same_as<std::array<uint8_t, 3>>;
    { d.template read_register<3>(uint16_t{0x10}, std::chrono::milliseconds(100)) } -> std::same_as<std::array<uint8_t, 3>>;
};
static_assert(read_register_forms<master_device>);

template<typename D>
concept read_register_sized = requires(D& d) { d.read_register(0x10, 3); };
template<typename D>
concept read_register_sized_timeout = requires(D& d) {
    d.read_register(uint16_t{0x10}, size_t{3}, std::chrono::milliseconds(100));
};
template<typename D>
concept read_register_zero = requires(D& d) { d.template read_register<0>(uint16_t{0x10}); };
template<typename D>
concept receive_sized = requires(D& d) { d.receive(size_t{4}); };
template<typename D>
concept transmit_pointer_length = requires(D& d, const uint8_t* p) { d.transmit(p, size_t{2}); };
static_assert(!read_register_sized<master_device>);
static_assert(!read_register_sized_timeout<master_device>);
static_assert(!read_register_zero<master_device>);
static_assert(!receive_sized<master_device>);
static_assert(!transmit_pointer_length<master_device>);

template<typename D>
concept write_forms = requires(
    D& d,
    std::vector<uint8_t>& v,
    std::array<std::byte, 2>& b,
    std::array<idfxx::byte_view, 2>& views,
    std::span<const uint16_t> regs
) {
    { d.transmit(v) } -> std::same_as<void>;
    { d.transmit(b, std::chrono::milliseconds(100)) } -> std::same_as<void>;
    { d.transmit({0x01, 0x02}) } -> std::same_as<void>;
    { d.transmit({0x01}, std::chrono::milliseconds(100)) } -> std::same_as<void>;
    { d.transmit(v, b) } -> std::same_as<void>;
    { d.transmit(views) } -> std::same_as<void>;
    { d.transmit(views, std::chrono::milliseconds(100)) } -> std::same_as<void>;
    { d.write_register(uint16_t{0x10}, b) } -> std::same_as<void>;
    { d.write_register(uint16_t{0x10}, {0x01, 0x02}) } -> std::same_as<void>;
    { d.write_register(uint16_t{0x10}, {0x01}, std::chrono::milliseconds(100)) } -> std::same_as<void>;
    { d.write_registers(regs, b) } -> std::same_as<void>;
    { d.write_registers(regs, {0x00}, std::chrono::milliseconds(100)) } -> std::same_as<void>;
    { d.write_registers({0x10, 0x11}, v) } -> std::same_as<void>;
    { d.write_registers({0x10, 0x11}, {0x00, 0x01}) } -> std::same_as<void>;
    { d.write_registers({0x10, 0x11}, {0x00, 0x01}, std::chrono::milliseconds(100)) } -> std::same_as<void>;
};
static_assert(write_forms<master_device>);

template<typename D>
concept receive_forms = requires(D& d, std::vector<uint8_t>& v, std::array<std::byte, 2>& b) {
    { d.receive(v) } -> std::same_as<void>;
    { d.receive(b, std::chrono::milliseconds(100)) } -> std::same_as<void>;
    { d.template receive<2>() } -> std::same_as<std::array<uint8_t, 2>>;
    { d.template receive<2>(std::chrono::milliseconds(100)) } -> std::same_as<std::array<uint8_t, 2>>;
};
static_assert(receive_forms<master_device>);
#endif

// The configured SCL speed is readable from the device
template<typename D>
concept has_scl_speed = requires(const D& d) {
    { d.scl_speed() } -> std::same_as<quanta::frequency::hertz>;
};
static_assert(has_scl_speed<master_device>);

// Register values: unsigned integers only, excluding bool and the character types
static_assert(idfxx::i2c::detail::register_value<uint8_t>);
static_assert(idfxx::i2c::detail::register_value<uint16_t>);
static_assert(idfxx::i2c::detail::register_value<uint32_t>);
static_assert(idfxx::i2c::detail::register_value<uint64_t>);
static_assert(idfxx::i2c::detail::register_value<unsigned int>);
static_assert(idfxx::i2c::detail::register_value<unsigned long>);
static_assert(idfxx::i2c::detail::register_value<unsigned char>);
static_assert(!idfxx::i2c::detail::register_value<int>);
static_assert(!idfxx::i2c::detail::register_value<int16_t>);
static_assert(!idfxx::i2c::detail::register_value<bool>);
static_assert(!idfxx::i2c::detail::register_value<const bool>);
static_assert(!idfxx::i2c::detail::register_value<char>);
static_assert(!idfxx::i2c::detail::register_value<char8_t>);
static_assert(!idfxx::i2c::detail::register_value<char16_t>);
static_assert(!idfxx::i2c::detail::register_value<char32_t>);
static_assert(!idfxx::i2c::detail::register_value<wchar_t>);

// Register value byte order: exact layout and round trips
template<typename T>
constexpr std::array<uint8_t, sizeof(T)> encoded(T value, std::endian order) {
    std::array<uint8_t, sizeof(T)> out{};
    idfxx::i2c::detail::encode_register_value<T>(value, order, out);
    return out;
}
template<typename T>
constexpr T decoded(std::array<uint8_t, sizeof(T)> in, std::endian order) {
    return idfxx::i2c::detail::decode_register_value<T>(in, order);
}
static_assert(encoded<uint8_t>(0xA5, std::endian::big) == std::array<uint8_t, 1>{0xA5});
static_assert(encoded<uint8_t>(0xA5, std::endian::little) == std::array<uint8_t, 1>{0xA5});
static_assert(encoded<uint16_t>(0x1234, std::endian::big) == std::array<uint8_t, 2>{0x12, 0x34});
static_assert(encoded<uint16_t>(0x1234, std::endian::little) == std::array<uint8_t, 2>{0x34, 0x12});
static_assert(encoded<uint32_t>(0x12345678, std::endian::big) == std::array<uint8_t, 4>{0x12, 0x34, 0x56, 0x78});
static_assert(encoded<uint32_t>(0x12345678, std::endian::little) == std::array<uint8_t, 4>{0x78, 0x56, 0x34, 0x12});
static_assert(decoded<uint16_t>({0x12, 0x34}, std::endian::big) == 0x1234);
static_assert(decoded<uint16_t>({0x12, 0x34}, std::endian::little) == 0x3412);
static_assert(decoded<uint32_t>({0x12, 0x34, 0x56, 0x78}, std::endian::big) == 0x12345678);
static_assert(decoded<uint32_t>({0x12, 0x34, 0x56, 0x78}, std::endian::little) == 0x78563412);
static_assert(decoded<uint16_t>(encoded<uint16_t>(0xBEEF, std::endian::big), std::endian::big) == 0xBEEF);
static_assert(decoded<uint16_t>(encoded<uint16_t>(0xBEEF, std::endian::little), std::endian::little) == 0xBEEF);
static_assert(decoded<uint32_t>(encoded<uint32_t>(0xDEADBEEF, std::endian::big), std::endian::big) == 0xDEADBEEF);
static_assert(decoded<uint32_t>(encoded<uint32_t>(0xDEADBEEF, std::endian::little), std::endian::little) == 0xDEADBEEF);
static_assert(decoded<uint64_t>(encoded<uint64_t>(0x0123456789ABCDEF, std::endian::big), std::endian::big) == 0x0123456789ABCDEF);

// The single-byte and multi-byte value forms resolve, alongside the fixed-size read
template<typename D>
concept try_register_value_forms = requires(D& d) {
    { d.template try_read_register<1>(uint16_t{0x10}) } -> std::same_as<idfxx::result<std::array<uint8_t, 1>>>;
    { d.template try_read_register<uint8_t>(uint16_t{0x10}, std::endian::big) } -> std::same_as<idfxx::result<uint8_t>>;
    { d.try_read_register(uint16_t{0x10}) } -> std::same_as<idfxx::result<uint8_t>>;
    { d.try_read_register(uint16_t{0x10}, std::chrono::milliseconds(100)) } -> std::same_as<idfxx::result<uint8_t>>;
    { d.template try_read_register<uint16_t>(uint16_t{0x10}, std::endian::big) } -> std::same_as<idfxx::result<uint16_t>>;
    { d.template try_read_register<uint32_t>(uint16_t{0x10}, std::endian::little, std::chrono::milliseconds(100)) } -> std::same_as<idfxx::result<uint32_t>>;
    { d.try_write_register(uint16_t{0x10}, 0) } -> std::same_as<idfxx::result<void>>;
    { d.try_write_register(uint16_t{0x10}, 0x80) } -> std::same_as<idfxx::result<void>>;
    { d.try_write_register(uint16_t{0x10}, uint8_t{0x80}, std::chrono::milliseconds(100)) } -> std::same_as<idfxx::result<void>>;
    { d.template try_write_register<uint16_t>(uint16_t{0x10}, 0x8583, std::endian::big) } -> std::same_as<idfxx::result<void>>;
    { d.try_write_register(uint16_t{0x10}, uint16_t{0x8583}, std::endian::big) } -> std::same_as<idfxx::result<void>>;
    { d.template try_write_register<uint32_t>(uint16_t{0x10}, 1, std::endian::little, std::chrono::milliseconds(100)) } -> std::same_as<idfxx::result<void>>;
};
static_assert(try_register_value_forms<master_device>);

// A value wider than a byte needs a byte order, rather than silently writing its low byte
template<typename D>
concept try_write_wide_value_without_order = requires(D& d) { d.try_write_register(uint16_t{0x10}, uint16_t{1}); };
template<typename D>
concept try_write_wide_value_without_order_timeout = requires(D& d) {
    d.try_write_register(uint16_t{0x10}, uint16_t{1}, std::chrono::milliseconds(10));
};
template<typename D>
concept try_write_explicit_wide_value_without_order = requires(D& d) {
    d.template try_write_register<uint16_t>(uint16_t{0x10}, 1);
};
// An int literal deduces int, which is not a register value: name the width explicitly
template<typename D>
concept try_write_int_literal_with_order = requires(D& d) {
    d.try_write_register(uint16_t{0x10}, 0x8583, std::endian::big);
};
template<typename D>
concept try_read_bool_value = requires(D& d) { d.template try_read_register<bool>(uint16_t{0x10}, std::endian::big); };
// The guard is by type: wide unsigned masks are rejected even when the value fits in a byte
template<typename D>
concept try_write_unsigned_literal = requires(D& d) { d.try_write_register(uint16_t{0x10}, 0x80u); };
template<typename D>
concept try_write_unsigned_long_mask = requires(D& d) { d.try_write_register(uint16_t{0x10}, 1UL << 3); };
static_assert(!try_write_unsigned_literal<master_device>);
static_assert(!try_write_unsigned_long_mask<master_device>);
static_assert(!try_write_wide_value_without_order<master_device>);
static_assert(!try_write_wide_value_without_order_timeout<master_device>);
static_assert(!try_write_explicit_wide_value_without_order<master_device>);
static_assert(!try_write_int_literal_with_order<master_device>);
static_assert(!try_read_bool_value<master_device>);

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
template<typename D>
concept register_value_forms = requires(D& d) {
    { d.template read_register<1>(uint16_t{0x10}) } -> std::same_as<std::array<uint8_t, 1>>;
    { d.template read_register<uint8_t>(uint16_t{0x10}, std::endian::big) } -> std::same_as<uint8_t>;
    { d.read_register(uint16_t{0x10}) } -> std::same_as<uint8_t>;
    { d.read_register(uint16_t{0x10}, std::chrono::milliseconds(100)) } -> std::same_as<uint8_t>;
    { d.template read_register<uint16_t>(uint16_t{0x10}, std::endian::big) } -> std::same_as<uint16_t>;
    { d.template read_register<uint32_t>(uint16_t{0x10}, std::endian::little, std::chrono::milliseconds(100)) } -> std::same_as<uint32_t>;
    { d.write_register(uint16_t{0x10}, 0) } -> std::same_as<void>;
    { d.write_register(uint16_t{0x10}, 0x80) } -> std::same_as<void>;
    { d.write_register(uint16_t{0x10}, uint8_t{0x80}, std::chrono::milliseconds(100)) } -> std::same_as<void>;
    { d.template write_register<uint16_t>(uint16_t{0x10}, 0x8583, std::endian::big) } -> std::same_as<void>;
    { d.write_register(uint16_t{0x10}, uint16_t{0x8583}, std::endian::big) } -> std::same_as<void>;
    { d.template write_register<uint32_t>(uint16_t{0x10}, 1, std::endian::little, std::chrono::milliseconds(100)) } -> std::same_as<void>;
};
static_assert(register_value_forms<master_device>);

template<typename D>
concept write_wide_value_without_order = requires(D& d) { d.write_register(uint16_t{0x10}, uint16_t{1}); };
template<typename D>
concept write_wide_value_without_order_timeout = requires(D& d) {
    d.write_register(uint16_t{0x10}, uint16_t{1}, std::chrono::milliseconds(10));
};
template<typename D>
concept write_explicit_wide_value_without_order = requires(D& d) {
    d.template write_register<uint16_t>(uint16_t{0x10}, 1);
};
template<typename D>
concept write_int_literal_with_order = requires(D& d) {
    d.write_register(uint16_t{0x10}, 0x8583, std::endian::big);
};
template<typename D>
concept read_bool_value = requires(D& d) { d.template read_register<bool>(uint16_t{0x10}, std::endian::big); };
template<typename D>
concept write_unsigned_literal = requires(D& d) { d.write_register(uint16_t{0x10}, 0x80u); };
template<typename D>
concept write_unsigned_long_mask = requires(D& d) { d.write_register(uint16_t{0x10}, 1UL << 3); };
static_assert(!write_unsigned_literal<master_device>);
static_assert(!write_unsigned_long_mask<master_device>);
static_assert(!write_wide_value_without_order<master_device>);
static_assert(!write_wide_value_without_order_timeout<master_device>);
static_assert(!write_explicit_wide_value_without_order<master_device>);
static_assert(!write_int_literal_with_order<master_device>);
static_assert(!read_bool_value<master_device>);
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

TEST_CASE("master_device scl_speed accessor reflects the config or the bus frequency", "[idfxx][i2c][master_device]") {
    bool device_created = false;
    int32_t default_hz = 0;
    int32_t configured_hz = 0;
    int32_t moved_hz = 0;
    {
        auto bus_result = master_bus::make(port::i2c0, TEST_SDA, TEST_SCL, quanta::frequency::kilohertz(100));
        if (bus_result.has_value()) {
            auto default_device = master_device::make(*bus_result, 0x50);
            auto configured_device = master_device::make(*bus_result, 0x51, {
                .scl_speed = quanta::frequency::kilohertz(400),
            });
            if (default_device.has_value() && configured_device.has_value()) {
                device_created = true;
                default_hz = static_cast<int32_t>(default_device->scl_speed().count());
                configured_hz = static_cast<int32_t>(configured_device->scl_speed().count());
                master_device moved = std::move(*configured_device);
                moved_hz = static_cast<int32_t>(moved.scl_speed().count());
            }
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(device_created, "Failed to create I2C bus or devices");
    TEST_ASSERT_EQUAL_INT32(100000, default_hz);
    TEST_ASSERT_EQUAL_INT32(400000, configured_hz);
    TEST_ASSERT_EQUAL_INT32(400000, moved_hz);
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
                std::array<std::byte, 2> payload{std::byte{0x04}, std::byte{0x05}};
                std::array<idfxx::byte_view, 2> parts{data, payload};
                [[maybe_unused]] auto result1 = device.try_transmit(data);
                [[maybe_unused]] auto result2 = device.try_transmit({data.data(), data.size()});
                [[maybe_unused]] auto result3 = device.try_transmit(data, std::chrono::milliseconds(100));
                [[maybe_unused]] auto result4 = device.try_transmit({0x01, 0x02});
                [[maybe_unused]] auto result5 = device.try_transmit({0x01, 0x02}, std::chrono::milliseconds(100));
                [[maybe_unused]] auto result6 = device.try_transmit(data, payload);
                [[maybe_unused]] auto result7 = device.try_transmit(parts);
                [[maybe_unused]] auto result8 = device.try_transmit(parts, std::chrono::milliseconds(100));
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
                [[maybe_unused]] auto result2 = device.try_receive({buffer.data(), buffer.size()});
                [[maybe_unused]] auto result3 = device.try_receive(buffer, std::chrono::milliseconds(100));
                [[maybe_unused]] auto result4 = device.try_receive<4>();
                [[maybe_unused]] auto result5 = device.try_receive<4>(std::chrono::milliseconds(100));
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
                [[maybe_unused]] auto result2 = device.try_write_register(0x0010, {data.data(), data.size()});
                [[maybe_unused]] auto result3 = device.try_write_register(0x0010, data, std::chrono::milliseconds(100));
                [[maybe_unused]] auto result4 = device.try_write_register(0x0010, {0xAB, 0xCD});
                [[maybe_unused]] auto result5 = device.try_write_register(0x0010, {0xAB, 0xCD}, std::chrono::milliseconds(100));
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
                [[maybe_unused]] auto result2 = device.try_read_register(0x0010, {buffer.data(), buffer.size()});
                [[maybe_unused]] auto result3 = device.try_read_register(0x0010, buffer, std::chrono::milliseconds(100));
                [[maybe_unused]] auto result4 = device.try_read_register<4>(0x0010);
                [[maybe_unused]] auto result5 = device.try_read_register<4>(0x0010, std::chrono::milliseconds(100));
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
                [[maybe_unused]] auto result2 = device.try_write_registers(registers, {data.data(), data.size()});
                [[maybe_unused]] auto result3 = device.try_write_registers(registers, data, std::chrono::milliseconds(10));
                [[maybe_unused]] auto result4 = device.try_write_registers(registers, {0xAB, 0xCD});
                [[maybe_unused]] auto result5 = device.try_write_registers({0x0010, 0x0011}, data);
                [[maybe_unused]] auto result6 = device.try_write_registers({0x0010, 0x0011}, {0xAB}, std::chrono::milliseconds(10));
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
    std::error_code read_ec, read_fixed_ec, read_into_ec, write_ec, write_literal_ec, multi_ec, multi_literal_ec;
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
                std::array<uint8_t, 2> buffer{};
                read_ec = error_of(device.try_read_register(0x100));
                read_fixed_ec = error_of(device.try_read_register<2>(0x100));
                read_into_ec = error_of(device.try_read_register(0x100, buffer));
                write_ec = error_of(device.try_write_register(0x100, data));
                write_literal_ec = error_of(device.try_write_register(0x100, {0xAB, 0xCD}));
                multi_ec = error_of(device.try_write_registers(registers, data));
                multi_literal_ec = error_of(device.try_write_registers({0x0010, 0x0100}, {0xAB}));
            }
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(device_created, "Failed to create I2C bus or device");
    TEST_ASSERT_TRUE_MESSAGE(read_ec == idfxx::errc::invalid_arg, read_ec.message().c_str());
    TEST_ASSERT_TRUE_MESSAGE(read_fixed_ec == idfxx::errc::invalid_arg, read_fixed_ec.message().c_str());
    TEST_ASSERT_TRUE_MESSAGE(read_into_ec == idfxx::errc::invalid_arg, read_into_ec.message().c_str());
    TEST_ASSERT_TRUE_MESSAGE(write_ec == idfxx::errc::invalid_arg, write_ec.message().c_str());
    TEST_ASSERT_TRUE_MESSAGE(write_literal_ec == idfxx::errc::invalid_arg, write_literal_ec.message().c_str());
    TEST_ASSERT_TRUE_MESSAGE(multi_ec == idfxx::errc::invalid_arg, multi_ec.message().c_str());
    TEST_ASSERT_TRUE_MESSAGE(multi_literal_ec == idfxx::errc::invalid_arg, multi_literal_ec.message().c_str());
}

TEST_CASE("master_device read_register with an empty buffer fails with invalid_arg", "[idfxx][i2c][master_device]") {
    bool device_created = false;
    std::error_code default_ec, empty_vector_ec;
    {
        auto bus_result = master_bus::make(port::i2c0, TEST_SDA, TEST_SCL, quanta::frequency::kilohertz(100));
        if (bus_result.has_value()) {
            auto device_result = master_device::make(*bus_result, 0x50);
            if (device_result.has_value()) {
                device_created = true;
                auto& device = *device_result;
                std::vector<uint8_t> empty;
                default_ec = error_of(device.try_read_register(0x10, idfxx::byte_span{}));
                empty_vector_ec = error_of(device.try_read_register(0x10, empty));
            }
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(device_created, "Failed to create I2C bus or device");
    TEST_ASSERT_TRUE_MESSAGE(default_ec == idfxx::errc::invalid_arg, default_ec.message().c_str());
    TEST_ASSERT_TRUE_MESSAGE(empty_vector_ec == idfxx::errc::invalid_arg, empty_vector_ec.message().c_str());
}

TEST_CASE("master_device fixed-size, read-into and literal forms reach the bus", "[idfxx][i2c][master_device]") {
    // No device is present at 0x50, so each form that gets as far as the bus is NACKed (or times out on the bus) and
    // reports invalid_response, rather than being rejected before anything is sent.
    bool device_created = false;
    std::error_code transmit_literal_ec, transmit_multi_ec, receive_into_ec, receive_fixed_ec, read_into_ec,
        read_fixed_ec, write_literal_ec, multi_literal_ec;
    {
        auto bus_result = master_bus::make(port::i2c0, TEST_SDA, TEST_SCL, quanta::frequency::kilohertz(100));
        if (bus_result.has_value()) {
            auto device_result = master_device::make(*bus_result, 0x50);
            if (device_result.has_value()) {
                device_created = true;
                auto& device = *device_result;
                std::array<uint8_t, 1> reg{0x10};
                std::array<std::byte, 2> payload{std::byte{0x01}, std::byte{0x02}};
                std::array<std::byte, 3> buffer{};
                transmit_literal_ec = error_of(device.try_transmit({0xAB, 0xCD}));
                transmit_multi_ec = error_of(device.try_transmit(reg, payload));
                receive_into_ec = error_of(device.try_receive(buffer));
                receive_fixed_ec = error_of(device.try_receive<2>());
                read_into_ec = error_of(device.try_read_register(0x10, buffer));
                read_fixed_ec = error_of(device.try_read_register<2>(0x10));
                write_literal_ec = error_of(device.try_write_register(0x10, {0xAB, 0xCD}));
                multi_literal_ec = error_of(device.try_write_registers({0x10, 0x11}, {0xAB}));
            }
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(device_created, "Failed to create I2C bus or device");
    TEST_ASSERT_TRUE_MESSAGE(transmit_literal_ec == idfxx::errc::invalid_response, transmit_literal_ec.message().c_str());
    TEST_ASSERT_TRUE_MESSAGE(transmit_multi_ec == idfxx::errc::invalid_response, transmit_multi_ec.message().c_str());
    TEST_ASSERT_TRUE_MESSAGE(receive_into_ec == idfxx::errc::invalid_response, receive_into_ec.message().c_str());
    TEST_ASSERT_TRUE_MESSAGE(receive_fixed_ec == idfxx::errc::invalid_response, receive_fixed_ec.message().c_str());
    TEST_ASSERT_TRUE_MESSAGE(read_into_ec == idfxx::errc::invalid_response, read_into_ec.message().c_str());
    TEST_ASSERT_TRUE_MESSAGE(read_fixed_ec == idfxx::errc::invalid_response, read_fixed_ec.message().c_str());
    TEST_ASSERT_TRUE_MESSAGE(write_literal_ec == idfxx::errc::invalid_response, write_literal_ec.message().c_str());
    TEST_ASSERT_TRUE_MESSAGE(multi_literal_ec == idfxx::errc::invalid_response, multi_literal_ec.message().c_str());
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
                    read_ec = error_of(device.try_read_register(0x10, buffer, std::chrono::milliseconds(10)));
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
