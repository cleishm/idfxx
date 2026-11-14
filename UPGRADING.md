# Upgrading

This file lists the steps for moving to each release's breaking component versions, for components
whose migration needs more than the one-line summary in [CHANGELOG.md](CHANGELOG.md). The changelog
summarizes every change in each release, including the changes shared across components; this file
gives the per-component details. Releases are listed newest first.

## Unreleased

### idfxx_gfx 2.0.0 → 3.0.0

- The `canvas(surface, x, y)` constructor is removed. Use `canvas(surface, x, y, width, height)`,
  where `width` and `height` are the size of the drawing space, usually the frame or screen size;
  pixels outside it are clipped. Passing `x + surface.width(), y + surface.height()` gives the same
  `width()` and `height()` as before.
- The canvas passed to a `render_banded` callback reports the whole frame's size in every band. In
  2.x its `height()` was the bottom of the current band, which put layout anchored to the bottom
  edge, and `gfx::rotated` views of the canvas, in the wrong place. Remove any code that worked
  around the per-band `height()`.

### idfxx_http_client 2.0.0 → 3.0.0

- `write()` returns nothing and `try_write()` returns `result<void>`; both returned the number of
  bytes written. A partial write fails with `client::errc::write_data`, and data longer than
  `INT_MAX` bytes fails with `idfxx::errc::invalid_arg`. Drop any use of the return value.
- `event_data::data` is an `idfxx::byte_view`, so its elements are `std::byte`. Use
  `std::to_integer<uint8_t>()` where you need a number, or convert it to
  `std::span<const uint8_t>`.

### idfxx_http_server 2.0.0 → 3.0.0

- Request body reads are renamed: `recv()` is `read()`, `try_recv()` is `try_read()`, `recv_body()`
  is `read_body()` and `try_recv_body()` is `try_read_body()`. Byte buffers are `idfxx::byte_span`;
  the `std::span<char>` overloads remain.
- A body cut short by the client fails with `server::errc::invalid_request`; in 2.x `recv()`
  returned 0 and `recv_body()` returned the bytes received so far. `read()` returns 0 once
  `content_length()` bytes have been read, and `read_body()` returns only the bytes not yet read.
- Body bytes read through `idf_handle()` with `httpd_req_recv()` are not counted by `read()` and
  `read_body()`, which then fail with `server::errc::invalid_request` at the end of the body. Read
  each body through one or the other.
- `set_status(std::string_view)`, `set_content_type()` and `set_header()` throw on failure and are
  available only with `CONFIG_COMPILER_CXX_EXCEPTIONS`. Result-based code calls
  `try_set_status()`, `try_set_content_type()` and `try_set_header()`, which return
  `result<void>`. `set_status(int)` is unchanged.
- Response text containing CR, LF or NUL, and a header name that isn't a valid HTTP token, fail with
  `idfxx::errc::invalid_arg`.
- Setting more than `server::config::max_resp_headers` headers (8 by default) fails with
  `server::errc::resp_header`; in 2.x the extra headers were dropped silently. Raise
  `max_resp_headers` if a handler sets more.
- An empty `send_chunk()` no longer ends a chunked response; call `end_chunked()` to finish it.
- `request::ws_frame::data` is an `idfxx::byte_view`, and `ws_frame_reader::read()` takes and
  returns an `idfxx::byte_span`, so frame bytes are `std::byte`. Compare against `std::byte{...}`
  or use `std::to_integer<uint8_t>()`. `ws_recv()` takes a `byte_span` and `ws_send_binary()` a
  `byte_view`.
- idfxx_https_server 4.0.0 servers take the same request API, so these steps apply to their
  handlers too.

### idfxx_i2c 2.0.0 → 3.0.0

- Register addresses are one byte by default. In 2.x the register methods always sent two register
  address bytes; devices that relied on that must set
  `register_address = idfxx::i2c::register_address_size::two_bytes`. The `(high, low)` register
  overloads are replaced by a single `uint16_t` register. In one-byte mode, a register above 0xFF
  fails with `idfxx::errc::invalid_arg`.
- `read_register()` sends the register address and reads back in one transaction with a repeated
  START. In 2.x it wrote the address, sent a STOP, waited 20 ms, then read. Devices that need a STOP
  or a conversion delay between the two should use `transmit()` then `receive()`.
- `write_registers()` no longer waits 4 ms between registers. Devices with a write cycle, such as
  EEPROMs, need the caller to wait or poll between writes.
- Failed transfers are no longer reported as `idfxx::errc::invalid_arg`. A NACK, a bus timeout or a
  transfer that doesn't finish in time fails with `idfxx::errc::invalid_response`, and other driver
  errors pass through.
- Construction and probe errors from the driver now pass through instead of being reported as
  `idfxx::errc::invalid_arg`. For example, a port that's already in use fails with
  `idfxx::errc::invalid_state`.
- Each transfer and probe waits for the bus lock for at most its timeout, then fails with
  `idfxx::errc::timeout`; in 2.x it waited indefinitely. Holding the bus with `std::lock_guard`
  for longer than another task's timeout makes that task's transfers fail.
- Asynchronous transfers were removed: `master_bus::config::trans_queue_depth`, `wait_all_done()`
  and `register_event_callbacks()` no longer exist, and every transfer finishes before the call
  returns.
- Byte buffers are `idfxx::byte_view` (writes) and `idfxx::byte_span` (reads), so `std::byte` and
  `uint8_t` buffers both work. Pointer-and-length overloads are gone: pass `{ptr, len}`. Literal
  bytes pass directly, as in `dev.transmit({0x01, 0x02})`.
