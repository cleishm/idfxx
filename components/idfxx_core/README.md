# idfxx_core

Core utilities for the idfxx component family, providing foundational error handling, memory allocators, and chrono utilities for modern C++ ESP-IDF development.

📚 **[Full API Documentation](https://cleishm.github.io/idfxx/group__idfxx__core.html)**

## Features

- **Result-based error handling** with `idfxx::result<T>` (C++23 `std::expected`)
- **ESP-IDF error code integration** via `idfxx::errc` enum
- **Chrono utilities** for FreeRTOS tick conversions
- **Custom memory allocators** for DRAM, IRAM, and DMA-capable memory
- **Exception support** with `unwrap()` helper (when exceptions enabled)

## Requirements

- ESP-IDF 5.5 or later
- C++23 compiler

## Installation

### ESP-IDF Component Manager

Add to your project's `idf_component.yml`:

```yaml
dependencies:
  idfxx_core:
    version: "^0.9.0"
```

Or add `idfxx_core` to the `REQUIRES` list in your component's `CMakeLists.txt`.

## Usage

### Error Handling

```cpp
#include <idfxx/error>

idfxx::result<int> read_sensor() {
    if (error_condition) {
        return idfxx::error(idfxx::errc::invalid_state);
    }
    return 42;
}

// Check result
auto result = read_sensor();
if (result) {
    int value = *result;
} else {
    idfxx::log::error("app", "Error: {}", result.error().value());
}
```

### Chrono Utilities

```cpp
#include <idfxx/chrono>
#include <chrono>

using namespace std::chrono_literals;

TickType_t delay = idfxx::chrono::to_ticks(100ms);
vTaskDelay(delay);
```

### Memory Allocators

```cpp
#include <idfxx/memory>
#include <vector>

// ISR-safe vector using DRAM allocator
std::vector<int, idfxx::dram_allocator<int>> isr_safe_vector;
```

## API Overview

### Error Handling (`<idfxx/error>`)

- `result<T>` - Alias for `std::expected<T, std::error_code>`
- `errc` - Enum of ESP-IDF compatible error codes
- `error(errc)` - Create `std::unexpected` from an error code enum
- `error(esp_err_t)` - Create `std::unexpected` from an ESP-IDF error code
- `error(std::error_code)` - Create `std::unexpected` from a `std::error_code` (for error propagation)
- `unwrap(result)` - Extract value or throw `std::system_error` (requires exceptions)

### Chrono Utilities (`<idfxx/chrono>`)

- `to_ticks(duration)` - Convert `std::chrono::duration` to `TickType_t`

### Memory Allocators (`<idfxx/memory>`)

- `dram_allocator<T>` - Allocates from internal DRAM (ISR-safe)
- `iram_allocator<T>` - Allocates from instruction RAM
- `dma_allocator<T>` - Allocates DMA-capable memory

## Error Codes

The `idfxx::errc` enum provides common error codes compatible with ESP-IDF:

- `fail` - Generic failure
- `no_mem` - Out of memory
- `invalid_arg` - Invalid argument
- `invalid_state` - Invalid state
- `timeout` - Operation timed out
- And more...

See the [full documentation](https://cleishm.github.io/idfxx/group__idfxx__core.html) for complete API reference.

## Important Notes

- All idfxx components depend on idfxx_core for error handling
- `result<T>` is the standard return type for fallible operations across idfxx
- Memory allocators are stateless and can be used with standard containers
- Chrono conversions handle overflow by clamping to `portMAX_DELAY`

## License

Apache License 2.0 - see [LICENSE](LICENSE) for details.
