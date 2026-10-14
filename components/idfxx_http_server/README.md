# idfxx_http_server

Type-safe HTTP server for ESP32.

## Features

- RAII lifecycle management (start/stop)
- URI handler registration with `std::move_only_function` handlers
- Convenience methods for GET, POST, PUT, PATCH, DELETE, HEAD
- Wildcard and any-method URI matching
- Server-wide error handlers for custom error responses (e.g. 404 pages), also callable from URI handlers
- Request header, query string, and body parsing
- Chunked and complete response sending
- Session open/close callbacks
- WebSocket endpoint support (when `CONFIG_HTTPD_WS_SUPPORT` is enabled)
- Reading WebSocket frames larger than the receive buffer in parts (ESP-IDF 6.1+)
- Binding the server to a single network interface (ESP-IDF 6.1+)
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
    version: "^2.0.0"
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

### Custom Error Responses

Register a handler to replace the default response for an HTTP error, whether the
server detects it itself or a URI handler raises it with `handle_error()`:

```cpp
using idfxx::http::status_code;

srv.on_error(status_code::not_found, [](idfxx::http::request& req, status_code) -> idfxx::result<void> {
    req.set_status(404);
    req.set_content_type("text/html");
    req.send("<h1>Page not found</h1>");
    return {}; // keep the connection open
});

srv.on_get("/items", [&](idfxx::http::request& req) -> idfxx::result<void> {
    auto id = req.query_param("id");
    if (!id || !items.contains(*id)) {
        req.handle_error(status_code::not_found); // sends the custom 404 page
        return {};
    }
    req.send(items.at(*id));
    return {};
});
```

Only the error statuses the server can raise itself are accepted (`error_status`); a
constant `status_code` outside that set fails to compile, and a status code known only
at run time is checked with `error_status::from()`.

### Reading Large WebSocket Frames

From ESP-IDF 6.1, a WebSocket handler can read a frame's payload in parts, so
frames larger than the available buffer can be processed:

```cpp
srv.on_ws("/ws", [](idfxx::http::request& req) -> idfxx::result<void> {
    auto reader = req.ws_recv_begin();
    if (reader.type() != idfxx::http::request::ws_frame_type::binary) {
        reader.skip(); // discard frames we don't handle
        return {};
    }
    std::array<uint8_t, 256> buf;
    while (reader.remaining() > 0) {
        auto part = reader.read(buf); // unmasked payload, referencing buf
        process(part);
    }
    return {};
});
```

The whole payload must be read, or discarded with `skip()`, before the handler
returns — see [Important Notes](#important-notes).

### Binding to a Network Interface

From ESP-IDF 6.1, the server can be restricted to connections arriving through a
single network interface, identified by its lwIP name:

```cpp
idfxx::http::server srv({.server_port = 80, .bind_to_device = "st1"});
```

## API Overview

### Types

- `request` - Non-owning view of an HTTP request (valid only during handler invocation)
- `request::ws_frame_reader` - Reads one WebSocket frame's payload in parts (ESP-IDF 6.1+)
- `server` - HTTP server with RAII lifecycle and handler registration
- `status_code` - HTTP response status codes
- `error_status` - A `status_code` the server can pass to an error handler (checked at compile time, or at run time with `error_status::from()`)

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
- `handle_error(status)` / `try_handle_error(status)` - Respond with the server's error handler for `status`, or the default error response

**WebSocket (CONFIG_HTTPD_WS_SUPPORT):**
- `ws_recv(buf)` / `try_ws_recv(buf)` - Receive WebSocket frame (the payload must fit in `buf`)
- `ws_recv_begin()` / `try_ws_recv_begin()` - Begin receiving a frame whose payload is read in parts (ESP-IDF 6.1+)
- `ws_send_text(text)` / `try_ws_send_text(text)` - Send text frame
- `ws_send_binary(data)` / `try_ws_send_binary(data)` - Send binary frame
- `ws_send(frame)` / `try_ws_send(frame)` - Send arbitrary frame

### `request::ws_frame_reader` (ESP-IDF 6.1+, CONFIG_HTTPD_WS_SUPPORT)

- `type()` / `final()` - Frame type and FIN flag
- `size()` / `remaining()` - Total payload length and bytes not yet read
- `read(buf)` / `try_read(buf)` - Read the next part of the payload into `buf`
- `skip()` / `try_skip()` - Discard the rest of the payload

### `server`

**Creation:**
- `server(cfg)` - Constructor (exception-based, if enabled)
- `make(cfg)` - Create server (result-based)
- `config::bind_to_device` - Accept connections on one network interface only (ESP-IDF 6.1+)

**Handler Registration:**
- `on(method, uri, handler)` / `try_on(...)` - Register for specific method
- `on_any(uri, handler)` / `try_on_any(...)` - Register for any method
- `on_get` / `on_post` / `on_put` / `on_patch` / `on_delete` / `on_head` + `try_` variants
- `unregister_handler(method, uri)` / `try_unregister_handler(...)`

**Error Handlers:**
- `on_error(status, handler)` / `try_on_error(...)` - Register a server-wide handler for an HTTP error
- `unregister_error_handler(status)` / `try_unregister_error_handler(...)` - Restore the default error response

**WebSocket (CONFIG_HTTPD_WS_SUPPORT):**
- `on_ws(uri, handler[, ws_cfg])` / `try_on_ws(...)` - Register WebSocket endpoint

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
- **WebSocket frame size**: `ws_recv()` needs the whole payload to fit in the buffer. A larger frame fails with `invalid_size` and its payload is left unread; since the connection cannot continue past unread payload, it is closed when the handler returns, even if the handler reports success. The same applies when receiving the frame fails, since the position in the stream is then unknown. From ESP-IDF 6.1, use `ws_recv_begin()` for frames that may not fit
- **Consume the whole WebSocket frame**: When using `ws_recv_begin()`, read the whole payload or discard the rest with `skip()` before the handler returns. Unread payload would otherwise be parsed as the next frame, so if a `ws_frame_reader` is destroyed with payload left unread, or receiving the frame fails, the connection is closed when the handler returns (even if the handler reports success) and a warning is logged. `ws_recv_begin()` can be called at most once per handler invocation, and not together with `ws_recv()`. The reader must not outlive the handler invocation
- **Error handlers and the connection**: An error handler that returns success keeps the connection open, except for `internal_server_error`, which always closes it; one that returns an error closes it. After `handle_error()`, the response is complete, so send nothing further; the connection is closed once the URI handler returns if the error handler failed, the status is `internal_server_error`, or no error handler is registered (the default error response is sent then). For errors the server detects while receiving a request (e.g. `bad_request`, `request_timeout`, `uri_too_long`, `request_header_fields_too_large`), the handler may see a partially parsed request, so `uri()` and headers can be incomplete or empty
- **Limits and keep-alive tuning**: `max_req_hdr_len` and `max_uri_len` default to the project's `CONFIG_HTTPD_MAX_REQ_HDR_LEN` and `CONFIG_HTTPD_MAX_URI_LEN` settings; `keep_alive_idle`, `keep_alive_interval` and `keep_alive_count` default to `std::nullopt` to let the driver choose. An explicit value of zero or less is rejected with `invalid_arg`
- **Interface binding**: `bind_to_device` takes the lwIP interface name (e.g. `"st1"`, from `esp_netif_get_netif_impl_name()`), not the esp_netif key. The interface must exist when the server starts, otherwise starting fails; names of 6 or more characters are rejected with `invalid_arg`

## License

Apache License 2.0 - see [LICENSE](LICENSE) for details.
