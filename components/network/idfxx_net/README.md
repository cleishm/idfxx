# idfxx_net

Type-safe IP transport API (BSD sockets + Netconn) for ESP-IDF.

📚 **[Full API Documentation](https://cleishm.github.io/idfxx/group__idfxx__net.html)**

## Features

- Transport-specific RAII socket types — `stream_socket` (TCP), `datagram_socket` (UDP),
  `raw_socket` (raw IP)
- `listener` for accepting TCP connections, producing connected `stream_socket` instances
- Single `endpoint` value type for IPv4/IPv6 address-port pairs with parsing and formatting
- DNS resolver: `resolve_one` for the first match, `resolve_each` to iterate candidates without
  allocating, and `resolve` for the full `std::vector<endpoint>`
- Named socket option setters and getters with type-safe enums
- `connect_for` and `accept_for`, which block with a timeout
- `stream_socket` and `netconn::stream_channel` model `idfxx::byte_source` via `read` / `try_read`,
  so code that consumes a byte stream can read a TCP connection directly
- IPv4 and IPv6 multicast membership (`join_multicast_v4` / `join_multicast_v6`, etc.)
- Lower-level Netconn API (`netconn::stream_channel`, `netconn::datagram_channel`,
  `netconn::raw_channel`, `netconn::listener`, `netconn::buffer`) for zero-copy receive
- Transport-level options checked at compile time. `set_broadcast` exists only on
  `datagram_socket` and `set_no_delay` only on `stream_socket`, so applying one to the wrong
  transport is a compile error. Family-specific calls (e.g. the `_v4` / `_v6` multicast helpers) are
  checked at runtime and return `errc::wrong_protocol_type` on a mismatch.
- Domain-specific error codes that compare equal to standard `std::errc` synonyms

## Requirements

- ESP-IDF 5.5 or later
- C++23 compiler
- A configured network interface (typically via `idfxx_wifi` or `idfxx_eth`)

## Installation

### ESP-IDF Component Manager

Add to your project's `idf_component.yml`:

```yaml
dependencies:
  idfxx_net:
    version: "^3.0.0"
```

Or add `idfxx_net` to the `REQUIRES` list in your component's `CMakeLists.txt`.

## Usage

### TCP echo server

```cpp
#include <idfxx/log>
#include <idfxx/net/listener>
#include <idfxx/net/stream_socket>

void echo_server() {
    using namespace idfxx::net;
    listener a(8080);
    idfxx::log::info("echo", "listening on {}", a.local_endpoint().value());

    while (true) {
        auto [client, peer] = a.accept_with_peer();
        idfxx::log::info("echo", "accepted {}", peer);

        std::array<std::byte, 512> buf;
        while (true) {
            auto data = client.recv(buf);
            if (data.empty()) break;
            client.send_all(data);
        }
    }
}
```

### TCP client with resolver

```cpp
#include <idfxx/log>
#include <idfxx/net/stream_socket>

// The (host, port) constructor resolves the host and tries each returned
// endpoint in order until one connects (same as the connect_to(host, port)
// factory). It blocks, and it throws on failure.
idfxx::net::stream_socket sock("example.com", 80, {.no_delay = true});
sock.set_recv_timeout(std::chrono::seconds(5));

constexpr std::string_view req = "GET / HTTP/1.0\r\nHost: example.com\r\n\r\n";
sock.send_all(req);

std::array<std::byte, 1024> buf;
while (true) {
    auto data = sock.recv(buf);
    if (data.empty()) break;
    idfxx::log::info("client", "got {} bytes", data.size());
}
```

### Reading a TCP stream as a byte source

`read` receives into a byte buffer and returns the byte count. It returns 0 once the peer has
closed the connection. A stream socket or channel can therefore be passed to any code written for
`idfxx::byte_source`:

```cpp
#include <idfxx/net/stream_socket>

idfxx::net::stream_socket sock("example.com", 80);
sock.send_all("GET / HTTP/1.0\r\nHost: example.com\r\n\r\n");

std::array<uint8_t, 512> buf;
while (size_t n = sock.read(buf)) {
    // process idfxx::byte_view{buf}.first(n)...
}
```

### UDP datagram receiver

```cpp
#include <idfxx/log>
#include <idfxx/net/datagram_socket>

idfxx::net::datagram_socket sock(idfxx::net::address_family::ipv4);
sock.bind({idfxx::net::ipv4_addr::any(), 5005});

std::array<std::byte, 1500> buf;
auto dg = sock.recv_from(buf);
idfxx::log::info("udp", "received {} bytes from {}{}", dg.data.size(), dg.from,
                 dg.truncated ? " (truncated)" : "");
```

### Result-based API

If `CONFIG_COMPILER_CXX_EXCEPTIONS` is *not* enabled, use the `try_*` methods:

```cpp
#include <idfxx/net/listener>

auto a = idfxx::net::listener::make(8080);
if (!a) {
    // handle error
    return;
}
auto client = a->try_accept();
if (!client) {
    // handle error
    return;
}
// use *client (idfxx::net::stream_socket)
```

### Netconn (advanced)

```cpp
#include <idfxx/net/netconn/listener>
#include <idfxx/net/netconn/buffer>
#include <idfxx/net/netconn/stream_channel>

namespace nc = idfxx::net::netconn;
nc::listener srv(8080);  // bind + listen in one step

while (true) {
    nc::stream_channel client = srv.accept();
    while (true) {
        nc::buffer buf = client.recv();
        if (!buf.is_open()) break;  // graceful peer close
        for (auto data : buf.segments()) {  // idfxx::byte_view per segment
            // process data without copying
        }
    }
}
```

## API Overview

The component provides two API styles:
- **Exception-based API** (requires `CONFIG_COMPILER_CXX_EXCEPTIONS`) — methods throw
  `std::system_error` on error
- **Result-based API** (always available) — methods prefixed with `try_*` return `result<T>`

Byte buffers are passed as `idfxx::byte_view` (data to send) and `idfxx::byte_span` (buffers to
receive into). Arrays, vectors and spans of `std::byte` or `uint8_t` all work directly, and so does
a `{ptr, len}` pair. `recv` returns a `byte_span` of the bytes received, at the start of the
caller's buffer. `recv_from` returns the same view in `datagram::data`. Stream `send` / `send_all`
also accept a `std::string_view`.

### Endpoint

`endpoint` carries an IPv4 or IPv6 address paired with a port. Construct from an address and port,
parse a string with `endpoint::parse`, or format with `to_string` / `std::format`.

### Stream Socket

`stream_socket` is the TCP socket type. It is the type returned by `listener::accept` and is
constructed directly for outbound client connections. TCP-only options (`set_no_delay`,
`set_keep_alive`, `set_keep_idle`/`_interval`/`_count`) are on this type.

Connecting on construction:
- `connect_to(endpoint)` connects to a known peer.
- `connect_to(host, port)` resolves a name and tries each candidate until one connects.
- The `_for` variants (`connect_to_for`, `connect_for`) limit the attempt with a timeout.

Each factory has an equivalent throwing constructor.

Key operations: `bind`, `connect`, `connect_for`, `send`, `recv`, `send_all`, `recv_exact`, `read`,
`shutdown`, `wait_readable`, `wait_writable`.

`read(buf)` / `try_read(buf)` receive into an `idfxx::byte_span` and return the number of bytes
received (`size_t` / `result<size_t>`). They return 0 once the peer has closed the connection, and
further reads then fail with `errc::not_connected`. Their errors, including receive timeouts, are
the same as those of `recv`. These methods make `stream_socket` an `idfxx::byte_source`.

### Datagram Socket

`datagram_socket` is the UDP socket type. Use `send_to` / `recv_from` for explicit per-message
addressing, or call `connect` to set a default peer for `send` / `recv`. UDP-only options
(`set_broadcast`, multicast membership) are on this type. Multicast helpers come in `_v4` / `_v6`
variants. Calling the wrong variant for the socket's family returns `errc::wrong_protocol_type`.

### Raw Socket

`raw_socket` is for raw IP traffic. The IP-layer protocol is set at construction (e.g.
`ip_protocol::icmp`). I/O mirrors `datagram_socket`: `send_to` / `recv_from` for connectionless use,
`send` / `recv` after `connect`.

### Listener

`listener` binds and listens on a TCP address. `accept`, `accept_with_peer`, and
`accept_for(timeout)` return connected `stream_socket` instances.

### Resolver

`resolve_one` / `try_resolve_one` return the first matching `endpoint`. This is the common case.
`resolve_each` / `try_resolve_each` visit each candidate without allocating. The visitor may return
`bool` to stop early (e.g. once a connection attempt succeeds). `resolve` / `try_resolve` collect
the full set into a `std::vector<endpoint>`. Each accepts either a numeric `port` or a `service`
name.

### Netconn

The `idfxx::net::netconn` namespace mirrors the BSD socket types with a lower-level zero-copy
receive API:

- `netconn::stream_channel` — TCP
- `netconn::datagram_channel` — UDP
- `netconn::raw_channel` — raw IP
- `netconn::listener` — TCP listener. `accept` / `accept_with_peer` produce `stream_channel`s.
- `netconn::buffer` — received buffer handle

Every channel exposes `local_endpoint()`. Connected `stream_channel` and `datagram_channel` add
`peer_endpoint()` / `try_peer_endpoint()`.

`stream_channel` also has `read(buf)` / `try_read(buf)`. They behave as on `stream_socket`, except
that reads after the peer has closed continue to return 0. These methods make `stream_channel` an
`idfxx::byte_source`.

## Error Handling

The `idfxx::net::errc` enum covers transport-layer errors. The error category maps each code to its
`std::errc` synonym (via `default_error_condition`), so callers can compare against either form:

```cpp
auto r = sock.try_send(buf);
if (!r) {
    if (r.error() == idfxx::net::errc::operation_would_block ||
        r.error() == std::errc::operation_would_block) {
        // Non-blocking socket with no buffer space: retry later.
    }
}
```

Error codes are organized into two groups:
- Socket-level errors (`operation_would_block`, `timed_out`, `connection_refused`, `address_in_use`,
  `name_not_found`, `wrong_protocol_type`, …)
- Netconn-level errors (`netconn_aborted`, `netconn_reset`, `netconn_closed`, …)

`errc::wrong_protocol_type` is returned when a family-specific operation is called on a socket of a
different family (e.g. `set_multicast_interface_v4` on an IPv6 datagram socket).

## Important Notes

- **Thread safety**: a `close()` from one task while another is blocked in `recv`/`send` is
  supported (the blocked task receives an error). For portability, prefer `shutdown()` first. Two
  concurrent reads or writes on the same socket are not supported.
- **Move-only**: all socket and connection types are move-only. A moved-from object may only be
  destroyed, move-assigned, or queried via `is_open()` (which returns `false`) and `idf_handle()`
  (which returns the closed sentinel, `-1` for sockets or `nullptr` for netconn types). Calling any
  other method on a moved-from object is undefined behavior.
- **`idf_handle()`**: the raw descriptor / `netconn*` accessors are for observation and interop
  only. Do not close, free, or change the blocking mode of the returned handle. Doing so makes the
  state of the object inconsistent with the handle.
- **DNS**: by default a single address is returned per query. Enable
  `CONFIG_LWIP_USE_ESP_GETADDRINFO` to receive multiple records.
- **Timeouts vs. non-blocking mode**: on a blocking socket, a call fails with `errc::timed_out`
  when its receive or send timeout expires (`set_recv_timeout` / `set_send_timeout`). On a
  `listener`, the receive timeout limits `accept`. On a non-blocking socket with no data, buffer
  space, or pending connection, the call fails immediately with `errc::operation_would_block`
  instead.
- **Timeout resolution**: receive and send timeouts have millisecond resolution. Finer-grained
  `chrono` durations are rounded up.
- **Stream receive**: `recv` with an empty buffer returns an empty view at once without receiving.
  Pass a non-empty buffer to detect the peer closing the connection. If `recv_exact` fails part-way
  (for example on a timeout), an unspecified number of bytes have been consumed and the position in
  the stream is no longer known.
- **Interface names**: `bind_to_device` enforces an interface-name length limit. Longer names return
  `errc::invalid_argument`.
- **Netconn**: prefer the BSD socket API for most uses. Use the Netconn types when you need
  zero-copy receive or finer control of send flags.
- **PBUF exhaustion**: under sustained load, lwIP may exhaust its packet buffer pool. Send and
  receive calls then fail with `errc::no_buffer_space` (equivalent to `std::errc::no_buffer_space`).
  Treat this error as temporary. Retry after a short delay, or after receiving the pending traffic
  from the peer.

## Hardware tests

Multicast send/receive on Wi-Fi, `bind_to_device` across multiple netifs, and TCP keepalive timing
require physical hardware. Their tests have a `[hw]` Unity tag and are excluded from QEMU CI runs.

## License

Apache License 2.0 - see [LICENSE](LICENSE) for details.
