// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

// Internal: chooses the clock that a card on an SPI bus is configured with, so that the clock the bus runs at stays at
// or below a limit. The bus is asked rather than predicted: each clock tried is set on the card's device and read back.
// The search itself takes the bus as a function, so it can be unit-tested (in QEMU) against synthetic buses.

#include <idfxx/error>

#include <algorithm>
#include <cstdint>
#include <driver/sdspi_host.h>
#include <optional>
#include <quanta/frequency>
#include <sd_protocol_types.h>

namespace idfxx::sdcard::internal {

// The SD high-speed clock limit.
inline constexpr uint32_t sd_high_speed_max_hz = 50'000'000;

// The clock a bus runs at, from its clock source and the clock as the SPI master driver reports it: in whole kHz,
// rounded down. The bus divides the source by a whole number, so the clock is exact when only one divider gives that
// reading; otherwise the highest clock the reading allows is returned.
constexpr uint32_t spi_clock_hz(uint32_t source_hz, uint32_t khz) {
    const uint32_t highest_hz = khz * 1000 + 999;
    if (khz == 0) {
        return highest_hz;
    }
    const uint32_t max_div = source_hz / (khz * 1000);
    const uint32_t min_div = source_hz / ((khz + 1) * 1000) + 1;
    return max_div != 0 && min_div == max_div ? source_hz / max_div : highest_hz;
}

struct spi_clock_choice {
    // The clock to configure the card with (sdmmc_host_t::max_freq_khz).
    uint32_t request_khz;
    // The clock the bus runs at for it.
    uint32_t hz;
};

// Chooses the clock to configure a card on an SPI bus with: the highest the bus runs at, at or below limit_hz, that is
// found by requesting each of source_hz divided by a whole number, in whole kHz, from the highest at or below the limit
// down. set_clock(khz) sets the card's device to a clock and returns the clock the driver reports for it, in whole
// kHz, or nullopt if the driver rejects it.
//
// ESP-IDF switches a card to high speed when the clock it is configured with is above SDMMC_FREQ_DEFAULT, and
// configures a card without high speed with SDMMC_FREQ_DEFAULT instead. A clock above SDMMC_FREQ_DEFAULT is chosen
// only if the bus runs above it for that request, and only if the bus runs at or below the limit for
// SDMMC_FREQ_DEFAULT.
//
// A lower request can give a higher clock where the driver picks a divider before the peripheral from the request
// (esp32p4, for example), so lower requests are tried as long as they could still do better. Should no request from
// SDMMC_FREQ_PROBING up give a clock low enough, the card stays at the identification clock, SDMMC_FREQ_PROBING, as
// the bus runs it. Returns nullopt if the limit is below SDMMC_FREQ_PROBING, or the bus rejects that clock.
template<typename SetClock>
std::optional<spi_clock_choice> choose_spi_clock(uint32_t source_hz, uint32_t limit_hz, SetClock set_clock) {
    constexpr uint32_t probing_hz = SDMMC_FREQ_PROBING * 1000;
    if (limit_hz < probing_hz) {
        return std::nullopt;
    }
    auto clock_for = [&](uint32_t khz) -> std::optional<uint32_t> {
        const std::optional<uint32_t> reported_khz = set_clock(khz);
        if (!reported_khz) {
            return std::nullopt;
        }
        return spi_clock_hz(source_hz, *reported_khz);
    };
    const auto default_speed_hz = clock_for(SDMMC_FREQ_DEFAULT);
    const bool high_speed_allowed = default_speed_hz && *default_speed_hz <= limit_hz;

    std::optional<spi_clock_choice> best;
    for (uint32_t d = std::max<uint32_t>(1, source_hz / limit_hz); source_hz / d >= probing_hz; ++d) {
        const uint32_t candidate_hz = source_hz / d;
        if (best && candidate_hz <= best->hz) {
            break;
        }
        const uint32_t khz = candidate_hz / 1000;
        const bool high_speed = khz > SDMMC_FREQ_DEFAULT;
        if (high_speed && !high_speed_allowed) {
            continue;
        }
        const auto hz = clock_for(khz);
        if (!hz || *hz > limit_hz || (high_speed && *hz <= SDMMC_FREQ_DEFAULT * 1000)) {
            continue;
        }
        if (!best || *hz > best->hz) {
            best = spi_clock_choice{khz, *hz};
        }
    }
    if (!best) {
        if (const auto hz = clock_for(SDMMC_FREQ_PROBING)) {
            best = spi_clock_choice{SDMMC_FREQ_PROBING, *hz};
        }
    }
    return best;
}

// Chooses the clock for a card whose device on the SPI bus host has been added (sdspi_host_init_device()) but whose
// card hasn't been contacted, for a max_frequency of cap: see choose_spi_clock(). The device is left at the
// identification clock, which card initialization starts with without setting it.
//
// Returns errc::invalid_arg if cap is below SDMMC_FREQ_PROBING.
[[nodiscard]] result<spi_clock_choice>
choose_sdspi_clock(sdspi_dev_handle_t handle, spi_host_device_t host, quanta::frequency::hertz cap);

} // namespace idfxx::sdcard::internal
