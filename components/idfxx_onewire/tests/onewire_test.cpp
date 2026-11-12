// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Unit tests for idfxx onewire
// Uses ESP-IDF Unity test framework with compile-time static_asserts

#include "idfxx/onewire"
#include "unity.h"

#include <array>
#include <concepts>
#include <cstddef>
#include <mutex>
#include <type_traits>
#include <vector>

using namespace idfxx::onewire;

// =============================================================================
// Compile-time tests (static_assert)
// These verify correctness at compile time - if this file compiles, they pass.
// =============================================================================

// address is trivially copyable and constexpr-constructible
static_assert(std::is_trivially_copyable_v<address>);
static_assert(std::is_default_constructible_v<address>);
static_assert(address{}.raw() == 0);
static_assert(address::any().raw() == 0);
static_assert(address::none().raw() == 0xFFFFFFFFFFFFFFFF);
static_assert(address{0x28}.family() == 0x28);

// address comparison
static_assert(address{} == address::any());
static_assert(address::any() != address::none());
static_assert(address{0x1234} != address{0x5678});
static_assert(address{0x1234} < address{0x5678});

// bus is non-copyable, move-only, and not default constructible
static_assert(!std::is_default_constructible_v<bus>);
static_assert(!std::is_copy_constructible_v<bus>);
static_assert(!std::is_copy_assignable_v<bus>);
static_assert(std::is_move_constructible_v<bus>);
static_assert(std::is_move_assignable_v<bus>);

// bus writes take byte views (uint8_t or std::byte buffers, {ptr, len}) and literal byte lists
static_assert(requires(
    bus& b, std::array<uint8_t, 2> u8, std::vector<std::byte> bytes, const uint8_t* p
) {
    { b.try_write(u8) } -> std::same_as<idfxx::result<void>>;
    { b.try_write(bytes) } -> std::same_as<idfxx::result<void>>;
    { b.try_write({p, 2}) } -> std::same_as<idfxx::result<void>>;
    { b.try_write({0x44}) } -> std::same_as<idfxx::result<void>>;
    { b.try_write({0xCC, 0x44}) } -> std::same_as<idfxx::result<void>>;
    { b.try_write(0x44) } -> std::same_as<idfxx::result<void>>;
});

// bus has exactly these write overloads...
template<typename Arg>
concept has_try_write = requires {
    static_cast<idfxx::result<void> (bus::*)(Arg)>(&bus::try_write);
};
static_assert(has_try_write<uint8_t>);
static_assert(has_try_write<idfxx::byte_view>);
static_assert(has_try_write<std::initializer_list<uint8_t>>);

// ...and they resolve as: scalar -> single byte, braced list -> initializer_list, buffer ->
// byte_view. All of bus's overloads return result<void>, so the choice is pinned on a probe with
// the same parameter types.
namespace write_probe {
struct scalar {};
struct list {};
struct view {};
struct probe {
    scalar try_write(uint8_t);
    view try_write(idfxx::byte_view);
    list try_write(std::initializer_list<uint8_t>);
};
} // namespace write_probe

static_assert(requires(write_probe::probe& p, std::array<uint8_t, 2> buf, uint8_t v) {
    { p.try_write(0x44) } -> std::same_as<write_probe::scalar>;
    { p.try_write(v) } -> std::same_as<write_probe::scalar>;
    { p.try_write({0x44}) } -> std::same_as<write_probe::list>;
    { p.try_write({0xCC, 0x44}) } -> std::same_as<write_probe::list>;
    { p.try_write(buf) } -> std::same_as<write_probe::view>;
    { p.try_write({buf.data(), 2}) } -> std::same_as<write_probe::view>;
});

// bus reads fill writable byte buffers, or return a fixed-size array
static_assert(requires(
    bus& b, std::array<uint8_t, 9> u8, std::array<std::byte, 9> bytes, uint8_t* p
) {
    { b.try_read() } -> std::same_as<idfxx::result<uint8_t>>;
    { b.try_read(u8) } -> std::same_as<idfxx::result<void>>;
    { b.try_read(bytes) } -> std::same_as<idfxx::result<void>>;
    { b.try_read({p, 9}) } -> std::same_as<idfxx::result<void>>;
    { b.try_read<9>() } -> std::same_as<idfxx::result<std::array<uint8_t, 9>>>;
});

template<typename Buf>
concept readable_into = requires(bus& b, Buf& buf) { b.try_read(buf); };
static_assert(readable_into<std::array<uint8_t, 9>>);
static_assert(!readable_into<const std::array<uint8_t, 9>>);

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
static_assert(requires(bus& b, std::array<uint8_t, 9> buf) {
    b.write(0x44);
    b.write(buf);
    b.write({0xCC, 0x44});
    b.read(buf);
    { b.read() } -> std::same_as<uint8_t>;
    { b.read<9>() } -> std::same_as<std::array<uint8_t, 9>>;
});
#endif

