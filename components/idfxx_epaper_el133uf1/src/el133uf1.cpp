// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#include <idfxx/epaper/color6_framebuffer>
#include <idfxx/epaper/color>
#include <idfxx/epaper/el133uf1>
#include <idfxx/epaper/mono_framebuffer>
#include <idfxx/sched>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <mutex>
#include <utility>

using namespace std::chrono_literals;

namespace idfxx::epaper {

namespace {

// EL133UF1 command set. The controller IC is unpublished; the commands
// follow the UltraChip lineage (cf. UC8179), and the initialization
// sequence and register values follow Seeed Studio's GxEPD2 port and
// Seeed_GFX driver for the T133A01 module built on this glass
// (MIT/BSD-licensed).
constexpr uint8_t cmd_panel_setting = 0x00; // Panel setting
constexpr uint8_t cmd_power_setting = 0x01; // Power setting (gate/source voltages)
constexpr uint8_t cmd_power_off = 0x02;     // Power off the drive rails
constexpr uint8_t cmd_power_on = 0x04;      // Power on the drive rails
constexpr uint8_t cmd_booster_neg = 0x05;   // Booster soft start (negative rail)
constexpr uint8_t cmd_booster_pos = 0x06;   // Booster soft start (positive rail)
constexpr uint8_t cmd_deep_sleep = 0x07;    // Deep sleep (parameter 0xA5)
constexpr uint8_t cmd_data_start = 0x10;    // Write image data (4 bpp)
constexpr uint8_t cmd_refresh = 0x12;       // Display refresh
constexpr uint8_t cmd_pll = 0x30;           // PLL control (frame rate)
constexpr uint8_t cmd_vcom_interval = 0x50; // VCOM and data interval (border)
constexpr uint8_t cmd_tcon = 0x60;          // TCON (gate/source non-overlap)
constexpr uint8_t cmd_resolution = 0x61;    // Resolution setting
constexpr uint8_t cmd_reg_0x74 = 0x74;      // Vendor: analog block trims
constexpr uint8_t cmd_reg_0x86 = 0x86;      // Vendor: unknown
constexpr uint8_t cmd_reg_0xa5 = 0xA5;      // Vendor: DC/DC settings
constexpr uint8_t cmd_reg_0xb0 = 0xB0;      // Vendor: unknown
constexpr uint8_t cmd_reg_0xb1 = 0xB1;      // Vendor: unknown
constexpr uint8_t cmd_reg_0xb6 = 0xB6;      // Vendor: unknown
constexpr uint8_t cmd_reg_0xb7 = 0xB7;      // Vendor: unknown
constexpr uint8_t cmd_cascade = 0xE0;       // Cascade setting
constexpr uint8_t cmd_power_saving = 0xE3;  // Power saving
constexpr uint8_t cmd_reg_0xf0 = 0xF0;      // Vendor: unknown

// Six-color codes for the two pixels of a byte.
constexpr uint8_t pair_white = 0x11;
constexpr uint8_t code_black = std::to_underlying(color6::black);
constexpr uint8_t code_white = std::to_underlying(color6::white);

// Shadow-frame geometry: each controller drives half of every row.
constexpr size_t chip_width = el133uf1::native_width / 2; // 600 pixels
constexpr size_t plane_stride = chip_width / 2;           // 300 bytes
constexpr size_t plane_bytes = plane_stride * el133uf1::native_height;
constexpr size_t shadow_bytes = 2 * plane_bytes; // 960,000

// Vendor timing: the controllers need a short pause after every register
// write during initialization.
constexpr auto init_settle = 10ms;

// Largest transmit staged through the driver's DMA buffer.
constexpr size_t max_chunk_bytes = 4096;

// Holds the chip-select line(s) of a transfer low (selected) for its
// lifetime, releasing them high on every exit path. Declared after the
// device lock, so chip select releases before the bus does.
class chip_select {
public:
    explicit chip_select(gpio first, gpio second) noexcept
        : _first(first)
        , _second(second) {
        _first.set_level(gpio::level::low);
        if (_second.is_connected()) {
            _second.set_level(gpio::level::low);
        }
    }

