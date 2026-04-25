# idfxx_core

Core utilities for the idfxx component family, providing foundational error handling, memory allocators, and chrono utilities for modern C++ ESP-IDF development.

📚 **[Full API Documentation](https://cleishm.github.io/idfxx/group__idfxx__core.html)**

## Features

- **Result-based error handling** with `idfxx::result<T>` (C++23 `std::expected`)
- **ESP-IDF error code integration** via `idfxx::errc` enum
- **Chrono utilities** for FreeRTOS tick conversions
- **Memory utilities** — capability flags, allocators, heap queries, walking, integrity checking
- **System information** — reset reason, restart, shutdown handlers
- **Application metadata** — version, project name, build timestamps, ELF hash
- **Random number generation** — hardware RNG with `UniformRandomBitGenerator` support
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
    version: "^1.0.0"
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

// Large buffer in external PSRAM (requires CONFIG_SPIRAM)
std::vector<uint8_t, idfxx::spiram_allocator<uint8_t>> psram_buffer;

// DMA-capable buffer for peripheral transfers
std::vector<uint8_t, idfxx::dma_allocator<uint8_t>> dma_buffer;

// 32-byte aligned DRAM buffer (e.g. for cache-line alignment)
std::vector<uint8_t, idfxx::aligned_dram_allocator<uint8_t, 32>> aligned_buffer;
```

### C-style Heap Allocation

```cpp
#include <idfxx/memory>

// Allocate from internal DRAM
void* buf = idfxx::malloc(256, idfxx::memory::capabilities::dram);
// ... use buf ...
idfxx::free(buf);

// Aligned allocation for DMA buffers
void* dma_buf = idfxx::aligned_alloc(64, 1024, idfxx::memory::capabilities::dma);
// ... use dma_buf ...
idfxx::free(dma_buf);
```

### Heap Walking and Integrity Checking

```cpp
#include <idfxx/memory>

namespace memory = idfxx::memory;

// Walk all blocks in default heap
memory::walk(memory::capabilities::default_heap, [](memory::region rgn, memory::block blk) {
    // process each block...
    return true; // continue walking
});

// Check heap integrity
bool ok = memory::check_integrity();
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

### Memory (`<idfxx/memory>`)

- `memory::capabilities` - Composable flags enum for memory capability flags (`internal`, `spiram`, `dma`, `dram`, etc.)
- `dram_allocator<T>` - Allocates from internal DRAM (ISR-safe)
- `spiram_allocator<T>` - Allocates from external PSRAM (requires `CONFIG_SPIRAM`)
- `dma_allocator<T>` - Allocates DMA-capable memory
- `aligned_dram_allocator<T, Alignment>` - Aligned allocation from internal DRAM
- `aligned_spiram_allocator<T, Alignment>` - Aligned allocation from external PSRAM
- `aligned_dma_allocator<T, Alignment>` - Aligned DMA-capable allocation
- `malloc(size, caps)` - Allocate memory from matching heap regions
- `calloc(n, size, caps)` - Allocate zero-initialized memory from matching regions
- `realloc(ptr, size, caps)` - Reallocate memory from matching regions
- `free(ptr)` - Free memory allocated by allocation functions
- `aligned_alloc(alignment, size, caps)` - Aligned allocation from matching regions
- `aligned_calloc(alignment, n, size, caps)` - Aligned, zero-initialized allocation
- `memory::capabilities` - Composable flags enum for memory capability flags (`internal`, `spiram`, `dma`, `dram`, etc.)
- `memory::info` - Struct containing heap region statistics
- `memory::total_size(caps)` - Total size of heap regions matching capabilities
- `memory::free_size(caps)` - Current free size of matching heap regions
- `memory::largest_free_block(caps)` - Largest contiguous free block in matching regions
- `memory::minimum_free_size(caps)` - Minimum free size since boot (high-water mark)
- `memory::get_info(caps)` - Detailed heap statistics for matching regions
- `memory::walk(caps, walker)` - Walk heap blocks in matching regions
- `memory::check_integrity(caps)` / `memory::check_integrity()` - Check heap integrity
- `memory::dump(caps)` / `memory::dump()` - Dump heap structure to serial console

### System (`<idfxx/system>`)

- `reset_reason` - Enum for all chip reset reasons
- `last_reset_reason()` - Returns the reason for the most recent reset
- `restart()` - Restarts the chip
- `register_shutdown_handler()` / `try_register_shutdown_handler()` - Register shutdown callbacks
- `unregister_shutdown_handler()` / `try_unregister_shutdown_handler()` - Remove shutdown callbacks

### Application Info (`<idfxx/app>`)

- `app::version()` - Application version string
- `app::project_name()` - Project name
- `app::compile_time()` / `app::compile_date()` - Build timestamps
- `app::idf_version()` - ESP-IDF version used for the build
- `app::secure_version()` - Secure version counter
- `app::elf_sha256_hex()` - ELF SHA-256 hash as hex string

### Random Numbers (`<idfxx/random>`)

- `random()` - Hardware-generated random 32-bit value
- `fill_random(span)` - Fill a buffer with random bytes
- `random_device` - `UniformRandomBitGenerator` for use with standard distributions

## Error Codes

The `idfxx::errc` enum provides common error codes compatible with ESP-IDF:

- `fail` - Generic failure
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
- **Out-of-memory is always fatal**: any allocation failure (C++, ESP-IDF, or FreeRTOS) throws
  `std::bad_alloc` when exceptions are enabled, or calls `abort()` otherwise. OOM is never
  returned as a recoverable error in `result<T>`. Use the ESP-IDF and FreeRTOS APIs directly
  if you need to handle potential out-of-memory conditions gracefully.

## License

Apache License 2.0 - see [LICENSE](LICENSE) for details.
