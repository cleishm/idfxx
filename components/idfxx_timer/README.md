# idfxx_timer

High-resolution timer management for ESP32.

## Features

- **Timer lifecycle management** with automatic cleanup
- **Type-safe duration handling** with std::chrono
- **One-shot and periodic timers**
- **Task-dispatched and ISR-dispatched callbacks**
- **Monotonic uptime clock access** compatible with std::chrono

## Requirements

- ESP-IDF 5.5 or later
- C++23 compiler
- idfxx_core component

## Installation

### ESP-IDF Component Manager

Add to your project's `idf_component.yml`:

```yaml
dependencies:
  idfxx_timer:
    version: "^0.9.0"
```

Or add `idfxx_timer` to the `REQUIRES` list in your component's `CMakeLists.txt`.

## Usage

### Exception-based API

If `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled:

```cpp
#include <idfxx/timer>
#include <idfxx/sched>
#include <esp_log.h>

using namespace std::chrono_literals;

try {
    // Create a one-shot timer
    idfxx::timer timer(
        {.name = "my_timer"},
        []() { ESP_LOGI("timer", "Timer fired!"); }
    );

    // Start timer - will fire in 1 second
    timer.start_once(1s);

    // Wait for timer to fire
    idfxx::delay(1500ms);

} catch (const std::system_error& e) {
    ESP_LOGE("timer", "Error: %s", e.what());
}
```

### Result-based API

If `CONFIG_COMPILER_CXX_EXCEPTIONS` is *not* enabled:

```cpp
#include <idfxx/timer>
#include <idfxx/sched>
#include <esp_log.h>

using namespace std::chrono_literals;

// Create timer
auto result = idfxx::timer::make(
    {.name = "my_timer"},
    []() { ESP_LOGI("timer", "Timer fired!"); }
);

if (!result) {
    auto message = result.error().message();
    ESP_LOGE("timer", "Failed to create timer: %s", message.c_str());
    return;
}

auto& timer = *result;

// Start timer
if (auto start_result = timer->try_start_once(1s); !start_result) {
    auto message = start_result.error().message();
    ESP_LOGE("timer", "Failed to start timer: %s", message.c_str());
    return;
}

// Wait for timer to fire
idfxx::delay(1500ms);
```

### Periodic Timer

```cpp
#include <idfxx/timer>
#include <idfxx/sched>

using namespace std::chrono_literals;

int count = 0;
auto timer = idfxx::timer::make(
    {.name = "periodic"},
    [&count]() { count++; }
);

if (timer) {
    // Fire every 100ms
    (*timer)->try_start_periodic(100ms);

    // Let it run for a while
    idfxx::delay(1s);

    // Stop the timer
    (*timer)->try_stop();
    // count should be ~10
}
```

### ISR Dispatch Mode

For callbacks that need to run directly in ISR context, use the raw function pointer overload:

```cpp
#include <idfxx/timer>

void IRAM_ATTR my_isr_callback(void* arg) {
    // This runs in ISR context - keep it short!
    auto* flag = static_cast<bool*>(arg);
    *flag = true;
}

bool flag = false;
auto timer = idfxx::timer::make(
    {.name = "isr_timer", .dispatch = idfxx::timer::dispatch_method::isr},
    my_isr_callback,
    &flag
);
```

Note that `idfxx::timer::dispatch_method::isr` is only available when `CONFIG_ESP_TIMER_SUPPORTS_ISR_DISPATCH_METHOD` is
enabled.

### ISR-Safe Timer Control

For controlling timers from ISR context (when `CONFIG_ESP_TIMER_IN_IRAM` is enabled), use the `_isr` methods. These return raw `esp_err_t` and are placed in IRAM:

```cpp
#include <idfxx/timer>

// Pre-compute timeout at compile time for ISR safety
constexpr uint64_t timeout_us = std::chrono::microseconds(100ms).count();

// In ISR context:
timer->try_start_once_isr(timeout_us);
timer->try_restart_isr(timeout_us);
timer->try_stop_isr();
```

These methods are always available, but are only ISR-safe when `CONFIG_ESP_TIMER_IN_IRAM=y`.

### Using the Clock

The timer component provides a monotonic clock compatible with std::chrono:

```cpp
#include <idfxx/timer>

// Get current time
auto now = idfxx::timer::clock::now();

// Calculate remaining time for one-shot timers
auto expiry = timer->expiry_time();
if (expiry != idfxx::timer::clock::time_point::max()) {
    auto remaining = expiry - idfxx::timer::clock::now();
    auto remaining_us = std::chrono::microseconds(remaining).count();
    ESP_LOGI("timer", "Time remaining: %lld us", remaining_us);
}

// Get next alarm time across all timers
auto next = idfxx::timer::next_alarm();
```

## API Overview

### Factory Methods

- `timer::make(config, callback)` - Create timer with std::move_only_function callback
- `timer::make(config, fn, arg)` - Create timer with raw function pointer callback

### Constructors (exception-based)

- `timer(config, callback)` - Create timer with std::move_only_function callback
- `timer(config, fn, arg)` - Create timer with raw function pointer callback

### Timer Control

- `start_once(duration)` / `try_start_once(duration)` - Start as one-shot timer
- `start_periodic(duration)` / `try_start_periodic(duration)` - Start as periodic timer
- `restart(duration)` / `try_restart(duration)` - Restart with new timeout
- `stop()` / `try_stop()` - Stop running timer
- `start_once_isr(us)` / `start_periodic_isr(us)` / `restart_isr(us)` / `stop_isr()` - ISR-safe methods

### Status

- `is_active()` - Check if timer is running
- `period()` - Get periodic timer interval (duration), returns 0 for one-shot timers
- `expiry_time()` - Get one-shot expiry time (time_point), returns max() for periodic timers or on error
- `idf_handle()` - Get underlying esp_timer handle

### Static Methods

- `timer::clock::now()` - Get current time (time_point)
- `timer::next_alarm()` - Get time of next scheduled alarm (time_point)

## Error Handling

Timer operations use error codes from `idfxx::errc`:

- `invalid_state` - Timer already running (start) or not running (stop)
- `invalid_arg` - Invalid configuration or duration
- `no_mem` - Memory allocation failed

All `try_*` methods return `idfxx::result<T>`. Exception-based methods (without `try_` prefix) throw `std::system_error` when `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled.

## Important Notes

- **ISR dispatch**: Use the raw function pointer overload with `dispatch_method::isr` for ISR-safe callbacks. The std::move_only_function overload allocates memory that may not be in IRAM.
- **Non-copyable/movable**: Timer is non-copyable and non-movable to ensure callback pointer stability.
- **Automatic cleanup**: The destructor automatically stops and deletes the timer.
- **Name storage**: Timer names are copied internally and remain valid for the timer's lifetime.

## License

Apache License 2.0 - see [LICENSE](LICENSE) for details.
