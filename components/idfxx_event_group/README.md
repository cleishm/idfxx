# idfxx_event_group

Type-safe event group for inter-task synchronization.

## Features

- **Type-safe event bits** using `idfxx::flags<E>` with scoped enums
- **Set, clear, and get** event bits with no-fail guarantees
- **Wait for any or all bits** with timeout and deadline support
- **Task rendezvous** via `sync()` for multi-task synchronization
- **ISR-safe operations** for setting, clearing, and reading bits from interrupts
- **Header-only** template implementation

## Requirements

- ESP-IDF 5.5 or later
- C++23 compiler

## Installation

### ESP-IDF Component Manager

Add to your project's `idf_component.yml`:

```yaml
dependencies:
  idfxx_event_group:
    version: "^0.9.0"
```

Or add `idfxx_event_group` to the `REQUIRES` list in your component's `CMakeLists.txt`.

## Usage

### Define Event Bits

```cpp
#include <idfxx/event_group>

enum class my_event : uint32_t {
    data_ready = 1u << 0,
    timeout    = 1u << 1,
    error      = 1u << 2,
};
template<> inline constexpr bool idfxx::enable_flags_operators<my_event> = true;
```

### Exception-based API

```cpp
#include <idfxx/event_group>
#include <idfxx/sched>
#include <idfxx/log>

using namespace std::chrono_literals;

idfxx::event_group<my_event> eg;

// Set bits from one task
eg.set(my_event::data_ready);

// Wait for any bit in another task
try {
    auto bits = eg.wait(my_event::data_ready | my_event::timeout,
                        idfxx::wait_mode::any);

    if (bits.contains(my_event::data_ready)) {
        idfxx::log::info("app", "Data is ready");
    }
} catch (const std::system_error& e) {
    idfxx::log::error("app", "Wait error: {}", e.what());
}
```

### Result-based API

```cpp
#include <idfxx/event_group>
#include <idfxx/sched>
#include <idfxx/log>

using namespace std::chrono_literals;

idfxx::event_group<my_event> eg;

// Set bits from one task
eg.set(my_event::data_ready);

// Wait for any bit with timeout
auto bits = eg.try_wait(my_event::data_ready | my_event::timeout,
                        idfxx::wait_mode::any, 100ms);
if (bits) {
    if (bits->contains(my_event::data_ready)) {
        idfxx::log::info("app", "Data is ready");
    }
} else {
    idfxx::log::error("app", "Wait failed: {}", bits.error().message());
}
```

### Task Rendezvous with sync()

```cpp
#include <idfxx/event_group>
#include <idfxx/task>

using namespace std::chrono_literals;

idfxx::event_group<my_event> eg;

auto wait_bits = my_event::data_ready | my_event::timeout;

// Task 1: set data_ready, wait for both
idfxx::task t1({.name = "task1"}, [&](idfxx::task::self&) {
    eg.sync(my_event::data_ready, wait_bits, 500ms);
});

// Task 2: set timeout, wait for both
idfxx::task t2({.name = "task2"}, [&](idfxx::task::self&) {
    eg.sync(my_event::timeout, wait_bits, 500ms);
});
```

### ISR Operations

```cpp
#include <idfxx/event_group>
#include <idfxx/sched>

void IRAM_ATTR my_isr() {
    auto [success, yield] = eg->set_from_isr(my_event::data_ready);
    idfxx::yield_from_isr(yield);
}
```

## API Overview

### Construction

- `event_group()` - Create event group

### Set / Clear / Get

- `set(bits)` - Set event bits, returns bits at time of return
- `clear(bits)` - Clear event bits, returns bits before clearing
- `get()` - Get current event bits

### Wait

- `wait(bits, mode, clear_on_exit)` / `try_wait(bits, mode, clear_on_exit)` - Wait indefinitely
- `wait(bits, mode, timeout, clear_on_exit)` / `try_wait(bits, mode, timeout, clear_on_exit)` - Wait with timeout
- `wait_until(bits, mode, deadline, clear_on_exit)` / `try_wait_until(bits, mode, deadline, clear_on_exit)` - Wait with deadline

### Sync

- `sync(set_bits, wait_bits)` / `try_sync(set_bits, wait_bits)` - Sync indefinitely
- `sync(set_bits, wait_bits, timeout)` / `try_sync(set_bits, wait_bits, timeout)` - Sync with timeout
- `sync_until(set_bits, wait_bits, deadline)` / `try_sync_until(set_bits, wait_bits, deadline)` - Sync with deadline

### ISR Operations

- `set_from_isr(bits)` - Set bits from ISR (returns `isr_set_result{success, yield}`)
- `clear_from_isr(bits)` - Clear bits from ISR (returns bits before clear)
- `get_from_isr()` - Get bits from ISR

### Handle Access

- `idf_handle()` - Get underlying `EventGroupHandle_t`

## Error Handling

Wait and sync operations use error codes from `idfxx::errc`:

- `timeout` - The wait condition was not satisfied within the specified time

All `try_*` methods return `idfxx::result<T>`. Exception-based methods (without `try_` prefix) throw `std::system_error` when `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled.

## Important Notes

- **clear_on_exit defaults to true**: Wait operations clear the matched bits by default, preventing accidental double-processing of events.
- **Non-copyable/movable**: Event groups are non-copyable and non-movable.
- **Automatic cleanup**: The destructor automatically deletes the event group. Tasks blocked on the group are unblocked.
- **ISR set is deferred**: `set_from_isr()` posts to the timer daemon task. It can fail if the command queue is full.
- **Bit width**: The enum's underlying type must fit within `EventBits_t` (typically 24 usable bits on ESP32).

## License

Apache License 2.0 - see [LICENSE](LICENSE) for details.
