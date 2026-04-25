# idfxx_https_server

Type-safe HTTPS server for ESP32 with TLS support.

## Features

- `ssl_server` class extending `server` with TLS encryption
- All handler, session, and WebSocket APIs inherited from `server`
- Type-level distinction: require HTTPS with `ssl_server&`, accept both with `server&`
- RAII lifecycle management (start/stop) with TLS encryption
- Server certificate and private key configuration (PEM format)
- Optional client certificate verification (mutual TLS)
- TLS session tickets and secure element support
- Chrono-typed timeouts and keep-alive intervals
- Dual API: exception-based and result-based error handling

## Requirements

- ESP-IDF 5.5 or later
- C++23 compiler

## Installation

### ESP-IDF Component Manager

Add to your project's `idf_component.yml`:

```yaml
dependencies:
  idfxx_https_server:
    version: "^1.0.0"
```

Or add `idfxx_https_server` to the `REQUIRES` list in your component's `CMakeLists.txt`.

## Usage

### Simple HTTPS Server

If `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled:

```cpp
#include <idfxx/http/ssl_server>
#include <idfxx/log>

// PEM-encoded certificate and key (typically embedded or loaded from NVS)
extern const char server_cert_pem[];
extern const char server_key_pem[];

try {
    idfxx::http::ssl_server srv({
        .server_port = 443,
        .server_cert = server_cert_pem,
        .private_key = server_key_pem,
    });

    srv.on_get("/api/status", [](idfxx::http::request& req) -> idfxx::result<void> {
        req.set_content_type("application/json");
        req.send(R"({"status": "ok"})");
        return {};
    });

    srv.on_post("/api/data", [](idfxx::http::request& req) -> idfxx::result<void> {
        auto body = req.recv_body();
        idfxx::log::info("HTTPS", "Received: {}", body);
        req.send("accepted");
        return {};
    });

    // Server runs until srv goes out of scope

} catch (const std::system_error& e) {
    idfxx::log::error("HTTPS", "Server failed: {}", e.what());
}
```

### Result-based API

If `CONFIG_COMPILER_CXX_EXCEPTIONS` is *not* enabled, the result-based API must be used:

```cpp
#include <idfxx/http/ssl_server>
#include <idfxx/log>

extern const char server_cert_pem[];
extern const char server_key_pem[];

auto result = idfxx::http::ssl_server::make({
    .server_port = 443,
    .server_cert = server_cert_pem,
    .private_key = server_key_pem,
});
if (!result) {
    idfxx::log::error("HTTPS", "Failed to start server: {}", result.error().message());
    return;
}
auto& srv = *result;

auto r = srv.try_on_get("/api/status", [](idfxx::http::request& req) -> idfxx::result<void> {
    req.set_content_type("text/plain");
    auto res = req.try_send("hello");
    if (!res) return res;
    return {};
});
if (!r) {
    idfxx::log::error("HTTPS", "Failed to register handler: {}", r.error().message());
}
```

### Polymorphic Usage

Since `ssl_server` inherits from `server`, generic functions accept both HTTP and HTTPS servers:

```cpp
// Accepts both HTTP and HTTPS servers
void setup_routes(idfxx::http::server& srv) {
    srv.on_get("/status", [](idfxx::http::request& req) -> idfxx::result<void> {
        req.send("ok");
        return {};
    });
}

idfxx::http::ssl_server srv({...});
setup_routes(srv);  // works — ssl_server is a server

// Require HTTPS specifically
void setup_secure_routes(idfxx::http::ssl_server& srv) { /* ... */ }
setup_secure_routes(srv);
```

## API Overview

### `ssl_server`

Inherits from `server`. All handler registration, WebSocket, session management,
and error code APIs are provided by the base class — see the
[idfxx_http_server](../idfxx_http_server/README.md) documentation.

**Construction:**
- `ssl_server(cfg)` - Create HTTPS server (exception-based, if enabled)
- `ssl_server::make(cfg)` - Create HTTPS server (result-based)

### `ssl_server::config`

TLS-specific configuration fields:
- `server_cert` - Server certificate in PEM format
- `private_key` - Server private key in PEM format
- `client_ca_cert` - CA certificate for client verification (mutual TLS)
- `use_ecdsa_peripheral` - Use ECDSA peripheral for key operations
- `session_tickets` - Enable TLS session tickets
- `use_secure_element` - Use secure element for key storage
- `handshake_timeout` - TLS handshake timeout

Plus all standard HTTP server fields (task priority, ports, limits, timeouts, etc.)
with HTTPS-appropriate defaults (10KB stack, port 443, LRU purge enabled, 4 max sockets).

### Request and Response

See the [idfxx_http_server](../idfxx_http_server/README.md) documentation for the full request/response API.

## Error Handling

- **Error codes**: Uses `server::errc` error codes (from `idfxx::http::server`)
- **Dual API Pattern**: `ssl_server::make` returns `idfxx::result<ssl_server>`, `ssl_server(cfg)` throws `std::system_error`

## Important Notes

- **Type distinction**: `ssl_server` inherits from `server` — functions taking `server&` accept both, while `ssl_server&` enforces HTTPS at the type level
- **Certificate lifetime**: Certificate and key PEM strings are copied by ESP-IDF during server startup; they do not need to outlive the creation call
- **Handler lifetime**: Handlers are stored internally and must not reference stack-local data that goes out of scope
- **Request lifetime**: The `request` object is only valid during the handler invocation; do not store references to it
- **Thread safety**: Handler registration is not thread-safe; register all handlers before the server begins accepting connections
- **Default differences from HTTP**: Stack size is 10KB (vs 4KB), max sockets is 4 (vs 7), LRU purge is enabled by default, default port is 443

## License

Apache License 2.0 - see [LICENSE](LICENSE) for details.
