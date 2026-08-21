// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#include <idfxx/epaper/gray4_framebuffer>
#include <idfxx/epaper/mono_framebuffer>
#include <idfxx/epaper/panel>
#include <idfxx/sched>

#include <chrono>

using namespace std::chrono_literals;

namespace idfxx::epaper {

// Shared body of the write family: the panel must be awake and in the
// framebuffer's color mode, the destination column must land on a RAM byte
// boundary, the row range must be a valid band of the framebuffer, and the
// placed band must fit within the panel.
template<typename FB>
result<void>
panel::_try_write_rows(const FB& fb, enum color_mode expected, size_t row_start, size_t row_end, size_t x, size_t y) {
    if (_asleep || _color_mode != expected) {
        return error(errc::invalid_state);
    }
    if (x % 8 != 0 || row_start >= row_end || row_end > fb.height()) {
        return error(errc::invalid_arg);
    }
    if (x + fb.width() > _width || y + row_end > _height) {
        return error(errc::invalid_arg);
    }
    if (auto r = do_wait(std::nullopt); !r) {
        return r;
    }
    return do_write(fb, row_start, row_end, x, y);
}

result<void> panel::try_write(const mono_framebuffer& fb, size_t x, size_t y) {
    return _try_write_rows(fb, color_mode::mono, 0, fb.height(), x, y);
}

result<void> panel::try_write(const gray4_framebuffer& fb, size_t x, size_t y) {
    return _try_write_rows(fb, color_mode::gray4, 0, fb.height(), x, y);
}

result<void> panel::try_write_rows(const mono_framebuffer& fb, size_t row_start, size_t row_end, size_t x, size_t y) {
    return _try_write_rows(fb, color_mode::mono, row_start, row_end, x, y);
}

result<void> panel::try_write_rows(const gray4_framebuffer& fb, size_t row_start, size_t row_end, size_t x, size_t y) {
    return _try_write_rows(fb, color_mode::gray4, row_start, row_end, x, y);
}

result<void> panel::configure_control_lines(gpio busy_gpio, gpio reset_gpio) {
    if (!busy_gpio.is_connected()) {
        return error(errc::invalid_arg);
    }
    // BUSY is driven by the panel (no pull needed); reset idles high.
    if (auto r = busy_gpio.try_set_direction(gpio::mode::input); !r) {
        return error(r.error());
    }
    if (reset_gpio.is_connected()) {
        if (auto r = reset_gpio.try_set_direction(gpio::mode::output); !r) {
            return error(r.error());
        }
        reset_gpio.set_level(gpio::level::high);
    }
    return {};
}

namespace {

// Polls `busy` until it leaves `busy_level`, reporting errc::timeout after
// `timeout` (or `default_timeout` when unset). Succeeds immediately for an
// unconnected line. Shared by wait_busy and the start_refresh future.
result<void> poll_busy(
    gpio busy,
    enum gpio::level busy_level,
    std::optional<std::chrono::milliseconds> timeout,
    std::chrono::milliseconds default_timeout
) {
    if (!busy.is_connected()) {
        return {};
    }
    // ePaper refreshes hold BUSY for hundreds of milliseconds to seconds, so
    // there is no point spin-yielding: poll once per RTOS tick.
    const auto deadline = std::chrono::steady_clock::now() + timeout.value_or(default_timeout);
    while (busy.get_level() == busy_level) {
        if (std::chrono::steady_clock::now() > deadline) {
            return error(errc::timeout);
        }
        delay(next_tick);
    }
    return {};
}

} // namespace

result<void> panel::_start_refresh(refresh_mode mode) {
    if (_asleep) {
        return error(errc::invalid_state);
    }
    if (mode == refresh_mode::partial && !_has_baseline) {
        mode = refresh_mode::full;
    }
    if (auto r = do_wait(std::nullopt); !r) {
        return r;
    }
    if (auto r = do_refresh(mode); !r) {
        return r;
    }
    _has_baseline = true;
    return {};
}

idfxx::future<void> panel::_busy_future() const {
    const gpio busy = _busy_gpio;
    const enum gpio::level level = _busy_level;
    const std::chrono::milliseconds default_timeout = _busy_timeout;
    return idfxx::future<void>(
        [=](std::optional<std::chrono::milliseconds> timeout) -> result<void> {
            return poll_busy(busy, level, timeout, default_timeout);
        },
        [=]() noexcept -> bool { return !busy.is_connected() || busy.get_level() != level; }
    );
}

result<void> panel::wait_busy(std::optional<std::chrono::milliseconds> timeout) {
    return poll_busy(_busy_gpio, _busy_level, timeout, _busy_timeout);
}

result<void> panel::hardware_reset() {
    if (!_reset_gpio.is_connected()) {
        return error(errc::invalid_state);
    }
    _reset_gpio.set_level(gpio::level::low);
    delay(10ms);
    _reset_gpio.set_level(gpio::level::high);
    delay(10ms);
    return wait_busy();
}

} // namespace idfxx::epaper
