# idfxx_event

Type-safe event loop for asynchronous event handling.

## Features

- **Type-safe event bases** with templated ID enum associations
- **Dual error handling** - exception-based and `result<T>` returning methods
- **RAII listener handles** for automatic cleanup
- **User event loops** with configurable task and queue settings
- **System event loop** static interface for system events
- **Raw function pointer support** for C interop and IRAM placement

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

### Basic Example (Exception-based)

If `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled:

```cpp
#include <idfxx/event>
#include <esp_wifi.h>

using namespace idfxx;

// Create user event loop with dedicated task
auto loop = event_loop::make_user(32, {.name = "events"});

// Register typed listener for specific WiFi event
auto handle = loop->listener_add(
    event_base<wifi_event_t>{WIFI_EVENT},
    WIFI_EVENT_STA_START,
    [](event_base<wifi_event_t>, wifi_event_t id, void* data) {
        idfxx::log::info("app", "WiFi started!");
    });

// Or use system event loop for system events
event_loop::create_system();
event_loop::system().listener_add(
    event_base<ip_event_t>{IP_EVENT},
    IP_EVENT_STA_GOT_IP,
    [](event_base<ip_event_t>, ip_event_t id, void* data) {
        idfxx::log::info("app", "Got IP address!");
    });
```

### Result-based API

If `CONFIG_COMPILER_CXX_EXCEPTIONS` is *not* enabled:

```cpp
#include <idfxx/event>
#include <esp_wifi.h>

using namespace idfxx;

// Create event loop
auto loop_result = event_loop::try_make_user(32, {.name = "events"});
if (!loop_result) {
    idfxx::log::error("app", "Failed to create event loop: {}", loop_result.error().message());
    return;
}
auto loop = std::move(*loop_result);

// Register listener
auto handle_result = loop->try_listener_add(
    event_base<wifi_event_t>{WIFI_EVENT},
    WIFI_EVENT_STA_START,
    [](event_base<wifi_event_t>, wifi_event_t id, void* data) {
        idfxx::log::info("app", "WiFi started!");
    });

if (!handle_result) {
    idfxx::log::error("app", "Failed to register listener: {}", handle_result.error().message());
    return;
}
```

### User-defined Events

```cpp
// Define your event ID enum
enum class my_event : int32_t {
    started = 0,
    data_ready = 1,
    stopped = 2
};

// Create a typed event base
ESP_EVENT_DEFINE_BASE(MY_APP_EVENT);
inline constexpr event_base<my_event> my_app{MY_APP_EVENT};

// Register listener
loop->listener_add(my_app, my_event::started,
    [](event_base<my_event>, my_event id, void* data) {
        idfxx::log::info("app", "App started!");
    });

// Post event with data
struct my_data { int value; };
my_data payload{42};
loop->post(my_app, my_event::data_ready, &payload, sizeof(payload));
```

### Listen for Any Event from a Base

```cpp
// Omit the ID parameter to listen for any event
loop->listener_add(event_base<wifi_event_t>{WIFI_EVENT},
    [](event_base<wifi_event_t>, wifi_event_t id, void* data) {
        idfxx::log::info("app", "WiFi event: {}", static_cast<int>(id));
    });
```

### Using event_type Syntax

```cpp
// Create event_type using operator()
auto wifi_start = event_base<wifi_event_t>{WIFI_EVENT}(WIFI_EVENT_STA_START);

loop->listener_add(wifi_start,
    [](event_base<wifi_event_t>, wifi_event_t id, void* data) {
        idfxx::log::info("app", "WiFi started!");
    });

loop->post(wifi_start);
```

### RAII Listener Handle

```cpp
{
    // Take ownership with unique_listener_handle
    event_loop::unique_listener_handle handle{
        loop->listener_add(base, id, callback)
    };

    // Listener is active while handle is in scope

} // Listener automatically removed here
```

### Raw Function Pointer (for IRAM)

```cpp
// Use untyped event_base<void> for raw function pointers
void IRAM_ATTR my_handler(void* arg, esp_event_base_t base, int32_t id, void* data) {
    // IRAM-safe handler
}

loop->listener_add(
    event_base<void>{WIFI_EVENT},
    WIFI_EVENT_STA_START,
    my_handler,
    nullptr);
```

### Event Loop Without Dedicated Task

```cpp
using namespace std::chrono_literals;

// Create loop without task
auto loop = event_loop::make_user(16);

// Manually dispatch events
while (running) {
    loop->run(100ms);
}
```

## API Overview

### event_base<IdEnum>

- `event_base(std::string_view name)` - Construct from name string
- `idf_base()` - Get underlying esp_event_base_t
- `operator()(IdEnum id)` - Create event_type with specific ID

### event_base<void>

- `any()` - Static method returning wildcard base (ESP_EVENT_ANY_BASE)
- Accepts `int32_t` IDs instead of typed enum

### event_type<IdEnum>

- Combines `event_base<IdEnum>` + `IdEnum id`
- `idf_base()` / `idf_id()` - Get underlying values

### event_callback<IdEnum>

- Type alias for `std::move_only_function<void(event_base<IdEnum>, IdEnum, void*) const>`
- Callback type for typed event listeners

### event_loop

Static methods for system event loop:
- `create_system()` / `try_create_system()` - Create system event loop
- `destroy_system()` / `try_destroy_system()` - Destroy system event loop
- `system()` - Get reference to system event loop

Static factory methods for user event loops:
- `make_user(queue_size)` / `try_make_user(queue_size)` - Create loop without task (returns `user_event_loop`)
- `make_user(queue_size, task_config)` / `try_make_user(queue_size, task_config)` - Create loop with task

Instance methods:
- `listener_add(...)` / `try_listener_add(...)` - Register listeners (multiple overloads)
- `listener_remove(handle)` / `try_listener_remove(handle)` - Remove listener by handle
- `post(...)` / `try_post(...)` - Post events
- `idf_handle()` - Get underlying esp_event_loop_handle_t

### user_event_loop

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
- `no_mem` - Out of memory
- `invalid_state` - Loop not created or already destroyed

All `try_*` methods return `idfxx::result<T>`. Exception-based methods throw `std::system_error` when `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled.

## Important Notes

- **Dual API Pattern**: Methods prefixed with `try_` return `result<T>`; unprefixed methods throw exceptions (requires `CONFIG_COMPILER_CXX_EXCEPTIONS`)
- Create the system event loop with `event_loop::create_system()` before registering system event listeners
- Event callbacks may be invoked from different task contexts depending on loop configuration
- Use `unique_listener_handle` for automatic cleanup; `listener_handle` requires manual removal
- For IRAM-safe handlers, use the raw function pointer overload with `event_base<void>`

## License

Apache License 2.0 - see [LICENSE](LICENSE) for details.
