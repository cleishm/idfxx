# idfxx_http

Common HTTP type definitions for ESP32.

## Features

- Type-safe enums for HTTP methods, authentication types, and transport types
- `to_string()` conversion for all HTTP types
- `std::formatter` specializations for `std::format` integration (with `CONFIG_IDFXX_STD_FORMAT`)

## Requirements

- ESP-IDF 5.5 or later
- C++23 compiler

## Installation

### ESP-IDF Component Manager

Add to your project's `idf_component.yml`:

```yaml
dependencies:
  idfxx_http:
    version: "^1.0.0"
```

Or add `idfxx_http` to the `REQUIRES` list in your component's `CMakeLists.txt`.

## Types

### `idfxx::http::method`

HTTP request methods: `get`, `post`, `put`, `patch`, `delete_`, `head`, `notify`, `subscribe`, `unsubscribe`, `options`, `copy`, `move`, `lock`, `unlock`, `propfind`, `proppatch`, `mkcol`, `report`.

### `idfxx::http::auth_type`

HTTP authentication types: `none`, `basic`, `digest`.

### `idfxx::http::transport`

HTTP transport types: `unknown`, `tcp`, `ssl`.

## String Conversion

All types support `idfxx::to_string()`:

```cpp
#include <idfxx/http/types>

auto s = idfxx::to_string(idfxx::http::method::get); // "GET"
```

With `CONFIG_IDFXX_STD_FORMAT` enabled, types also support `std::format`:

```cpp
auto s = std::format("Method: {}", idfxx::http::method::post); // "Method: POST"
```

## License

Apache License 2.0 - see [LICENSE](LICENSE) for details.
