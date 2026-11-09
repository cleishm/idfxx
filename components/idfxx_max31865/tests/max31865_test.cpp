// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Unit tests for idfxx max31865
// Uses ESP-IDF Unity test framework with compile-time static_asserts

#include "idfxx/max31865"
#include "unity.h"

#include <idfxx/gpio>
#include <idfxx/spi/master>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <type_traits>
#include <utility>

using namespace idfxx::max31865;
using namespace quanta::electro_literals;
using namespace quanta::frequency_literals;
using namespace quanta::thermo_literals;
using namespace std::chrono_literals;
using quanta::electro::milliohms;
using quanta::thermo::millicelsius;

namespace {

// Scales a value to an integer count of millionths, so comparisons run on
// Unity's int32 assertions.
int32_t micro(double v) {
    return static_cast<int32_t>(std::llround(v * 1e6));
}

constexpr cvd_coefficients iec60751{};

} // namespace

// =============================================================================
// Compile-time tests (static_assert)
// These verify correctness at compile time - if this file compiles, they pass.
// =============================================================================

// device is move-only
static_assert(!std::is_copy_constructible_v<device>);
static_assert(!std::is_copy_assignable_v<device>);
static_assert(std::is_move_constructible_v<device>);
static_assert(std::is_move_assignable_v<device>);
static_assert(!std::is_default_constructible_v<device>);

// config has required fields, so it cannot be default-constructed
static_assert(std::is_aggregate_v<device::config>);
static_assert(!std::is_default_constructible_v<device::config>);

// fault enum values match the Fault Status register bits
static_assert(std::to_underlying(fault::high_threshold) == 0x80);
static_assert(std::to_underlying(fault::low_threshold) == 0x40);
static_assert(std::to_underlying(fault::refin_high) == 0x20);
static_assert(std::to_underlying(fault::refin_low) == 0x10);
static_assert(std::to_underlying(fault::rtdin_low) == 0x08);
static_assert(std::to_underlying(fault::over_under_voltage) == 0x04);
static_assert(std::is_same_v<std::underlying_type_t<fault>, uint8_t>);

// IEC 60751 default coefficients
static_assert(iec60751.a == 3.9083e-3);
static_assert(iec60751.b == -5.775e-7);
static_assert(iec60751.c == -4.183e-12);

// Register addresses
static_assert(detail::reg_config == 0x00);
static_assert(detail::reg_rtd_msb == 0x01);
static_assert(detail::reg_rtd_lsb == 0x02);
static_assert(detail::reg_high_threshold_msb == 0x03);
static_assert(detail::reg_high_threshold_lsb == 0x04);
static_assert(detail::reg_low_threshold_msb == 0x05);
static_assert(detail::reg_low_threshold_lsb == 0x06);
static_assert(detail::reg_fault_status == 0x07);
static_assert((detail::reg_config | detail::write_bit) == 0x80);
static_assert((detail::reg_high_threshold_msb | detail::write_bit) == 0x83);

// Configuration register bits
static_assert(detail::cfg_bias == 0x80);
static_assert(detail::cfg_continuous == 0x40);
static_assert(detail::cfg_one_shot == 0x20);
static_assert(detail::cfg_three_wire == 0x10);
static_assert(detail::cfg_fault_cycle_mask == 0x0C);
static_assert(detail::cfg_fault_auto == 0x04);
static_assert(detail::cfg_fault_manual_1 == 0x08);
static_assert(detail::cfg_fault_manual_2 == 0x0C);
static_assert(detail::cfg_fault_clear == 0x02);
static_assert(detail::cfg_notch_50hz == 0x01);
static_assert(detail::cfg_self_clearing == 0x2E);
static_assert(detail::fault_mask == 0xFC);

