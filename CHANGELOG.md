# Changelog

All notable changes to **idfxx** are documented in this file.

The repository follows [calendar versioning](https://calver.org/); individual
components follow [semantic versioning](https://semver.org/) independently. A
component's version only bumps when that component changes.

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