    ~chip_select() {
        _first.set_level(gpio::level::high);
        if (_second.is_connected()) {
            _second.set_level(gpio::level::high);
        }
    }

    chip_select(const chip_select&) = delete;
    chip_select& operator=(const chip_select&) = delete;

private:
    gpio _first;
    gpio _second;
};

// Configures a chip-select line as an output, idling high (deselected).
// The level is latched before the output is enabled, so the line never
// glitches low.
result<void> configure_cs(gpio cs) {
    cs.set_level(gpio::level::high);
    return cs.try_set_direction(gpio::mode::output);
}

} // namespace

result<el133uf1> el133uf1::make(spi::master_bus& bus, el133uf1::config config) {
    if (!config.master_cs_gpio->is_connected() || !config.slave_cs_gpio->is_connected() ||
        !config.dc_gpio->is_connected()) {
        return error(errc::invalid_arg);
    }
    if (config.master_cs_gpio == config.slave_cs_gpio) {
        return error(errc::invalid_arg);
    }
    if (auto r = configure_control_lines(config.busy_gpio, config.reset_gpio); !r) {
        return error(r.error());
    }
    if (auto r = configure_cs(config.master_cs_gpio); !r) {
        return error(r.error());
    }
    if (auto r = configure_cs(config.slave_cs_gpio); !r) {
        return error(r.error());
    }
    gpio dc = config.dc_gpio;
    dc.set_level(gpio::level::low);
    if (auto r = dc.try_set_direction(gpio::mode::output); !r) {
        return error(r.error());
    }

    // The driver drives both chip selects itself, so the device has none.
    // The panel is write-only: there is no MISO phase.
    auto device = spi::master_device::make(
        bus,
        {
            .mode = 0,
            .clock_speed = config.pclk,
            .cs = gpio::nc(),
            .flags = spi::device_flags::halfduplex,
            .queue_size = 1,
        }
    );
    if (!device) {
        return error(device.error());
    }

    el133uf1 display(std::move(*device), config);
    // MALLOC_CAP_DMA memory is always internal RAM. Allocation failure
    // follows the project's OOM policy (bad_alloc / abort).
    display._chunk.resize(std::min(max_chunk_bytes, bus.max_transaction_length()));
    // The shadow starts as blank paper, matching what a fresh
    // color6_framebuffer holds. PSRAM is a hard requirement.
    display._shadow.assign(shadow_bytes, pair_white);
    if (config.reset_gpio.is_connected()) {
        if (auto r = display._reset(); !r) {
            return error(r.error());
        }
    }
    if (auto r = display._init(); !r) {
        return error(r.error());
    }
    // The panel's native format is six-color; the base starts in mono.
    if (auto r = display.try_set_color_mode(color_mode::color6); !r) {
        return error(r.error());
    }
    return display;
}

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
el133uf1::el133uf1(spi::master_bus& bus, el133uf1::config config)
    : el133uf1(unwrap(make(bus, config))) {}
#endif

el133uf1::el133uf1(el133uf1&& other) noexcept
    : panel(std::move(other))
    , _device(std::move(other._device))
    , _master_cs(other._master_cs)
    , _slave_cs(other._slave_cs)
    , _dc(other._dc)
    , _chunk(std::move(other._chunk))
    , _pending(std::exchange(other._pending, pending::none))
    , _shadow(std::move(other._shadow)) {}

el133uf1& el133uf1::operator=(el133uf1&& other) noexcept {
    if (this != &other) {
        if (!_moved_from() && !asleep()) {
            (void)_settle();
            (void)_enter_deep_sleep();
        }
        panel::operator=(std::move(other));
        _device = std::move(other._device);
        _master_cs = other._master_cs;
        _slave_cs = other._slave_cs;
        _dc = other._dc;
        _chunk = std::move(other._chunk);
        _pending = std::exchange(other._pending, pending::none);
        _shadow = std::move(other._shadow);
    }
    return *this;
}

el133uf1::~el133uf1() {
    // Leaving the controllers powered degrades the glass; power off and
    // park them in deep sleep unless the caller already did. A refresh
    // still driving finishes first — powering off mid-waveform also
    // stresses the glass.
    if (!_moved_from() && !asleep()) {
        (void)_settle();
        (void)_enter_deep_sleep();
    }
}

// =============================================================================
// epaper::panel hooks
// =============================================================================

result<void> el133uf1::do_write(const mono_framebuffer& fb, size_t row_start, size_t row_end, size_t x, size_t y) {
    if (_moved_from()) {
        return error(errc::invalid_state);
    }
    // Expand 1-bpp ink/paper into the shadow's six-color nibbles: ink is
    // black, paper is white. The destination column is a multiple of 8, so
    // pixel pairs align with shadow bytes; an odd-width framebuffer's last
    // pixel shares its byte with a neighbour outside the band, which is
    // preserved.
    const size_t width = fb.width();
    const auto code_at = [](std::span<const uint8_t> row, size_t c) -> uint8_t {
        return (row[c / 8] & (0x80u >> (c % 8))) ? code_white : code_black;
    };
    for (size_t r = row_start; r < row_end; ++r) {
        const auto src = fb.row(r);
        const size_t dst_y = y + r;
        size_t col = 0;
        for (; col + 1 < width; col += 2) {
            *_shadow_byte(x + col, dst_y) = static_cast<uint8_t>((code_at(src, col) << 4) | code_at(src, col + 1));
        }
        if (col < width) {
            uint8_t& dst = *_shadow_byte(x + col, dst_y);
            dst = static_cast<uint8_t>((code_at(src, col) << 4) | (dst & 0x0F));
        }
    }
    return {};
}

result<void> el133uf1::do_write(const color6_framebuffer& fb, size_t row_start, size_t row_end, size_t x, size_t y) {
    if (_moved_from()) {
        return error(errc::invalid_state);
    }
    // The framebuffer's nibble packing matches the shadow's, and x is even,
    // so each row is a byte copy split at the master/slave boundary (byte
    // 300 of the panel row). An odd-width framebuffer's last byte carries a
    // padding nibble that must not clobber the neighbouring pixel.
    const size_t width = fb.width();
    const size_t whole = width / 2;   // full bytes per row
    const bool tail = width % 2 != 0; // trailing single pixel
    const size_t x0 = x / 2;          // first panel byte
    for (size_t r = row_start; r < row_end; ++r) {
        const auto src = fb.row(r);
        const size_t dst_y = y + r;
        // Bytes [x0, x0 + whole) split between plane 0 (< plane_stride) and
        // plane 1 (>= plane_stride).
        const size_t first_end = std::min(x0 + whole, plane_stride);
        if (x0 < first_end) {
            std::memcpy(_shadow_byte(x, dst_y), src.data(), first_end - x0);
        }
        if (x0 + whole > plane_stride) {
            const size_t start = std::max(x0, plane_stride);
            std::memcpy(_shadow_byte(start * 2, dst_y), src.data() + (start - x0), x0 + whole - start);
        }
        if (tail) {
            uint8_t& dst = *_shadow_byte(x + width - 1, dst_y);
            dst = static_cast<uint8_t>((src[whole] & 0xF0) | (dst & 0x0F));
        }
    }
    return {};
}

result<void> el133uf1::do_clear() {
    if (_moved_from()) {
        return error(errc::invalid_state);
    }
    // White in both supported color modes is the same nibble code.
    std::ranges::fill(_shadow, pair_white);
    return {};
}

result<void> el133uf1::do_refresh(refresh_mode mode) {
    if (_moved_from()) {
        return error(errc::invalid_state);
    }
    // The glass has a single full-update waveform: no fast or differential
    // modes.
    if (mode != refresh_mode::full) {
        return error(errc::not_supported);
    }
    if (auto r = _upload(); !r) {
        return r;
    }
    if (auto r = _cmd(target::both, cmd_power_on); !r) {
        return r;
    }
    if (auto r = wait_busy(); !r) {
        return r;
    }
    idfxx::delay(30ms);
    if (auto r = _cmd(target::both, cmd_refresh, {0x01}); !r) {
        return r;
    }
    // Give the controllers time to assert BUSY before anyone polls it, then
    // return with the glass still driving: the base class settles (via
    // do_wait) before the next controller command, and _settle powers the
    // rails off once the update finishes.
    idfxx::delay(10ms);
    _pending = pending::power_off;
    return {};
}

bool el133uf1::do_supports(enum color_mode mode) const noexcept {
    return mode == color_mode::color6;
}

result<void> el133uf1::do_set_color_mode(enum color_mode mode) {
    (void)mode;
    if (_moved_from()) {
        return error(errc::invalid_state);
    }
    // Both supported modes share the six-color upload format; the mode only
    // selects how framebuffers are expanded into the shadow, so nothing
    // changes on the controllers.
    return {};
}

result<void> el133uf1::do_sleep() {
    if (_moved_from()) {
        return error(errc::invalid_state);
    }
    return _enter_deep_sleep();
}

result<void> el133uf1::do_wake() {
    if (_moved_from()) {
        return error(errc::invalid_state);
    }
    // The reset discards any update do_refresh left running. The shadow
    // frame survives: the next refresh re-uploads it.
    _pending = pending::none;
    if (auto r = _reset(); !r) {
        return r;
    }
    return _init();
}

result<void> el133uf1::do_wait(std::optional<std::chrono::milliseconds> timeout) {
    if (_moved_from()) {
        return error(errc::invalid_state);
    }
    if (_pending == pending::none) {
        return wait_busy(timeout);
    }
    return _settle(timeout);
}

// =============================================================================
// Command sequencing
// =============================================================================

result<void> el133uf1::_cmd(target to, uint8_t cmd) {
    return _cmd(to, cmd, {});
}

result<void> el133uf1::_cmd(target to, uint8_t cmd, std::initializer_list<uint8_t> params) {
    // Every addressed controller stays selected for the whole sequence, as
    // the vendor drivers send commands addressed to both.
    const gpio first = to == target::slave ? _slave_cs : _master_cs;
    const gpio second = to == target::both ? _slave_cs : gpio::nc();
    std::lock_guard lock(_device);
    const chip_select selected(first, second);
    _dc.set_level(gpio::level::low);
    if (auto r = _send({&cmd, 1}); !r) {
        return r;
    }
    if (params.size() != 0) {
        _dc.set_level(gpio::level::high);
        if (auto r = _send({params.begin(), params.size()}); !r) {
            return r;
        }
    }
    return {};
}

result<void> el133uf1::_stream(target to, uint8_t cmd, std::span<const uint8_t> plane) {
    // The controller counts image bytes from the data-start command, so
    // chip select stays asserted, and the bus held, across every chunk.
    std::lock_guard lock(_device);
    const chip_select selected(to == target::slave ? _slave_cs : _master_cs, gpio::nc());
    _dc.set_level(gpio::level::low);
    if (auto r = _send({&cmd, 1}); !r) {
        return r;
    }
    _dc.set_level(gpio::level::high);
    for (size_t offset = 0; offset < plane.size(); offset += _chunk.size()) {
        if (auto r = _send(plane.subspan(offset, std::min(_chunk.size(), plane.size() - offset))); !r) {
            return r;
        }
    }
    return {};
}

result<void> el133uf1::_send(std::span<const uint8_t> bytes) {
    // Stage through the internal DMA buffer: the shadow lives in PSRAM and
    // command parameters in flash, neither of which the SPI DMA reads, so
    // the bus driver would otherwise allocate a bounce buffer per transfer.
    // An interrupt-driven transfer lets other tasks run while DMA sends
    // each chunk; the device lock already keeps the sequence exclusive.
    std::ranges::copy(bytes, _chunk.begin());
    return _device.try_transmit(std::span<const uint8_t>(_chunk).first(bytes.size()));
}

result<void> el133uf1::_reset() {
    // Vendor timing (longer than the base's hardware_reset pulse).
    if (!reset_gpio().is_connected()) {
        return error(errc::invalid_state);
    }
    reset_gpio().set_level(gpio::level::low);
    idfxx::delay(20ms);
    reset_gpio().set_level(gpio::level::high);
    idfxx::delay(20ms);
    return wait_busy();
}

result<void> el133uf1::_init() {
    // Register sequence from the vendor drivers; several registers are
    // undocumented and their values are carried over verbatim. Each write
    // is followed by the vendor's settle delay.
    const struct {
        target to;
        uint8_t cmd;
        std::initializer_list<uint8_t> params;
    } sequence[]{
        {target::master, cmd_reg_0x74, {0x00, 0x0C, 0x0C, 0xD9, 0xDD, 0xDD, 0x15, 0x15, 0x55}},
        {target::both, cmd_reg_0xf0, {0x49, 0x55, 0x13, 0x5D, 0x05, 0x10}},
        {target::both, cmd_panel_setting, {0xDF, 0x69}},
        {target::both, cmd_pll, {0x08}},
        {target::master, cmd_reg_0xa5, {0x44, 0x54, 0x00}},
        {target::both, cmd_vcom_interval, {0x37}},
        {target::both, cmd_tcon, {0x03, 0x03}},
        {target::both, cmd_reg_0x86, {0x10}},
        {target::both, cmd_power_saving, {0x22}},
        // Resolution bytes carried over verbatim from the vendor sequence.
        {target::both, cmd_resolution, {0x04, 0xB0, 0x03, 0x20}},
        {target::master, cmd_power_setting, {0x0F, 0x00, 0x28, 0x2C, 0x28, 0x38}},
        {target::master, cmd_reg_0xb6, {0x07}},
        {target::master, cmd_booster_pos, {0xE0, 0x20}},
        {target::master, cmd_reg_0xb7, {0x01}},
        {target::master, cmd_booster_neg, {0xE0, 0x20}},
        {target::master, cmd_reg_0xb0, {0x01}},
        {target::master, cmd_reg_0xb1, {0x02}},
    };
    for (const auto& step : sequence) {
        if (auto r = _cmd(step.to, step.cmd, step.params); !r) {
            return r;
        }
        idfxx::delay(init_settle);
    }
    return {};
}

result<void> el133uf1::_upload() {
    // Stream the shadow frame, one plane per controller. The cascade
    // command first re-arms both controllers' data pointers.
    if (auto r = _cmd(target::both, cmd_cascade, {0x01}); !r) {
        return r;
    }
    if (auto r = wait_busy(); !r) {
        return r;
    }
    idfxx::delay(init_settle);
    if (auto r = _stream(target::master, cmd_data_start, _plane(0)); !r) {
        return r;
    }
    idfxx::delay(init_settle);
    return _stream(target::slave, cmd_data_start, _plane(1));
}

result<void> el133uf1::_settle(std::optional<std::chrono::milliseconds> timeout) {
    // Completes the update do_refresh left running: wait out BUSY, then
    // power the drive rails off. The base class calls this (via do_wait)
    // before every controller hook, so a running refresh delays the *next*
    // operation rather than blocking refresh's caller; the destructor and
    // move-assignment call it directly. On failure the work stays pending
    // for the next attempt.
    if (_pending == pending::none) {
        return {};
    }
    if (auto r = wait_busy(timeout); !r) {
        return r;
    }
    if (auto r = _cmd(target::both, cmd_power_off, {0x00}); !r) {
        return r;
    }
    if (auto r = wait_busy(); !r) {
        return r;
    }
    idfxx::delay(30ms);
    _pending = pending::none;
    return {};
}

result<void> el133uf1::_enter_deep_sleep() {
    if (auto r = _cmd(target::both, cmd_deep_sleep, {0xA5}); !r) {
        return r;
    }
    // BUSY is not meaningful while entering deep sleep; give the
    // controllers time to power down.
    idfxx::delay(100ms);
    return {};
}

// =============================================================================
// Shadow-frame addressing
// =============================================================================

std::span<uint8_t> el133uf1::_plane(size_t index) noexcept {
    return std::span(_shadow).subspan(index * plane_bytes, plane_bytes);
}

std::span<const uint8_t> el133uf1::_plane(size_t index) const noexcept {
    return std::span(_shadow).subspan(index * plane_bytes, plane_bytes);
}

uint8_t* el133uf1::_shadow_byte(size_t x, size_t y) noexcept {
    // Panel byte x / 2 lives in plane 0 for the left half of the row and
    // plane 1 for the right half.
    const size_t byte_x = x / 2;
    const size_t plane = byte_x / plane_stride;
    return _shadow.data() + plane * plane_bytes + y * plane_stride + (byte_x % plane_stride);
}

} // namespace idfxx::epaper
