# idfxx_http_client

Type-safe HTTP/HTTPS client for ESP32.

## Features

- Blocking `perform()` for simple request/response workflows
- Streaming `open()`/`write()`/`read()`/`close()` for large payloads
- TLS support with certificate pinning and bundle verification
- HTTP Basic and Digest authentication
- Automatic and manual redirect handling
- TCP keep-alive configuration
- Event callbacks for headers, data, and connection lifecycle
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
  idfxx_http_client:
    version: "^1.0.0"
```

Or add `idfxx_http_client` to the `REQUIRES` list in your component's `CMakeLists.txt`.

## Usage

### Simple GET Request

If `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled:

```cpp
#include <idfxx/http/client>
#include <idfxx/log>

try {
    idfxx::http::client c({
        .url = "https://httpbin.org/get",
        .crt_bundle_attach = esp_crt_bundle_attach,
    });

    c.perform();
    idfxx::log::info("HTTP", "Status: {}", c.status_code());

} catch (const std::system_error& e) {
    idfxx::log::error("HTTP", "Request failed: {}", e.what());
}
```

### POST with Event Callback

```cpp
#include <idfxx/http/client>
#include <idfxx/log>

std::string response;

try {
    idfxx::http::client c({
        .url = "https://httpbin.org/post",
        .method = idfxx::http::method::post,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .on_event = [&](const idfxx::http::event_data& evt) {
            if (evt.id == idfxx::http::event_id::on_data) {
                response.append(reinterpret_cast<const char*>(evt.data.data()),
                                evt.data.size());
            }
        },
    });

    c.set_header("Content-Type", "application/json");
    c.set_post_field(R"({"key": "value"})");
    c.perform();

    idfxx::log::info("HTTP", "Status: {}, Body: {}", c.status_code(), response);

} catch (const std::system_error& e) {
    idfxx::log::error("HTTP", "Request failed: {}", e.what());
}
```

### Streaming API

```cpp
#include <idfxx/http/client>
#include <idfxx/log>

try {
    idfxx::http::client c({
        .url = "https://httpbin.org/get",
        .crt_bundle_attach = esp_crt_bundle_attach,
    });

    c.open();
    auto content_length = c.fetch_headers();
    idfxx::log::info("HTTP", "Content-Length: {}", content_length);

    std::array<uint8_t, 256> buf;
    int total = 0;
    while (true) {
        int n = c.read(buf);
        if (n == 0) break;
        total += n;
    }
    idfxx::log::info("HTTP", "Read {} bytes", total);
    c.close();

} catch (const std::system_error& e) {
    idfxx::log::error("HTTP", "Request failed: {}", e.what());
}
```

### Result-based API

If `CONFIG_COMPILER_CXX_EXCEPTIONS` is *not* enabled, the result-based API must be used:

```cpp
#include <idfxx/http/client>
#include <idfxx/log>

auto result = idfxx::http::client::make({
    .url = "https://httpbin.org/get",
    .crt_bundle_attach = esp_crt_bundle_attach,
});
if (!result) {
    idfxx::log::error("HTTP", "Failed to create client: {}", result.error().message());
    return;
}
auto& c = **result;

if (auto r = c.try_perform(); !r) {
    idfxx::log::error("HTTP", "Request failed: {}", r.error().message());
    return;
}

idfxx::log::info("HTTP", "Status: {}", c.status_code());
```

## API Overview

### Types

- `method` - HTTP request methods (GET, POST, PUT, DELETE, etc.)
- `auth_type` - Authentication types (none, basic, digest)
- `transport` - Transport types (unknown, tcp, ssl)
- `event_id` - Event identifiers for callbacks
- `event_data` - Data passed to event callbacks
- `client` - HTTP client session

### `client`

**Creation:**
- `client(cfg)` - Constructor (exception-based, if enabled)
- `make(cfg)` - Create client (result-based)

**Request Configuration:**
- `set_url(url)` / `try_set_url(url)` - Set request URL
- `set_method(m)` - Set HTTP method
- `set_header(key, value)` - Set request header
- `delete_header(key)` - Remove request header
- `set_post_field(data)` - Set POST body
- `set_username(user)` - Set auth username
- `set_password(pass)` - Set auth password
- `set_auth_type(type)` - Set auth type
- `set_timeout(dur)` - Set request timeout

**Simple Request:**
- `perform()` / `try_perform()` - Execute blocking request

**Streaming:**
- `open(write_len)` / `try_open(write_len)` - Open connection
- `write(data)` / `try_write(data)` - Write request body
- `fetch_headers()` / `try_fetch_headers()` - Read response headers
- `read(buf)` / `try_read(buf)` - Read response body
- `flush_response()` / `try_flush_response()` - Discard remaining data
- `close()` / `try_close()` - Close connection

**Response:**
- `status_code()` - HTTP status code
- `content_length()` - Response content length
- `is_chunked_response()` - Check chunked encoding
- `get_header(key)` - Get response header (returns `std::optional`)
- `get_url()` - Get current effective URL

**Redirect:**
- `set_redirection()` / `try_set_redirection()` - Apply redirect URL
- `reset_redirect_counter()` - Reset redirect counter

## Error Handling

- **HTTP-specific errors**: `client::errc` enum maps to ESP-IDF `ESP_ERR_HTTP_*` codes
  - `max_redirect` - Too many redirects
  - `connect` - Connection failed
  - `write_data` - Write error
  - `fetch_header` - Header read error
  - `invalid_transport` - Bad transport type
  - `connecting` - Connection in progress (async)
  - `eagain` - Try again later
  - `connection_closed` - Remote closed connection
- **Dual API Pattern**: `try_*` methods return `idfxx::result<T>`, throwing methods raise `std::system_error`

## Important Notes

- **Not thread-safe**: The client handle is not thread-safe; callers must synchronize access
- **Certificate lifetime**: TLS certificate strings are copied internally and kept alive for the client's lifetime
- **POST field ownership**: `set_post_field()` copies the data internally since ESP-IDF stores only a pointer
- **Event callbacks**: The `on_event` callback runs in the context of the HTTP task; keep handlers brief
- **Streaming workflow**: Call `open()` → optional `write()` → `fetch_headers()` → `read()` in a loop → `close()`

## License

Apache License 2.0 - see [LICENSE](LICENSE) for details.