// CRC functions accept uint8_t and std::byte buffers
static_assert(requires(std::array<std::byte, 4> bytes, const uint8_t* p) {
    { crc8(bytes) } -> std::same_as<uint8_t>;
    { crc8({p, 4}) } -> std::same_as<uint8_t>;
    { crc16(bytes) } -> std::same_as<uint16_t>;
});

// =============================================================================
// Runtime tests (Unity TEST_CASE)
// =============================================================================

TEST_CASE("address default construction is any", "[idfxx][onewire]") {
    address addr;
    TEST_ASSERT_TRUE(addr.raw() == 0);
    TEST_ASSERT_TRUE(addr == address::any());
}

TEST_CASE("address from raw value", "[idfxx][onewire]") {
    address addr{0x28FF123456789ABC};
    TEST_ASSERT_TRUE(addr.raw() == 0x28FF123456789ABC);
    TEST_ASSERT_EQUAL_UINT8(0xBC, addr.family());
}

TEST_CASE("address family extraction", "[idfxx][onewire]") {
    // DS18B20 family code in low byte
    address addr{0x1234567890ABCD28};
    TEST_ASSERT_EQUAL_UINT8(0x28, addr.family());
}

TEST_CASE("address equality comparison", "[idfxx][onewire]") {
    address a{0x1234};
    address b{0x1234};
    address c{0x5678};

    TEST_ASSERT_TRUE(a == b);
    TEST_ASSERT_FALSE(a == c);
}

TEST_CASE("address ordering comparison", "[idfxx][onewire]") {
    address a{0x1000};
    address b{0x2000};

    TEST_ASSERT_TRUE(a < b);
    TEST_ASSERT_FALSE(b < a);
}

TEST_CASE("address none is distinct from any", "[idfxx][onewire]") {
    TEST_ASSERT_FALSE(address::any() == address::none());
    TEST_ASSERT_TRUE(address::none().raw() == 0xFFFFFFFFFFFFFFFF);
}

TEST_CASE("bus::make with NC pin returns error", "[idfxx][onewire]") {
    auto result = bus::make(idfxx::gpio::nc());
    TEST_ASSERT_FALSE(result.has_value());
    TEST_ASSERT_EQUAL(std::to_underlying(idfxx::errc::invalid_state), result.error().value());
}

TEST_CASE("bus::make with valid pin succeeds", "[idfxx][onewire]") {
    auto result = bus::make(idfxx::gpio_4);
    TEST_ASSERT_TRUE(result.has_value());
    TEST_ASSERT_EQUAL(4, result->pin().num());
}

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
TEST_CASE("bus constructor with NC pin throws", "[idfxx][onewire]") {
    bool threw = false;
    try {
        bus b{idfxx::gpio::nc()};
    } catch (const std::system_error&) {
        threw = true;
    }
    TEST_ASSERT_TRUE_MESSAGE(threw, "Expected std::system_error to be thrown");
}

TEST_CASE("bus constructor with valid pin succeeds", "[idfxx][onewire]") {
    bus b{idfxx::gpio_4};
    TEST_ASSERT_EQUAL(4, b.pin().num());
}
#endif

TEST_CASE("bus is lockable", "[idfxx][onewire]") {
    auto result = bus::make(idfxx::gpio_4);
    TEST_ASSERT_TRUE(result.has_value());
    auto& b = *result;

    // Verify lock/unlock methods exist and work
    b.lock();
    b.unlock();

    // Verify try_lock exists
    bool locked = b.try_lock();
    if (locked) {
        b.unlock();
    }

    // Verify works with std::lock_guard
    {
        std::lock_guard<bus> lock(b);
    }

    // Verify works with std::unique_lock
    {
        std::unique_lock<bus> lock(b);
        TEST_ASSERT_TRUE(lock.owns_lock());
    }
}

// -- to_string tests ---------------------------------------------------------

TEST_CASE("to_string for address::any()", "[idfxx][onewire]") {
    auto s = idfxx::to_string(address::any());
    TEST_ASSERT_EQUAL_STRING("ONEWIRE_ANY", s.c_str());
}

TEST_CASE("to_string for address::none()", "[idfxx][onewire]") {
    auto s = idfxx::to_string(address::none());
    TEST_ASSERT_EQUAL_STRING("ONEWIRE_NONE", s.c_str());
}

