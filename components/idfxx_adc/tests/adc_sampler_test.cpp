// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Unit tests for idfxx::adc::sampler
// Continuous conversion needs real analog hardware; these cover the API
// surface, the no-hardware error paths, and a lifecycle pass on a floating
// ADC1 pin. esp_adc's boot-time calibration hangs under QEMU, so idfxx_adc
// is kept out of the QEMU test image (see tests/CMakeLists.txt) and these
// run on hardware.

#include "idfxx/adc"
#include "sdkconfig.h"
#include "unity.h"

#include <array>
#include <chrono>
#include <electro/electro>
#include <type_traits>

using namespace idfxx::adc;
using namespace std::chrono_literals;

// A floating ADC1-capable pin on the target under test. Avoid GPIO 19/20
// (USB D-/D+) and GPIO 26-32 (SPI flash/PSRAM) on ESP32-S3.
#if CONFIG_IDF_TARGET_ESP32
constexpr auto test_pin = idfxx::gpio_36; // ADC1_CH0
#elif CONFIG_IDF_TARGET_ESP32P4
constexpr auto test_pin = idfxx::gpio_16; // ADC1 maps onto GPIO 16-23
#else
constexpr auto test_pin = idfxx::gpio_2;
#endif

// A pin that exists but carries no ADC channel on the target under test.
#if CONFIG_IDF_TARGET_ESP32P4
constexpr auto non_adc_pin = idfxx::gpio_24;
#else
constexpr auto non_adc_pin = idfxx::gpio_21;
#endif

// =============================================================================
// Compile-time tests (static_assert)
// =============================================================================

// sampler is move-only.
static_assert(!std::is_copy_constructible_v<sampler>);
static_assert(!std::is_copy_assignable_v<sampler>);
static_assert(std::is_move_constructible_v<sampler>);
static_assert(std::is_move_assignable_v<sampler>);

// config and sample are default-constructible (every field has a default).
static_assert(std::is_default_constructible_v<sampler::config>);
static_assert(std::is_default_constructible_v<sampler::sample>);

// =============================================================================
// Runtime tests
// =============================================================================

