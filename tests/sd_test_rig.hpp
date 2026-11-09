// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// SD card wiring shared by the idfxx_sdcard and idfxx_fatfs tests. The board is
// chosen with the "SD card test board" option in tests/Kconfig.projbuild.

#pragma once

#include "idfxx/gpio"
#include "idfxx/sched"
#include "idfxx/sdcard"
#include "idfxx/spi/master"
#include "unity.h"

#include <array>
#include <chrono>
#include <utility>

namespace idfxx::sdcard::test {

#if CONFIG_IDFXX_TEST_SD_RIG_E1004

// Seeed Studio reTerminal E1004: the microSD slot is on SPI2, shared with the e-paper panel.
inline constexpr const char* rig_name = "reTerminal E1004";
inline constexpr gpio sclk = gpio_7;
inline constexpr gpio mosi = gpio_9;
inline constexpr gpio miso = gpio_8;
inline constexpr gpio cs = gpio_14;
inline constexpr gpio detect = gpio_15; // Active low
inline constexpr gpio power = gpio_16;  // Slot power enable, active high
// The panel's CS lines are left alone: driving them high while the panel is unpowered back-powers it.
inline constexpr std::array<gpio, 0> other_cs{};

#else

// Unexpected Maker FeatherS3 with an Adafruit 2.4" TFT FeatherWing (V1). The SD slot, the
// ILI9341 and the STMPE610 touch controller share SPI2. V1 has no card-detect line.
inline constexpr const char* rig_name = "TFT FeatherWing";
inline constexpr gpio sclk = gpio_36;
inline constexpr gpio mosi = gpio_35;
inline constexpr gpio miso = gpio_37;
inline constexpr gpio cs = gpio_33;
inline constexpr gpio detect = gpio::nc();
inline constexpr gpio power = gpio::nc();
inline constexpr std::array<gpio, 2> other_cs{gpio_1, gpio_38}; // TFT and touch CS, held high

#endif

/// Returns whether a card is in the slot. Without a card-detect line, a card is assumed.
inline bool card_inserted() {
    if (!detect.is_connected()) {
        return true;
    }
    gpio d = detect;
    TEST_ASSERT_TRUE(d.try_set_direction(gpio::mode::input).has_value());
    TEST_ASSERT_TRUE(d.try_set_pull_mode(gpio::pull_mode::pullup).has_value());
    delay(std::chrono::milliseconds(1));
    return d.get_level() == gpio::level::low;
}

/// Switches slot power, where the board can, and deselects the bus's other devices.
inline void slot_power(bool on) {
    for (gpio pin : other_cs) {
        TEST_ASSERT_TRUE(pin.try_set_direction(gpio::mode::output).has_value());
        pin.set_level(gpio::level::high);
    }
    if (!power.is_connected()) {
        return;
    }
    gpio p = power;
    TEST_ASSERT_TRUE(p.try_set_direction(gpio::mode::output).has_value());
    p.set_level(on ? gpio::level::high : gpio::level::low);
    if (on) {
        delay(std::chrono::milliseconds(500)); // supply settling time
    }
}

inline spi::master_bus make_bus() {
    spi::bus_config cfg{};
    cfg.mosi = mosi;
    cfg.miso = miso;
    cfg.sclk = sclk;
    cfg.max_transfer_sz = 4092;
    auto r = spi::master_bus::make(spi::host_device::spi2, spi::dma_chan::ch_auto, cfg);
    TEST_ASSERT_TRUE(r.has_value());
    return std::move(*r);
}

inline card make_card(spi::master_bus& bus) {
    auto r = card::make(bus, {.cs = cs, .card_detect = detect});
    TEST_ASSERT_TRUE_MESSAGE(r.has_value(), r ? "" : r.error().message().c_str());
    return std::move(*r);
}

} // namespace idfxx::sdcard::test

// Skips the test, before anything needing cleanup is created, when the slot is empty.
#define IDFXX_REQUIRE_SD_CARD()                                                                                        \
    do {                                                                                                               \
        if (!::idfxx::sdcard::test::card_inserted()) {                                                                 \
            TEST_IGNORE_MESSAGE("No card in the SD slot");                                                             \
        }                                                                                                              \
    } while (0)
