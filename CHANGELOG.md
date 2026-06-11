# Changelog

All notable changes to **idfxx** are documented in this file.

The repository follows [calendar versioning](https://calver.org/); individual
components follow [semantic versioning](https://semver.org/) independently. A
component's version only bumps when that component changes.

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
