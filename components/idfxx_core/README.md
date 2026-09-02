# idfxx_core

Core utilities for the idfxx component family, providing foundational error handling, memory allocators, and chrono utilities for modern C++ ESP-IDF development.

📚 **[Full API Documentation](https://cleishm.github.io/idfxx/group__idfxx__core.html)**

## Features

- **Result-based error handling** with `idfxx::result<T>` (C++23 `std::expected`)
- **ESP-IDF error code integration** via `idfxx::errc` enum
- **Chrono utilities** — FreeRTOS tick conversions, and `std::chrono` clocks over the tick count and the RTC timer
- **Scheduling utilities** — `delay()`, `delay_until()`, `delay(next_tick)`, `yield()`
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
    version: "^1.2.0"
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

TickType_t timeout = idfxx::chrono::ticks(100ms);
xQueueReceive(queue, &item, timeout);
```

### RTC Clock

`rtc_clock` counts from the last power-on reset and keeps running through deep sleep and
every other kind of reset, so time points survive where `std::chrono::steady_clock` (which
restarts at every boot) does not:

```cpp
#include <idfxx/chrono>
#include <chrono>

using namespace std::chrono_literals;
using idfxx::chrono::rtc_clock;

// Time since power-on, including time spent in deep sleep
auto uptime = rtc_clock::now().time_since_epoch();

// Rate-limit an action across deep-sleep cycles: the time point lives in RTC memory
RTC_DATA_ATTR static rtc_clock::time_point last_report;
if (rtc_clock::now() - last_report >= 1h) {
    send_report();
    last_report = rtc_clock::now();
}
```

### Scheduling

```cpp
#include <idfxx/sched>
#include <chrono>

using namespace std::chrono_literals;

idfxx::delay(100ms);   // block for at least 100ms (other tasks run)
idfxx::delay(500us);   // shorter than a tick: busy-waits with microsecond precision

// Polling back-off: block until the next tick so every other task gets to run
while (busy_pin.get_level() == idfxx::gpio::level::high) {
    idfxx::delay(idfxx::next_tick);
}

// Drift-free periodic loop
auto next = idfxx::chrono::tick_clock::now();
while (true) {
    do_work();
    next += 100ms;
    idfxx::delay_until(next);
}
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

- `ticks(duration)` - Convert `std::chrono::duration` to `TickType_t`
- `tick_clock` - `std::chrono` clock over the FreeRTOS tick count
- `rtc_clock` - `std::chrono` clock over the RTC timer: microsecond resolution, counts from power-on, keeps running through deep sleep and resets

### Scheduling (`<idfxx/sched>`)

- `delay(duration)` - Delay for at least `duration`; sub-tick durations busy-wait, longer ones block
- `delay_until(time_point)` - Delay until a time point (drift-free periodic loops)
- `delay(next_tick)` - Block until the next scheduler tick; the back-off for polling loops
- `yield()` - Hand the CPU to ready tasks of equal priority without blocking
- `yield_from_isr(woken)` - Request a context switch at the end of an ISR

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
- **`rtc_clock` accuracy follows the RTC slow clock source**: the default internal RC
  oscillator drifts with temperature, so long intervals measured across sleep drift with it.
  Select an external 32 kHz crystal in the project configuration when timing across sleep
  needs to be accurate
- **`delay()` busy-waits below one tick**: durations shorter than the scheduler tick
  (`1 / CONFIG_FREERTOS_HZ`, 10 ms by default) spin the CPU for precision instead of blocking.
  Never use a short `delay()` as the back-off in a polling loop — use `delay(next_tick)`, which
  always blocks until the next tick. Longer delays block and never return early, but may
  overshoot by up to one tick
- **Out-of-memory is always fatal**: any allocation failure (C++, ESP-IDF, or FreeRTOS) throws
  `std::bad_alloc` when exceptions are enabled, or calls `abort()` otherwise. OOM is never
  returned as a recoverable error in `result<T>`. Use the ESP-IDF and FreeRTOS APIs directly
  if you need to handle potential out-of-memory conditions gracefully.

## License

Apache License 2.0 - see [LICENSE](LICENSE) for details.
