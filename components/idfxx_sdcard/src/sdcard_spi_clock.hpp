// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

// Internal: chooses the clock that a card on an SPI bus is configured with, so that the clock the SPI peripheral
// actually generates stays at or below a limit. Kept free of driver dependencies so it can be unit-tested (in QEMU):
// the peripheral's clock divider is passed in.

#include <algorithm>
#include <cstdint>
#include <optional>

namespace idfxx::sdcard::internal {

// The card identification clock (SDMMC_FREQ_PROBING), the lowest clock a card is configured with.
inline constexpr uint32_t sdspi_probing_khz = 400;

// The highest configured clock (SDMMC_FREQ_DEFAULT) that keeps an SD card in default speed. Above it, ESP-IDF
// switches the card to high speed and configures the bus with the clock as given; a card without high speed is
// configured with this clock instead.
inline constexpr uint32_t sdspi_default_speed_khz = 20'000;

// The SD high-speed clock limit.
inline constexpr uint32_t sd_high_speed_max_hz = 50'000'000;

// The highest clock an SPI peripheral takes from its clock source, on targets where the SPI master driver divides
// the source down before the peripheral.
inline constexpr uint32_t spi_peripheral_source_max_hz = 80'000'000;

// How the SPI master driver clocks a device.
struct spi_clock_source {
    // The frequency of the clock source.
    uint32_t hz;
    // 1 where the source clocks the peripheral directly. Otherwise, the smallest (even) divider the driver applies to
    // the source before the peripheral (see spi_min_pre_div()).
    uint32_t pre_div = 1;
};

// The smallest divider the SPI master driver applies to a clock source before the peripheral, on targets where it
// divides the source.
constexpr uint32_t spi_min_pre_div(uint32_t source_hz) {
    return std::max<uint32_t>(2, ((source_hz / spi_peripheral_source_max_hz) + 1) & ~uint32_t{1});
}

// The clock the peripheral generates for a device configured with request_hz, or nullopt if the driver would
// reject the request or the clock can't be predicted. divide(base_hz, request_hz) is the peripheral's divider: the
// clock it generates from base_hz for request_hz, which is the nearest one it can generate.
//
// Where the driver divides the source, it picks the divider itself, from the request. A request is then predictable
// only when the smallest divider divides the source's ratio to the request (rounded down), and the peripheral
// generates exactly the source divided by that ratio: ESP-IDF 5.5.1 and later use the smallest divider then. (ESP-IDF
// 5.5.0 picks the divider differently, and generates the same clock or a lower one, except where its divider range
// runs out: below about 940 kHz on esp32p4.)
template<typename Divide>
std::optional<uint32_t> spi_generated_clock(const spi_clock_source& src, uint32_t request_hz, Divide divide) {
    const uint32_t base_hz = src.hz / src.pre_div;
    if (request_hz == 0 || request_hz > base_hz) {
        return std::nullopt;
    }
    const uint32_t generated = divide(base_hz, request_hz);
    if (src.pre_div > 1) {
        const uint32_t ratio = src.hz / request_hz;
        if (ratio % src.pre_div != 0 || generated != src.hz / ratio) {
            return std::nullopt;
        }
    }
    return generated;
}

struct spi_clock_choice {
    // The clock to configure the card with (sdmmc_host_t::max_freq_khz).
    uint32_t request_khz;
    // The clock the peripheral generates for it.
    uint32_t generated_hz;
};

// Chooses the clock to configure a card on an SPI bus with, so that the clock the peripheral generates is the
// highest it can generate at or below the lowest of cap_hz, the SD high-speed limit and pin_limit_hz (the highest
// clock the bus's pins allow, where they limit it). That also holds for a card without high speed, which ESP-IDF
// configures with sdspi_default_speed_khz instead. Clocks at or below 20 MHz are configured without a switch to high
// speed. Returns nullopt only if no clock from sdspi_probing_khz up is low enough.
template<typename Divide>
std::optional<spi_clock_choice>
choose_spi_clock(const spi_clock_source& src, int64_t cap_hz, std::optional<uint32_t> pin_limit_hz, Divide divide) {
    uint32_t limit_hz = static_cast<uint32_t>(std::clamp<int64_t>(cap_hz, 0, sd_high_speed_max_hz));
    if (pin_limit_hz) {
        limit_hz = std::min(limit_hz, *pin_limit_hz);
    }
    const uint32_t base_hz = src.hz / src.pre_div;
    if (limit_hz < sdspi_probing_khz * 1000 || base_hz < sdspi_probing_khz * 1000) {
        return std::nullopt;
    }
    auto generated_for = [&](uint32_t khz) { return spi_generated_clock(src, khz * 1000, divide); };
    const auto default_speed_generated = generated_for(sdspi_default_speed_khz);
    const bool high_speed_allowed = default_speed_generated && *default_speed_generated <= limit_hz;

    // Every clock the peripheral generates is base_hz divided by an integer, so try those from the highest at or
    // below the limit down, each requested in whole kHz. A lower one can't do better once it is below the best found.
    std::optional<spi_clock_choice> best;
    for (uint32_t d = std::max<uint32_t>(1, base_hz / limit_hz); base_hz / d >= sdspi_probing_khz * 1000; ++d) {
        const uint32_t candidate_hz = base_hz / d;
        if (best && candidate_hz < best->generated_hz) {
            break;
        }
        const uint32_t khz = candidate_hz / 1000;
        if (khz > sdspi_default_speed_khz && !high_speed_allowed) {
            continue;
        }
        const auto generated = generated_for(khz);
        if (generated && *generated <= limit_hz && (!best || *generated > best->generated_hz)) {
            best = spi_clock_choice{khz, *generated};
        }
    }
    // A clock of 20 MHz or less that the default-speed request generates as well doesn't need high speed.
    if (best && best->request_khz > sdspi_default_speed_khz && best->generated_hz <= sdspi_default_speed_khz * 1000
        && default_speed_generated == best->generated_hz) {
        best->request_khz = sdspi_default_speed_khz;
    }
    return best;
}

} // namespace idfxx::sdcard::internal
