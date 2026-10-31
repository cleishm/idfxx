// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#include <idfxx/epaper/gray4_framebuffer>
#include <idfxx/epaper/mono_framebuffer>
#include <idfxx/epaper/uc8179>
#include <idfxx/memory>
#include <idfxx/sched>

#include <algorithm>
#include <array>
#include <chrono>
#include <esp_lcd_panel_io.h>
#include <optional>
#include <utility>
#include <vector>

using namespace std::chrono_literals;

namespace idfxx::epaper {

namespace {

// UC8179 command set (UltraChip UC8179 datasheet; sequences validated on the
// GDEY075T7 glass; see the notice above the waveform tables below for the
// vendor-derived parts).
constexpr uint8_t cmd_panel_setting = 0x00;  // Panel setting (LUT source, scan direction)
constexpr uint8_t cmd_power_setting = 0x01;  // Power setting (gate/source voltages)
constexpr uint8_t cmd_power_off = 0x02;      // Power off the drive rails
constexpr uint8_t cmd_power_on = 0x04;       // Power on the drive rails
constexpr uint8_t cmd_booster = 0x06;        // Booster soft start
constexpr uint8_t cmd_deep_sleep = 0x07;     // Deep sleep (parameter 0xA5)
constexpr uint8_t cmd_data_old = 0x10;       // Write RAM (previous image plane / gray plane 0)
constexpr uint8_t cmd_refresh = 0x12;        // Display refresh
constexpr uint8_t cmd_data_new = 0x13;       // Write RAM (new image plane / gray plane 1)
constexpr uint8_t cmd_dual_spi = 0x15;       // Dual SPI mode (off)
constexpr uint8_t cmd_lut_vcom = 0x20;       // VCOM LUT
constexpr uint8_t cmd_lut_ww = 0x21;         // White-to-white LUT
constexpr uint8_t cmd_lut_kw = 0x22;         // Black-to-white LUT
constexpr uint8_t cmd_lut_wk = 0x23;         // White-to-black LUT
constexpr uint8_t cmd_lut_kk = 0x24;         // Black-to-black LUT
constexpr uint8_t cmd_pll = 0x30;            // PLL control (frame rate)
constexpr uint8_t cmd_vcom_interval = 0x50;  // VCOM and data interval (border behaviour)
constexpr uint8_t cmd_end_voltage = 0x52;    // End voltage setting (VCOM/border level after the LUTs)
constexpr uint8_t cmd_tcon = 0x60;           // TCON (gate/source non-overlap)
constexpr uint8_t cmd_resolution = 0x61;     // Resolution setting
constexpr uint8_t cmd_vcom_dc = 0x82;        // VCOM DC level
constexpr uint8_t cmd_partial_window = 0x90; // Partial window bounds
constexpr uint8_t cmd_partial_in = 0x91;     // Enter partial (windowed) mode
constexpr uint8_t cmd_partial_out = 0x92;    // Exit partial mode
constexpr uint8_t cmd_cascade = 0xE0;        // Cascade setting (external temperature enable)
constexpr uint8_t cmd_power_saving = 0xE3;   // Power saving (PWS)
constexpr uint8_t cmd_forced_temp = 0xE5;    // Forced temperature (waveform bank selection)

// VCOM and data interval values (cmd_vcom_interval parameters). The first
// parameter's low bits matter beyond the border behaviour: DDX (bits 1:0)
// selects the image-data polarity (01 = bit 1 is white, matching the
// framebuffer encoding) and N2OCP (bit 3) makes the controller copy the
// new-image RAM into the previous-image RAM when a refresh completes, which
// keeps the differential (partial) waveform's old-image plane current
// without any bookkeeping writes. Every value used around a refresh must
// keep DDX = 01 and N2OCP set.
constexpr uint8_t cdi_ddx_n2ocp = 0x09; // DDX = 01, N2OCP set
// The high bits select the border drive: BDZ (bit 7) floats the border
// electrode (Hi-Z); otherwise BDV (bits 5:4) picks the LUT that drives it.
constexpr uint8_t cdi_bdz = 0x80;    // Border Hi-Z (BDV is then ignored)
constexpr uint8_t cdi_bdv_kk = 0x00; // Border follows LUTKK
constexpr uint8_t cdi_bdv_wk = 0x10; // Border follows LUTWK
constexpr uint8_t cdi_bdv_kw = 0x20; // Border follows LUTKW
// The second parameter: the VCOM and data interval (CDI).
constexpr uint8_t cdi_interval = 0x07;

// Returns the cmd_vcom_interval parameters driving the border as `b` on a
// refresh using the monochrome (`gray` false) or grayscale (`gray` true)
// waveform bank, or floating it whatever the setting when `held` (partial
// refreshes and active partial windows, so the edge does not flicker).
// In the monochrome banks the border follows LUTKW (black-to-white, so a
// white border flashes with the refresh) for white and LUTWK
// (white-to-black) for black. White is not a border option in the
// grayscale bank, whose LUTKW and LUTWK carry gray levels, so a white
// setting holds the border there; LUTKK drives it black.
constexpr std::array<uint8_t, 2> border_params(enum border b, bool gray, bool held) noexcept {
    auto drive = static_cast<uint8_t>(cdi_bdz | cdi_bdv_kw); // Hi-Z, as in the vendor hold value
    if (!held) {
        switch (b) {
        case border::white:
            if (!gray) {
                drive = cdi_bdv_kw;
            }
            break;
        case border::black:
            drive = gray ? cdi_bdv_kk : cdi_bdv_wk;
            break;
        case border::hold:
            break;
        }
    }
    return {static_cast<uint8_t>(drive | cdi_ddx_n2ocp), cdi_interval};
}

// First-parameter values per the UC8179 datasheet (0x29 and 0xA9 are the
// vendor sequences' white-drive and hold values).
static_assert(border_params(border::white, false, false) == std::array<uint8_t, 2>{0x29, 0x07});
static_assert(border_params(border::black, false, false) == std::array<uint8_t, 2>{0x19, 0x07});
static_assert(border_params(border::hold, false, false) == std::array<uint8_t, 2>{0xA9, 0x07});
static_assert(border_params(border::white, true, false) == std::array<uint8_t, 2>{0xA9, 0x07});
static_assert(border_params(border::black, true, false) == std::array<uint8_t, 2>{0x09, 0x07});
static_assert(border_params(border::black, false, true) == std::array<uint8_t, 2>{0xA9, 0x07});

// Border held (Hi-Z) while a partial window is active.
constexpr auto border_held = border_params(border::hold, false, true);
// Border driven white, as the vendor initialization sequences program it;
// do_refresh programs the configured drive before every update.
constexpr auto border_init = border_params(border::white, false, false);
// Border floating for deep sleep (the vendor sequence's value; powering off
// floats the border anyway).
constexpr std::initializer_list<uint8_t> border_float{0xF7};

// The fast and grayscale waveform tables below, and the initialization
// sequences that load them (_init_fast, _init_gray), are from Seeed Studio's
// Seeed_GFX UC8179 driver (the vendor driver of the XIAO ePaper Display Board
// EE05), used under the MIT License:
//
// Copyright (c) 2026 Seeed Studio
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

// Register-written waveform tables for the fast refresh mode (42 bytes
// each, per-panel-batch tunable).
constexpr std::array<uint8_t, 42> lut_vcom_fast{
    0x26, 0x0F, 0x18, 0x18, 0x14, 0x01, 0x00, 0x0A, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};
constexpr std::array<uint8_t, 42> lut_ww_fast{
    0x55, 0x06, 0x0C, 0x17, 0x02, 0x01, 0x2A, 0x02, 0x1C, 0x02, 0x0D, 0x01, 0x80, 0x02,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};
constexpr std::array<uint8_t, 42> lut_kw_fast = lut_ww_fast;
constexpr std::array<uint8_t, 42> lut_wk_fast{
    0xAA, 0x06, 0x0C, 0x17, 0x02, 0x01, 0x15, 0x02, 0x1C, 0x02, 0x0D, 0x01, 0x40, 0x02,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};
constexpr std::array<uint8_t, 42> lut_kk_fast = lut_wk_fast;

// Register-written waveform tables for 4-level grayscale (42 bytes each):
// the glass's factory grayscale waveform, byte for byte.
constexpr std::array<uint8_t, 42> lut_vcom_gray{
    0x00, 0x00, 0x06, 0x08, 0x07, 0x01, 0x00, 0x06, 0x0A, 0x0B, 0x0A, 0x01, 0x00, 0x03,
    0x03, 0x00, 0x00, 0x03, 0x00, 0x05, 0x09, 0x06, 0x06, 0x01, 0x00, 0x02, 0x02, 0x0A,
    0x0A, 0x01, 0x00, 0x0A, 0x11, 0x06, 0x07, 0x01, 0x00, 0x02, 0x01, 0x02, 0x01, 0x01,
};
constexpr std::array<uint8_t, 42> lut_ww_gray{
    0x15, 0x00, 0x06, 0x08, 0x07, 0x01, 0x54, 0x06, 0x0A, 0x0B, 0x0A, 0x01, 0x90, 0x03,
    0x03, 0x00, 0x00, 0x03, 0x2A, 0x05, 0x09, 0x06, 0x06, 0x01, 0xAA, 0x02, 0x02, 0x0A,
    0x0A, 0x01, 0x00, 0x0A, 0x11, 0x06, 0x07, 0x01, 0x28, 0x02, 0x01, 0x02, 0x01, 0x01,
};
constexpr std::array<uint8_t, 42> lut_kw_gray{
    0x2A, 0x00, 0x06, 0x08, 0x07, 0x01, 0x59, 0x06, 0x0A, 0x0B, 0x0A, 0x01, 0x90, 0x03,
    0x03, 0x00, 0x00, 0x03, 0x5A, 0x05, 0x09, 0x06, 0x06, 0x01, 0xA8, 0x02, 0x02, 0x0A,
    0x0A, 0x01, 0x45, 0x0A, 0x11, 0x06, 0x07, 0x01, 0xA8, 0x02, 0x01, 0x02, 0x01, 0x01,
};
constexpr std::array<uint8_t, 42> lut_wk_gray{
    0x16, 0x00, 0x06, 0x08, 0x07, 0x01, 0xA0, 0x06, 0x0A, 0x0B, 0x0A, 0x01, 0x90, 0x03,
    0x03, 0x00, 0x00, 0x03, 0x99, 0x05, 0x09, 0x06, 0x06, 0x01, 0xA0, 0x02, 0x02, 0x0A,
    0x0A, 0x01, 0x40, 0x0A, 0x11, 0x06, 0x07, 0x01, 0x20, 0x02, 0x01, 0x02, 0x01, 0x01,
};
constexpr std::array<uint8_t, 42> lut_kk_gray{
    0x26, 0x00, 0x06, 0x08, 0x07, 0x01, 0x6A, 0x06, 0x0A, 0x0B, 0x0A, 0x01, 0x90, 0x03,
    0x03, 0x00, 0x00, 0x03, 0x65, 0x05, 0x09, 0x06, 0x06, 0x01, 0x50, 0x02, 0x02, 0x0A,
    0x0A, 0x01, 0x10, 0x0A, 0x11, 0x06, 0x07, 0x01, 0x10, 0x02, 0x01, 0x02, 0x01, 0x01,
};

// Controller limits (800 sources x 600 gates).
constexpr size_t max_width = 800;
constexpr size_t max_height = 600;

// Returns the PLL control (frame rate) code for `rate`, or nullopt if the
// controller has no such frame rate.
constexpr std::optional<uint8_t> pll_code(quanta::frequency::hertz rate) noexcept {
    constexpr std::array<int64_t, 16> rates{5, 10, 15, 20, 30, 40, 50, 60, 70, 80, 90, 100, 110, 130, 150, 200};
    for (size_t i = 0; i < rates.size(); ++i) {
        if (rates[i] == rate.count()) {
            return static_cast<uint8_t>(i);
        }
    }
    return std::nullopt;
}

// Returns the power setting VDH/VDL level code for a source drive voltage
// magnitude of `v` (2.4 V to 15 V in 0.2 V steps), or nullopt if out of range
// or off-step.
constexpr std::optional<uint8_t> source_code(quanta::electro::millivolts v) noexcept {
    const int64_t mv = v.count();
    if (mv < 2'400 || mv > 15'000 || (mv - 2'400) % 200 != 0) {
        return std::nullopt;
    }
    return static_cast<uint8_t>((mv - 2'400) / 200);
}

static_assert(pll_code(quanta::frequency::hertz{50}) == 6);
static_assert(pll_code(quanta::frequency::hertz{20}) == 3);
static_assert(pll_code(quanta::frequency::hertz{30}) == 4);
static_assert(pll_code(quanta::frequency::hertz{100}) == 11);
static_assert(pll_code(quanta::frequency::hertz{200}) == 15);
static_assert(pll_code(quanta::frequency::hertz{25}) == std::nullopt);
static_assert(source_code(quanta::electro::millivolts{15'000}) == 0x3F);
static_assert(source_code(quanta::electro::millivolts{10'000}) == 0x26);
static_assert(source_code(quanta::electro::millivolts{2'400}) == 0x00);
static_assert(source_code(quanta::electro::millivolts{10'100}) == std::nullopt);
static_assert(source_code(quanta::electro::millivolts{15'200}) == std::nullopt);
static_assert(source_code(quanta::electro::millivolts{2'200}) == std::nullopt);

} // namespace

panel_io::spi_config uc8179::spi_io_config(gpio cs, gpio dc, quanta::frequency::hertz pclk) noexcept {
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

result<uc8179> uc8179::make(idfxx::panel_io& panel_io, uc8179::config config) {
    if (config.width == 0 || config.width % 8 != 0 || config.width > max_width || config.height == 0 ||
        config.height > max_height) {
        return error(errc::invalid_arg);
    }
    const auto gray_frame_rate_code = pll_code(config.gray4_frame_rate);
    const auto gray_source_code = source_code(config.gray4_source_voltage);
    if (!gray_frame_rate_code || !gray_source_code) {
        return error(errc::invalid_arg);
    }
    if (auto r = configure_control_lines(config.busy_gpio, config.reset_gpio); !r) {
        return error(r.error());
    }

    uc8179 display(raw_tag{}, panel_io, config);
    display._gray_frame_rate_code = *gray_frame_rate_code;
    display._gray_source_code = *gray_source_code;
    // Store the configured border drive before any controller I/O; it is
    // applied by do_refresh.
    if (auto r = display.try_set_border(config.border); !r) {
        return error(r.error());
    }
    if (config.reset_gpio.is_connected()) {
        if (auto r = display.hardware_reset(); !r) {
            return error(r.error());
        }
    }
    if (auto r = display._init_full(); !r) {
        return error(r.error());
    }
    // Both planes: after power-up the previous image is unknown, so give a
    // first partial-frame write a known background rather than RAM noise.
    if (auto r = display._clear_ram(true); !r) {
        return error(r.error());
    }
    return display;
}

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
uc8179::uc8179(idfxx::panel_io& panel_io, uc8179::config config)
    : uc8179(unwrap(make(panel_io, config))) {}
#endif

uc8179::uc8179(uc8179&& other) noexcept
    : panel(std::move(other))
    , _io_handle(std::exchange(other._io_handle, nullptr))
    , _waveform(other._waveform)
    , _gray_frame_rate_code(other._gray_frame_rate_code)
    , _gray_source_code(other._gray_source_code) {}

uc8179& uc8179::operator=(uc8179&& other) noexcept {
    if (this != &other) {
        if (_io_handle != nullptr && !asleep()) {
            (void)wait_busy();
            (void)_enter_deep_sleep();
        }
        panel::operator=(std::move(other));
        _io_handle = std::exchange(other._io_handle, nullptr);
        _waveform = other._waveform;
        _gray_frame_rate_code = other._gray_frame_rate_code;
        _gray_source_code = other._gray_source_code;
    }
    return *this;
}

uc8179::~uc8179() {
    // Leaving the controller powered degrades the glass; power off and park
    // it in deep sleep unless the caller already did. A refresh still
    // driving finishes first — powering off mid-waveform also stresses the
    // glass.
    if (_io_handle != nullptr && !asleep()) {
        (void)wait_busy();
        (void)_enter_deep_sleep();
    }
}

// =============================================================================
// epaper::panel hooks
// =============================================================================

result<void> uc8179::do_write(const mono_framebuffer& fb, size_t row_start, size_t row_end, size_t x, size_t y) {
    if (_io_handle == nullptr) {
        return error(errc::invalid_state);
    }
    const size_t stride = fb.stride_bytes();
    const size_t rows = row_end - row_start;
    // The framebuffer's encoding (bit 1 = white) matches the controller's
    // new-image plane directly; the band streams as one transfer into a
    // partial window covering it (esp_lcd chunks it to the SPI limit).
    // Exiting the window waits for the queued transfer, so the caller may
    // mutate the framebuffer immediately.
    return _in_window({x, y + row_start, fb.width(), rows}, [&] {
        return _stream(cmd_data_new, fb.data().subspan(row_start * stride, rows * stride));
    });
}

result<void> uc8179::do_write(const gray4_framebuffer& fb, size_t row_start, size_t row_end, size_t x, size_t y) {
    if (_io_handle == nullptr) {
        return error(errc::invalid_state);
    }
    const size_t stride = fb.stride_bytes();
    // The controller's gray planes use the opposite bit sense from the
    // canonical framebuffer encoding, so plane bytes are inverted through a
    // bounded staging buffer on their way out: plane 0 to the 0x10 RAM
    // bank, plane 1 to the 0x13 RAM bank. Each sub-band gets its own
    // partial window; exiting the window drains the queued transfer, so the
    // staging buffer can be refilled for the next sub-band (and a full
    // 800x480 frame never needs a frame-sized staging allocation).
    constexpr size_t staging_bytes_max = 4096;
    const size_t band_rows = std::max<size_t>(1, staging_bytes_max / stride);
    std::vector<uint8_t, dram_allocator<uint8_t>> staging(band_rows * stride);
    const struct {
        uint8_t cmd;
        std::span<const uint8_t> plane;
    } planes[]{{cmd_data_old, fb.plane(0)}, {cmd_data_new, fb.plane(1)}};
    for (const auto& [cmd, plane] : planes) {
        for (size_t row = row_start; row < row_end; row += band_rows) {
            const size_t rows = std::min(band_rows, row_end - row);
            const auto band = plane.subspan(row * stride, rows * stride);
            std::ranges::transform(band, staging.begin(), [](uint8_t b) { return static_cast<uint8_t>(~b); });
            const auto data = std::span(staging).first(band.size());
            if (auto r = _in_window({x, y + row, fb.width(), rows}, [&] { return _stream(cmd, data); }); !r) {
                return r;
            }
        }
    }
    return {};
}

result<void> uc8179::do_clear() {
    if (_io_handle == nullptr) {
        return error(errc::invalid_state);
    }
    // The all-ones fill is white in both color modes: set bits are white in
    // the mono planes, and RAM (1, 1) is the gray-plane encoding of
    // gray4::white (the planes carry the inverted level bits). In mono mode
    // only the new-image plane is cleared: the previous-image plane keeps
    // the image on the glass, so the next refresh applies the deep
    // black-to-white erase waveform to previously-black pixels rather than
    // the gentler white-to-white one. In gray4 mode both planes carry the
    // image and must be written.
    return _clear_ram(color_mode() == color_mode::gray4);
}

result<void> uc8179::do_restore_baseline(const mono_framebuffer& fb) {
    if (_io_handle == nullptr) {
        return error(errc::invalid_state);
    }
    // A partial refresh drives each pixel from its previous-image (0x10)
    // value to its new-image (0x13) value, so the frame on the glass goes
    // into both planes: a later region-only write then leaves every other
    // pixel unchanged. The planes share the framebuffer's encoding (the
    // controller copies one into the other after a refresh), and both
    // stream into one full-panel window, as in _clear_ram. Exiting the
    // window waits for the queued transfers, so the caller may mutate the
    // framebuffer immediately.
    return _in_window({0, 0, width(), height()}, [&] {
        auto r = _stream(cmd_data_old, fb.data());
        return r ? _stream(cmd_data_new, fb.data()) : r;
    });
}

result<void> uc8179::do_refresh(refresh_mode mode) {
    if (_io_handle == nullptr) {
        return error(errc::invalid_state);
    }
    if (color_mode() == color_mode::gray4) {
        // do_set_color_mode already loaded the grayscale waveform.
        if (auto r = _ensure_waveform(waveform::gray); !r) {
            return r;
        }
        // White is not a border option in the grayscale waveform bank, so a
        // white setting holds the border; LUTKK drives it black.
        if (auto r = _cmd(cmd_vcom_interval, border_params(border(), true, false)); !r) {
            return r;
        }
        return _update();
    }

    const waveform target = mode == refresh_mode::full ? waveform::full
        : mode == refresh_mode::fast                   ? waveform::fast
                                                       : waveform::partial;
    if (auto r = _ensure_waveform(target); !r) {
        return r;
    }
    // Drive the border as configured, but always hold it on differential
    // updates so it does not flicker.
    if (auto r = _cmd(cmd_vcom_interval, border_params(border(), false, mode == refresh_mode::partial)); !r) {
        return r;
    }
    return _update();
}

bool uc8179::do_supports(enum color_mode mode) const noexcept {
    return mode == color_mode::gray4;
}

bool uc8179::do_supports(refresh_mode mode) const noexcept {
    // The grayscale waveform has no fast or differential variant.
    return color_mode() != color_mode::gray4 || mode == refresh_mode::full;
}

bool uc8179::do_supports(enum border b) const noexcept {
    switch (b) {
    case border::white:
    case border::black:
    case border::hold:
        return true;
    }
    return false;
}

result<void> uc8179::do_set_color_mode(enum color_mode mode) {
    if (_io_handle == nullptr) {
        return error(errc::invalid_state);
    }
    if (mode == color_mode::gray4) {
        return _ensure_waveform(waveform::gray);
    }
    return _ensure_waveform(waveform::full);
}

result<void> uc8179::do_sleep() {
    if (_io_handle == nullptr) {
        return error(errc::invalid_state);
    }
    return _enter_deep_sleep();
}

result<void> uc8179::do_wake() {
    if (_io_handle == nullptr) {
        return error(errc::invalid_state);
    }
    if (auto r = hardware_reset(); !r) {
        return r;
    }
    if (color_mode() == color_mode::gray4) {
        return _init_gray();
    }
    return _init_full();
}

// =============================================================================
// Command sequencing
// =============================================================================

result<void> uc8179::_cmd(uint8_t cmd) {
    return wrap(esp_lcd_panel_io_tx_param(_io_handle, cmd, nullptr, 0));
}

result<void> uc8179::_cmd(uint8_t cmd, std::initializer_list<uint8_t> params) {
    return wrap(esp_lcd_panel_io_tx_param(_io_handle, cmd, params.begin(), params.size()));
}

result<void> uc8179::_cmd(uint8_t cmd, std::span<const uint8_t> params) {
    return wrap(esp_lcd_panel_io_tx_param(_io_handle, cmd, params.data(), params.size()));
}

result<void> uc8179::_stream(int cmd, std::span<const uint8_t> data) {
    // A negative cmd continues the previous data stream without a command
    // phase (esp_lcd convention).
    return wrap(esp_lcd_panel_io_tx_color(_io_handle, cmd, data.data(), data.size()));
}

result<void> uc8179::_power_on() {
    if (auto r = _cmd(cmd_power_on); !r) {
        return r;
    }
    idfxx::delay(100ms);
    return wait_busy();
}

result<void> uc8179::_set_resolution() {
    const auto w = static_cast<uint16_t>(width());
    const auto h = static_cast<uint16_t>(height());
    return _cmd(
        cmd_resolution,
        {static_cast<uint8_t>(w >> 8),
         static_cast<uint8_t>(w & 0xFF),
         static_cast<uint8_t>(h >> 8),
         static_cast<uint8_t>(h & 0xFF)}
    );
}

result<void> uc8179::_load_luts(
    std::span<const uint8_t> vcom,
    std::span<const uint8_t> ww,
    std::span<const uint8_t> kw,
    std::span<const uint8_t> wk,
    std::span<const uint8_t> kk
) {
    if (auto r = _cmd(cmd_lut_vcom, vcom); !r) {
        return r;
    }
    if (auto r = _cmd(cmd_lut_ww, ww); !r) {
        return r;
    }
    if (auto r = _cmd(cmd_lut_kw, kw); !r) {
        return r;
    }
    if (auto r = _cmd(cmd_lut_wk, wk); !r) {
        return r;
    }
    return _cmd(cmd_lut_kk, kk);
}

result<void> uc8179::_init_full() {
    // OTP-waveform initialization for full refreshes.
    if (auto r = _cmd(cmd_power_setting, {0x07, 0x07, 0x3F, 0x3F}); !r) { // VGH/VGL 20V, VDH/VDL 15V
        return r;
    }
    if (auto r = _cmd(cmd_booster, {0x17, 0x17, 0x28, 0x17}); !r) {
        return r;
    }
    if (auto r = _power_on(); !r) {
        return r;
    }
    if (auto r = _cmd(cmd_panel_setting, {0x1F}); !r) { // KW mode, LUT from OTP
        return r;
    }
    if (auto r = _set_resolution(); !r) {
        return r;
    }
    if (auto r = _cmd(cmd_dual_spi, {0x00}); !r) {
        return r;
    }
    // The vendor value; do_refresh programs the configured border drive
    // before every update.
    if (auto r = _cmd(cmd_vcom_interval, border_init); !r) {
        return r;
    }
    if (auto r = _cmd(cmd_tcon, {0x22}); !r) {
        return r;
    }
    _waveform = waveform::full;
    return {};
}

result<void> uc8179::_init_fast() {
    // Register-LUT initialization for the shortened "fast" full waveform.
    if (auto r = _cmd(cmd_power_setting, {0x07, 0x17, 0x3F, 0x3F, 0x09}); !r) {
        return r;
    }
    if (auto r = _cmd(cmd_pll, {0x06}); !r) {
        return r;
    }
    if (auto r = _cmd(cmd_vcom_dc, {0x16}); !r) {
        return r;
    }
    if (auto r = _cmd(cmd_booster, {0x17, 0x17, 0x28, 0x17}); !r) {
        return r;
    }
    if (auto r = _power_on(); !r) {
        return r;
    }
    if (auto r = _cmd(cmd_panel_setting, {0x3F}); !r) { // KW mode, LUT from registers
        return r;
    }
    if (auto r = _set_resolution(); !r) {
        return r;
    }
    // The vendor value; do_refresh programs the configured border drive
    // before every update.
    if (auto r = _cmd(cmd_vcom_interval, border_init); !r) {
        return r;
    }
    if (auto r = _load_luts(lut_vcom_fast, lut_ww_fast, lut_kw_fast, lut_wk_fast, lut_kk_fast); !r) {
        return r;
    }
    _waveform = waveform::fast;
    return {};
}

result<void> uc8179::_init_partial() {
    // Partial (differential) refreshes use an OTP waveform bank selected by
    // a forced temperature. Runs against post-reset register defaults for
    // everything it does not set (per the vendor sequence).
    if (auto r = _cmd(cmd_panel_setting, {0x1F}); !r) {
        return r;
    }
    if (auto r = _set_resolution(); !r) {
        return r;
    }
    if (auto r = _power_on(); !r) {
        return r;
    }
    if (auto r = _cmd(cmd_cascade, {0x02}); !r) { // temperature from the forced-temperature register
        return r;
    }
    if (auto r = _cmd(cmd_forced_temp, {0x6E}); !r) {
        return r;
    }
    _waveform = waveform::partial;
    return {};
}

result<void> uc8179::_init_gray() {
    // Register-LUT initialization for 4-level grayscale. The tables are the
    // glass's factory grayscale waveform, loaded through registers so the
    // frame rate and source voltage are programmable: the factory bank fixes
    // them at 50 Hz and +/-15 V, where the drive supplies sag under
    // source-switching load and the gray levels follow the image content.
    const uint8_t src = _gray_source_code;
    if (auto r = _cmd(cmd_power_setting, {0x07, 0x17, src, src, 0x07}); !r) {
        return r;
    }
    if (auto r = _cmd(cmd_pll, {_gray_frame_rate_code}); !r) {
        return r;
    }
    if (auto r = _cmd(cmd_vcom_dc, {0x12}); !r) {
        return r;
    }
    if (auto r = _cmd(cmd_booster, {0x27, 0x27, 0x28, 0x17}); !r) {
        return r;
    }
    if (auto r = _power_on(); !r) {
        return r;
    }
    if (auto r = _cmd(cmd_panel_setting, {0x3F}); !r) { // KW mode, LUT from registers
        return r;
    }
    if (auto r = _set_resolution(); !r) {
        return r;
    }
    // The vendor value; do_refresh programs the configured border drive
    // before every update.
    if (auto r = _cmd(cmd_vcom_interval, border_init); !r) {
        return r;
    }
    if (auto r = _cmd(cmd_power_saving, {0x88}); !r) {
        return r;
    }
    if (auto r = _cmd(cmd_end_voltage, {0x00}); !r) {
        return r;
    }
    if (auto r = _load_luts(lut_vcom_gray, lut_ww_gray, lut_kw_gray, lut_wk_gray, lut_kk_gray); !r) {
        return r;
    }
    _waveform = waveform::gray;
    return {};
}

result<void> uc8179::_ensure_waveform(waveform target) {
    if (_waveform == target) {
        return {};
    }
    // Each waveform configuration leaves different sticky registers behind
    // (PLL, VCOM DC, temperature overrides). With a reset line every mode
    // starts from reset defaults — the image RAM survives the reset. Without
    // one the drive rails are powered down first, because the power and
    // clock registers must not be written while the rails are up, and
    // registers the target sequence does not set keep their previous values.
    if (reset_gpio().is_connected()) {
        if (auto r = hardware_reset(); !r) {
            return r;
        }
    } else {
        if (auto r = _cmd(cmd_power_off); !r) {
            return r;
        }
        if (auto r = wait_busy(); !r) {
            return r;
        }
    }
    switch (target) {
    case waveform::full:
        return _init_full();
    case waveform::fast:
        return _init_fast();
    case waveform::partial:
        return _init_partial();
    case waveform::gray:
        return _init_gray();
    }
    return error(errc::invalid_arg);
}

result<void> uc8179::_window_begin(region r) {
    // Hold the border while the window is active; do_refresh programs the
    // configured drive before each update.
    if (auto e = _cmd(cmd_vcom_interval, border_held); !e) {
        return e;
    }
    if (auto e = _cmd(cmd_partial_in); !e) {
        return e;
    }
    // Horizontal bounds cover whole bytes; end coordinates are inclusive
    // (the vendor sequences program these from exclusive ends as end - 1).
    const size_t x_start = r.x;
    const size_t x_end = r.x + (r.width + 7) / 8 * 8 - 1;
    const size_t y_start = r.y;
    const size_t y_end = r.y + r.height - 1;
    return _cmd(
        cmd_partial_window,
        {static_cast<uint8_t>(x_start >> 8),
         static_cast<uint8_t>(x_start & 0xFF),
         static_cast<uint8_t>(x_end >> 8),
         static_cast<uint8_t>(x_end & 0xFF),
         static_cast<uint8_t>(y_start >> 8),
         static_cast<uint8_t>(y_start & 0xFF),
         static_cast<uint8_t>(y_end >> 8),
         static_cast<uint8_t>(y_end & 0xFF),
         0x01}
    );
}

result<void> uc8179::_window_end() {
    // tx_param waits for all queued color transfers to finish, so exiting
    // the window doubles as a DMA drain for the streamed data.
    return _cmd(cmd_partial_out);
}

template<typename Write>
result<void> uc8179::_in_window(region r, Write&& write) {
    auto res = _window_begin(r);
    if (res) {
        res = write();
    }
    // Exit the window even after a failure: a failed stream may leave
    // earlier transfers still reading their data, and the controller would
    // otherwise stay in partial mode, where a refresh covers only the
    // window.
    auto end = _window_end();
    return res ? end : res;
}

result<void> uc8179::_clear_ram(bool include_old_plane) {
    // Set the image planes to white. The constant fill streams through a
    // small buffer with command-less continuation transfers, so no
    // frame-sized allocation is needed (48 KB at 800x480); the buffer's
    // content never changes, so reuse across queued transfers is safe.
    std::array<uint8_t, 1024> white;
    white.fill(0xFF);
    const size_t plane_bytes = (width() + 7) / 8 * height();
    return _in_window({0, 0, width(), height()}, [&]() -> result<void> {
        for (const uint8_t cmd : include_old_plane ? std::initializer_list<uint8_t>{cmd_data_old, cmd_data_new}
                                                   : std::initializer_list<uint8_t>{cmd_data_new}) {
            for (size_t sent = 0; sent < plane_bytes; sent += white.size()) {
                const auto chunk = std::span(white).first(std::min(white.size(), plane_bytes - sent));
                if (auto r = _stream(sent == 0 ? cmd : -1, chunk); !r) {
                    return r;
                }
            }
        }
        return {};
    });
}

result<void> uc8179::_update() {
    if (auto r = _cmd(cmd_refresh); !r) {
        return r;
    }
    // Give the controller time to assert BUSY before anyone polls it, then
    // return with the glass still driving: the base class waits for BUSY
    // (default do_wait) before the next controller command. No old-plane
    // bookkeeping to defer: the N2OCP bit set in the VCOM/data-interval
    // register makes the controller copy the new-image RAM to the
    // previous-image RAM when the refresh completes.
    idfxx::delay(10ms);
    return {};
}

result<void> uc8179::_enter_deep_sleep() {
    if (auto r = _cmd(cmd_vcom_interval, border_float); !r) {
        return r;
    }
    if (auto r = _cmd(cmd_power_off); !r) {
        return r;
    }
    if (auto r = wait_busy(); !r) {
        return r;
    }
    if (auto r = _cmd(cmd_deep_sleep, {0xA5}); !r) {
        return r;
    }
    // BUSY is not meaningful while entering deep sleep; give the controller
    // time to power down.
    idfxx::delay(100ms);
    return {};
}

} // namespace idfxx::epaper
