# idfxx_http_server

Type-safe HTTP server for ESP32.

## Features

- RAII lifecycle management (start/stop)
- URI handler registration with `std::move_only_function` handlers
- Convenience methods for GET, POST, PUT, PATCH, DELETE, HEAD
- Wildcard and any-method URI matching
- Request header, query string, and body parsing
- Chunked and complete response sending
- Session open/close callbacks
- WebSocket endpoint support (when `CONFIG_HTTPD_WS_SUPPORT` is enabled)
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
  idfxx_http_server:
    version: "^0.9.0"
```

Or add `idfxx_http_server` to the `REQUIRES` list in your component's `CMakeLists.txt`.

## Usage

### Simple GET Handler

If `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled:

```cpp
#include <idfxx/http/server>
#include <idfxx/log>

try {
    idfxx::http::server srv({.server_port = 80});

    srv.on_get("/api/status", [](idfxx::http::request& req) -> idfxx::result<void> {
        req.set_content_type("application/json");
        req.send(R"({"status": "ok"})");
        return {};
    });

    srv.on_post("/api/data", [](idfxx::http::request& req) -> idfxx::result<void> {
        auto body = req.recv_body();
        idfxx::log::info("HTTP", "Received: {}", body);
        req.send("accepted");
        return {};
    });

    // Server runs until srv goes out of scope

} catch (const std::system_error& e) {
    idfxx::log::error("HTTP", "Server failed: {}", e.what());
}
```

### Result-based API

If `CONFIG_COMPILER_CXX_EXCEPTIONS` is *not* enabled, the result-based API must be used:

```cpp
#include <idfxx/http/server>
#include <idfxx/log>

auto result = idfxx::http::server::make({.server_port = 80});
if (!result) {
    idfxx::log::error("HTTP", "Failed to start server: {}", result.error().message());
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
    idfxx::log::error("HTTP", "Failed to register handler: {}", r.error().message());
}
```

## API Overview

### Types

- `request` - Non-owning view of an HTTP request (valid only during handler invocation)
- `server` - HTTP server with RAII lifecycle and handler registration

### `request`

**Properties:**
- `method()` - HTTP method
- `uri()` - Request URI
- `content_length()` - Body length
- `socket_fd()` - Connection socket

**Headers & Query:**
- `header(field)` - Get request header (returns `std::optional`)
- `query_string()` - Full query string (returns `std::optional`)
- `query_param(key)` - Single query parameter (returns `std::optional`)

**Body:**
- `recv(buf)` / `try_recv(buf)` - Receive raw body data
- `recv_body()` / `try_recv_body()` - Receive entire body as string

**Response:**
- `set_status(code)` / `set_status(str)` - Set response status
- `set_content_type(type)` - Set Content-Type
- `set_header(field, value)` - Add response header
- `send(body)` / `try_send(body)` - Send complete response
- `send_chunk(data)` / `try_send_chunk(data)` - Send chunked data
- `end_chunked()` / `try_end_chunked()` - End chunked response
- `send_error(code, msg)` / `try_send_error(code, msg)` - Send error response

**WebSocket (CONFIG_HTTPD_WS_SUPPORT):**
- `ws_recv(buf)` / `try_ws_recv(buf)` - Receive WebSocket frame
- `ws_send_text(text)` / `try_ws_send_text(text)` - Send text frame
- `ws_send_binary(data)` / `try_ws_send_binary(data)` - Send binary frame
- `ws_send(frame)` / `try_ws_send(frame)` - Send arbitrary frame

### `server`

**Creation:**
- `server(cfg)` - Constructor (exception-based, if enabled)
- `make(cfg)` - Create server (result-based)

**Handler Registration:**
- `on(method, uri, handler)` / `try_on(...)` - Register for specific method
- `on_any(uri, handler)` / `try_on_any(...)` - Register for any method
- `on_get` / `on_post` / `on_put` / `on_patch` / `on_delete` / `on_head` + `try_` variants
- `unregister_handler(method, uri)` / `try_unregister_handler(...)`

**WebSocket (CONFIG_HTTPD_WS_SUPPORT):**
- `on_ws(uri, handler, ws_cfg)` / `try_on_ws(...)` - Register WebSocket endpoint

**Session:**
- `close_session(sockfd)` / `try_close_session(sockfd)` - Close client connection

## Error Handling

- **HTTP server errors**: `server::errc` enum maps to ESP-IDF `ESP_ERR_HTTPD_*` codes
  - `handlers_full` - All URI handler slots are full
  - `handler_exists` - Duplicate handler registration
  - `invalid_request` - Invalid request
  - `result_truncated` - Result string truncated
  - `resp_header` - Response header too large
  - `resp_send` - Error sending response
  - `alloc_mem` - Memory allocation failed
  - `task` - Failed to create server task
- **Dual API Pattern**: `try_*` methods return `idfxx::result<T>`, throwing methods raise `std::system_error`

## Important Notes

- **Handler lifetime**: Handlers are stored internally and must not reference stack-local data that goes out of scope
- **Request lifetime**: The `request` object is only valid during the handler invocation; do not store references to it
- **Thread safety**: Handler registration is not thread-safe; register all handlers before the server begins accepting connections
- **Response string lifetimes**: `set_status()`, `set_content_type()`, and `set_header()` copy strings internally to ensure they remain valid until the response is sent
- **WebSocket**: WebSocket support requires `CONFIG_HTTPD_WS_SUPPORT` to be enabled in menuconfig

## License

Apache License 2.0 - see [LICENSE](LICENSE) for details.
