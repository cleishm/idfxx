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
#include <optional>
#include <utility>

namespace idfxx::sdcard::test {

#if CONFIG_IDFXX_TEST_SD_RIG_E1004

// Seeed Studio reTerminal E1004: the microSD slot is on SPI2, shared with the e-paper panel.
inline constexpr bool has_board = true;
inline constexpr const char* rig_name = "reTerminal E1004";
inline constexpr gpio sclk = gpio_7;
inline constexpr gpio mosi = gpio_9;
inline constexpr gpio miso = gpio_8;
inline constexpr gpio cs = gpio_14;
inline constexpr gpio detect = gpio_15; // Active low
inline constexpr gpio power = gpio_16;  // Slot power enable, active high
// The panel's CS lines are left alone: driving them high while the panel is unpowered back-powers it.
inline constexpr std::array<gpio, 0> other_cs{};

#elif CONFIG_IDFXX_TEST_SD_RIG_TFT_FEATHERWING

// Unexpected Maker FeatherS3 with an Adafruit 2.4" TFT FeatherWing (V1). The SD slot, the
// ILI9341 and the STMPE610 touch controller share SPI2. V1 has no card-detect line.
inline constexpr bool has_board = true;
inline constexpr const char* rig_name = "TFT FeatherWing";
inline constexpr gpio sclk = gpio_36;
inline constexpr gpio mosi = gpio_35;
inline constexpr gpio miso = gpio_37;
inline constexpr gpio cs = gpio_33;
inline constexpr gpio detect = gpio::nc();
inline constexpr gpio power = gpio::nc();
inline constexpr std::array<gpio, 2> other_cs{gpio_1, gpio_38}; // TFT and touch CS, held high

#else

// No SD card board: card_inserted() reports an empty slot, so every [hw] SD test is ignored.
// The pins are left unconnected so that this header builds for every target.
inline constexpr bool has_board = false;
inline constexpr const char* rig_name = "none";
inline constexpr gpio sclk = gpio::nc();
inline constexpr gpio mosi = gpio::nc();
inline constexpr gpio miso = gpio::nc();
inline constexpr gpio cs = gpio::nc();
inline constexpr gpio detect = gpio::nc();
inline constexpr gpio power = gpio::nc();
inline constexpr std::array<gpio, 0> other_cs{};

#endif

/// Returns whether a card is in the slot. Without a board there is no slot; without a card-detect line, a card
/// is assumed.
inline bool card_inserted() {
    if (!has_board) {
        return false;
    }
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

inline card make_card(spi::master_bus& bus, quanta::frequency::hertz max_frequency) {
    auto r = card::make(bus, {.cs = cs, .card_detect = detect, .max_frequency = max_frequency});
    TEST_ASSERT_TRUE_MESSAGE(r.has_value(), r ? "" : r.error().message().c_str());
    return std::move(*r);
}

namespace detail {

// The bus and card a test is using. They are held in static storage rather than in the test's stack frame: a
// failed Unity assertion longjmps out of the test, skipping destructors, which would leave SPI2 initialized and
// fail every later test that uses it.
inline std::optional<spi::master_bus>& held_bus() {
    static std::optional<spi::master_bus> bus;
    return bus;
}

inline std::optional<card>& held_card() {
    static std::optional<card> c;
    return c;
}

// Destroys whatever a test left behind: the card, then its bus.
inline void release_held() {
    held_card().reset();
    held_bus().reset();
}

} // namespace detail

/// A [hw] test's bus and card, on the board's SD slot. Construction first destroys anything a failed earlier test
/// left behind, then powers the slot and initializes the card; destruction releases the card and the bus, then
/// powers the slot off.
class card_fixture {
public:
    card_fixture() {
        detail::release_held();
        slot_power(true);
        detail::held_bus().emplace(make_bus());
        reopen();
    }

    ~card_fixture() {
        detail::release_held();
        slot_power(false);
    }

    card_fixture(const card_fixture&) = delete;
    card_fixture& operator=(const card_fixture&) = delete;

    spi::master_bus& bus() { return *detail::held_bus(); }

    card& get() { return *detail::held_card(); }

    /// Destroys the card and initializes it again on the same bus.
    card& reopen() {
        detail::held_card().reset();
        detail::held_card().emplace(make_card(bus()));
        return get();
    }

    /// Destroys the card and initializes it again on the same bus, with a clock limit.
    card& reopen(quanta::frequency::hertz max_frequency) {
        detail::held_card().reset();
        detail::held_card().emplace(make_card(bus(), max_frequency));
        return get();
    }
};

/// An SPI2 bus with no pins, for tests whose card configurations are rejected, or fail for lack of a card. Held in the
/// same storage as card_fixture's bus, so that a failed test doesn't leave SPI2 initialized.
class unwired_bus_fixture {
public:
    explicit unwired_bus_fixture(spi::dma_chan dma = spi::dma_chan::ch_auto) {
        detail::release_held();
        auto r = spi::master_bus::make(spi::host_device::spi2, dma, spi::bus_config{});
        TEST_ASSERT_TRUE(r.has_value());
        detail::held_bus().emplace(std::move(*r));
    }

    ~unwired_bus_fixture() { detail::release_held(); }

    unwired_bus_fixture(const unwired_bus_fixture&) = delete;
    unwired_bus_fixture& operator=(const unwired_bus_fixture&) = delete;

    spi::master_bus& get() { return *detail::held_bus(); }
};

} // namespace idfxx::sdcard::test

// Skips the test, before anything needing cleanup is created, when there is no board or its slot is empty.
#define IDFXX_REQUIRE_SD_CARD()                                                                                        \
    do {                                                                                                               \
        if (!::idfxx::sdcard::test::has_board) {                                                                       \
            TEST_IGNORE_MESSAGE("No SD card test board selected");                                                     \
        }                                                                                                              \
        if (!::idfxx::sdcard::test::card_inserted()) {                                                                 \
            TEST_IGNORE_MESSAGE("No card in the SD slot");                                                             \
        }                                                                                                              \
    } while (0)