- `receive(size)` and `read_register(reg, size)` no longer return a `std::vector`: read into your
  own buffer with `receive(buf)` / `read_register(reg, buf)`, or use `receive<N>()` /
  `read_register<N>(reg)` for a `std::array<uint8_t, N>`.
- `transmit()` of several buffers takes a `std::span<const idfxx::byte_view>` instead of a span of
  `std::span<const uint8_t>`.
- `operation::write_data` / `read_data` are `byte_view` / `byte_span`.
- A null buffer with a nonzero size is a precondition violation rather than an
  `idfxx::errc::invalid_arg` error.

### idfxx_lcd 3.0.0 → 4.0.0

- `draw_bitmap()` and `try_draw_bitmap()` take the pixel data as an `idfxx::byte_view` instead of a
  `const void*`. Pass bytes, as in `std::as_bytes(std::span{pixels})`.
- The data must be exactly the size of the region in the panel's pixel format, or the draw fails
  with `idfxx::errc::invalid_size`. An empty or inverted region fails with
  `idfxx::errc::invalid_arg`.
- Writing a framebuffer whose pixel format doesn't match the panel fails with
  `idfxx::errc::invalid_size`, for example an RGB565 framebuffer on an ILI9341 configured for
  18-bit color. Writing a mono framebuffer while the axes are swapped fails with
  `idfxx::errc::invalid_state`.
- Panel drivers: the protected `panel` constructors are replaced by
  `panel(width, height, bits_per_pixel, queued_io = nullptr)`, and `do_draw_bitmap()` takes a
  `byte_view`. It is called only for a valid region with correctly sized data.

### idfxx_nvs 1.1.0 → 2.0.0

- `set_blob(key, const void* data, size_t length)` is removed. Pass a `byte_view` as
  `set_blob(key, {ptr, len})`, or pass a trivially copyable value directly with
  `set_blob(key, value)`.
- `get_blob(key)` returns `std::vector<std::byte>` instead of `std::vector<uint8_t>`. Use
  `std::to_integer<uint8_t>()` where you need numbers, read into your own buffer with
  `get_blob(key, buf)` (which returns the length), or read a value with `get_blob<T>(key)`.
- `get_blob(key, buf)` with a buffer smaller than the blob fails with `idfxx::errc::invalid_size`,
  and `get_blob<T>(key)` fails with it unless the blob is exactly `sizeof(T)` bytes.
- `nvs::flash::secure_config::eky` and `tky` are `std::array<std::byte, 32>`.
- Blobs stored by 1.x read back unchanged.

### idfxx_ota 1.1.0 → 2.0.0

- `update::write(const void*, size_t)` and `write_with_offset(offset, const void*, size_t)` are
  removed, with their `try_` forms. Pass `{ptr, len}`; a `char` buffer, such as one filled by an
  HTTP read, needs `{static_cast<const void*>(buf), len}` or `std::as_bytes(std::span{buf, len})`.
- `app_description::app_elf_sha256()` returns a `std::array<std::byte, 32>` instead of a
  `std::span<const uint8_t, 32>`.

### idfxx_partition 1.1.0 → 2.0.0

- The pointer-and-length `read()`, `write()`, `read_raw()` and `write_raw()` overloads are removed,
  with their `try_` forms. Pass `{ptr, len}`, or view an object's bytes, as in
  `part.read(offset, std::as_writable_bytes(std::span{&header, 1}))`.
- `sha256()` returns `std::array<std::byte, 32>` instead of `std::array<uint8_t, 32>`.

### idfxx_radio 2.0.0 → 3.0.0

- Transceiver implementations: `do_start_transmit()` takes an `idfxx::byte_view`, and
  `do_start_receive()` and `do_read_received()` take an `idfxx::byte_span`. Update the overrides;
  ones with the old `std::span` parameters no longer override anything.

### idfxx_spi 2.0.0 → 3.0.0

- `transaction::length` and `rx_length` are `std::optional<size_t>`. Leave them unset to derive
  them from the buffers; in 2.x a `length` left at 0 was sent as 0 bits. Code that reads them must
  dereference them.
- An explicit `rx_length` of 0 on a full-duplex device fails with `idfxx::errc::invalid_arg`; in 2.x
  it meant the same as `length`. Leave it unset instead.
- `trans_flags::use_txdata` and `use_rxdata` are removed; pass the data in `tx_buffer` and
  `rx_buffer`.
- `receive(size)` and `polling_receive(size)` no longer return a `std::vector`: read into your own
  buffer, or use `receive<N>()` / `polling_receive<N>()` for a `std::array<uint8_t, N>`.
- Buffers, including `transaction::tx_buffer` and `rx_buffer`, are `idfxx::byte_view` /
  `idfxx::byte_span`, so `std::byte` and `uint8_t` buffers both work. Literal bytes pass directly,
  as in `dev.transmit({0x01, 0x02})`.

### idfxx_wifi 3.0.0 → 4.0.0

- `connect_sta()` and `try_connect_sta()` make the station the only role, which stops a running AP;
  in 3.x they added the station to the current roles. For AP+STA, call
  `set_roles(role::sta | role::ap)`, `set_ap_config()`, `set_sta_config()` and `start()`, then the
  new `connect(timeout)`.
- `set_vendor_ie()` takes the whole vendor element as an `idfxx::byte_view` instead of a
  `const void*`: element ID `0xDD`, length, OUI, OUI type, then the payload. A malformed element
  fails with `idfxx::errc::invalid_arg`. To remove an element, pass `{}`.
- A `vendor_ie_cb` callback receives the whole element as an `idfxx::byte_view` instead of a
  `const vendor_ie_data_t*`, in the layout `set_vendor_ie()` takes and valid only during the
  callback. The payload is `ie.subspan(6)`.
