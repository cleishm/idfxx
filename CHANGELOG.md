# Changelog

All notable changes to **idfxx** are documented in this file.

The repository follows [calendar versioning](https://calver.org/); individual components follow
[semantic versioning](https://semver.org/) independently. A component's version only bumps when that
component changes.

## v2026.11.08

Five new components add an image pipeline (JPEG decoding, and showing images on LCD and ePaper
panels) and an EL133UF1 Spectra 6 ePaper driver. ESP-IDF 6.1 is supported, and many components take
breaking API changes; those shared across components are summarized under Upgrading. Only components
that changed since v2026.08.07 are listed, each with its new version.

### Upgrading

These changes apply across many of the components listed below:

- **Panels write framebuffers.** LCD and ePaper framebuffers and gfx canvases no longer flush
  themselves; pass them to the panel's `write` / `write_rows`.
- **Required config fields.** Fields with no safe default are `idfxx::required<T>`, so leaving one
  out of a designated initializer is a compile error.
- **Driver defaults are `std::optional`.** Fields that used 0 to mean "driver default" are
  `std::optional`, with `std::nullopt` letting the driver choose. Explicit meaningless values fail
  with `errc::invalid_arg`.
- **quanta.** The thermo, frequency and electro dependencies are replaced by `cleishm/quanta`
  `^0.1.0`. Types move to `quanta::thermo::`, `quanta::frequency::` and `quanta::electro::`, and
  headers to `<quanta/...>`. Format specs render a quantity in its own unit; use `{:b.1f}` for the
  base unit.
- **Throwing callbacks.** A user callback that throws, when run from an ESP-IDF task or C code, is
  logged instead of terminating the program.
- **`explicit` constructors.** Multi-argument throwing constructors are `explicit`, so
  copy-list-initialization no longer compiles.

### New components

- `idfxx_image` `1.0.0` — pixel image views and buffers, EXIF orientation, and a `renderer` that
  crops, scales and rotates a decoded image onto a screen area in bounded memory
- `idfxx_jpeg` `1.0.0` — baseline and progressive JPEG decoding from any `idfxx::byte_source`, with
  M/8 scaling, EXIF orientation and an optional memory limit
- `idfxx_lcd_image` `1.0.0` — `write_image()` shows a decoded image on an RGB565 LCD panel, streamed
  in bands, with an optional overlay callback
- `idfxx_epaper_image` `1.0.0` — `write_image()` shows a decoded image on an ePaper panel, dithered
  using a calibration of the panel's real inks; includes calibrations for the EL133UF1 and GDEY075T7
- `idfxx_epaper_el133uf1` `1.0.0` — EL133UF1 13.3" Spectra 6 ePaper driver (Seeed T133A01, as in the
  reTerminal E1004), in six-color and monochrome

### Breaking changes

- `idfxx_lcd` `3.0.0` — `panel::write`, `write_rows` and `write_region` replace the framebuffers'
  `flush*` methods. The deprecated `idfxx::lcd::panel_io` alias is removed; use `idfxx::panel_io`
  and depend on idfxx_panel_io directly. Over SPI, draws wait for the transfer to finish, so the
  buffer can be reused at once
- `idfxx_lcd_ili9341` `3.0.0` — requires idfxx_lcd `^3.0.0`
- `idfxx_lcd_ssd1306` `2.0.0` — requires idfxx_lcd `^3.0.0`; adds `config::contrast`
- `idfxx_gfx` `2.0.0` — `canvas::flush` is removed, and `render_banded` writes each band with
  `dest.try_write`. Adds `gfx::rotated` for drawing on a surface mounted a quarter turn
- `idfxx_epaper` `2.0.0` — `start_refresh()` returns a future, replacing the `wait*` methods. Adds
  Spectra 6 (`color6`), selectable border drive, `restore_baseline` (keeps partial refreshes working
  after a wake or a deep-sleep reboot) and `write_inks`. Unsupported color or refresh modes fail
  with `errc::not_supported`; drivers implement new `do_supports` hooks
- `idfxx_epaper_ssd1680` `2.0.0` — a refresh returns once the update starts. Grayscale fast and
  partial refreshes fail with `errc::not_supported`. Adds `restore_baseline`, with a
  `deep_sleep_partial` example
- `idfxx_epaper_uc8179` `2.0.0` — a refresh returns once the update starts. Adds border drive and
  the `gray4_frame_rate` / `gray4_source_voltage` settings. Gray levels differ from before, so redo
  existing gray calibrations. Grayscale fast and partial refreshes fail with `errc::not_supported`.
  Adds `restore_baseline`
- `idfxx_panel_io` `2.0.0` — required fields in `spi_config` and `i2c_config`.
  `spi_config::trans_queue_depth` defaults to 10; the old default of 0 aborted. Requires idfxx_spi
  and idfxx_i2c `^2.0.0`
- `idfxx_spi` `2.0.0` — `max_transfer_sz` and `duty_cycle_pos` are optional. Adds `dma_burst_size`
  (ESP-IDF 6.1)
- `idfxx_i2c` `2.0.0` — the bus's `sda`, `scl` and `frequency` are required, and the device's
  `scl_speed` and `scl_wait_us` are optional. Register-access delays are honored in full
- `idfxx_console` `2.0.0` — transports without settings are selected by tag:
  `repl(cfg, repl::usb_cdc)`, `repl::usb_serial_jtag`, or the new `repl::stdio` (ESP-IDF 6.1).
  `command::name` is required
- `idfxx_http_client` `2.0.0` — fields the driver defaults are optional. Timeouts report
  `client::errc::eagain`, and a timed-out write no longer reports success. Adds `open_chunked()` for
  streamed request bodies and `get_response_header()` (ESP-IDF 6.0)
- `idfxx_http_server` `2.0.0` — config defaults come from Kconfig or are optional, and the protected
  `_populate_httpd_config` returns `result<void>`. Adds error handlers (`on_error`,
  `http::status_code`), plus `bind_to_device` and incremental WebSocket reads on ESP-IDF 6.1. The
  WebSocket API now compiles, and a session's socket is no longer leaked when `on_session_close` is
  set
- `idfxx_https_server` `3.0.0` — requires idfxx_http_server `^2.0.0`; `handshake_timeout` is
  optional. An `on_session_open` error now rejects the session
- `idfxx_wifi` `3.0.0` — FTM fields use typed units (`picoseconds`, `nanoseconds`,
  `quanta::spatial::centimeters`), and `sta_config::listen_interval` is optional. Replacing a
  sniffer callback no longer races with packets in flight. Adds ESP-IDF 6.1 options
- `idfxx_net` `2.0.0` — `raw_socket::config::protocol` is required. Timeouts on blocking sockets
  fail with `errc::timed_out`, as documented. Stream sockets and channels are byte sources
- `idfxx_netif` `2.0.0` — `key()` and `description()` return `std::string_view`. Adds static IPv6
  neighbor entries (ESP-IDF 6.1)
- `idfxx_task` `2.0.0` — `name()` and `current_name()` return `std::string_view`
- `idfxx_adc` `2.0.0` — uses quanta; `pin` and `pins` are required
- `idfxx_dht` `2.0.0` — uses quanta; `pin` is required
- `idfxx_ds18x20` `3.0.0` — uses quanta
- `idfxx_pwm` `3.0.0` — uses quanta; `timer::config::frequency` is required
- `idfxx_button` `2.0.0` — `pin` and `callback` are required. Adds an extended-press event
- `idfxx_rotary_encoder` `2.0.0` — `pin_a`, `pin_b` and `callback` are required
- `idfxx_lcd_touch` `3.0.0` — `x_max` and `y_max` are required
- `idfxx_lcd_touch_stmpe610` `3.0.0` — requires idfxx_lcd_touch `^3.0.0` and idfxx_panel_io `^2.0.0`
- `idfxx_radio` `2.0.0` — adds `probe()`, and implementers must provide `do_probe()`. Uses quanta,
  and `lora_link::frequency` is required. An oversized packet fails with `errc::invalid_size`
  instead of being truncated
- `idfxx_radio_sx126x` `2.0.0` — `cs`, `busy` and `dio1` are required. Construction fails with
  `errc::not_found` if no chip answers. Fixes missed IRQs at SPI clocks of 10 MHz and above, and a
  slow wake from sleep that logged a false BUSY timeout

### Enhancements

- `idfxx_core` `1.2.0` — adds `required<T>`, the `byte_source` concept (with memory and `FILE*`
  sources), `chrono::rtc_clock` and `chrono::picoseconds`. `invalid_arg`, `not_supported`, `timeout`
  and `not_allowed` compare equal to their `std::errc` counterparts. `delay()` never returns early
- `idfxx_partition` `1.1.0` — adds `partition_source` and `mmap_config`; the `mmap` overloads taking
  a bare `mmap_memory` are deprecated
- `idfxx_timer` `1.1.0` — adds `stop_and_wait`, absolute-time starts and `restart`
- `idfxx_sleep` `1.1.0` — adds GPIO wake edges and `deep_sleep_gpio_wakeup_status()` (ESP-IDF 6.1).
  On ESP-IDF 6.1, deep sleep is refused when the calling task's stack is in PSRAM
- `idfxx_nvs` `1.1.0` — stores `float` and `double` values (ESP-IDF 6.1)
- `idfxx_ota` `1.1.0` — adds ESP-IDF 6.1 error codes, `set_boot_partition(skip_validation)`,
  `check_image_validity` and `image_flash_mode`
- `idfxx_gpio` `1.1.0` — adds a formatter for `gpio::mode`. Control functions are placed in IRAM
  with `CONFIG_GPIO_CTRL_FUNC_IN_IRAM`
- `idfxx_hw_support` `1.1.0` — adds `intr_flag::shared_private` and new chip models

### Fixes

- `idfxx_event` `1.0.2` — a listener that throws no longer stops event dispatch
- `idfxx_event_group` `1.0.2`, `idfxx_queue` `1.0.2`, `idfxx_onewire` `1.0.2`, `idfxx_http` `1.0.2`
  — headers include `sdkconfig.h` themselves
- `idfxx_log` `1.0.2`, `idfxx_font` `1.0.1`, `idfxx_font_spleen` `1.0.1` — documentation fixes

### Other changes

- CI builds against ESP-IDF v6.1; v5.5 remains the minimum.
- Every example has a README, shown on the ESP Component Registry.
- CI cancels superseded builds, and skips rebuilding main when a pull request already built the same
  tree.
- The API docs include conditionally compiled APIs.

## v2026.08.07

Four new components: the panel I/O transport is now a component of its own, and a new ePaper stack
builds on it with an abstract panel interface and SSD1680 and UC8179 drivers. The LCD drivers move
onto the extracted transport, keeping a deprecated alias so existing code compiles unchanged. Only
components that changed since v2026.07.26 are listed, each with its new version.

### New components

- `idfxx_panel_io` `1.0.0` — panel I/O interface for SPI- and I2C-connected displays
  (`idfxx::panel_io`), moved out of `idfxx_lcd` so display drivers can depend on the I/O transport
  without the LCD panel, color, and framebuffer APIs
- `idfxx_epaper` `1.0.0` — abstract ePaper panel interface (`idfxx::epaper::panel`) with
  ePaper-native vocabulary: framebuffer writes to controller RAM, explicit full/fast/partial
  refreshes, BUSY waiting, deep sleep, and wake. Includes a 1-bpp `mono_framebuffer` and a two-plane
  `gray4_framebuffer` for 4-level grayscale, both satisfying the `idfxx::gfx::pixel_surface` concept
  for use with gfx canvases and banded rendering
- `idfxx_epaper_ssd1680` `1.0.0` — SSD1680 ePaper panel driver (up to 176x296, e.g. the 2.13"
  122x250 panel), supporting full, fast, and partial refreshes and 4-level grayscale via a
  register-written waveform
- `idfxx_epaper_uc8179` `1.0.0` — UC8179 ePaper panel driver (up to 800x600, e.g. the 7.5" 800x480
  panel), supporting full, fast, and partial refreshes and 4-level grayscale; the controller
  maintains the previous frame itself, so the driver keeps no host-side shadow copy

### Enhancements

- `idfxx_lcd` `2.2.0` — the panel I/O class moved to the new `idfxx_panel_io` component as
  `idfxx::panel_io`; `<idfxx/lcd/panel_io>` remains as a compatibility header providing the
  deprecated `idfxx::lcd::panel_io` alias, so existing code compiles unchanged
- `idfxx_lcd_ili9341` `2.2.0` — constructs from `idfxx::panel_io` (the new home of the panel I/O
  class; the deprecated alias names the same type, so existing code compiles unchanged)
- `idfxx_lcd_ssd1306` `1.1.0` — constructs from `idfxx::panel_io` (the new home of the panel I/O
  class; the deprecated alias names the same type, so existing code compiles unchanged)
- `idfxx_lcd_touch_stmpe610` `2.1.0` — constructs from `idfxx::panel_io`, and the `idfxx_lcd`
  dependency is replaced by `idfxx_panel_io`

### Fixes

- `idfxx_https_server` `2.0.1` — `ssl_server::make()` no longer fails to compile against ESP-IDF
  releases after v6.0.2, where `HTTPD_SSL_CONFIG_DEFAULT()` stopped initializing the deprecated
  `use_secure_element` member that remains in `httpd_ssl_config_t`. Leaving it value-initialized is
  correct — setting the deprecated members makes `httpd_ssl_start()` fail — but the resulting
  missing-field-initializer warning was fatal under `-Werror` in C++
- `idfxx_gfx` `1.0.1` — the examples construct `idfxx::panel_io` from the new `idfxx_panel_io`
  component
- `idfxx_lcd_touch` `2.0.1` — updated documentation references to `idfxx::panel_io`, the new home of
  the panel I/O class
- `idfxx_spi` `1.1.1` — updated documentation references to `idfxx::panel_io`, the new home of the
  panel I/O class

### Other changes

- CI pins ESP-IDF to a specific point release (v6.0.2) and a weekly workflow opens a pull request
  when a newer one appears, so toolchain updates land deliberately instead of silently changing
  every build.
- Component uploads are verified against the registry after publishing, rather than trusting the
  upload tool's exit status — it reports failure for uploads that in fact succeed moments later.
- Branch protection gates on a single required check that aggregates the build matrix, so adding a
  matrix entry no longer requires updating the protection rule.
- The build discovers the local ESP-IDF installation instead of hardcoding a path.

## v2026.07.26

Eight new components — LoRa radio support, ADC acquisition, a DHT sensor driver, an SSD1306 OLED
driver, and a bitmap graphics/font stack — plus breaking refinements to the WiFi, PWM, HTTPS server,
and DS18x20 APIs. Only components that changed since v2026.06.11 are listed, each with its new
version.

### New components

- `idfxx_radio` `1.0.0` — abstract LoRa transceiver interface (`idfxx::radio::lora_transceiver`)
  with shared LoRa types, constexpr time-on-air and duty-cycle helpers, and a typed event base. A
  link is configured in one call with `configure(lora_link)`, covering frequency, output power,
  modulation, packet framing, and network; the transceiver caches the applied modulation and framing
  and exposes them through `modulation()` and `packet_params()`. Power and signal quantities use
  electro decibel types
- `idfxx_radio_sx126x` `1.0.0` — Semtech SX126x family driver (SX1261/SX1262/SX1268) selecting PA
  configuration and output-power limits per chip variant, with blocking, future-based, and
  event-loop APIs, duty-cycled receive, and channel-activity detection
- `idfxx_adc` `1.0.0` — one-shot and continuous ADC reads with calibrated voltages: `adc::input`
  reads a single analog input on demand (with optional external voltage-divider scaling), and
  `adc::sampler` converts one or more pins round-robin at a fixed rate on ADC1's digital controller
  with blocking or timed reads and an overrun counter
- `idfxx_dht` `1.0.0` — DHT11/DHT22 temperature and humidity driver using RMT capture, with a pure
  hardware-free decoder that locates the response preamble at any phase and verifies the frame
  checksum, and enforcement of the sensor's minimum sampling period
- `idfxx_lcd_ssd1306` `1.0.0` — SSD1306 monochrome OLED panel driver (128x64 / 128x32) over I2C,
  including `set_contrast()`/`try_set_contrast()` across the full 0x00–0xFF range
- `idfxx_gfx` `1.0.0` — drawing primitives for pixel surfaces: filled and outlined rectangles,
  lines, and bitmap-font text with integer scaling, over a structural `pixel_surface` concept
  satisfied by both `idfxx_lcd` framebuffers
- `idfxx_font` `1.0.0` — fixed-cell bitmap font model and constexpr text metrics, with a BDF-to-C
  converter script for adding fonts
- `idfxx_font_spleen` `1.0.0` — the Spleen 5x8 and 8x16 bitmap fonts (BSD-2-Clause) as idfxx font
  data, one translation unit per font so unused fonts are dropped at link time

### Breaking changes

- `idfxx_wifi` `2.0.0` — renamed the netif factories `create_default_sta_netif()` /
  `create_default_ap_netif()` to `make_sta_netif()` / `make_ap_netif()`, and moved signal quantities
  onto electro decibel types (RSSI fields and the scan threshold are `electro::dbm`; the
  max-TX-power accessors take and return `electro::centi_dbm`, matching the hardware's 0.25 dBm
  resolution). Adds `connect_sta()`, which configures, connects, and waits for an IPv4 address in
  one call with a caller-supplied timeout, plus equality operators on the config, record, and
  event-data structs and `[[nodiscard]]` on result-returning functions
- `idfxx_pwm` `2.0.0` — `fill_multi_fade_params()` now takes the maximum fade duration as any chrono
  duration instead of a raw millisecond count, rounding up to whole milliseconds
- `idfxx_https_server` `2.0.0` — removed the `ssl_server` secure-element config field. ESP-IDF
  dropped `use_secure_element` from `httpd_ssl_config_t` in v6.0.2, and the field only ever
  functioned with the external esp-cryptoauthlib component and dedicated hardware
- `idfxx_ds18x20` `2.0.0` — raised the public `thermo` dependency to `^2.0.0`. thermo 2.0.0 removes
  the `celsius_real` / `kelvin_real` / `fahrenheit_real` typedefs and changes `millifahrenheit`
  counts by 10×; because `thermo::millicelsius` appears in this component's public API, callers
  rendering temperatures via `temperature_cast<thermo::celsius_real>` must switch to applying a
  floating-point format spec directly (`std::format("{:.1f}", temp)`)

### Enhancements

- `idfxx_lcd` `2.1.0` — added I2C panel I/O (`panel_io::i2c_config` and construction from an
  `idfxx::i2c::master_bus`), `draw_bitmap`/`invert_color` on the `panel` base class, default
  implementations for every `panel` hook except `do_idf_handle()` (existing drivers compile
  unchanged; new drivers need only supply their panel handle), `width()`/`height()` on the `panel`
  base class reporting native dimensions, a `mono_framebuffer` helper for monochrome (1-bpp,
  page-packed) displays with full-frame, row-band, and rectangular-region flushes, an `rgb565` color
  value type stored in panel byte order, an `rgb565_framebuffer` helper for 16-bpp color displays
  with offset flushes for band-at-a-time rendering, and a shared internal panel-creation helper for
  esp_lcd-based drivers
- `idfxx_lcd_ili9341` `2.1.0` — panels now report `width()`/`height()`, and the example and
  documentation draw via `panel::draw_bitmap` instead of the raw ESP-IDF handle

### Fixes

- `idfxx_spi` `1.1.0` — `master_device` locking is now real mutual exclusion. The Lockable
  implementation previously mapped straight onto `spi_device_acquire_bus`, which arbitrates between
  devices rather than between threads sharing one device: `lock()` discarded acquisition failures so
  a `std::lock_guard` could silently proceed unserialized, and `try_lock()` passed a finite timeout
  the driver rejects outright, so it always returned false. Locking is now composed from a
  per-device recursive mutex plus bus acquisition on the outermost lock, polling transactions take
  the same mutex internally, and a failed acquisition fails loudly instead of continuing unlocked
- `idfxx_core` `1.1.1` — fixed the `flags<E>` bitwise operators (`|`, `&`, `^`, `-`, `~`) for enums
  with an underlying type smaller than `int` (e.g. `uint16_t`), where integer promotion made the
  result a narrowing error and combining such flags failed to compile
- `idfxx_netif` `1.1.1` — updated documentation references to the renamed
  `idfxx::wifi::make_sta_netif()` factory

### Other changes

- The test app partition layouts now use a single OTA slot: the suite only reads `ota_0` (never
  writes a second image), and the full suite no longer fits the two-slot 4MB layout on the IDF 5.5
  toolchain.
- Added a `Justfile` capturing the common build, flash, monitor, format, docs, config-matrix, and
  QEMU test tasks.

## v2026.06.11

Maintenance release: two new components, an LCD API alignment, and broader target support across the
library. Only components that changed since v2026.04.24 are listed, each with its new version.

### New components

- `idfxx_net` `1.0.0` — type-safe IP transport with `ipv4`/`ipv6` address types and `LWIP_IPV6=0`
  build support
- `idfxx_sleep` `1.0.0` — light and deep sleep with wakeup-source configuration

### Breaking changes

- `idfxx_lcd` `2.0.0`, `idfxx_lcd_ili9341` `2.0.0`, `idfxx_lcd_touch` `2.0.0`,
  `idfxx_lcd_touch_stmpe610` `2.0.0` — reworked the panel and touch interfaces onto the non-virtual
  interface (NVI) pattern with verb-based method names, updated teardown attributes, and clarified
  moved-from semantics. Code that subclassed these interfaces or called the prior method names must
  be updated.

### Enhancements

- `idfxx_core` `1.1.0` — added `ipv4_*`/`ipv6_*` address type names (`ip4_*`/`ip6_*` retained as
  deprecated aliases), `ipv6_info` parity, `std::format` spec handling, and namespaced `errc` values
  in place of leaked `std::errc` codes
- `idfxx_netif` `1.1.0` — aligned with the new `ipv4`/`ipv6` core types and added `LWIP_IPV6=0`
  build support
- `idfxx_event_group` `1.0.1` — wait mode now defaults to "all", added mode-less wait overloads, and
  relaxed `[[nodiscard]]` on exception-throwing waits

### Other changes

- Removed per-component `targets:` restrictions so every component builds for all supported targets,
  and added esp32p4 to CI.
- Patch releases (`1.0.1`) across the remaining changed components — `idfxx_button`,
  `idfxx_console`, `idfxx_ds18x20`, `idfxx_event`, `idfxx_gpio`, `idfxx_http`, `idfxx_http_client`,
  `idfxx_http_server`, `idfxx_https_server`, `idfxx_hw_support`, `idfxx_i2c`, `idfxx_log`,
  `idfxx_nvs`, `idfxx_onewire`, `idfxx_ota`, `idfxx_partition`, `idfxx_pwm`, `idfxx_queue`,
  `idfxx_rotary_encoder`, `idfxx_spi`, `idfxx_task`, `idfxx_timer`, `idfxx_wifi` — picking up the
  interface alignment, target changes, and `errc` namespacing where applicable.

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
