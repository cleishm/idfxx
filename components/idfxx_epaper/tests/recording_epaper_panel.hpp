// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Shared test fixture for idfxx_epaper tests.

#pragma once

#include "idfxx/epaper/panel"

#include <vector>

namespace idfxx_epaper_test {

// Stub panel recording driver-hook invocations, for verifying the base
// class's validation and state machine without hardware.
class recording_epaper_panel : public idfxx::epaper::panel {
public:
    recording_epaper_panel(size_t width, size_t height)
        : idfxx::epaper::panel(width, height) {}

    struct write {
        size_t x;
        size_t y;
        size_t row_start;
        size_t row_end;
    };

    std::vector<write> mono_writes;
    std::vector<write> gray_writes;
    std::vector<idfxx::epaper::refresh_mode> refreshes;
    size_t clears = 0;
    size_t sleeps = 0;
    size_t wakes = 0;
    // do_wait invocations (the base settles before every controller hook
    // and at the end of a blocking refresh), plus the hook count at the
    // time of the last one, for ordering assertions.
    size_t waits = 0;
    size_t hooks_before_last_wait = 0;

    [[nodiscard]] size_t hooks() const noexcept {
        return mono_writes.size() + gray_writes.size() + refreshes.size() + clears + sleeps + wakes;
    }

private:
    [[nodiscard]] idfxx::result<void>
    do_write(const idfxx::epaper::mono_framebuffer&, size_t row_start, size_t row_end, size_t x, size_t y) override {
        mono_writes.push_back({x, y, row_start, row_end});
        return {};
    }

    [[nodiscard]] idfxx::result<void>
    do_write(const idfxx::epaper::gray4_framebuffer&, size_t row_start, size_t row_end, size_t x, size_t y) override {
        gray_writes.push_back({x, y, row_start, row_end});
        return {};
    }

    [[nodiscard]] idfxx::result<void> do_clear() override {
        ++clears;
        return {};
    }

    [[nodiscard]] idfxx::result<void> do_refresh(idfxx::epaper::refresh_mode mode) override {
        refreshes.push_back(mode);
        return {};
    }

    [[nodiscard]] idfxx::result<void> do_set_color_mode(enum idfxx::epaper::color_mode) override { return {}; }

    [[nodiscard]] idfxx::result<void> do_sleep() override {
        ++sleeps;
        return {};
    }

    [[nodiscard]] idfxx::result<void> do_wake() override {
        ++wakes;
        return {};
    }

    // With no BUSY line configured the base default returns success
    // immediately; record the call and defer to it.
    [[nodiscard]] idfxx::result<void> do_wait(std::optional<std::chrono::milliseconds> timeout) override {
        ++waits;
        hooks_before_last_wait = hooks();
        return idfxx::epaper::panel::do_wait(timeout);
    }
};

} // namespace idfxx_epaper_test
