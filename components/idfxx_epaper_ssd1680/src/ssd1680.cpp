// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#include <idfxx/epaper/gray4_framebuffer>
#include <idfxx/epaper/mono_framebuffer>
#include <idfxx/epaper/ssd1680>
#include <idfxx/sched>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <esp_lcd_panel_io.h>
#include <utility>

using namespace std::chrono_literals;

namespace idfxx::epaper {

namespace {

// SSD1680 command set (Solomon Systech SSD1680 datasheet; sequences follow
// Seeed Studio's Seeed_GFX SSD1680 ePaper driver, MIT/BSD-licensed).
constexpr uint8_t cmd_driver_output = 0x01;   // Driver output control (MUX, gate scan)
constexpr uint8_t cmd_gate_voltage = 0x03;    // Gate driving voltage (VGH)
constexpr uint8_t cmd_source_voltage = 0x04;  // Source driving voltage (VSH1/VSH2/VSL)
constexpr uint8_t cmd_deep_sleep = 0x10;      // Deep sleep mode
constexpr uint8_t cmd_data_entry = 0x11;      // Data entry mode (address increment direction)
constexpr uint8_t cmd_sw_reset = 0x12;        // Software reset
constexpr uint8_t cmd_temp_sensor = 0x18;     // Temperature sensor selection
constexpr uint8_t cmd_write_temp = 0x1A;      // Write temperature register
constexpr uint8_t cmd_master_activate = 0x20; // Execute the staged update sequence
constexpr uint8_t cmd_update_ctrl1 = 0x21;    // Display update control (RAM content options)
constexpr uint8_t cmd_update_ctrl2 = 0x22;    // Display update sequence selection
constexpr uint8_t cmd_write_ram = 0x24;       // Write RAM (new image plane)
constexpr uint8_t cmd_write_ram_old = 0x26;   // Write RAM (previous image plane)
constexpr uint8_t cmd_write_vcom = 0x2C;      // VCOM voltage
constexpr uint8_t cmd_write_lut = 0x32;       // Write LUT register (waveform)
constexpr uint8_t cmd_border = 0x3C;          // Border waveform control
constexpr uint8_t cmd_end_option = 0x3F;      // LUT end option (EOPQ)
constexpr uint8_t cmd_analog_ctrl = 0x74;     // Set analog block control
constexpr uint8_t cmd_digital_ctrl = 0x7E;    // Set digital block control
constexpr uint8_t cmd_ram_x_range = 0x44;     // RAM x address start/end (bytes)
constexpr uint8_t cmd_ram_y_range = 0x45;     // RAM y address start/end (rows)
constexpr uint8_t cmd_ram_x_counter = 0x4E;   // RAM x address counter
constexpr uint8_t cmd_ram_y_counter = 0x4F;   // RAM y address counter
constexpr uint8_t cmd_nop = 0x7F;             // No operation

// Display update sequences (cmd_update_ctrl2 parameter).
constexpr uint8_t seq_full = 0xF7;     // load temp + LUT, display (mode 1)
constexpr uint8_t seq_partial = 0xFF;  // load temp + LUT, display (mode 2, differential)
constexpr uint8_t seq_load_lut = 0x91; // load LUT for the written temperature, no display
constexpr uint8_t seq_display = 0xC7;  // display with the currently loaded LUT

// Border waveform values: follow-LUT for normal refreshes, HiZ during
// partial refreshes so the border does not flicker.
constexpr uint8_t border_normal = 0x05;
constexpr uint8_t border_hiz = 0x80;

// Temperature-register value selecting an alternate OTP waveform bank
// (Seeed's recipe; per-panel-batch tunable): a shortened "fast" full
// waveform.
constexpr uint8_t temp_fast = 0x64;

// 4-level grayscale waveform for GDEY0213B74-class SSD1680 panels: the
// Good Display reference recipe, as published in Adafruit_EPD (MIT) and
// GxEPD2_4G. Each pixel's LUT row is selected by its two RAM bits as
// (0x26-plane << 1) | 0x24-plane: L0 white, L1 light gray, L2 dark gray,
// L3 black. Loaded with cmd_write_lut; the voltage registers below
// (_init_gray) belong to the same recipe.
// clang-format off
constexpr std::array<uint8_t, 153> lut_gray4 = {
    // VS: voltage sequence per level (L0-L3), then VCOM (L4)
    0x40, 0x48, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // L0 white
    0x08, 0x48, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // L1 light gray
    0x02, 0x48, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // L2 dark gray
    0x20, 0x48, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // L3 black
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // L4 VCOM
    // TP/RP: phase timings per group
    0x0A, 0x19, 0x00, 0x03, 0x08, 0x00, 0x00,
    0x14, 0x01, 0x00, 0x14, 0x01, 0x00, 0x03,
    0x0A, 0x03, 0x00, 0x08, 0x19, 0x00, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    // FR: frame rates per group
    0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x00, 0x00, 0x00,
};
// clang-format on

// Controller RAM limits (176 sources x 296 gates).
constexpr size_t max_width = 176;
constexpr size_t max_height = 296;

} // namespace

panel_io::spi_config ssd1680::spi_io_config(gpio cs, gpio dc, freq::hertz pclk) noexcept {
    return {
        .cs_gpio = cs,
        .dc_gpio = dc,
        .spi_mode = 0,
        .pclk_freq = pclk,
        .trans_queue_depth = 10,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
}

result<ssd1680> ssd1680::make(idfxx::panel_io& panel_io, ssd1680::config config) {
    if (config.width == 0 || config.width > max_width || config.height == 0 || config.height > max_height) {
        return error(errc::invalid_arg);
    }
    if (auto r = configure_control_lines(config.busy_gpio, config.reset_gpio); !r) {
        return error(r.error());
    }

    const size_t stride = (config.width + 7) / 8;
    std::vector<uint8_t, dram_allocator<uint8_t>> shadow(stride * config.height, 0xFF);

    ssd1680 display(panel_io, config, std::move(shadow));
    if (config.reset_gpio.is_connected()) {
        if (auto r = display.hardware_reset(); !r) {
            return error(r.error());
        }
    }
    if (auto r = display._init(); !r) {
        return error(r.error());
    }
    return display;
}

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
ssd1680::ssd1680(idfxx::panel_io& panel_io, ssd1680::config config)
    : ssd1680(unwrap(make(panel_io, config))) {}
#endif

ssd1680::ssd1680(ssd1680&& other) noexcept
    : panel(std::move(other))
    , _io(std::exchange(other._io, nullptr))
    , _mirror_x(other._mirror_x)
    , _mirror_y(other._mirror_y)
    , _shadow(std::move(other._shadow)) {}

ssd1680& ssd1680::operator=(ssd1680&& other) noexcept {
    if (this != &other) {
        if (_io != nullptr && !asleep()) {
            (void)_cmd(cmd_deep_sleep, {0x01});
        }
        panel::operator=(std::move(other));
        _io = std::exchange(other._io, nullptr);
        _mirror_x = other._mirror_x;
        _mirror_y = other._mirror_y;
        _shadow = std::move(other._shadow);
    }
    return *this;
}

ssd1680::~ssd1680() {
    // Leaving the controller active degrades the glass; park it in deep
    // sleep unless the caller already did.
    if (_io != nullptr && !asleep()) {
        (void)_cmd(cmd_deep_sleep, {0x01});
    }
}

// =============================================================================
// epaper::panel hooks
// =============================================================================

result<void> ssd1680::do_write(const mono_framebuffer& fb, size_t row_start, size_t row_end, size_t x, size_t y) {
    if (_io == nullptr) {
        return error(errc::invalid_state);
    }
    const size_t stride = fb.stride_bytes();
    const size_t rows = row_end - row_start;
    if (auto r = _set_ram_window({x, y + row_start, fb.width(), rows}); !r) {
        return r;
    }
    // The RAM window auto-wraps the x address at its end, so the whole band
    // streams in one transfer (esp_lcd chunks it to the SPI limit).
    if (auto r = _stream(cmd_write_ram, fb.data().subspan(row_start * stride, rows * stride)); !r) {
        return r;
    }
    // Mirror the bytes into the shadow frame so the old-image plane can be
    // brought up to date after the next refresh.
    for (size_t row = row_start; row < row_end; ++row) {
        std::memcpy(_shadow.data() + (y + row) * _stride() + x / 8, fb.row(row).data(), stride);
    }
    return _drain();
}

result<void> ssd1680::do_write(const gray4_framebuffer& fb, size_t row_start, size_t row_end, size_t x, size_t y) {
    if (_io == nullptr) {
        return error(errc::invalid_state);
    }
    const size_t stride = fb.stride_bytes();
    const size_t rows = row_end - row_start;
    // The two gray planes stream to the controller's two RAM banks: bit 0 of
    // each pixel's level to the new-image plane (0x24), bit 1 to the
    // previous-image plane (0x26). lut_gray4 selects each pixel's row as
    // (0x26 << 1) | 0x24, which then equals the gray level.
    return _write_planes(
        {x, y + row_start, fb.width(), rows},
        fb.plane(0).subspan(row_start * stride, rows * stride),
        fb.plane(1).subspan(row_start * stride, rows * stride)
    );
}

result<void> ssd1680::do_clear() {
    if (_io == nullptr) {
        return error(errc::invalid_state);
    }
    if (color_mode() == color_mode::gray4) {
        // Gray level 0 (white) has both plane bits clear. Borrow the shadow
        // frame as an all-zero source (it is unused in gray4 mode), then
        // restore its all-white mono invariant.
        std::ranges::fill(_shadow, 0x00);
        auto r = _write_planes({0, 0, width(), height()}, _shadow, _shadow);
        std::ranges::fill(_shadow, 0xFF);
        return r;
    }
    // White into both planes, and into the shadow frame so later
    // non-partial refreshes re-send the blank image.
    std::ranges::fill(_shadow, 0xFF);
    return _write_planes({0, 0, width(), height()}, _shadow, _shadow);
}

result<void> ssd1680::do_refresh(refresh_mode mode) {
    if (_io == nullptr) {
        return error(errc::invalid_state);
    }
    if (color_mode() == color_mode::gray4) {
        if (mode != refresh_mode::full) {
            return error(errc::not_supported);
        }
        // The grayscale LUT was loaded by _init_gray; display without
        // reloading it.
        if (auto r = _cmd(cmd_update_ctrl2, {seq_display}); !r) {
            return r;
        }
        if (auto r = _cmd(cmd_master_activate); !r) {
            return r;
        }
        return wait_busy();
    }

    // Differential (mode-2) updates ping-pong the controller's two RAM
    // planes: each one swaps which physical plane the write commands address
    // and which a mode-1 update displays, so a full or fast refresh issued
    // after partial refreshes would display a stale frame. Re-send the
    // shadow frame to both planes first, so the update sequence displays
    // the current image regardless of which plane it reads.
    if (mode != refresh_mode::partial) {
        if (auto r = _write_planes({0, 0, width(), height()}, _shadow, _shadow); !r) {
            return r;
        }
    }

    switch (mode) {
    case refresh_mode::full:
        if (auto r = _cmd(cmd_border, {border_normal}); !r) {
            return r;
        }
        if (auto r = _cmd(cmd_update_ctrl2, {seq_full}); !r) {
            return r;
        }
        break;
    case refresh_mode::fast:
        // Temperature-forcing recipe (tunable, validate on hardware): load
        // the LUT for a forced high temperature — a shortened waveform —
        // then display with it. The next full refresh reloads the sensor
        // temperature, undoing the override.
        if (auto r = _cmd(cmd_border, {border_normal}); !r) {
            return r;
        }
        if (auto r = _cmd(cmd_write_temp, {temp_fast, 0x00}); !r) {
            return r;
        }
        if (auto r = _cmd(cmd_update_ctrl2, {seq_load_lut}); !r) {
            return r;
        }
        if (auto r = _cmd(cmd_master_activate); !r) {
            return r;
        }
        if (auto r = wait_busy(); !r) {
            return r;
        }
        if (auto r = _cmd(cmd_update_ctrl2, {seq_display}); !r) {
            return r;
        }
        break;
    case refresh_mode::partial:
        // HiZ border keeps the panel edge from flickering on differential
        // updates.
        if (auto r = _cmd(cmd_border, {border_hiz}); !r) {
            return r;
        }
        if (auto r = _cmd(cmd_update_ctrl2, {seq_partial}); !r) {
            return r;
        }
        break;
    }
    if (auto r = _cmd(cmd_master_activate); !r) {
        return r;
    }
    if (auto r = wait_busy(); !r) {
        return r;
    }
    return _sync_old_plane();
}

result<void> ssd1680::do_set_color_mode(enum color_mode mode) {
    if (_io == nullptr) {
        return error(errc::invalid_state);
    }
    if (mode == color_mode::gray4) {
        // Hardware-reset first (when possible) so the grayscale init starts
        // from a clean controller state, free of any forced-temperature or
        // partial-mode residue from earlier refreshes.
        if (reset_gpio().is_connected()) {
            if (auto r = hardware_reset(); !r) {
                return r;
            }
        }
        return _init_gray();
    }
    // Back to monochrome: re-run the full initialization so the next full
    // refresh reloads the normal waveform, and reset the shadow to match
    // the all-white state a fresh frame is composed against.
    if (auto r = _init(); !r) {
        return r;
    }
    std::ranges::fill(_shadow, 0xFF);
    return {};
}

result<void> ssd1680::do_sleep() {
    if (_io == nullptr) {
        return error(errc::invalid_state);
    }
    if (auto r = _cmd(cmd_deep_sleep, {0x01}); !r) {
        return r;
    }
    // BUSY is not meaningful while entering deep sleep; give the controller
    // time to power down.
    idfxx::delay(100ms);
    return {};
}

result<void> ssd1680::do_wake() {
    if (_io == nullptr) {
        return error(errc::invalid_state);
    }
    if (auto r = hardware_reset(); !r) {
        return r;
    }
    if (color_mode() == color_mode::gray4) {
        return _init_gray();
    }
    return _init();
}

// =============================================================================
// Command sequencing
// =============================================================================

result<void> ssd1680::_cmd(uint8_t cmd) {
    return wrap(esp_lcd_panel_io_tx_param(_io->idf_handle(), cmd, nullptr, 0));
}

result<void> ssd1680::_cmd(uint8_t cmd, std::initializer_list<uint8_t> params) {
    return wrap(esp_lcd_panel_io_tx_param(_io->idf_handle(), cmd, params.begin(), params.size()));
}

result<void> ssd1680::_cmd(uint8_t cmd, std::span<const uint8_t> params) {
    return wrap(esp_lcd_panel_io_tx_param(_io->idf_handle(), cmd, params.data(), params.size()));
}

result<void> ssd1680::_stream(uint8_t cmd, std::span<const uint8_t> data) {
    return wrap(esp_lcd_panel_io_tx_color(_io->idf_handle(), cmd, data.data(), data.size()));
}

result<void> ssd1680::_drain() {
    // tx_param waits for all queued color transfers to finish, so a NOP
    // guarantees the caller's buffer is no longer referenced by DMA.
    return _cmd(cmd_nop);
}

result<void> ssd1680::_init_scan() {
    // Shared scan configuration for both init paths: software reset, gate
    // MUX for the panel height, and the address increment direction —
    // mirrors flip to decrement on the respective axis, paired with the
    // reflected windows _set_ram_window programs.
    if (auto r = _cmd(cmd_sw_reset); !r) {
        return r;
    }
    if (auto r = wait_busy(); !r) {
        return r;
    }
    const auto mux = static_cast<uint16_t>(height() - 1);
    if (auto r = _cmd(cmd_driver_output, {static_cast<uint8_t>(mux & 0xFF), static_cast<uint8_t>(mux >> 8), 0x00});
        !r) {
        return r;
    }
    uint8_t entry = 0x03;
    if (_mirror_x) {
        entry &= static_cast<uint8_t>(~0x01);
    }
    if (_mirror_y) {
        entry &= static_cast<uint8_t>(~0x02);
    }
    return _cmd(cmd_data_entry, {entry});
}

result<void> ssd1680::_init() {
    if (auto r = _init_scan(); !r) {
        return r;
    }
    if (auto r = _cmd(cmd_border, {border_normal}); !r) {
        return r;
    }
    if (auto r = _cmd(cmd_temp_sensor, {0x80}); !r) { // internal sensor
        return r;
    }
    if (auto r = _set_ram_window({0, 0, width(), height()}); !r) {
        return r;
    }
    return wait_busy();
}

result<void> ssd1680::_init_gray() {
    // Grayscale needs an explicitly written waveform (the OTP banks only
    // hold black/white waveforms): full re-init with the reference analog
    // settings, then load lut_gray4. The subsequent refresh displays with
    // the loaded LUT (seq_display) instead of reloading from OTP.
    if (auto r = _init_scan(); !r) {
        return r;
    }
    if (auto r = _cmd(cmd_analog_ctrl, {0x54}); !r) {
        return r;
    }
    if (auto r = _cmd(cmd_digital_ctrl, {0x3B}); !r) {
        return r;
    }
    if (auto r = _cmd(cmd_border, {0x00}); !r) { // border follows LUT VS L0 (white)
        return r;
    }
    if (auto r = _cmd(cmd_write_vcom, {0x1C}); !r) {
        return r;
    }
    if (auto r = _cmd(cmd_end_option, {0x22}); !r) {
        return r;
    }
    if (auto r = _cmd(cmd_gate_voltage, {0x17}); !r) {
        return r;
    }
    if (auto r = _cmd(cmd_source_voltage, {0x41, 0x00, 0x32}); !r) {
        return r;
    }
    if (auto r = _cmd(cmd_update_ctrl1, {0x00, 0x80}); !r) {
        return r;
    }
    if (auto r = _cmd(cmd_write_lut, lut_gray4); !r) {
        return r;
    }
    // Both RAM planes to a known all-white state, so pixels outside any
    // subsequently written region refresh cleanly. The shadow frame is
    // unused in gray mode (and refilled on the switch back to mono), so it
    // doubles as the blank buffer instead of a fresh allocation.
    std::ranges::fill(_shadow, 0x00);
    return _write_planes({0, 0, width(), height()}, _shadow, _shadow);
}

result<void> ssd1680::_set_ram_window(region r) {
    size_t x_start = r.x / 8;
    size_t x_end = x_start + (r.width + 7) / 8 - 1;
    size_t y_start = r.y;
    size_t y_end = r.y + r.height - 1;
    // Mirroring pairs address decrement with a reflected window, so the
    // start address is the mirrored position of the region's first byte.
    if (_mirror_x) {
        x_start = _stride() - 1 - x_start;
        x_end = _stride() - 1 - x_end;
    }
    if (_mirror_y) {
        y_start = height() - 1 - y_start;
        y_end = height() - 1 - y_end;
    }
    if (auto e = _cmd(cmd_ram_x_range, {static_cast<uint8_t>(x_start), static_cast<uint8_t>(x_end)}); !e) {
        return e;
    }
    if (auto e = _cmd(
            cmd_ram_y_range,
            {static_cast<uint8_t>(y_start & 0xFF),
             static_cast<uint8_t>(y_start >> 8),
             static_cast<uint8_t>(y_end & 0xFF),
             static_cast<uint8_t>(y_end >> 8)}
        );
        !e) {
        return e;
    }
    if (auto e = _cmd(cmd_ram_x_counter, {static_cast<uint8_t>(x_start)}); !e) {
        return e;
    }
    return _cmd(cmd_ram_y_counter, {static_cast<uint8_t>(y_start & 0xFF), static_cast<uint8_t>(y_start >> 8)});
}

result<void>
ssd1680::_write_planes(region window, std::span<const uint8_t> new_plane, std::span<const uint8_t> old_plane) {
    if (auto r = _set_ram_window(window); !r) {
        return r;
    }
    if (auto r = _stream(cmd_write_ram, new_plane); !r) {
        return r;
    }
    if (auto r = _set_ram_window(window); !r) { // rewind the address counters
        return r;
    }
    if (auto r = _stream(cmd_write_ram_old, old_plane); !r) {
        return r;
    }
    return _drain();
}

result<void> ssd1680::_sync_old_plane() {
    // Re-send the frame just displayed to the previous-image plane, so the
    // next partial refresh diffs against what is actually on the glass.
    if (auto r = _set_ram_window({0, 0, width(), height()}); !r) {
        return r;
    }
    if (auto r = _stream(cmd_write_ram_old, _shadow); !r) {
        return r;
    }
    return _drain();
}

} // namespace idfxx::epaper
