# Components

All idfxx components, grouped by area. Each component is published separately on the [ESP Component
Registry](https://components.espressif.com/) as `cleishm/<component>`; see the [main
README](../README.md) for installation and conventions shared by every component.

## Core Infrastructure

| Component | Description | Documentation |
|-----------|-------------|---------------|
| [idfxx_core](https://github.com/cleishm/idfxx/tree/main/components/idfxx_core) | Core utilities: error handling, memory allocators, chrono, scheduling, system info, app metadata, random, byte sources | [API Docs](https://cleishm.github.io/idfxx/group__idfxx__core.html) |
| [idfxx_hw_support](https://github.com/cleishm/idfxx/tree/main/components/idfxx_hw_support) | Hardware support: interrupt allocation, chip info, MAC addresses | [API Docs](https://cleishm.github.io/idfxx/group__idfxx__hw__support.html) |
| [idfxx_log](https://github.com/cleishm/idfxx/tree/main/components/idfxx_log) | Type-safe logging with std::format | [API Docs](https://cleishm.github.io/idfxx/group__idfxx__log.html) |

## System Services

| Component | Description | Documentation |
|-----------|-------------|---------------|
| [idfxx_console](https://github.com/cleishm/idfxx/tree/main/components/idfxx_console) | Interactive console REPL and command management | [API Docs](https://cleishm.github.io/idfxx/group__idfxx__console.html) |
| [idfxx_event](https://github.com/cleishm/idfxx/tree/main/components/idfxx_event) | Type-safe event loop for asynchronous events | [API Docs](https://cleishm.github.io/idfxx/group__idfxx__event.html) |
| [idfxx_event_group](https://github.com/cleishm/idfxx/tree/main/components/idfxx_event_group) | Type-safe FreeRTOS event group for inter-task synchronization | [API Docs](https://cleishm.github.io/idfxx/group__idfxx__event__group.html) |
| [idfxx_queue](https://github.com/cleishm/idfxx/tree/main/components/idfxx_queue) | Type-safe FreeRTOS queue for inter-task communication | [API Docs](https://cleishm.github.io/idfxx/group__idfxx__queue.html) |
| [idfxx_sleep](https://github.com/cleishm/idfxx/tree/main/components/idfxx_sleep) | Light and deep sleep with type-safe wake-up source configuration | [API Docs](https://cleishm.github.io/idfxx/group__idfxx__sleep.html) |
| [idfxx_task](https://github.com/cleishm/idfxx/tree/main/components/idfxx_task) | FreeRTOS task management with join, cooperative stop, and fire-and-forget support | [API Docs](https://cleishm.github.io/idfxx/group__idfxx__task.html) |
| [idfxx_timer](https://github.com/cleishm/idfxx/tree/main/components/idfxx_timer) | High-resolution timer (esp_timer) | [API Docs](https://cleishm.github.io/idfxx/group__idfxx__timer.html) |

## Networking

| Component | Description | Documentation |
|-----------|-------------|---------------|
| [idfxx_net](https://github.com/cleishm/idfxx/tree/main/components/idfxx_net) | Type-safe IP transport: TCP/UDP/raw sockets, listeners, DNS resolver, and Netconn | [API Docs](https://cleishm.github.io/idfxx/group__idfxx__net.html) |
| [idfxx_netif](https://github.com/cleishm/idfxx/tree/main/components/idfxx_netif) | Network interface management, DHCP, DNS, and SNTP | [API Docs](https://cleishm.github.io/idfxx/group__idfxx__netif.html) |
| [idfxx_wifi](https://github.com/cleishm/idfxx/tree/main/components/idfxx_wifi) | WiFi station and access point management | [API Docs](https://cleishm.github.io/idfxx/group__idfxx__wifi.html) |

## Radio

| Component | Description | Documentation |
|-----------|-------------|---------------|
| [idfxx_radio](https://github.com/cleishm/idfxx/tree/main/components/idfxx_radio) | Abstract LoRa radio transceiver interface and shared types | [API Docs](https://cleishm.github.io/idfxx/group__idfxx__radio.html) |
| [idfxx_radio_sx126x](https://github.com/cleishm/idfxx/tree/main/components/idfxx_radio_sx126x) | Semtech SX126x family LoRa radio driver (SX1261/SX1262/SX1268) | [API Docs](https://cleishm.github.io/idfxx/group__idfxx__radio.html) |

## Peripherals

| Component | Description | Documentation |
|-----------|-------------|---------------|
| [idfxx_gpio](https://github.com/cleishm/idfxx/tree/main/components/idfxx_gpio) | GPIO pin management with ISR support | [API Docs](https://cleishm.github.io/idfxx/group__idfxx__gpio.html) |
| [idfxx_i2c](https://github.com/cleishm/idfxx/tree/main/components/idfxx_i2c) | I2C master bus and device driver | [API Docs](https://cleishm.github.io/idfxx/group__idfxx__i2c.html) |
| [idfxx_spi](https://github.com/cleishm/idfxx/tree/main/components/idfxx_spi) | SPI master bus and device driver | [API Docs](https://cleishm.github.io/idfxx/group__idfxx__spi.html) |
| [idfxx_onewire](https://github.com/cleishm/idfxx/tree/main/components/idfxx_onewire) | 1-Wire bus protocol driver | [API Docs](https://cleishm.github.io/idfxx/group__idfxx__onewire.html) |
| [idfxx_button](https://github.com/cleishm/idfxx/tree/main/components/idfxx_button) | GPIO push-button with debounce, click, long press, and autorepeat | [API Docs](https://cleishm.github.io/idfxx/group__idfxx__button.html) |
| [idfxx_rotary_encoder](https://github.com/cleishm/idfxx/tree/main/components/idfxx_rotary_encoder) | Incremental rotary encoder position tracking with acceleration | [API Docs](https://cleishm.github.io/idfxx/group__idfxx__rotary__encoder.html) |
| [idfxx_nvs](https://github.com/cleishm/idfxx/tree/main/components/idfxx_nvs) | Non-Volatile Storage (NVS) wrapper | [API Docs](https://cleishm.github.io/idfxx/group__idfxx__nvs.html) |
| [idfxx_ota](https://github.com/cleishm/idfxx/tree/main/components/idfxx_ota) | OTA firmware update session management with rollback support | [API Docs](https://cleishm.github.io/idfxx/group__idfxx__ota.html) |
| [idfxx_partition](https://github.com/cleishm/idfxx/tree/main/components/idfxx_partition) | Flash partition discovery, reading, writing, and memory mapping | [API Docs](https://cleishm.github.io/idfxx/group__idfxx__partition.html) |
| [idfxx_sdcard](https://github.com/cleishm/idfxx/tree/main/components/idfxx_sdcard) | SD and MMC/eMMC memory cards over SPI or SDMMC, with raw sector access | [API Docs](https://cleishm.github.io/idfxx/group__idfxx__sdcard.html) |
| [idfxx_pwm](https://github.com/cleishm/idfxx/tree/main/components/idfxx_pwm) | PWM output with automatic or explicit timer and channel allocation | [API Docs](https://cleishm.github.io/idfxx/group__idfxx__pwm.html) |
| [idfxx_adc](https://github.com/cleishm/idfxx/tree/main/components/idfxx_adc) | One-shot and continuous ADC reads with calibrated voltages | [API Docs](https://cleishm.github.io/idfxx/group__idfxx__adc.html) |

## Displays

| Component | Description | Documentation |
|-----------|-------------|---------------|
| [idfxx_panel_io](https://github.com/cleishm/idfxx/tree/main/components/idfxx_panel_io) | Panel I/O interface for SPI- and I2C-connected displays | [API Docs](https://cleishm.github.io/idfxx/group__idfxx__panel__io.html) |
| [idfxx_lcd](https://github.com/cleishm/idfxx/tree/main/components/idfxx_lcd) | LCD panel interface, color types, and framebuffers | [API Docs](https://cleishm.github.io/idfxx/group__idfxx__lcd.html) |
| [idfxx_lcd_ili9341](https://github.com/cleishm/idfxx/tree/main/components/idfxx_lcd_ili9341) | ILI9341 LCD controller driver (240x320) | [API Docs](https://cleishm.github.io/idfxx/group__idfxx__lcd.html) |
| [idfxx_lcd_ssd1306](https://github.com/cleishm/idfxx/tree/main/components/idfxx_lcd_ssd1306) | SSD1306 monochrome OLED panel driver (128x64 / 128x32) | [API Docs](https://cleishm.github.io/idfxx/group__idfxx__lcd.html) |
| [idfxx_lcd_image](https://github.com/cleishm/idfxx/tree/main/components/idfxx_lcd_image) | Decoded images on RGB565 LCD panels and surfaces, placed, sized and oriented on the screen | [API Docs](https://cleishm.github.io/idfxx/group__idfxx__lcd__image.html) |
| [idfxx_lcd_touch](https://github.com/cleishm/idfxx/tree/main/components/idfxx_lcd_touch) | LCD touch controller interface | [API Docs](https://cleishm.github.io/idfxx/group__idfxx__lcd__touch.html) |
| [idfxx_lcd_touch_stmpe610](https://github.com/cleishm/idfxx/tree/main/components/idfxx_lcd_touch_stmpe610) | STMPE610 resistive touch controller driver | [API Docs](https://cleishm.github.io/idfxx/group__idfxx__lcd__touch.html) |
| [idfxx_epaper](https://github.com/cleishm/idfxx/tree/main/components/idfxx_epaper) | Abstract ePaper panel interface with mono, 4-level grayscale, and six-color framebuffers | [API Docs](https://cleishm.github.io/idfxx/group__idfxx__epaper.html) |
| [idfxx_epaper_ssd1680](https://github.com/cleishm/idfxx/tree/main/components/idfxx_epaper_ssd1680) | SSD1680 ePaper panel driver (up to 176x296, e.g. 2.13" 122x250) | [API Docs](https://cleishm.github.io/idfxx/group__idfxx__epaper.html) |
| [idfxx_epaper_uc8179](https://github.com/cleishm/idfxx/tree/main/components/idfxx_epaper_uc8179) | UC8179 ePaper panel driver (up to 800x600, e.g. 7.5" 800x480) | [API Docs](https://cleishm.github.io/idfxx/group__idfxx__epaper.html) |
| [idfxx_epaper_el133uf1](https://github.com/cleishm/idfxx/tree/main/components/idfxx_epaper_el133uf1) | EL133UF1 ePaper panel driver (E Ink 13.3" 1200x1600 Spectra 6 six-color glass, e.g. Seeed T133A01 module in the reTerminal E1004) | [API Docs](https://cleishm.github.io/idfxx/group__idfxx__epaper.html) |
| [idfxx_epaper_image](https://github.com/cleishm/idfxx/tree/main/components/idfxx_epaper_image) | Decoded images on e-paper panels and surfaces, in a panel's measured colors, placed, sized and oriented on the screen | [API Docs](https://cleishm.github.io/idfxx/group__idfxx__epaper__image.html) |
| [idfxx_gfx](https://github.com/cleishm/idfxx/tree/main/components/idfxx_gfx) | Drawing primitives for pixel surfaces: rectangles, lines, and bitmap-font text | [API Docs](https://cleishm.github.io/idfxx/group__idfxx__gfx.html) |
| [idfxx_font](https://github.com/cleishm/idfxx/tree/main/components/idfxx_font) | Fixed-cell bitmap font model and text metrics | [API Docs](https://cleishm.github.io/idfxx/group__idfxx__font.html) |
| [idfxx_font_spleen](https://github.com/cleishm/idfxx/tree/main/components/idfxx_font_spleen) | The Spleen bitmap fonts (5x8 and 8x16) as idfxx font data | [API Docs](https://cleishm.github.io/idfxx/group__idfxx__font__spleen.html) |
| [idfxx_image](https://github.com/cleishm/idfxx/tree/main/components/idfxx_image) | Pixel image views, owning buffers, EXIF orientation transforms, and placing images on a screen | [API Docs](https://cleishm.github.io/idfxx/group__idfxx__image.html) |
| [idfxx_jpeg](https://github.com/cleishm/idfxx/tree/main/components/idfxx_jpeg) | JPEG decoding into RGB888 images, with output scaling and EXIF orientation | [API Docs](https://cleishm.github.io/idfxx/group__idfxx__jpeg.html) |

## Sensors

| Component | Description | Documentation |
|-----------|-------------|---------------|
| [idfxx_dht](https://github.com/cleishm/idfxx/tree/main/components/idfxx_dht) | DHT11/DHT22 temperature and humidity sensor driver | [API Docs](https://cleishm.github.io/idfxx/group__idfxx__dht.html) |
| [idfxx_ds18x20](https://github.com/cleishm/idfxx/tree/main/components/idfxx_ds18x20) | DS18x20 1-Wire temperature sensor driver | [API Docs](https://cleishm.github.io/idfxx/group__idfxx__ds18x20.html) |

## Real-Time Clocks

| Component | Description | Documentation |
|-----------|-------------|---------------|
| [idfxx_rtc](https://github.com/cleishm/idfxx/tree/main/components/idfxx_rtc) | Abstract interface for external real-time-clock chips | [API Docs](https://cleishm.github.io/idfxx/group__idfxx__rtc.html) |
| [idfxx_rtc_pcf8563](https://github.com/cleishm/idfxx/tree/main/components/idfxx_rtc_pcf8563) | NXP PCF8563 real-time clock driver, with alarm, countdown timer and clock output | [API Docs](https://cleishm.github.io/idfxx/group__idfxx__rtc.html) |

## Application Protocols

| Component | Description | Documentation |
|-----------|-------------|---------------|
| [idfxx_http](https://github.com/cleishm/idfxx/tree/main/components/idfxx_http) | Common HTTP types (methods, auth, transport) | [API Docs](https://cleishm.github.io/idfxx/group__idfxx__http.html) |
| [idfxx_http_client](https://github.com/cleishm/idfxx/tree/main/components/idfxx_http_client) | HTTP/HTTPS client | [API Docs](https://cleishm.github.io/idfxx/group__idfxx__http__client.html) |
| [idfxx_http_server](https://github.com/cleishm/idfxx/tree/main/components/idfxx_http_server) | HTTP server with URI handler registration | [API Docs](https://cleishm.github.io/idfxx/group__idfxx__http__server.html) |
| [idfxx_https_server](https://github.com/cleishm/idfxx/tree/main/components/idfxx_https_server) | HTTPS server with TLS support | [API Docs](https://cleishm.github.io/idfxx/group__idfxx__https__server.html) |
