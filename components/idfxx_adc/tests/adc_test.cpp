// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Unit tests for idfxx_adc
// Reads need real analog hardware; these cover the API surface and the
// no-hardware error paths. esp_adc's boot-time calibration hangs under QEMU, so
// idfxx_adc is kept out of the QEMU test image (see tests/CMakeLists.txt) and
// these run on hardware.

#include "idfxx/adc"
#include "sdkconfig.h"
#include "unity.h"

#include <type_traits>

using namespace idfxx::adc;

// =============================================================================
// Compile-time tests (static_assert)
// =============================================================================

// input is move-only.
static_assert(!std::is_copy_constructible_v<input>);
static_assert(!std::is_copy_assignable_v<input>);
static_assert(std::is_move_constructible_v<input>);
static_assert(std::is_move_assignable_v<input>);

// config is default-constructible (every field has a default).
static_assert(std::is_default_constructible_v<input::config>);

// =============================================================================
// Runtime tests
// =============================================================================

TEST_CASE("adc config defaults", "[idfxx][adc]") {
    input::config cfg{};
    TEST_ASSERT_FALSE(cfg.pin.is_connected());
    TEST_ASSERT_EQUAL(attenuation::db_12, cfg.attenuation);
}

TEST_CASE("adc make rejects an unconnected pin", "[idfxx][adc]") {
    auto r = input::make({});
    TEST_ASSERT_FALSE(r.has_value());
    TEST_ASSERT_TRUE(r.error() == idfxx::errc::invalid_arg);
}

TEST_CASE("adc make rejects a non-ADC pin", "[idfxx][adc]") {
    // A pin that exists but carries no ADC channel on the target under test.
    // Most targets place ADC on the low GPIOs, leaving GPIO 21 free; the
    // ESP32-P4 maps ADC1 onto GPIO 16-23, so pick a pin outside that range.
#if CONFIG_IDF_TARGET_ESP32P4
    constexpr auto non_adc_pin = idfxx::gpio_24;
#else
    constexpr auto non_adc_pin = idfxx::gpio_21;
#endif
    auto r = input::make({.pin = non_adc_pin});
    TEST_ASSERT_FALSE(r.has_value());
    TEST_ASSERT_TRUE(r.error() == idfxx::errc::invalid_arg);
}