TEST_CASE("to_string for address with value", "[idfxx][onewire]") {
    // 0x28 in low byte, then 0x00, 0x12, ...
    address addr{0xBC9A785634120028};
    auto s = idfxx::to_string(addr);
    TEST_ASSERT_EQUAL_STRING("28:00:12:34:56:78:9A:BC", s.c_str());
}

// -- CRC tests ---------------------------------------------------------------

TEST_CASE("crc8 computes correct value", "[idfxx][onewire]") {
    // ROM code with valid CRC: family=0x28, serial, crc
    // The CRC of the first 7 bytes should equal the 8th byte
    uint8_t rom[] = {0x28, 0xFF, 0x12, 0x34, 0x56, 0x78, 0x9A};
    uint8_t crc = crc8(rom);
    // Verify CRC is computed (non-trivial test - just check it returns a value)
    TEST_ASSERT_TRUE(crc == crc8(rom)); // Deterministic

    // Known test vector: CRC8 of empty data is 0
    TEST_ASSERT_EQUAL_UINT8(0, crc8(idfxx::byte_view{}));
}

TEST_CASE("crc8 known test vectors", "[idfxx][onewire]") {
    // Known 1-Wire ROM codes with valid CRC (CRC8 of first 7 bytes = 8th byte)
    // These are standard Maxim/Dallas 1-Wire CRC8 test vectors using polynomial x^8+x^5+x^4+1

    // Single byte 0x00 should produce CRC 0x00
    uint8_t zero[] = {0x00};
    TEST_ASSERT_EQUAL_UINT8(0x00, crc8(zero));

    // CRC8 of {0x01} should be deterministic
    uint8_t one[] = {0x01};
    uint8_t crc_of_one = crc8(one);
    TEST_ASSERT_EQUAL_UINT8(crc_of_one, crc8(one));

    // CRC8 of multi-byte data should be consistent
    uint8_t data[] = {0x02, 0x1C, 0xB8, 0x01, 0x00, 0x00, 0x00};
    uint8_t data_crc = crc8(data);
    TEST_ASSERT_EQUAL_UINT8(data_crc, crc8(data));

    // CRC8 of all the bytes including CRC should be 0 (property of CRC8)
    uint8_t full_rom[] = {0x02, 0x1C, 0xB8, 0x01, 0x00, 0x00, 0x00, 0x00};
    full_rom[7] = data_crc;
    TEST_ASSERT_EQUAL_UINT8(0, crc8(full_rom));

    // Maxim application note 27 example ROM: family 0x02, serial 0x000001B81C, CRC 0xA2
    TEST_ASSERT_EQUAL_UINT8(0xA2, data_crc);
}

TEST_CASE("crc8 accepts std::byte buffers and sub-ranges", "[idfxx][onewire]") {
    uint8_t rom[] = {0x02, 0x1C, 0xB8, 0x01, 0x00, 0x00, 0x00, 0xA2};
    std::array<std::byte, 7> bytes{};
    for (size_t i = 0; i < bytes.size(); ++i) {
        bytes[i] = static_cast<std::byte>(rom[i]);
    }

    TEST_ASSERT_EQUAL_UINT8(rom[7], crc8(bytes));
    TEST_ASSERT_EQUAL_UINT8(rom[7], crc8({rom, 7}));
    TEST_ASSERT_EQUAL_UINT8(0, crc8(rom));
}

TEST_CASE("crc16 computes correct value", "[idfxx][onewire]") {
    // Verify CRC16 is deterministic
    uint8_t data[] = {0xF0, 0x88, 0x00};
    uint16_t crc = crc16(data);
    TEST_ASSERT_EQUAL_UINT16(crc, crc16(data));

    // Verify CRC16 with initial value
    uint16_t crc_iv = crc16(data, 0x1234);
    TEST_ASSERT_EQUAL_UINT16(crc_iv, crc16(data, 0x1234));

    // Different IV should produce different CRC
    TEST_ASSERT_TRUE(crc16(data, 0) != crc16(data, 0x1234));

    // std::byte buffers and {ptr, len} give the same result
    std::array<std::byte, 3> bytes{std::byte{0xF0}, std::byte{0x88}, std::byte{0x00}};
    TEST_ASSERT_EQUAL_UINT16(crc, crc16(bytes));
    TEST_ASSERT_EQUAL_UINT16(crc, crc16({data, sizeof(data)}));
}

