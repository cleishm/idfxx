// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#include <idfxx/epaper/gray4_framebuffer>
#include <idfxx/epaper/mono_framebuffer>
#include <idfxx/epaper/panel>
#include <idfxx/sched>

#include <chrono>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

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

result<void> panel::wait_busy(std::optional<std::chrono::milliseconds> timeout) {
    if (!_busy_gpio.is_connected()) {
        return {};
    }
    // ePaper refreshes hold BUSY for hundreds of milliseconds to seconds, so
    // there is no point spin-yielding: poll once per RTOS tick.
    const auto deadline = std::chrono::steady_clock::now() + timeout.value_or(_busy_timeout);
    while (_busy_gpio.get_level() == _busy_level) {
        if (std::chrono::steady_clock::now() > deadline) {
            return error(errc::timeout);
        }
        vTaskDelay(1);
    }
    return {};
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
