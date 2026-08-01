# Changelog

All notable changes to **idfxx** are documented in this file.

The repository follows [calendar versioning](https://calver.org/); individual
components follow [semantic versioning](https://semver.org/) independently. A
component's version only bumps when that component changes.

## Unreleased

### New components

- `idfxx_panel_io` `1.0.0` — panel I/O interface for SPI- and I2C-connected
  displays (`idfxx::panel_io`), moved out of `idfxx_lcd` so display drivers can
  depend on the I/O transport without the LCD panel, color, and framebuffer
  APIs
- `idfxx_epaper` `1.0.0` — abstract ePaper panel interface
  (`idfxx::epaper::panel`) with ePaper-native vocabulary: framebuffer writes to
  controller RAM, explicit full/fast/partial refreshes, BUSY waiting, deep
  sleep, and wake. Includes a 1-bpp `mono_framebuffer` and a two-plane
  `gray4_framebuffer` for 4-level grayscale, both satisfying the
  `idfxx::gfx::pixel_surface` concept for use with gfx canvases and banded
  rendering
- `idfxx_epaper_ssd1680` `1.0.0` — SSD1680 ePaper panel driver (up to 176x296,
  e.g. the 2.13" 122x250 panel), supporting full, fast, and partial refreshes
  and 4-level grayscale via a register-written waveform
- `idfxx_epaper_uc8179` `1.0.0` — UC8179 ePaper panel driver (up to 800x600,
  e.g. the 7.5" 800x480 panel), supporting full, fast, and partial refreshes
  and 4-level grayscale; the controller maintains the previous frame itself, so
  the driver keeps no host-side shadow copy

### Enhancements

- `idfxx_lcd` `2.2.0` — the panel I/O class moved to the new `idfxx_panel_io`
  component as `idfxx::panel_io`; `<idfxx/lcd/panel_io>` remains as a
  compatibility header providing the deprecated `idfxx::lcd::panel_io` alias,
  so existing code compiles unchanged
- `idfxx_lcd_ili9341` `2.2.0` — constructs from `idfxx::panel_io` (the new home
  of the panel I/O class; the deprecated alias names the same type, so existing
  code compiles unchanged)
- `idfxx_lcd_ssd1306` `1.1.0` — constructs from `idfxx::panel_io` (the new home
  of the panel I/O class; the deprecated alias names the same type, so existing
  code compiles unchanged)
- `idfxx_lcd_touch_stmpe610` `2.1.0` — constructs from `idfxx::panel_io`, and
  the `idfxx_lcd` dependency is replaced by `idfxx_panel_io`

## v2026.07.26

Eight new components — LoRa radio support, ADC acquisition, a DHT sensor driver,
an SSD1306 OLED driver, and a bitmap graphics/font stack — plus breaking
refinements to the WiFi, PWM, HTTPS server, and DS18x20 APIs. Only components
that changed since v2026.06.11 are listed, each with its new version.

### New components

- `idfxx_radio` `1.0.0` — abstract LoRa transceiver interface
  (`idfxx::radio::lora_transceiver`) with shared LoRa types, constexpr
  time-on-air and duty-cycle helpers, and a typed event base. A link is
  configured in one call with `configure(lora_link)`, covering frequency, output
  power, modulation, packet framing, and network; the transceiver caches the
  applied modulation and framing and exposes them through `modulation()` and
  `packet_params()`. Power and signal quantities use electro decibel types
- `idfxx_radio_sx126x` `1.0.0` — Semtech SX126x family driver (SX1261/SX1262/SX1268)
  selecting PA configuration and output-power limits per chip variant, with
  blocking, future-based, and event-loop APIs, duty-cycled receive, and
  channel-activity detection
- `idfxx_adc` `1.0.0` — one-shot and continuous ADC reads with calibrated
  voltages: `adc::input` reads a single analog input on demand (with optional
  external voltage-divider scaling), and `adc::sampler` converts one or more pins
  round-robin at a fixed rate on ADC1's digital controller with blocking or timed
  reads and an overrun counter
- `idfxx_dht` `1.0.0` — DHT11/DHT22 temperature and humidity driver using RMT
  capture, with a pure hardware-free decoder that locates the response preamble
  at any phase and verifies the frame checksum, and enforcement of the sensor's
  minimum sampling period
- `idfxx_lcd_ssd1306` `1.0.0` — SSD1306 monochrome OLED panel driver (128x64 / 128x32)
  over I2C, including `set_contrast()`/`try_set_contrast()` across the full
  0x00–0xFF range
- `idfxx_gfx` `1.0.0` — drawing primitives for pixel surfaces: filled and outlined
  rectangles, lines, and bitmap-font text with integer scaling, over a structural
  `pixel_surface` concept satisfied by both `idfxx_lcd` framebuffers
- `idfxx_font` `1.0.0` — fixed-cell bitmap font model and constexpr text metrics,
  with a BDF-to-C converter script for adding fonts
- `idfxx_font_spleen` `1.0.0` — the Spleen 5x8 and 8x16 bitmap fonts (BSD-2-Clause)
  as idfxx font data, one translation unit per font so unused fonts are dropped at
  link time

### Breaking changes

- `idfxx_wifi` `2.0.0` — renamed the netif factories `create_default_sta_netif()` /
  `create_default_ap_netif()` to `make_sta_netif()` / `make_ap_netif()`, and moved
  signal quantities onto electro decibel types (RSSI fields and the scan threshold
  are `electro::dbm`; the max-TX-power accessors take and return
  `electro::centi_dbm`, matching the hardware's 0.25 dBm resolution). Adds
  `connect_sta()`, which configures, connects, and waits for an IPv4 address in one
  call with a caller-supplied timeout, plus equality operators on the config,
  record, and event-data structs and `[[nodiscard]]` on result-returning functions
- `idfxx_pwm` `2.0.0` — `fill_multi_fade_params()` now takes the maximum fade
  duration as any chrono duration instead of a raw millisecond count, rounding up
  to whole milliseconds
- `idfxx_https_server` `2.0.0` — removed the `ssl_server` secure-element config
  field. ESP-IDF dropped `use_secure_element` from `httpd_ssl_config_t` in v6.0.2,
  and the field only ever functioned with the external esp-cryptoauthlib component
  and dedicated hardware
- `idfxx_ds18x20` `2.0.0` — raised the public `thermo` dependency to `^2.0.0`.
  thermo 2.0.0 removes the `celsius_real` / `kelvin_real` / `fahrenheit_real`
  typedefs and changes `millifahrenheit` counts by 10×; because `thermo::millicelsius`
  appears in this component's public API, callers rendering temperatures via
  `temperature_cast<thermo::celsius_real>` must switch to applying a floating-point
  format spec directly (`std::format("{:.1f}", temp)`)

### Enhancements

- `idfxx_lcd` `2.1.0` — added I2C panel I/O (`panel_io::i2c_config` and construction from
  an `idfxx::i2c::master_bus`), `draw_bitmap`/`invert_color` on the `panel` base class,
  default implementations for every `panel` hook except `do_idf_handle()` (existing
  drivers compile unchanged; new drivers need only supply their panel handle),
  `width()`/`height()` on the `panel` base class reporting native dimensions, a
  `mono_framebuffer` helper for monochrome (1-bpp, page-packed) displays with
  full-frame, row-band, and rectangular-region flushes, an `rgb565` color value
  type stored in panel byte order, an `rgb565_framebuffer` helper for 16-bpp
  color displays with offset flushes for band-at-a-time rendering, and a shared
  internal panel-creation helper for esp_lcd-based drivers
- `idfxx_lcd_ili9341` `2.1.0` — panels now report `width()`/`height()`, and the example
  and documentation draw via `panel::draw_bitmap` instead of the raw ESP-IDF handle

### Fixes

- `idfxx_spi` `1.1.0` — `master_device` locking is now real mutual exclusion. The
  Lockable implementation previously mapped straight onto `spi_device_acquire_bus`,
  which arbitrates between devices rather than between threads sharing one device:
  `lock()` discarded acquisition failures so a `std::lock_guard` could silently
  proceed unserialized, and `try_lock()` passed a finite timeout the driver rejects
  outright, so it always returned false. Locking is now composed from a per-device
  recursive mutex plus bus acquisition on the outermost lock, polling transactions
  take the same mutex internally, and a failed acquisition fails loudly instead of
  continuing unlocked
- `idfxx_core` `1.1.1` — fixed the `flags<E>` bitwise operators (`|`, `&`, `^`, `-`,
  `~`) for enums with an underlying type smaller than `int` (e.g. `uint16_t`), where
  integer promotion made the result a narrowing error and combining such flags
  failed to compile
- `idfxx_netif` `1.1.1` — updated documentation references to the renamed
  `idfxx::wifi::make_sta_netif()` factory

### Other changes

- The test app partition layouts now use a single OTA slot: the suite only reads
  `ota_0` (never writes a second image), and the full suite no longer fits the
  two-slot 4MB layout on the IDF 5.5 toolchain.
- Added a `Justfile` capturing the common build, flash, monitor, format, docs,
  config-matrix, and QEMU test tasks.

## v2026.06.11

Maintenance release: two new components, an LCD API alignment, and broader target
support across the library. Only components that changed since v2026.04.24 are
listed, each with its new version.

### New components

- `idfxx_net` `1.0.0` — type-safe IP transport with `ipv4`/`ipv6` address types and
  `LWIP_IPV6=0` build support
- `idfxx_sleep` `1.0.0` — light and deep sleep with wakeup-source configuration

### Breaking changes

- `idfxx_lcd` `2.0.0`, `idfxx_lcd_ili9341` `2.0.0`, `idfxx_lcd_touch` `2.0.0`,
  `idfxx_lcd_touch_stmpe610` `2.0.0` — reworked the panel and touch interfaces onto
  the non-virtual interface (NVI) pattern with verb-based method names, updated
  teardown attributes, and clarified moved-from semantics. Code that subclassed
  these interfaces or called the prior method names must be updated.

### Enhancements

- `idfxx_core` `1.1.0` — added `ipv4_*`/`ipv6_*` address type names (`ip4_*`/`ip6_*`
  retained as deprecated aliases), `ipv6_info` parity, `std::format` spec handling,
  and namespaced `errc` values in place of leaked `std::errc` codes
- `idfxx_netif` `1.1.0` — aligned with the new `ipv4`/`ipv6` core types and added
  `LWIP_IPV6=0` build support
- `idfxx_event_group` `1.0.1` — wait mode now defaults to "all", added mode-less wait
  overloads, and relaxed `[[nodiscard]]` on exception-throwing waits

### Other changes

- Removed per-component `targets:` restrictions so every component builds for all
  supported targets, and added esp32p4 to CI.
- Patch releases (`1.0.1`) across the remaining changed components — `idfxx_button`,
  `idfxx_console`, `idfxx_ds18x20`, `idfxx_event`, `idfxx_gpio`, `idfxx_http`,
  `idfxx_http_client`, `idfxx_http_server`, `idfxx_https_server`, `idfxx_hw_support`,
  `idfxx_i2c`, `idfxx_log`, `idfxx_nvs`, `idfxx_onewire`, `idfxx_ota`,
  `idfxx_partition`, `idfxx_pwm`, `idfxx_queue`, `idfxx_rotary_encoder`, `idfxx_spi`,
  `idfxx_task`, `idfxx_timer`, `idfxx_wifi` — picking up the interface alignment,
  target changes, and `errc` namespacing where applicable.

## v2026.04.24 — Initial public release

First public release of idfxx. All 30 components published at version `1.0.0`.

### Components

**Core infrastructure**

- `idfxx_core` — base utilities (error handling, memory allocators, chrono, flags)
- `idfxx_log` — structured logging
- `idfxx_event` — event loop wrapper
- `idfxx_event_group` — FreeRTOS event group wrapper
- `idfxx_queue` — FreeRTOS queue wrapper
- `idfxx_task` — FreeRTOS task wrapper
- `idfxx_timer` — high-resolution timer
- `idfxx_hw_support` — hardware interrupt allocation

**System services**

- `idfxx_console` — console REPL
- `idfxx_nvs` — non-volatile storage
- `idfxx_ota` — over-the-air updates
- `idfxx_partition` — partition table access

**Networking**

- `idfxx_netif` — network interface management
- `idfxx_wifi` — Wi-Fi station and AP
- `idfxx_http` — HTTP shared types
- `idfxx_http_client` — HTTP client
- `idfxx_http_server` — HTTP server
- `idfxx_https_server` — HTTPS server

**Peripheral drivers**

- `idfxx_gpio` — GPIO with ISR support
- `idfxx_spi` — SPI master bus and devices
- `idfxx_i2c` — I2C master bus and devices
- `idfxx_onewire` — 1-Wire bus
- `idfxx_pwm` — LEDC-based PWM
- `idfxx_button` — debounced button input
- `idfxx_rotary_encoder` — quadrature rotary encoder

**Display drivers**

- `idfxx_lcd` — LCD panel I/O interface
- `idfxx_lcd_ili9341` — ILI9341 LCD controller (240x320)
- `idfxx_lcd_touch` — LCD touch controller interface
- `idfxx_lcd_touch_stmpe610` — STMPE610 resistive touch driver

**Sensor drivers**

- `idfxx_ds18x20` — DS18B20/DS18S20 1-Wire temperature sensors

See [README.md](README.md) for an overview, install instructions, and usage examples.