// Automatic fault cycle write is 100X010Xb; manual steps are 100X100Xb and 100X110Xb
static_assert((detail::cfg_bias | detail::cfg_fault_auto) == 0b1000'0100);
static_assert((detail::cfg_bias | detail::cfg_fault_manual_1) == 0b1000'1000);
static_assert((detail::cfg_bias | detail::cfg_fault_manual_2) == 0b1000'1100);

// Codes and thresholds
static_assert(detail::max_code == 0x7FFF);
static_assert(detail::por_low_threshold == 0);
static_assert(detail::por_high_threshold == 0x7FFF);

// Timing
static_assert(detail::max_clock_speed == 5_MHz);
static_assert(detail::max_conversion_time(notch_filter::hz_60) == 55ms);
static_assert(detail::max_conversion_time(notch_filter::hz_50) == 66ms);
static_assert(detail::bias_settle_time(100us) == 2050us);
static_assert(detail::bias_settle_time(0us) == 1ms);
static_assert(detail::auto_fault_cycle_time == 600us);
static_assert(detail::auto_fault_cycle_max_tau == 100us);

// CS setup: ceil(400 ns x f) + 1 SPI clocks
static_assert(detail::cs_pretrans_cycles(5_MHz) == 3);
static_assert(detail::cs_pretrans_cycles(4_MHz) == 3);
static_assert(detail::cs_pretrans_cycles(2500_kHz) == 2);
static_assert(detail::cs_pretrans_cycles(1_MHz) == 2);
static_assert(detail::cs_pretrans_cycles(100_kHz) == 2);

// =============================================================================
// Runtime tests (Unity TEST_CASE)
// =============================================================================

TEST_CASE("max31865 config designated initialization", "[idfxx][max31865]") {
    device::config cfg{
        .cs = idfxx::gpio_10,
        .r0 = 100_Ohm,
        .rref = 430_Ohm,
        .wiring = wiring::four_wire,
        .notch = notch_filter::hz_60,
        .filter_time_constant = 1ms,
    };
    TEST_ASSERT_EQUAL_INT32(100'000, static_cast<int32_t>(cfg.r0->count()));
    TEST_ASSERT_EQUAL_INT32(430'000, static_cast<int32_t>(cfg.rref->count()));
    TEST_ASSERT_TRUE(*cfg.wiring == wiring::four_wire);
    TEST_ASSERT_TRUE(*cfg.notch == notch_filter::hz_60);
    TEST_ASSERT_EQUAL_INT32(1000, static_cast<int32_t>(cfg.filter_time_constant->count()));
    TEST_ASSERT_EQUAL_INT32(5'000'000, static_cast<int32_t>(cfg.clock_speed.count()));
    TEST_ASSERT_FALSE(cfg.drdy.is_connected());
}

TEST_CASE("max31865 cs_pretrans_cycles", "[idfxx][max31865]") {
    TEST_ASSERT_EQUAL_UINT16(3, detail::cs_pretrans_cycles(5_MHz));
    TEST_ASSERT_EQUAL_UINT16(2, detail::cs_pretrans_cycles(1_MHz));
}

// =============================================================================
// make() argument validation (no SPI traffic)
// =============================================================================

namespace {

idfxx::spi::master_bus make_test_bus() {
    idfxx::spi::bus_config cfg{};
    cfg.mosi = idfxx::gpio_11;
    cfg.miso = idfxx::gpio_13;
    cfg.sclk = idfxx::gpio_12;
    auto r = idfxx::spi::master_bus::make(idfxx::spi::host_device::spi2, idfxx::spi::dma_chan::ch_auto, cfg);
    TEST_ASSERT_TRUE(r.has_value());
    return std::move(*r);
}

device::config valid_config() {
    return {
        .cs = idfxx::gpio_10,
        .r0 = 1000_Ohm,
        .rref = 4300_Ohm,
        .wiring = wiring::three_wire,
        .notch = notch_filter::hz_50,
        .filter_time_constant = 100us,
    };
}

} // namespace

TEST_CASE("max31865 make with nc cs fails with invalid_arg", "[idfxx][max31865]") {
    auto bus = make_test_bus();
    auto cfg = valid_config();
    cfg.cs = idfxx::gpio::nc();
    auto r = device::make(bus, cfg);
    TEST_ASSERT_FALSE(r.has_value());
    TEST_ASSERT_TRUE(r.error() == idfxx::errc::invalid_arg);
}

TEST_CASE("max31865 make with clock above 5 MHz fails with invalid_arg", "[idfxx][max31865]") {
    auto bus = make_test_bus();
    auto cfg = valid_config();
    cfg.clock_speed = quanta::frequency::hertz{5'000'001};
    auto r = device::make(bus, cfg);
    TEST_ASSERT_FALSE(r.has_value());
    TEST_ASSERT_TRUE(r.error() == idfxx::errc::invalid_arg);
}

TEST_CASE("max31865 make with zero clock fails with invalid_arg", "[idfxx][max31865]") {
    auto bus = make_test_bus();
    auto cfg = valid_config();
    cfg.clock_speed = 0_Hz;
    auto r = device::make(bus, cfg);
    TEST_ASSERT_FALSE(r.has_value());
    TEST_ASSERT_TRUE(r.error() == idfxx::errc::invalid_arg);
}

TEST_CASE("max31865 make with non-positive resistances fails with invalid_arg", "[idfxx][max31865]") {
    auto bus = make_test_bus();
    auto cfg = valid_config();
    cfg.rref = 0_Ohm;
    auto r = device::make(bus, cfg);
    TEST_ASSERT_FALSE(r.has_value());
    TEST_ASSERT_TRUE(r.error() == idfxx::errc::invalid_arg);

    cfg = valid_config();
    cfg.r0 = 0_Ohm;
    r = device::make(bus, cfg);
    TEST_ASSERT_FALSE(r.has_value());
    TEST_ASSERT_TRUE(r.error() == idfxx::errc::invalid_arg);
}

// =============================================================================
// Callendar–Van Dusen math
// =============================================================================

namespace {

struct table_point {
    double celsius;
    double ohms;
};

// IEC 60751 PT100 table values
constexpr table_point pt100_table[] = {
    {-200.0, 18.5201},
    {-100.0, 60.2558},
    {0.0, 100.000},
    {100.0, 138.5055},
    {200.0, 175.8560},
    {850.0, 390.4811},
};

} // namespace

TEST_CASE("max31865 CVD resistance matches the IEC 60751 PT100 table", "[idfxx][max31865]") {
    for (const auto& p : pt100_table) {
        double r = detail::cvd_resistance(p.celsius, 100.0, iec60751);
        TEST_ASSERT_INT32_WITHIN(1000, micro(p.ohms), micro(r)); // 1 mOhm
    }
}

TEST_CASE("max31865 CVD temperature matches the IEC 60751 PT100 table", "[idfxx][max31865]") {
    for (const auto& p : pt100_table) {
        double t = detail::cvd_temperature(p.ohms, 100.0, iec60751);
        TEST_ASSERT_INT32_WITHIN(1000, micro(p.celsius), micro(t)); // 1 m°C
    }
}

TEST_CASE("max31865 CVD inverse is within 1 m°C of the forward equation", "[idfxx][max31865]") {
    for (int tenths = -2000; tenths <= 8500; tenths += 5) {
        double t = tenths / 10.0;
        double r = detail::cvd_resistance(t, 100.0, iec60751);
        double back = detail::cvd_temperature(r, 100.0, iec60751);
        TEST_ASSERT_INT32_WITHIN(1000, micro(t), micro(back));
    }
}

TEST_CASE("max31865 PT1000 codes at RREF 4020", "[idfxx][max31865]") {
    const auto rref = 4020_Ohm;
    const auto r0 = 1000_Ohm;

    // Within one LSB of quantization (about 0.15 °C)
    millicelsius t0 = detail::code_to_temperature(8151, rref, r0, iec60751);
    TEST_ASSERT_INT32_WITHIN(150, 0, static_cast<int32_t>(t0.count()));
    millicelsius t150 = detail::code_to_temperature(12824, rref, r0, iec60751);
    TEST_ASSERT_INT32_WITHIN(150, 150'000, static_cast<int32_t>(t150.count()));

    TEST_ASSERT_EQUAL_UINT16(8151, detail::temperature_to_code(0_c, rref, r0, iec60751));
    TEST_ASSERT_EQUAL_UINT16(12824, detail::temperature_to_code(150_c, rref, r0, iec60751));
}

TEST_CASE("max31865 code to temperature to code round-trips within 1 LSB", "[idfxx][max31865]") {
    const auto rref = 430_Ohm;
    const auto r0 = 100_Ohm;
    for (int code = 0; code <= 0x7FFF; code += 7) {
        millicelsius t = detail::code_to_temperature(static_cast<uint16_t>(code), rref, r0, iec60751);
        uint16_t back = detail::temperature_to_code(t, rref, r0, iec60751);
        TEST_ASSERT_INT32_WITHIN(1, code, back);
    }
}

TEST_CASE("max31865 code to resistance", "[idfxx][max31865]") {
    const auto rref = 4020_Ohm;
    TEST_ASSERT_EQUAL_INT32(0, static_cast<int32_t>(detail::code_to_resistance(0, rref).count()));
    TEST_ASSERT_EQUAL_INT32(2'010'000, static_cast<int32_t>(detail::code_to_resistance(16384, rref).count()));
    TEST_ASSERT_EQUAL_INT32(4'019'877, static_cast<int32_t>(detail::code_to_resistance(32767, rref).count()));
    TEST_ASSERT_EQUAL_UINT16(16384, detail::resistance_to_code(2010_Ohm, rref));
}

TEST_CASE("max31865 to_code saturates", "[idfxx][max31865]") {
    const auto rref = 4020_Ohm;
    const auto r0 = 1000_Ohm;

    TEST_ASSERT_EQUAL_UINT16(0, detail::resistance_to_code(milliohms{-1}, rref));
    TEST_ASSERT_EQUAL_UINT16(0, detail::resistance_to_code(0_Ohm, rref));
    TEST_ASSERT_EQUAL_UINT16(0x7FFF, detail::resistance_to_code(rref - milliohms{1}, rref));
    TEST_ASSERT_EQUAL_UINT16(0x7FFF, detail::resistance_to_code(rref, rref));
    TEST_ASSERT_EQUAL_UINT16(0x7FFF, detail::resistance_to_code(40200_Ohm, rref));

    TEST_ASSERT_EQUAL_UINT16(0, detail::temperature_to_code(-300_c, rref, r0, iec60751));
    TEST_ASSERT_EQUAL_UINT16(0x7FFF, detail::temperature_to_code(1000_c, rref, r0, iec60751));
}

// =============================================================================
// String conversion and formatting
// =============================================================================

TEST_CASE("max31865 to_string for enums", "[idfxx][max31865]") {
    TEST_ASSERT_EQUAL_STRING("2-wire", idfxx::to_string(wiring::two_wire).c_str());
    TEST_ASSERT_EQUAL_STRING("3-wire", idfxx::to_string(wiring::three_wire).c_str());
    TEST_ASSERT_EQUAL_STRING("4-wire", idfxx::to_string(wiring::four_wire).c_str());
    TEST_ASSERT_EQUAL_STRING("unknown(7)", idfxx::to_string(static_cast<wiring>(7)).c_str());

    TEST_ASSERT_EQUAL_STRING("50Hz", idfxx::to_string(notch_filter::hz_50).c_str());
    TEST_ASSERT_EQUAL_STRING("60Hz", idfxx::to_string(notch_filter::hz_60).c_str());
    TEST_ASSERT_EQUAL_STRING("unknown(9)", idfxx::to_string(static_cast<notch_filter>(9)).c_str());

    TEST_ASSERT_EQUAL_STRING("high_threshold", idfxx::to_string(fault::high_threshold).c_str());
    TEST_ASSERT_EQUAL_STRING("low_threshold", idfxx::to_string(fault::low_threshold).c_str());
    TEST_ASSERT_EQUAL_STRING("refin_high", idfxx::to_string(fault::refin_high).c_str());
    TEST_ASSERT_EQUAL_STRING("refin_low", idfxx::to_string(fault::refin_low).c_str());
    TEST_ASSERT_EQUAL_STRING("rtdin_low", idfxx::to_string(fault::rtdin_low).c_str());
    TEST_ASSERT_EQUAL_STRING("over_under_voltage", idfxx::to_string(fault::over_under_voltage).c_str());
    TEST_ASSERT_EQUAL_STRING("unknown(0x01)", idfxx::to_string(static_cast<fault>(0x01)).c_str());
}

TEST_CASE("max31865 to_string for fault flags", "[idfxx][max31865]") {
    TEST_ASSERT_EQUAL_STRING("none", idfxx::to_string(idfxx::flags<fault>{}).c_str());
    TEST_ASSERT_EQUAL_STRING("rtdin_low", idfxx::to_string(idfxx::flags<fault>{fault::rtdin_low}).c_str());
    TEST_ASSERT_EQUAL_STRING(
        "high_threshold|refin_low|over_under_voltage",
        idfxx::to_string(fault::high_threshold | fault::refin_low | fault::over_under_voltage).c_str()
    );
    TEST_ASSERT_EQUAL_STRING(
        "low_threshold|unknown(0x03)", idfxx::to_string(idfxx::flags<fault>{static_cast<uint8_t>(0x43)}).c_str()
    );
}

#ifdef CONFIG_IDFXX_STD_FORMAT
static_assert(std::formattable<wiring, char>);
static_assert(std::formattable<notch_filter, char>);
static_assert(std::formattable<fault, char>);
static_assert(std::formattable<idfxx::flags<fault>, char>);

TEST_CASE("max31865 formatters", "[idfxx][max31865]") {
    TEST_ASSERT_EQUAL_STRING("3-wire", std::format("{}", wiring::three_wire).c_str());
    TEST_ASSERT_EQUAL_STRING("60Hz", std::format("{}", notch_filter::hz_60).c_str());
    TEST_ASSERT_EQUAL_STRING("refin_high", std::format("{}", fault::refin_high).c_str());
    TEST_ASSERT_EQUAL_STRING(
        "high_threshold|low_threshold", std::format("{}", fault::high_threshold | fault::low_threshold).c_str()
    );
    TEST_ASSERT_EQUAL_STRING("none", std::format("{}", idfxx::flags<fault>{}).c_str());
}
#endif // CONFIG_IDFXX_STD_FORMAT
