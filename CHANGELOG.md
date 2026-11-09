# Changelog

All notable changes to **idfxx** are documented in this file.

The repository follows [calendar versioning](https://calver.org/); individual
components follow [semantic versioning](https://semver.org/) independently. A
component's version only bumps when that component changes.

## v2026.11.08

Five new components — an image pipeline (in-memory pixel images and placement,
JPEG decoding, and showing decoded images on LCD and ePaper panels) and an
EL133UF1 Spectra 6 ePaper driver — plus ESP-IDF 6.1 support and a broad round
of breaking API refinements. Display framebuffers no longer flush themselves:
panels write them. Config fields with no safe default become
`idfxx::required<T>`, so omitting one fails to compile; fields the driver
defaults become `std::optional`, and explicit meaningless values are rejected.
The thermo, frequency and electro unit libraries are replaced by quanta, and
user callbacks that throw no longer terminate the program. Only components
that changed since v2026.08.07 are listed, each with its new version.

### New components

- `idfxx_image` `1.0.0` — pixel images in memory and placing them on a screen:
  an `rgb888` pixel, non-owning strided `view`s and owning `buffer`s with a
  configurable allocator (both drawable as gfx pixel surfaces), the eight EXIF
  `orientation`s with `inverse`, `compose`, `oriented_size`, `orient` and
  `oriented`, and the `image::sink` concept for producers that deliver pixels
  block by block. A `renderer`, driven from any decoder by `render`, turns,
  crops, resamples (box, bilinear, bicubic or Lanczos3, in fixed point with the
  same output on every target) and clips an image to a target rectangle in
  bounded memory, with `cover`, `fit` and `stretch` layouts, the smallest
  decode scale that covers the output, a working-memory limit that fails with
  `image::errc::too_large`, and `render_config::frame_mem` to choose the heap
  for the frame held for rotated orientations
- `idfxx_jpeg` `1.0.0` — JPEG decoding into `rgb888` images from any
  `idfxx::byte_source` (memory, a file, a flash partition, a socket, or an HTTP
  response body), built on `espressif/libjpeg-turbo` `^3.2.0`. Decodes baseline
  and progressive images, reads the size and EXIF orientation before any
  pixels, scales at M/8 (`scale_to_cover`), and decodes into a view or row by
  row to an `image::sink`. An optional working-memory limit makes an image too
  large for the device fail cleanly, and truncated or corrupt data fails the
  decode instead of producing a partly gray image
- `idfxx_lcd_image` `1.0.0` — `write_image()` shows a decoded image on an RGB565
  `idfxx::lcd::panel`, or renders it into any RGB565 surface, turned, cropped,
  resampled and clipped as an `image::placement` describes. It streams a band
  of rows at a time, so no whole frame is held in memory, with an optional
  overlay callback that draws over each band and a memory limit
- `idfxx_epaper_image` `1.0.0` — `write_image()` shows a decoded image on an
  `idfxx::epaper::panel`, or renders it into a `bool`, `gray4` or `color6`
  surface, converting it with error diffusion to six colors, four grays, or
  black and white using a `calibration` of what the panel's inks really look
  like. Built-in calibrations cover the EL133UF1 (reTerminal E1004) and the
  GDEY075T7 (XIAO EE05), each with rendering-intent presets that
  `calibration::preset()` looks up by name. Streams in bands like
  `idfxx_lcd_image`, with the same overlay callback and memory limit. Uses
  `cleishm/epaper-image` `^0.4.0`
- `idfxx_epaper_el133uf1` `1.0.0` — EL133UF1 ePaper panel driver (E Ink 13.3"
  1200x1600 Spectra 6 glass, as in Seeed Studio's T133A01 module in the
  reTerminal E1004), in six-color (`color6`) and monochrome modes with full
  refreshes. It drives the glass's two cascaded controllers on one
  `spi::master_bus`, handling both chip selects itself, and keeps a PSRAM
  shadow frame so band-sized framebuffers and banded rendering work. An opt-in
  `config::check_presence` reads each controller's ID register during
  construction and wake and fails with `errc::not_found` if either doesn't
  answer. Without a reset line, deep sleep can only be undone by cycling the
  panel's power

### Breaking changes

- `idfxx_lcd` `3.0.0` — framebuffers no longer flush themselves: `lcd::panel`
  gains `write`, `write_rows` and `write_region` (and their `try_*` forms) for
  `rgb565_framebuffer` and `mono_framebuffer`, replacing the framebuffers'
  `flush`, `flush_rows` and `flush_region`. Removed the deprecated
  `idfxx::lcd::panel_io` alias and `<idfxx/lcd/panel_io>` header (use
  `idfxx::panel_io`); idfxx_lcd no longer depends on idfxx_panel_io, so drivers
  declare it themselves. The framebuffers' multi-argument constructors are
  `explicit`. Over SPI, panel writes and `draw_bitmap` now wait for the color
  transfer to finish before returning, so the buffer can be redrawn
  immediately as documented; custom drivers pass the I/O handle to wait on
  through a new protected `panel(width, height, queued_io)` constructor. Adds
  `display_width()` / `display_height()`, the dimensions as currently addressed
  (exchanged while `swap_xy` is set)
- `idfxx_lcd_ili9341` `3.0.0` — requires idfxx_lcd `^3.0.0`; draws wait for the
  SPI color transfer to finish
- `idfxx_lcd_ssd1306` `2.0.0` — requires idfxx_lcd `^3.0.0` and draws through
  `panel::write`. Adds `config::contrast` (default 128), applied at
  construction on the same 0x00–0xFF scale as `set_contrast()`, and builds
  against ESP-IDF 6.1
- `idfxx_gfx` `2.0.0` — removed `canvas::flush` / `try_flush`; draw on a
  framebuffer and write it with the panel's `write`. `render_banded` writes each
  band with `dest.try_write(band, 0, y)`, so `banded_renderable` now requires
  that method of the destination. Adds `gfx::rotated`, a view for a surface
  mounted a quarter turn from the drawing layout (`gfx::rotation::cw90` /
  `ccw90`, the common case of a portrait-native ePaper panel used landscape),
  and `to_string` / `std::formatter` for `gfx::rotation`
- `idfxx_epaper` `2.0.0` — framebuffers no longer flush themselves; upload them
  with `panel::write` / `write_rows`. `start_refresh()` / `try_start_refresh()`
  start an update and return an `idfxx::future<void>` that completes when BUSY
  releases and stays valid across a move or destruction of the panel; the
  public `wait`, `wait_for`, `try_wait` and `try_wait_for` methods are removed,
  and the next panel operation first finishes any outstanding update. Adds E
  Ink Spectra 6 support: the `color6` pixel (whose enumerators are the
  panel-native codes, so a value-initialized `color6{}` is black),
  `color_mode::color6`, a `color6_framebuffer`, and `write` / `write_rows`
  overloads for it. Write alignment is now each framebuffer's `x_alignment` (8
  for mono and gray4, 2 for color6). `supports(color_mode)` reports a driver's
  pixel formats, and drivers must implement a new pure virtual
  `do_supports(color_mode)`; unsupported color and refresh modes are rejected
  with `errc::not_supported` before reaching the driver. Adds selectable border
  drive (`enum border { white, black, hold }`, `border()`, `supports(border)`,
  `set_border` / `try_set_border`), `restore_baseline` / `try_restore_baseline`
  to declare the image already on the glass so partial refreshes stay partial
  after a wake or a deep-sleep reboot (monochrome only), `write_inks` /
  `try_write_inks` (`<idfxx/epaper/ink_writer>`) for writing a frame already
  converted to panel inks from any `byte_source`, writable framebuffer
  accessors, `display_width()` / `display_height()`, and `to_string` /
  `std::formatter` for `refresh_mode`, `color_mode` and `border`. For driver
  authors, `do_refresh` may return while the glass is still updating, since the
  base class calls `do_wait` before every controller hook; new protected hooks
  `do_supports(refresh_mode)`, `do_supports(border)` and `do_restore_baseline`
  are added, and `hardware_reset` takes the reset pulse length. The
  framebuffers' constructors are `explicit`. `fill()` now keeps row padding
  bits white, so a framebuffer written at an offset no longer inks columns past
  the right edge, and `try_wake` on an awake panel lets an outstanding update
  finish before the reset
- `idfxx_epaper_ssd1680` `2.0.0` — `busy_gpio` is a required config field. A
  refresh now returns once the update has started, finishing its completion
  work before the next operation (or when a `start_refresh` future is awaited),
  and the destructor and move-assignment settle first. Reports 4-level
  grayscale support and rejects fast and partial grayscale refreshes with
  `errc::not_supported`, including the first one before a baseline exists
  (previously promoted to full). Supports `restore_baseline`, with a new
  `deep_sleep_partial` example that keeps partial refreshes across deep sleep.
  A failed RAM upload no longer returns while earlier SPI transfers still read
  the caller's framebuffer, and moving the `panel_io` after constructing the
  driver no longer leaves it pointing at a moved-from object. Requires
  idfxx_epaper `^2.0.0`
- `idfxx_epaper_uc8179` `2.0.0` — `busy_gpio` is a required config field.
  Refresh completion is deferred to the next operation, and the destructor and
  move-assignment wait for BUSY before powering off. Supports all three border
  drives through `config::border` (default `white`, the previous behavior) and
  `set_border`. Grayscale now loads the factory waveform through registers,
  with new `gray4_frame_rate` and `gray4_source_voltage` config fields (default
  20 Hz at ±15 V: deeper black and wider, content-independent gray spacing at
  about 9 s per refresh; 50 Hz at ±10 V keeps the 3.6 s refresh). The gray
  levels differ from the previous waveform and shift with these settings, so
  existing gray calibrations must be redone. Rejects fast and partial grayscale
  refreshes with `errc::not_supported`, including before a baseline exists, and
  supports `restore_baseline`. A failed RAM upload no longer returns while SPI
  transfers still read the caller's framebuffer, and leaves partial-window
  mode; moving the `panel_io` after construction is safe; without a reset line,
  waveform switches power the rails off before re-initializing. Requires
  idfxx_epaper `^2.0.0`
- `idfxx_panel_io` `2.0.0` — `spi_config` `cs_gpio`, `pclk_freq`,
  `lcd_cmd_bits` and `lcd_param_bits` and `i2c_config` `device_address`,
  `scl_speed`, `lcd_cmd_bits` and `lcd_param_bits` are required config fields.
  `spi_config::trans_queue_depth` now defaults to 10 and rejects 0 (the old
  default of 0 aborted in FreeRTOS). Adds `queues_color_transfers()`, true for
  SPI and false for I2C. Requires idfxx_spi and idfxx_i2c `^2.0.0`
- `idfxx_spi` `2.0.0` — fields where the driver chooses a default are
  `std::optional` (`std::nullopt` lets the driver decide), and explicit
  meaningless values fail with `errc::invalid_arg`: `bus_config::max_transfer_sz`
  and `master_device::config::duty_cycle_pos`. Adds
  `bus_config::dma_burst_size` (ESP-IDF 6.1 and later, likewise optional), and
  the `intr_flags` documentation now lists the flags and levels the driver
  rejects
- `idfxx_i2c` `2.0.0` — `master_bus::config` `sda`, `scl` and `frequency` are
  required config fields, and `master_device::config` `scl_speed`
  (`std::nullopt` uses the bus frequency) and `scl_wait_us` are
  `std::optional`. The 4 ms and 20 ms register-access delays are now honored in
  full; at the default 100 Hz tick rate they had rounded down to as little as
  a bare yield. Documents that on ESP-IDF 6.1 with
  `CONFIG_I2C_ISR_IRAM_SAFE`, receive buffers must be in internal RAM
- `idfxx_console` `2.0.0` — REPL transports without settings are selected by
  tag: `repl(cfg, repl::usb_cdc)`, `repl::usb_serial_jtag`, and the new
  `repl::stdio` (ESP-IDF 6.1 and later), replacing the empty `usb_cdc_config` /
  `usb_serial_jtag_config` structs; `repl({}, {})` still means UART.
  `repl::config::prompt` (`""` now gives a blank prompt) and
  `max_cmdline_length`, and `uart_config::tx_gpio` / `rx_gpio` (`nc` is
  rejected), are `std::optional`. `command::name` is a required field, and
  registering a command with an empty name or one containing a space fails with
  `errc::invalid_arg`. A command that throws now fails with status 1
- `idfxx_http_client` `2.0.0` — config fields the driver defaults are
  `std::optional`: `port`, `path` (`""` is rejected), `user_agent`, `timeout`,
  `buffer_size`, `buffer_size_tx`, the `keep_alive_*` fields,
  `max_redirection_count` (0 is rejected; use `disable_auto_redirect`), and
  `max_authorization_retries` (0 means no retries; negative is rejected).
  `try_read`, `try_write` and `try_fetch_headers` report a timeout as
  `client::errc::eagain`; `try_write` no longer reports a timed-out write as
  success, and `try_fetch_headers` no longer reports a failure after a chunked
  response as success. `client::errc` gains `not_modified`,
  `range_not_satisfiable`, `read_timeout` and `incomplete_data`, plus
  `redirect_downgrade` and `header_too_long` on ESP-IDF 6.0 and later. Adds
  `open_chunked()` and a `chunk_writer` for streaming a request body of unknown
  length, and `get_response_header()` (ESP-IDF 6.0 and later, with
  `CONFIG_ESP_HTTP_CLIENT_SAVE_RESPONSE_HEADERS`); `get_header()` is now
  documented as returning a request header, as it always did. The client
  models `idfxx::byte_source`. An exception thrown by an event callback is
  logged and skipped for that event, and a certificate bundle attach callback
  that throws fails the TLS handshake
- `idfxx_http_server` `2.0.0` — `max_req_hdr_len` and `max_uri_len` default to
  `CONFIG_HTTPD_MAX_REQ_HDR_LEN` / `CONFIG_HTTPD_MAX_URI_LEN` and reject 0, the
  `keep_alive_*` fields are `std::optional` with explicit values ≤0 rejected,
  and the protected `_populate_httpd_config` returns `result<void>` (affecting
  subclasses). Adds server-wide error handlers: an `http::status_code` enum of
  IANA codes (with `to_string` / `std::formatter`), the checked
  `http::error_status`, `server::on_error` / `unregister_error_handler`, and
  `request::handle_error`, which dispatches to the handler or sends the default
  page and closes the connection, plus an `error_pages` example. The WebSocket
  API, which previously failed to compile with `CONFIG_HTTPD_WS_SUPPORT`
  enabled, now builds; `ws_recv` returns a zero-length frame without reading a
  second header, and an oversized frame or failed receive now closes the
  connection when the handler returns. On ESP-IDF 6.1 and later, adds
  `ws_recv_begin()` and a `ws_frame_reader` for reading a frame's payload in
  parts, and `config::bind_to_device`. With `on_session_close` set, the server
  now closes the session's socket (previously leaked), and the callback also
  fires for rejected sessions. A handler that throws a `std::system_error` has
  the same effect as returning its error code, and any other exception closes
  the connection instead of terminating the program
- `idfxx_https_server` `3.0.0` — requires idfxx_http_server `^2.0.0`, whose
  config changes apply, and `handshake_timeout` is `std::optional` with
  explicit values ≤0 rejected. `on_session_open` returning an error or throwing
  now actually rejects an `ssl_server` session; previously the session stayed
  open and was served. `on_session_open` is documented as running after the
  TLS handshake, and `on_session_close` also fires for failed handshakes. Adds
  `ssl_server::config::bind_to_device` (ESP-IDF 6.1 and later)
- `idfxx_wifi` `3.0.0` — FTM measurements use typed units:
  `ftm_report_entry::rtt` and `t1`–`t4` are `idfxx::chrono::picoseconds`,
  `ftm_report_event_data::rtt_raw` / `rtt_est` are `std::chrono::nanoseconds`
  (their documentation had said picoseconds), `dist_est` is
  `quanta::spatial::centimeters`, and `ftm_resp_set_offset()` takes
  `quanta::spatial::centimeters` (documented as nanoseconds before), failing
  with `errc::invalid_arg` outside the int16 range. Signal quantities move to
  `quanta::electro`, and `sta_config::listen_interval` is `std::optional` (its
  getter maps 0 to `std::nullopt`). Replacing or clearing the promiscuous,
  vendor-IE or CSI callback while a packet is in flight no longer races, and a
  setter whose ESP-IDF call fails leaves the previous callback in place; a
  sniffer callback that throws is logged and skipped. On ESP-IDF 6.1 and later,
  adds `ftm_report_entry::freq_offset_ppm`, the `init_config`
  `privacy_enhancements` and `rmac_auto_reset_interval` options, and NAN
  pairing and cluster-join event IDs. `ap_config::beacon_interval` is now
  documented in TU (1024 µs), not milliseconds. Requires idfxx_core `^1.2.0`
- `idfxx_net` `2.0.0` — `raw_socket::config::protocol` is a required field
  (omitting it previously opened a socket for protocol 0). The multi-argument
  throwing constructors of `raw_socket` and `raw_channel` are `explicit`, and
  the throwing socket, listener and channel constructors are `[[nodiscard]]`.
  On a blocking socket, an expired receive or send timeout and a timed-out
  accept now fail with `errc::timed_out`, as documented (non-blocking sockets
  still report `errc::operation_would_block`), and a stream receive into an
  empty buffer returns at once. `stream_socket` and
  `netconn::stream_channel` gain `read` / `try_read`, so both model
  `idfxx::byte_source`
- `idfxx_netif` `2.0.0` — `interface::key()` and `description()` return
  `std::string_view` (empty where they returned `nullptr`; not NUL-terminated).
  Adds `add_static_neighbor` / `remove_static_neighbor` (and their `try_*`
  forms) for static IPv6 neighbor cache entries, on ESP-IDF 6.1 and later with
  `CONFIG_LWIP_ND6_SUPPORT_STATIC_ENTRIES`
- `idfxx_task` `2.0.0` — `task::self::name()` and `task::current_name()` return
  `std::string_view` and no longer allocate, and `spawn` takes its config by
  `const config&`. A task function that throws is logged and the task finishes
  as if it had returned
- `idfxx_adc` `2.0.0` — the electro and frequency dependencies are replaced by
  `cleishm/quanta` `^0.1.0`: types move to `quanta::electro::` and
  `quanta::frequency::`, and headers to `<quanta/...>`. `input::config::pin`
  and `sampler::config::pins` are required config fields (a braced pin list
  must name its type, as in `std::vector<gpio>{...}`). The read methods are
  documented as failing with `errc::timeout`, and the examples pick valid ADC
  pins on every target
- `idfxx_dht` `2.0.0` — the thermo dependency is replaced by `cleishm/quanta`
  `^0.1.0` (types in `quanta::thermo::`); format specs now render a quantity in
  its own unit, so use `{:b.1f}` for degrees Celsius. The data pin is a
  required config field
- `idfxx_ds18x20` `3.0.0` — the thermo dependency is replaced by
  `cleishm/quanta` `^0.1.0` (types in `quanta::thermo::`); format specs now
  render a quantity in its own unit, so use `{:b.1f}` for degrees Celsius
- `idfxx_pwm` `3.0.0` — the frequency dependency is replaced by
  `cleishm/quanta` `^0.1.0` (types in `quanta::frequency::`), and
  `timer::config::frequency` is a required config field. Equality operators are
  `[[nodiscard]]` and `noexcept`
- `idfxx_button` `2.0.0` — `pin` and `callback` are required config fields.
  Adds `config::extended_press_time` and `event_type::extended_press`, a hold
  threshold beyond long press, so one button can tell a click, a long press
  and a longer hold apart. A callback that throws is logged and skipped for
  that event. The documentation notes enabling `CONFIG_GPIO_CTRL_FUNC_IN_IRAM`
  with an IRAM ISR service, and the examples use the BOOT button on every
  target
- `idfxx_rotary_encoder` `2.0.0` — `pin_a`, `pin_b` and `callback` are required
  config fields; a callback that throws is logged and skipped for that event
- `idfxx_lcd_touch` `3.0.0` — `touch::config` `x_max` and `y_max` are required
  config fields. `process_coordinates` is now documented as running before
  swap and mirror, on controller-native coordinates
- `idfxx_lcd_touch_stmpe610` `3.0.0` — requires idfxx_lcd_touch `^3.0.0` and
  idfxx_panel_io `^2.0.0`. A touch read whose coordinate-processing callback
  throws reports no touch points, and the `touch_paint` example no longer
  frees its buffers while an SPI transfer is still reading them
- `idfxx_radio` `2.0.0` — adds `probe()` / `try_probe()` to confirm a radio is
  present and answering, backed by a new pure virtual `do_probe()` that
  implementers must provide. The electro and frequency dependencies are
  replaced by `cleishm/quanta` `^0.1.0`, and `lora_link::frequency` is a
  required field. `read_received` is specified as popping the oldest of a small
  FIFO of packets received while listening, and a packet larger than the
  caller's buffer is never truncated: nothing is written and `receive`,
  `start_receive` and `read_received` report `errc::invalid_size`.
  `rx_duty_cycle_for` accepts `min_sleep` in any duration unit, and
  `radio::chip_mode` gains `to_string` / `std::formatter`
- `idfxx_radio_sx126x` `2.0.0` — `cs`, `busy` and `dio1` are required config
  fields, and it requires idfxx_spi `^2.0.0` and idfxx_radio `^2.0.0`.
  Cold-start construction now verifies the chip and fails with
  `errc::not_found` if nothing answers (a floating BUSY line previously let
  bring-up succeed with no chip). Packets received while listening queue in a
  four-slot FIFO, so a delayed `rx_done` handler still collects every packet in
  order, and oversized packets fail with `errc::invalid_size` instead of being
  truncated. Get command reads are split after the opcode, fixing replies that
  arrived a byte late at 10 MHz and above in STBY_RC and caused missed or
  misclassified IRQs and dropped packets; the documented SPI clock limit is
  corrected to 16 MHz. The first access to a sleeping chip now wakes it after
  about 10 ms (plus the TCXO startup time) instead of waiting out
  `busy_timeout` and logging a false "BUSY timeout" warning, and the reset
  pulse and post-reset wait are no longer rounded down to a bare yield. The
  `ping_pong` example no longer has both boards become the initiator when they
  boot together

### Enhancements

- `idfxx_core` `1.2.0` — adds `idfxx::required<T>` (`<idfxx/required>`) for
  config fields with no safe default: it has no default constructor, so
  omitting the field from a designated initializer fails to compile, and it
  accepts only non-narrowing conversions. Adds the `byte_source` concept
  (`<idfxx/byte_source>`) with `memory_source` and `file_source`,
  `idfxx::chrono::rtc_clock`, a steady clock over the RTC timer that keeps
  counting through sleep and every reset other than power-on, and the
  `idfxx::chrono::picoseconds` alias. `delay()` now never returns before the
  requested duration and busy-waits only for durations shorter than a tick
  (previously a fixed 10 ms), and `delay(next_tick)` blocks until the next tick
  interrupt, a polling back-off that lets lower-priority tasks run.
  `errc::invalid_arg`, `not_supported`, `timeout` and `not_allowed` now compare
  equal to `std::errc::invalid_argument`, `not_supported`, `timed_out` and
  `operation_not_permitted`. Public headers that test `CONFIG_*` options now
  include `sdkconfig.h` themselves
- `idfxx_gpio` `1.1.0` — adds `to_string` and `std::formatter` for
  `gpio::mode`. With `CONFIG_GPIO_CTRL_FUNC_IN_IRAM` enabled, `set_level`,
  `get_level` and `intr_disable` are placed in IRAM, so an `intr_flag::iram`
  ISR can call them. The blink example's LED now blinks instead of staying on
- `idfxx_hw_support` `1.1.0` — adds `intr_flag::shared_private` (ESP-IDF 6.1 and
  later), the `esp32h21` and `esp32h4` chip models, and `esp32s31` on ESP-IDF
  6.1 and later. The `hardware_info` example no longer aborts on chips without
  Wi-Fi or a Bluetooth MAC address
- `idfxx_partition` `1.1.0` — adds `partition_source`, which reads a whole
  partition or a byte range of it from start to end as an `idfxx::byte_source`,
  decrypting encrypted partitions, and `partition::mmap_config`, carrying the
  memory type and, on ESP-IDF 6.1 and later, a `blocks_write` option. The
  `mmap` / `try_mmap` overloads taking a bare `mmap_memory` are deprecated in
  favor of `mmap_config`. Builds against ESP-IDF 6.1
- `idfxx_nvs` `1.1.0` — `set_value` / `get_value` accept `float` and `double` on
  ESP-IDF 6.1 and later (storing NaN fails with `errc::invalid_arg`). Requires
  idfxx_partition `^1.1.0`
- `idfxx_ota` `1.1.0` — on ESP-IDF 6.1 and later, adds the
  `already_in_progress` and `spi_mode_mismatch` error codes, a
  `skip_validation` overload of `set_boot_partition`, `check_image_validity`
  for the leading bytes of an image, and `image_flash_mode` with
  `app_description::flash_mode()`. Adds `to_string` and `std::formatter` for
  `ota::image_state`. The `ota_info` example no longer aborts when running from
  the factory partition. Requires idfxx_partition `^1.1.0`
- `idfxx_sleep` `1.1.0` — adds `wake_posedge`, `wake_negedge` and
  `wake_anyedge` deep-sleep GPIO wake modes (ESP-IDF 6.1 and later, on chips
  with edge wake-up) and `deep_sleep_gpio_wakeup_status()`. From ESP-IDF 6.1,
  `try_deep_sleep` and `deep_sleep(sources...)` fail with `errc::not_allowed`
  before arming any source when the calling task's stack is in PSRAM, and the
  per-target UART wake-up causes map to `wakeup_source::uart`. The sleep
  examples pick valid wake pins on every target, and the deep-sleep demo wakes
  by GPIO on the ESP32-C3 and ESP32-C6 without an external resistor
- `idfxx_timer` `1.1.0` — adds `stop_and_wait` (with timeout overloads), which
  disarms the timer and waits for a running callback to finish;
  `start_periodic` with an absolute first-fire time; and `restart` to an
  absolute time, optionally with a new period. On ESP-IDF 6.1,
  `start_once(time_point)` arms the timer at the exact absolute time. `make`
  and the `start_*` functions take their config by `const config&`, and a
  callback that throws is logged and the timer keeps firing

### Fixes

- `idfxx_event` `1.0.2` — a listener that throws is logged instead of
  terminating the program; other listeners for the event still run, and the
  loop keeps dispatching later events
- `idfxx_event_group` `1.0.2` — the header includes `sdkconfig.h` itself rather
  than relying on another header to have pulled it in before its `CONFIG_*`
  checks
- `idfxx_queue` `1.0.2` — the header includes `sdkconfig.h` itself rather than
  relying on another header to have pulled it in before its `CONFIG_*` checks
- `idfxx_onewire` `1.0.2` — the header includes `sdkconfig.h` before its first
  `CONFIG_*` check rather than just above its `std::format` support
- `idfxx_http` `1.0.2` — the header includes `sdkconfig.h` before its first
  `CONFIG_*` check rather than just above its `std::format` support
- `idfxx_log` `1.0.2` — fixed an unresolved reference to the `logger` class in
  the API documentation
- `idfxx_font` `1.0.1` — documentation fixes, including naming the
  `mono_font::advance` parameter
- `idfxx_font_spleen` `1.0.1` — documentation fixes

### Other changes

- CI builds against ESP-IDF v6.1, with the canary on release-v6.1; the v5.5
  compatibility floor is unchanged. The pin workflow accepts the vX.Y tag that
  starts a minor series.
- Every example now has a README, which the ESP Component Registry displays.
- A new push cancels a pull request's in-flight build, and a new merge
  supersedes main's. A push to main whose tree a passing pull request already
  built skips the rebuild and names that pull request, and the example builds
  wait for the format check and main builds.
- The API docs build with Doxygen 1.18.0, and guard macros are predefined so
  the generated docs include conditionally compiled API such as the console
  transports, the WebSocket API, and IPv6 options.
- GitHub Actions moved off Node.js 20.
- The ESP32-S3 test app enables PSRAM, so tests that need it run; the default
  and QEMU configurations enable `CONFIG_HTTPD_WS_SUPPORT` and
  `CONFIG_LWIP_ND6_SUPPORT_STATIC_ENTRIES`, and the HTTPS server runtime tests
  run in QEMU.

## v2026.08.07

Four new components: the panel I/O transport is now a component of its own, and a
new ePaper stack builds on it with an abstract panel interface and SSD1680 and
UC8179 drivers. The LCD drivers move onto the extracted transport, keeping a
deprecated alias so existing code compiles unchanged. Only components that
changed since v2026.07.26 are listed, each with its new version.

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

### Fixes

- `idfxx_https_server` `2.0.1` — `ssl_server::make()` no longer fails to compile
  against ESP-IDF releases after v6.0.2, where `HTTPD_SSL_CONFIG_DEFAULT()`
  stopped initializing the deprecated `use_secure_element` member that remains in
  `httpd_ssl_config_t`. Leaving it value-initialized is correct — setting the
  deprecated members makes `httpd_ssl_start()` fail — but the resulting
  missing-field-initializer warning was fatal under `-Werror` in C++
- `idfxx_gfx` `1.0.1` — the examples construct `idfxx::panel_io` from the new
  `idfxx_panel_io` component
- `idfxx_lcd_touch` `2.0.1` — updated documentation references to
  `idfxx::panel_io`, the new home of the panel I/O class
- `idfxx_spi` `1.1.1` — updated documentation references to `idfxx::panel_io`,
  the new home of the panel I/O class

### Other changes

- CI pins ESP-IDF to a specific point release (v6.0.2) and a weekly workflow
  opens a pull request when a newer one appears, so toolchain updates land
  deliberately instead of silently changing every build.
- Component uploads are verified against the registry after publishing, rather
  than trusting the upload tool's exit status — it reports failure for uploads
  that in fact succeed moments later.
- Branch protection gates on a single required check that aggregates the build
  matrix, so adding a matrix entry no longer requires updating the protection
  rule.
- The build discovers the local ESP-IDF installation instead of hardcoding a
  path.

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
