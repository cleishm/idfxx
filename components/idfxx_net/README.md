# idfxx_net

Type-safe IP transport API (BSD sockets + Netconn) for ESP-IDF.

📚 **[Full API Documentation](https://cleishm.github.io/idfxx/group__idfxx__net.html)**

## Features

- Transport-specific RAII socket types — `stream_socket` (TCP), `datagram_socket` (UDP), `raw_socket` (raw IP)
- `listener` for accepting TCP connections, producing connected `stream_socket` instances
- Single `endpoint` value type for IPv4/IPv6 address-port pairs with parsing and formatting
- DNS resolver returning `result<std::vector<endpoint>>` for forward and reverse lookups
- Named socket option setters/getters with a dual `set_*` / `try_set_*` API
- Synchronous-with-timeout `connect_for` and `accept_for`
- IPv4 and IPv6 multicast membership (`join_multicast_v4` / `join_multicast_v6`, etc.)
- Lower-level Netconn API (`netconn::stream_connection`, `netconn::datagram_connection`,
  `netconn::raw_connection`, `netconn::listener`, `netconn::netbuf`) for zero-copy receive
- Compile-time prevention of protocol mismatches — calling `set_broadcast` on a TCP socket
  or `set_no_delay` on a UDP socket is a compile error, not a silent runtime no-op
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
    version: "^1.0.0"
```

Or add `idfxx_net` to the `REQUIRES` list in your component's `CMakeLists.txt`.

## Usage

### TCP echo server

```cpp
#include <idfxx/net/listener>
#include <idfxx/net/stream_socket>
#include <esp_log.h>

void echo_server() {
    using namespace idfxx::net;
    listener a(8080);
    ESP_LOGI("echo", "listening on %s", to_string(a.local_endpoint()).c_str());

    while (true) {
        auto [client, peer] = a.accept_with_peer();
        ESP_LOGI("echo", "accepted %s", to_string(peer).c_str());

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
#include <idfxx/net/resolver>
#include <idfxx/net/stream_socket>

auto eps = idfxx::net::resolve("example.com", 80);
idfxx::net::stream_socket sock(eps.front(), {.no_delay = true});
sock.set_recv_timeout(std::chrono::seconds(5));

constexpr std::string_view req = "GET / HTTP/1.0\r\nHost: example.com\r\n\r\n";
sock.send_all(std::as_bytes(std::span(req)));

std::array<std::byte, 1024> buf;
while (true) {
    auto data = sock.recv(buf);
    if (data.empty()) break;
    ESP_LOGI("client", "got %zu bytes", data.size());
}
```

### UDP datagram receiver

```cpp
#include <idfxx/net/datagram_socket>

idfxx::net::datagram_socket sock(idfxx::net::family::ipv4);
sock.bind({idfxx::net::ipv4_addr::any(), 5005});

std::array<std::byte, 1500> buf;
auto [data, from] = sock.recv_from(buf);
ESP_LOGI("udp", "received %zu bytes from %s", data.size(), to_string(from).c_str());
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
#include <idfxx/net/netconn/netbuf>
#include <idfxx/net/netconn/stream_connection>

namespace nc = idfxx::net::netconn;
nc::listener srv;
srv.bind({idfxx::net::ipv4_addr::any(), 8080});
srv.listen();

while (true) {
    nc::stream_connection client = srv.accept();
    while (true) {
        nc::netbuf buf = client.recv();
        if (!buf.is_open()) break;  // graceful peer close
        do {
            auto data = buf.data();  // span<const std::byte>
            // process data without copying
        } while (buf.advance());
    }
}
```

Note: don't use `using namespace idfxx::net::netconn;` — `netbuf` clashes
with a forward-declared global symbol of the same name.

## API Overview

The component provides two API styles:
- **Exception-based API** (requires `CONFIG_COMPILER_CXX_EXCEPTIONS`) — methods throw
  `std::system_error` on error
- **Result-based API** (always available) — methods prefixed with `try_*` return
  `result<T>`

### Endpoint

`endpoint` carries an IPv4 or IPv6 address paired with a port. Construct from
an address and port, parse a string with `endpoint::parse`, or format with
`to_string` / `std::format`.

### Stream Socket

`stream_socket` is the TCP socket type. It is the type returned by
`listener::accept` and is constructed directly for outbound client
connections. TCP-only options (`set_no_delay`, `set_keep_alive`,
`set_keep_idle`/`_interval`/`_count`) live on this type.

Key operations: `bind`, `connect`, `connect_for`, `send`, `recv`, `send_all`,
`recv_exact`, `shutdown`, `wait_readable`, `wait_writable`.

### Datagram Socket

`datagram_socket` is the UDP socket type. Use `send_to` / `recv_from` for
explicit per-message addressing, or call `connect` to set a default peer for
`send` / `recv`. UDP-only options (`set_broadcast`, multicast membership) are
on this type. Multicast helpers come in `_v4` / `_v6` variants — calling the
wrong variant for the socket's family returns `errc::wrong_protocol_type`.

### Raw Socket

`raw_socket` is for raw IP traffic. The IP-layer protocol number is set at
construction (e.g. `IPPROTO_ICMP`). I/O mirrors `datagram_socket`:
`send_to` / `recv_from` for connectionless use, `send` / `recv` after
`connect`.

### Listener

`listener` binds and listens on a TCP address. `accept`, `accept_with_peer`,
and `accept_for(timeout)` return connected `stream_socket` instances.

### Resolver

`resolve` / `try_resolve` perform DNS lookups and return a list of
`endpoint`s. `resolve_one` / `try_resolve_one` return the first match.

### Netconn

The `idfxx::net::netconn` namespace mirrors the BSD socket types with a
lower-level zero-copy receive API:

- `netconn::stream_connection` — TCP
- `netconn::datagram_connection` — UDP
- `netconn::raw_connection` — raw IP
- `netconn::listener` — TCP listener, accepts to `stream_connection`
- `netconn::netbuf` — received buffer handle

## Error Handling

The `idfxx::net::errc` enum covers transport-layer errors. The error category
provides `equivalent()` mappings to `std::errc` synonyms, so callers can
compare against either form:

```cpp
auto r = sock.try_send(buf);
if (!r) {
    if (r.error() == idfxx::net::errc::would_block ||
        r.error() == std::errc::operation_would_block) {
        // ...
    }
}
```

Error codes are organised into two groups:
- Socket-level errors (`would_block`, `timed_out`, `connection_refused`,
  `address_in_use`, `name_not_found`, `wrong_protocol_type`, …)
- Netconn-level errors (`netconn_aborted`, `netconn_reset`, `netconn_closed`,
  …)

`errc::wrong_protocol_type` is returned when a family-specific operation is
called on a socket of a different family (e.g. `set_multicast_interface_v4`
on an IPv6 datagram socket).

## Important Notes

- **Thread safety**: a `close()` from one task while another is blocked in
  `recv`/`send` is supported (the blocked task receives an error). For
  portability, prefer `shutdown()` first. Two concurrent reads or writes
  on the same socket are not supported.
- **Move-only**: all socket and connection types are move-only. A moved-from
  object's `try_*` methods return `errc::invalid_state`; the destructor is a
  no-op.
- **DNS**: by default a single address is returned per query. Enable
  `CONFIG_LWIP_USE_ESP_GETADDRINFO` to receive multiple records.
- **Timeout resolution**: receive and send timeouts have millisecond
  resolution; finer-grained `chrono` durations are rounded up.
- **Interface names**: `bind_to_device` enforces an interface-name length
  limit; longer names return `errc::invalid_argument`.
- **Netconn**: prefer the BSD socket API for most uses; the Netconn types
  exist for cases needing zero-copy receive or finer send-flag control.

## Hardware tests

Multicast send/receive on Wi-Fi, `bind_to_device` across multiple netifs,
and TCP keepalive timing require physical hardware and are gated behind a
`[hw]` Unity tag — they are excluded from QEMU CI runs.

## License

Apache License 2.0 - see [LICENSE](LICENSE) for details.
