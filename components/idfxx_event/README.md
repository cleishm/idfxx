# idfxx_event

Type-safe event loop for asynchronous event handling.

## Features

- **Type-safe event listeners** with `event` for compile-time data type safety
- **Dual error handling** - exception-based and `result<T>` returning methods
- **RAII listener handles** for automatic cleanup
- **User event loops** with configurable task and queue settings
- **System event loop** static interface for system events

## Requirements

- ESP-IDF 5.5 or later
- C++23 compiler

## Installation

### ESP-IDF Component Manager

Add to your project's `idf_component.yml`:

```yaml
dependencies:
  idfxx_event:
    version: "^0.9.0"
```

Or add `idfxx_event` to the `REQUIRES` list in your component's `CMakeLists.txt`.

## Usage

### User-defined Events

```cpp
#include <idfxx/event>

using namespace idfxx;

// Define event IDs
enum class my_event_id { started, data_ready, stopped };

// Create a typed event base
IDFXX_EVENT_DEFINE_BASE(my_events, my_event_id);

// Define event data type (trivially copyable types work automatically)
struct my_data {
    int value;
};

// Define typed events
inline constexpr event<my_event_id> started{my_event_id::started};
inline constexpr event<my_event_id, my_data> data_ready{my_event_id::data_ready};
```

### Basic Example (Exception-based)

If `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled:

```cpp
// Create event loop with dedicated task
auto loop = event_loop({.name = "events"}, 32);

// Register type-safe listeners
loop.listener_add(started, []() {
    log::info("app", "App started!");
});

loop.listener_add(data_ready, [](const my_data& data) {
    log::info("app", "Data: {}", data.value);
});

// Post events (type-safe)
loop.post(started);
loop.post(data_ready, my_data{42});
```

### Result-based API

If `CONFIG_COMPILER_CXX_EXCEPTIONS` is *not* enabled:

```cpp
// Create event loop
auto loop_result = event_loop::make({.name = "events"}, 32);
if (!loop_result) {
    log::error("app", "Failed to create event loop: {}", loop_result.error().message());
    return;
}
auto loop = std::move(*loop_result);

// Register listener
auto handle_result = loop.try_listener_add(started, []() {
    log::info("app", "App started!");
});

if (!handle_result) {
    log::error("app", "Failed to register listener: {}", handle_result.error().message());
    return;
}
```

### Listen For Any Event From An Event Base (Wildcard)

```cpp
// Omit the event parameter to listen for any event (untyped callback)
loop.listener_add(my_events,
    [](event_base<my_event_id>, my_event_id id, void* data) {
        log::info("app", "Event: {}", static_cast<int>(id));
    });
```

### RAII Listener Handle

```cpp
{
    // Take ownership with unique_listener_handle
    event_loop::unique_listener_handle handle{
        loop.listener_add(started, []() { ... })
    };

    // Listener is active while handle is in scope

} // Listener automatically removed here
```

### Event Loop Without Dedicated Task

```cpp
using namespace std::chrono_literals;

// Create loop without task
auto loop = user_event_loop(16);

// Manually dispatch events
while (running) {
    loop.run(100ms);
}
```

### System Event Loop

```cpp
event_loop::create_system();
auto& sys = event_loop::system();

sys.listener_add(started, []() {
    log::info("app", "Got event on system loop");
});

sys.post(started);
```

## API Overview

### event_base<IdEnum>

- `idf_base()` - Get underlying esp_event_base_t

### event<IdEnum, DataType = void>

- Pairs an event ID with its data type for type-safe listener registration and posting. The event base is looked up automatically from the enum type via ADL
- `DataType` must satisfy `receivable_event_data` (trivially copyable types work automatically; types needing custom reconstruction provide `from_opaque`). Posting requires `event_data` (type must be trivially copyable). Defaults to `void` (no data provided with the event)

### event_loop

Static methods for system event loop:
- `create_system()` / `try_create_system()` - Create system event loop
- `destroy_system()` / `try_destroy_system()` - Destroy system event loop
- `system()` - Get reference to system event loop

Factory for event loop with dedicated task:
- `event_loop(task_config, queue_size)` / `make(task_config, queue_size)` - Create loop with task

Instance methods:
- `listener_add(event, callback)` / `try_listener_add(...)` - Register type-safe listener
- `listener_add(base, callback)` / `try_listener_add(...)` - Register wildcard listener
- `listener_remove(handle)` / `try_listener_remove(handle)` - Remove listener by handle
- `post(event)` / `try_post(...)` - Post event without data
- `post(event, data)` / `try_post(...)` - Post event with data
- `idf_handle()` - Get underlying esp_event_loop_handle_t

### user_event_loop

Factory for event loop without dedicated task:
- `user_event_loop(queue_size)` / `make(queue_size)` - Create loop without task

Extends `event_loop` with manual dispatch:
- `run(duration)` / `try_run(duration)` - Dispatch pending events

### event_loop::listener_handle

- Copyable, non-RAII handle to a registered listener

### event_loop::unique_listener_handle

- Move-only RAII handle for listener_handle
- `release()` - Transfer ownership back to listener_handle
- `reset()` - Remove listener and reset to empty

## Error Handling

Event operations use error codes from `idfxx::errc` and ESP-IDF:

- `invalid_arg` - Invalid handle or parameter
- `invalid_state` - Loop not created or already destroyed

All `try_*` methods return `idfxx::result<T>`. Exception-based methods throw `std::system_error` when `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled.

## Important Notes

- **Dual API Pattern**: Methods prefixed with `try_` return `result<T>`; unprefixed methods throw exceptions (requires `CONFIG_COMPILER_CXX_EXCEPTIONS`)
- **Type-safe listeners**: Use `event` constants for specific events; wildcard listeners still use untyped callbacks
- Create the system event loop with `event_loop::create_system()` before registering system event listeners
- Event callbacks may be invoked from different task contexts depending on loop configuration
- Use `unique_listener_handle` for automatic cleanup; `listener_handle` requires manual removal

## License

Apache License 2.0 - see [LICENSE](LICENSE) for details.
