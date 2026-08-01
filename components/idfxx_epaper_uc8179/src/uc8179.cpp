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
#include <utility>
#include <vector>

using namespace std::chrono_literals;

namespace idfxx::epaper {

namespace {

// UC8179 command set (UltraChip UC8179 datasheet; sequences and LUT tables
// follow Seeed Studio's Seeed_GFX UC8179 ePaper driver, MIT/BSD-licensed,
// validated on the GDEY075T7 glass).
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
constexpr uint8_t cmd_tcon = 0x60;           // TCON (gate/source non-overlap)
constexpr uint8_t cmd_resolution = 0x61;     // Resolution setting
constexpr uint8_t cmd_vcom_dc = 0x82;        // VCOM DC level
constexpr uint8_t cmd_partial_window = 0x90; // Partial window bounds
constexpr uint8_t cmd_partial_in = 0x91;     // Enter partial (windowed) mode
constexpr uint8_t cmd_partial_out = 0x92;    // Exit partial mode
constexpr uint8_t cmd_cascade = 0xE0;        // Cascade setting (external temperature enable)
constexpr uint8_t cmd_forced_temp = 0xE5;    // Forced temperature (waveform bank selection)

// VCOM and data interval values (cmd_vcom_interval parameters). The first
// parameter's low bits matter beyond the border behaviour: DDX (bits 1:0)
// selects the image-data polarity (01 = bit 1 is white, matching the
// framebuffer encoding) and N2OCP (bit 3) makes the controller copy the
// new-image RAM into the previous-image RAM when a refresh completes, which
// keeps the differential (partial) waveform's old-image plane current
// without any bookkeeping writes. Every value used around a refresh must
// keep DDX = 01 and N2OCP set.
constexpr std::initializer_list<uint8_t> border_normal{0x29, 0x07};
// Border held (no drive) while a partial window is active or a partial
// refresh runs, so the panel edge does not flicker.
constexpr std::initializer_list<uint8_t> border_hold{0xA9, 0x07};
// Border floating for deep sleep.
constexpr std::initializer_list<uint8_t> border_float{0xF7};

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

// Controller limits (800 sources x 600 gates).
constexpr size_t max_width = 800;
constexpr size_t max_height = 600;

} // namespace

panel_io::spi_config uc8179::spi_io_config(gpio cs, gpio dc, freq::hertz pclk) noexcept {
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
    if (auto r = configure_control_lines(config.busy_gpio, config.reset_gpio); !r) {
        return error(r.error());
    }

    uc8179 display(raw_tag{}, panel_io, config);
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
    , _io(std::exchange(other._io, nullptr))
    , _waveform(other._waveform) {}

uc8179& uc8179::operator=(uc8179&& other) noexcept {
    if (this != &other) {
        if (_io != nullptr && !asleep()) {
            (void)_enter_deep_sleep();
        }
        panel::operator=(std::move(other));
        _io = std::exchange(other._io, nullptr);
        _waveform = other._waveform;
    }
    return *this;
}

uc8179::~uc8179() {
    // Leaving the controller powered degrades the glass; power off and park
    // it in deep sleep unless the caller already did.
    if (_io != nullptr && !asleep()) {
        (void)_enter_deep_sleep();
    }
}

// =============================================================================
// epaper::panel hooks
// =============================================================================

result<void> uc8179::do_write(const mono_framebuffer& fb, size_t row_start, size_t row_end, size_t x, size_t y) {
    if (_io == nullptr) {
        return error(errc::invalid_state);
    }
    const size_t stride = fb.stride_bytes();
    const size_t rows = row_end - row_start;
    // The framebuffer's encoding (bit 1 = white) matches the controller's
    // new-image plane directly; the band streams as one transfer into a
    // partial window covering it (esp_lcd chunks it to the SPI limit).
    if (auto r = _window_begin({x, y + row_start, fb.width(), rows}); !r) {
        return r;
    }
    if (auto r = _stream(cmd_data_new, fb.data().subspan(row_start * stride, rows * stride)); !r) {
        return r;
    }
    // Exiting the window waits for the queued transfer, so the caller may
    // mutate the framebuffer immediately.
    return _window_end();
}

result<void> uc8179::do_write(const gray4_framebuffer& fb, size_t row_start, size_t row_end, size_t x, size_t y) {
    if (_io == nullptr) {
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
            if (auto r = _window_begin({x, y + row, fb.width(), rows}); !r) {
                return r;
            }
            if (auto r = _stream(cmd, std::span(staging).first(band.size())); !r) {
                return r;
            }
            if (auto r = _window_end(); !r) {
                return r;
            }
        }
    }
    return {};
}

result<void> uc8179::do_clear() {
    if (_io == nullptr) {
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

result<void> uc8179::do_refresh(refresh_mode mode) {
    if (_io == nullptr) {
        return error(errc::invalid_state);
    }
    if (color_mode() == color_mode::gray4) {
        if (mode != refresh_mode::full) {
            return error(errc::not_supported);
        }
        // do_set_color_mode already loaded the grayscale waveform.
        if (auto r = _ensure_waveform(waveform::gray); !r) {
            return r;
        }
        // Hold the border: the grayscale LUTs do not define a border drive.
        if (auto r = _cmd(cmd_vcom_interval, border_hold); !r) {
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
    // Hold the border on differential updates so it does not flicker.
    if (auto r = _cmd(cmd_vcom_interval, mode == refresh_mode::partial ? border_hold : border_normal); !r) {
        return r;
    }
    return _update();
}

result<void> uc8179::do_set_color_mode(enum color_mode mode) {
    if (_io == nullptr) {
        return error(errc::invalid_state);
    }
    if (mode == color_mode::gray4) {
        return _ensure_waveform(waveform::gray);
    }
    return _ensure_waveform(waveform::full);
}

result<void> uc8179::do_sleep() {
    if (_io == nullptr) {
        return error(errc::invalid_state);
    }
    return _enter_deep_sleep();
}

result<void> uc8179::do_wake() {
    if (_io == nullptr) {
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
    return wrap(esp_lcd_panel_io_tx_param(_io->idf_handle(), cmd, nullptr, 0));
}

result<void> uc8179::_cmd(uint8_t cmd, std::initializer_list<uint8_t> params) {
    return wrap(esp_lcd_panel_io_tx_param(_io->idf_handle(), cmd, params.begin(), params.size()));
}

result<void> uc8179::_cmd(uint8_t cmd, std::span<const uint8_t> params) {
    return wrap(esp_lcd_panel_io_tx_param(_io->idf_handle(), cmd, params.data(), params.size()));
}

result<void> uc8179::_stream(int cmd, std::span<const uint8_t> data) {
    // A negative cmd continues the previous data stream without a command
    // phase (esp_lcd convention).
    return wrap(esp_lcd_panel_io_tx_color(_io->idf_handle(), cmd, data.data(), data.size()));
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
    if (auto r = _cmd(cmd_vcom_interval, border_normal); !r) {
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
    if (auto r = _cmd(cmd_vcom_interval, border_normal); !r) {
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
    // OTP-waveform initialization for 4-level grayscale: the factory 4-gray
    // waveform bank is selected by a forced temperature of 0x5F (per the
    // vendor sequence). The glass-specific factory waveform ghosts less than
    // the generic register-LUT tables.
    if (auto r = _cmd(cmd_power_setting, {0x07, 0x07, 0x3F, 0x3F}); !r) {
        return r;
    }
    if (auto r = _cmd(cmd_booster, {0x27, 0x27, 0x18, 0x17}); !r) {
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
    if (auto r = _cmd(cmd_vcom_interval, border_normal); !r) {
        return r;
    }
    if (auto r = _cmd(cmd_cascade, {0x02}); !r) { // temperature from the forced-temperature register
        return r;
    }
    if (auto r = _cmd(cmd_forced_temp, {0x5F}); !r) {
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
    // (PLL, VCOM DC, temperature overrides), so start the new one from a
    // known state with a hardware reset — the image RAM survives it.
    // Without a reset line the initialization still runs, but registers the
    // target sequence does not set keep their previous values.
    if (reset_gpio().is_connected()) {
        if (auto r = hardware_reset(); !r) {
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
    // Hold the border while the partial window is active.
    if (auto e = _cmd(cmd_vcom_interval, border_hold); !e) {
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

result<void> uc8179::_clear_ram(bool include_old_plane) {
    // Set the image planes to white. The constant fill streams through a
    // small buffer with command-less continuation transfers, so no
    // frame-sized allocation is needed (48 KB at 800x480); the buffer's
    // content never changes, so reuse across queued transfers is safe.
    std::array<uint8_t, 1024> white;
    white.fill(0xFF);
    const size_t plane_bytes = (width() + 7) / 8 * height();
    if (auto r = _window_begin({0, 0, width(), height()}); !r) {
        return r;
    }
    for (const uint8_t cmd : include_old_plane ? std::initializer_list<uint8_t>{cmd_data_old, cmd_data_new}
                                               : std::initializer_list<uint8_t>{cmd_data_new}) {
        for (size_t sent = 0; sent < plane_bytes; sent += white.size()) {
            const auto chunk = std::span(white).first(std::min(white.size(), plane_bytes - sent));
            if (auto r = _stream(sent == 0 ? cmd : -1, chunk); !r) {
                return r;
            }
        }
    }
    return _window_end();
}

result<void> uc8179::_update() {
    if (auto r = _cmd(cmd_refresh); !r) {
        return r;
    }
    // Give the controller time to assert BUSY before polling it.
    idfxx::delay(10ms);
    return wait_busy();
    // No old-plane bookkeeping: the N2OCP bit set in the VCOM/data-interval
    // register makes the controller copy the new-image RAM to the
    // previous-image RAM when the refresh completes.
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
