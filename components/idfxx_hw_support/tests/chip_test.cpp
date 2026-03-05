// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Unit tests for idfxx chip info types
// Uses ESP-IDF Unity test framework with compile-time static_asserts

#include "idfxx/chip.hpp"
#include "unity.h"

#include <esp_chip_info.h>
#include <type_traits>

using namespace idfxx;

// =============================================================================
// Compile-time tests (static_assert)
// These verify correctness at compile time - if this file compiles, they pass.
// =============================================================================

// chip_model
static_assert(std::is_enum_v<chip_model>);
static_assert(std::is_same_v<std::underlying_type_t<chip_model>, int>);

static_assert(static_cast<int>(chip_model::esp32) == CHIP_ESP32);
static_assert(static_cast<int>(chip_model::esp32s2) == CHIP_ESP32S2);
static_assert(static_cast<int>(chip_model::esp32s3) == CHIP_ESP32S3);
static_assert(static_cast<int>(chip_model::esp32c3) == CHIP_ESP32C3);
static_assert(static_cast<int>(chip_model::esp32c2) == CHIP_ESP32C2);
static_assert(static_cast<int>(chip_model::esp32c6) == CHIP_ESP32C6);
static_assert(static_cast<int>(chip_model::esp32h2) == CHIP_ESP32H2);
static_assert(static_cast<int>(chip_model::esp32p4) == CHIP_ESP32P4);
static_assert(static_cast<int>(chip_model::esp32c5) == CHIP_ESP32C5);
static_assert(static_cast<int>(chip_model::esp32c61) == CHIP_ESP32C61);

// chip_feature
static_assert(std::is_enum_v<chip_feature>);
static_assert(std::is_same_v<std::underlying_type_t<chip_feature>, uint32_t>);
static_assert(enable_flags_operators<chip_feature>);
static_assert(flag_enum<chip_feature>);

static_assert(std::to_underlying(chip_feature::embedded_flash) == CHIP_FEATURE_EMB_FLASH);
static_assert(std::to_underlying(chip_feature::wifi) == CHIP_FEATURE_WIFI_BGN);
static_assert(std::to_underlying(chip_feature::ble) == CHIP_FEATURE_BLE);
static_assert(std::to_underlying(chip_feature::bt_classic) == CHIP_FEATURE_BT);
static_assert(std::to_underlying(chip_feature::ieee802154) == CHIP_FEATURE_IEEE802154);
static_assert(std::to_underlying(chip_feature::embedded_psram) == CHIP_FEATURE_EMB_PSRAM);

// Flags can be combined at compile time
static_assert((chip_feature::wifi | chip_feature::ble).contains(chip_feature::wifi));
static_assert((chip_feature::wifi | chip_feature::ble).contains(chip_feature::ble));

// =============================================================================
// Runtime tests (Unity TEST_CASE)
// =============================================================================

TEST_CASE("chip_info::get returns valid model", "[idfxx][hw_support][chip]") {
    auto info = chip_info::get();
    auto model = info.model();
    // Model should be a known value (positive integer)
    TEST_ASSERT_GREATER_THAN(0, static_cast<int>(model));
}

TEST_CASE("chip_info::get returns at least 1 core", "[idfxx][hw_support][chip]") {
    auto info = chip_info::get();
    TEST_ASSERT_GREATER_OR_EQUAL(1, info.cores());
}

TEST_CASE("chip_info::get revision is consistent", "[idfxx][hw_support][chip]") {
    auto info = chip_info::get();
    TEST_ASSERT_EQUAL(info.revision(), info.major_revision() * 100 + info.minor_revision());
}

TEST_CASE("to_string(chip_model) returns non-empty", "[idfxx][hw_support][chip]") {
    auto info = chip_info::get();
    auto s = to_string(info.model());
    TEST_ASSERT_FALSE(s.empty());
}

TEST_CASE("to_string(chip_model) handles unknown values", "[idfxx][hw_support][chip]") {
    auto unknown = static_cast<chip_model>(999);
    TEST_ASSERT_EQUAL_STRING("unknown(999)", to_string(unknown).c_str());
}

#ifdef CONFIG_IDFXX_STD_FORMAT
static_assert(std::formattable<chip_model, char>);

TEST_CASE("chip_model formatter", "[idfxx][hw_support][chip]") {
    auto info = chip_info::get();
    auto s = std::format("{}", info.model());
    TEST_ASSERT_FALSE(s.empty());
}
#endif // CONFIG_IDFXX_STD_FORMAT