TEST_CASE("adc sampler config defaults", "[idfxx][adc]") {
    sampler::config cfg{};
    TEST_ASSERT_TRUE(cfg.pins.empty());
    TEST_ASSERT_EQUAL(attenuation::db_12, cfg.attenuation);
    TEST_ASSERT_EQUAL(20'000, cfg.sample_rate.count());
    TEST_ASSERT_EQUAL(256, cfg.frame_samples);
    TEST_ASSERT_EQUAL(1024, cfg.buffer_samples);
}

TEST_CASE("adc sampler make rejects an empty pin list", "[idfxx][adc]") {
    auto r = sampler::make({});
    TEST_ASSERT_FALSE(r.has_value());
    TEST_ASSERT_TRUE(r.error() == idfxx::errc::invalid_arg);
}

TEST_CASE("adc sampler make rejects an unconnected pin", "[idfxx][adc]") {
    auto r = sampler::make({.pins = {idfxx::gpio::nc()}});
    TEST_ASSERT_FALSE(r.has_value());
    TEST_ASSERT_TRUE(r.error() == idfxx::errc::invalid_arg);
}

TEST_CASE("adc sampler make rejects a non-ADC pin", "[idfxx][adc]") {
    auto r = sampler::make({.pins = {non_adc_pin}});
    TEST_ASSERT_FALSE(r.has_value());
    TEST_ASSERT_TRUE(r.error() == idfxx::errc::invalid_arg);
}

TEST_CASE("adc sampler make rejects duplicate pins", "[idfxx][adc]") {
    auto r = sampler::make({.pins = {test_pin, test_pin}});
    TEST_ASSERT_FALSE(r.has_value());
    TEST_ASSERT_TRUE(r.error() == idfxx::errc::invalid_arg);
}

TEST_CASE("adc sampler make rejects zero frame_samples", "[idfxx][adc]") {
    auto r = sampler::make({.pins = {test_pin}, .frame_samples = 0});
    TEST_ASSERT_FALSE(r.has_value());
    TEST_ASSERT_TRUE(r.error() == idfxx::errc::invalid_arg);
}

TEST_CASE("adc sampler make rejects buffer smaller than frame", "[idfxx][adc]") {
    auto r = sampler::make({.pins = {test_pin}, .frame_samples = 256, .buffer_samples = 128});
    TEST_ASSERT_FALSE(r.has_value());
    TEST_ASSERT_TRUE(r.error() == idfxx::errc::invalid_arg);
}

TEST_CASE("adc sampler make rejects a zero sample rate", "[idfxx][adc]") {
    auto r = sampler::make({.pins = {test_pin}, .sample_rate = freq::hertz{0}});
    TEST_ASSERT_FALSE(r.has_value());
    TEST_ASSERT_TRUE(r.error() == idfxx::errc::invalid_arg);
}

TEST_CASE("adc sampler lifecycle", "[idfxx][adc]") {
    auto s = sampler::make({.pins = {test_pin}});
    TEST_ASSERT_TRUE(s.has_value());

    TEST_ASSERT_EQUAL(1, s->pins().size());
    TEST_ASSERT_TRUE(s->pins()[0] == test_pin);
    TEST_ASSERT_EQUAL(20'000, s->sample_rate().count());
    TEST_ASSERT_FALSE(s->running());
    TEST_ASSERT_EQUAL(0, s->overruns());

    std::array<sampler::sample, 64> buf{};

    // Reading before start is an error.
    auto pre = s->try_read(buf, 10ms);
    TEST_ASSERT_FALSE(pre.has_value());
    TEST_ASSERT_TRUE(pre.error() == idfxx::errc::invalid_state);

    TEST_ASSERT_TRUE(s->try_start().has_value());
    TEST_ASSERT_TRUE(s->running());

    // Starting twice is an error.
    auto restart = s->try_start();
    TEST_ASSERT_FALSE(restart.has_value());
    TEST_ASSERT_TRUE(restart.error() == idfxx::errc::invalid_state);

    // A timed read returns at least one sample, tagged with the pin.
    auto n = s->try_read(buf, 1s);
    TEST_ASSERT_TRUE(n.has_value());
    TEST_ASSERT_GREATER_THAN(0, *n);
    TEST_ASSERT_LESS_OR_EQUAL(buf.size(), *n);
    for (size_t i = 0; i < *n; ++i) {
        TEST_ASSERT_TRUE(buf[i].pin == test_pin);
    }

    TEST_ASSERT_TRUE(s->try_stop().has_value());
    TEST_ASSERT_FALSE(s->running());

    // Stop is idempotent.
    TEST_ASSERT_TRUE(s->try_stop().has_value());
}

TEST_CASE("adc sampler to_voltage rejects an unknown pin", "[idfxx][adc]") {
    auto s = sampler::make({.pins = {test_pin}});
    TEST_ASSERT_TRUE(s.has_value());

    auto v = s->try_to_voltage({.pin = idfxx::gpio::nc(), .raw = 0});
    TEST_ASSERT_FALSE(v.has_value());
    TEST_ASSERT_TRUE(v.error() == idfxx::errc::invalid_arg);
}

TEST_CASE("adc sampler batch to_voltage rejects an undersized output", "[idfxx][adc]") {
    auto s = sampler::make({.pins = {test_pin}});
    TEST_ASSERT_TRUE(s.has_value());

    std::array<sampler::sample, 2> in{};
    std::array<electro::millivolts, 1> out{};
    auto r = s->try_to_voltage(in, out);
    TEST_ASSERT_FALSE(r.has_value());
    TEST_ASSERT_TRUE(r.error() == idfxx::errc::invalid_arg);
}

TEST_CASE("adc sampler batch to_voltage rejects an unknown pin", "[idfxx][adc]") {
    auto s = sampler::make({.pins = {test_pin}});
    TEST_ASSERT_TRUE(s.has_value());

    std::array<sampler::sample, 1> in{sampler::sample{.pin = idfxx::gpio::nc(), .raw = 0}};
    std::array<electro::millivolts, 1> out{};
    auto r = s->try_to_voltage(in, out);
    TEST_ASSERT_FALSE(r.has_value());
    TEST_ASSERT_TRUE(r.error() == idfxx::errc::invalid_arg);
}

TEST_CASE("adc sampler voltage read before start is an error", "[idfxx][adc]") {
    auto s = sampler::make({.pins = {test_pin}});
    TEST_ASSERT_TRUE(s.has_value());

    std::array<electro::millivolts, 64> buf{};
    auto pre = s->try_read(buf, 10ms);
    TEST_ASSERT_FALSE(pre.has_value());
    TEST_ASSERT_TRUE(pre.error() == idfxx::errc::invalid_state);
}

TEST_CASE("adc sampler reads calibrated voltages", "[idfxx][adc]") {
    auto s = sampler::make({.pins = {test_pin}});
    TEST_ASSERT_TRUE(s.has_value());
    TEST_ASSERT_TRUE(s->try_start().has_value());

    // The fused voltage read requires calibration; on chips without it the read
    // reports not_supported rather than fabricating a voltage.
    std::array<electro::millivolts, 64> buf{};
    auto n = s->try_read(buf, 1s);
    if (s->calibrated()) {
        TEST_ASSERT_TRUE(n.has_value());
        TEST_ASSERT_GREATER_THAN(0, *n);
        TEST_ASSERT_LESS_OR_EQUAL(buf.size(), *n);
    } else {
        TEST_ASSERT_FALSE(n.has_value());
        TEST_ASSERT_TRUE(n.error() == idfxx::errc::not_supported);
    }

    TEST_ASSERT_TRUE(s->try_stop().has_value());
}