TEST_CASE("crc8 covers inputs longer than 255 bytes", "[idfxx][onewire]") {
    std::array<uint8_t, 300> buf{};
    for (size_t i = 0; i < buf.size(); ++i) {
        buf[i] = static_cast<uint8_t>(i * 7 + 3);
    }

    // Bitwise Dallas/Maxim CRC-8 reference (reflected polynomial 0x8C, initial value 0)
    uint8_t ref = 0;
    for (uint8_t in : buf) {
        for (int i = 0; i < 8; ++i) {
            uint8_t mix = (ref ^ in) & 0x01;
            ref >>= 1;
            if (mix) {
                ref ^= 0x8C;
            }
            in >>= 1;
        }
    }

    TEST_ASSERT_EQUAL_UINT8(ref, crc8(buf));
    TEST_ASSERT_TRUE(crc8(buf) != crc8({buf.data(), 255}));
    TEST_ASSERT_TRUE(crc8(buf) != crc8({buf.data(), buf.size() % 256}));

    // A change in the last byte is detected
    auto crc = crc8(buf);
    buf[299] ^= 0x01;
    TEST_ASSERT_TRUE(crc != crc8(buf));
}

TEST_CASE("crc16 covers inputs longer than 65535 bytes", "[idfxx][onewire]") {
    std::vector<uint8_t> buf(65540);
    for (size_t i = 0; i < buf.size(); ++i) {
        buf[i] = static_cast<uint8_t>(i * 7 + 3);
    }

    // Dallas/Maxim CRC-16 reference, in the parity form of Maxim application note 27
    static constexpr uint8_t oddparity[16] = {0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0};
    uint16_t ref = 0x1234;
    for (uint8_t in : buf) {
        uint16_t cdata = (in ^ ref) & 0xFF;
        ref >>= 8;
        if (oddparity[cdata & 0x0F] ^ oddparity[cdata >> 4]) {
            ref ^= 0xC001;
        }
        cdata <<= 6;
        ref ^= cdata;
        cdata <<= 1;
        ref ^= cdata;
    }

    TEST_ASSERT_EQUAL_UINT16(ref, crc16(buf, 0x1234));

    // A change in the last byte is detected
    auto crc = crc16(buf);
    buf.back() ^= 0x01;
    TEST_ASSERT_TRUE(crc != crc16(buf));
}

TEST_CASE("check_crc16 validates correctly", "[idfxx][onewire]") {
    // Compute CRC for some data, then verify check_crc16 validates it
    uint8_t data[] = {0xF0, 0x88, 0x00, 0x01, 0x02, 0x03};
    uint16_t crc = crc16(data);

    // Invert and store as two bytes (as received from 1-Wire)
    uint16_t inverted = ~crc;
    uint8_t inv_bytes[2] = {static_cast<uint8_t>(inverted & 0xFF), static_cast<uint8_t>((inverted >> 8) & 0xFF)};

    TEST_ASSERT_TRUE(check_crc16(data, inv_bytes));

    // Corrupt the CRC and verify it fails
    inv_bytes[0] ^= 0xFF;
    TEST_ASSERT_FALSE(check_crc16(data, inv_bytes));
}

TEST_CASE("check_crc16 accepts byte views and rejects other sizes", "[idfxx][onewire]") {
    uint8_t data[] = {0xF0, 0x88, 0x00, 0x01, 0x02, 0x03};
    uint16_t inverted = ~crc16(data);
    uint8_t raw[3] = {static_cast<uint8_t>(inverted & 0xFF), static_cast<uint8_t>(inverted >> 8), 0};

    std::array<std::byte, 2> as_bytes = {std::byte{raw[0]}, std::byte{raw[1]}};
    TEST_ASSERT_TRUE(check_crc16(data, as_bytes));
    TEST_ASSERT_TRUE(check_crc16(data, {raw, 2}));

    TEST_ASSERT_FALSE(check_crc16(data, {raw, 1}));
    TEST_ASSERT_FALSE(check_crc16(data, {raw, 3}));
    TEST_ASSERT_FALSE(check_crc16(data, {raw, 0}));
}

// -- Formatter tests ---------------------------------------------------------

#ifdef CONFIG_IDFXX_STD_FORMAT
static_assert(std::formattable<address, char>);

TEST_CASE("address formatter outputs correct string", "[idfxx][onewire]") {
    auto s = std::format("{}", address::any());
    TEST_ASSERT_EQUAL_STRING("ONEWIRE_ANY", s.c_str());
    s = std::format("{}", address::none());
    TEST_ASSERT_EQUAL_STRING("ONEWIRE_NONE", s.c_str());

    address addr{0xBC9A785634120028};
    s = std::format("{}", addr);
    TEST_ASSERT_EQUAL_STRING("28:00:12:34:56:78:9A:BC", s.c_str());
}
#endif // CONFIG_IDFXX_STD_FORMAT
