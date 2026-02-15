# idfxx_queue

Type-safe queue for inter-task communication on ESP32.

## Features

- **Chrono-based timeouts and deadlines** with `std::chrono::duration` and `std::chrono::time_point`
- **ISR-safe operations** with structured yield hints for context switches
- **Send-to-front support** for priority message insertion
- **Overwrite support** for "latest value" mailbox patterns
- **Peek without removing** items from the queue
- **PSRAM storage** for placing large queues in external memory
- **Query methods** - `size()`, `available()`, `empty()`, `full()`

## Requirements

- ESP-IDF 5.5 or later
- C++23 compiler

## Installation

### ESP-IDF Component Manager

Add to your project's `idf_component.yml`:

```yaml
dependencies:
  idfxx_queue:
    version: "^0.9.0"
```

Or add `idfxx_queue` to the `REQUIRES` list in your component's `CMakeLists.txt`.

## Usage

### Exception-based API

If `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled:

```cpp
#include <idfxx/queue>
#include <idfxx/log>

using namespace std::chrono_literals;

try {
    // Create a queue that holds up to 10 integers
    idfxx::queue<int> q(10);

    // Send and receive
    q.send(42);
    int value = q.receive();
    idfxx::log::info("queue", "Received: {}", value);

    // With timeout
    q.send(99, 100ms);
    int value2 = q.receive(100ms);

} catch (const std::system_error& e) {
    idfxx::log::error("queue", "Error: {}", e.what());
}
```

### Result-based API

If `CONFIG_COMPILER_CXX_EXCEPTIONS` is *not* enabled:

```cpp
#include <idfxx/queue>
#include <idfxx/log>

using namespace std::chrono_literals;

// Create the queue using the factory method
auto q = idfxx::queue<int>::make(10);
if (!q) {
    idfxx::log::error("queue", "Failed to create queue: {}", q.error().message());
    return;
}

// Send an item
if (auto result = (*q)->try_send(42, 100ms); !result) {
    idfxx::log::error("queue", "Failed to send: {}", result.error().message());
    return;
}

// Receive an item
auto item = (*q)->try_receive(100ms);
if (!item) {
    idfxx::log::error("queue", "Failed to receive: {}", item.error().message());
    return;
}

idfxx::log::info("queue", "Received: {}", *item);
```

### Producer-Consumer Pattern

```cpp
#include <idfxx/queue>

using namespace std::chrono_literals;

idfxx::queue<int> q(10);

// Producer task
void producer_task(void* arg) {
    auto& q = *static_cast<idfxx::queue<int>*>(arg);
    for (int i = 0; i < 100; ++i) {
        q.send(i);
    }
    vTaskDelete(nullptr);
}

// Consumer task
void consumer_task(void* arg) {
    auto& q = *static_cast<idfxx::queue<int>*>(arg);
    for (int i = 0; i < 100; ++i) {
        int value = q.receive();
        idfxx::log::info("consumer", "Got: {}", value);
    }
    vTaskDelete(nullptr);
}
```

### PSRAM Storage

Place queue storage in external PSRAM to free internal DRAM:

```cpp
#include <idfxx/queue>

// Create a large queue in PSRAM
idfxx::queue<sensor_reading> q(1000, idfxx::memory_type::spiram);
```

> **Note:** `memory_type::spiram` requires a device with external PSRAM and `CONFIG_SPIRAM` enabled.

### Send-to-Front (Priority Messages)

```cpp
idfxx::queue<int> q(10);

// Normal messages go to the back
q.send(1);
q.send(2);

// Priority message goes to the front
q.send_to_front(99);

// Receives: 99, 1, 2
int first = q.receive();   // 99
int second = q.receive();  // 1
```

### Overwrite Pattern (Mailbox)

Use a queue of length 1 with `overwrite()` to implement a "latest value" mailbox:

```cpp
// Mailbox always holds the most recent sensor reading
idfxx::queue<float> mailbox(1);

// Producer always succeeds, overwriting stale values
mailbox.overwrite(23.5f);
mailbox.overwrite(24.1f);  // Overwrites 23.5

// Consumer gets the latest value
float temperature = mailbox.receive();  // 24.1
```

### ISR Usage

```cpp
#include <idfxx/queue>
#include <idfxx/chrono>

idfxx::queue<uint32_t> event_queue(16);

void IRAM_ATTR gpio_isr_handler(void* arg) {
    auto& q = *static_cast<idfxx::queue<uint32_t>*>(arg);
    auto [success, yield] = q.send_from_isr(42);
    idfxx::yield_from_isr(yield);
}
```

## API Overview

### Construction

- `queue(length, mem_type)` - Create a queue with the specified capacity (exception-based)
- `queue::make(length, mem_type)` - Create a queue with the specified capacity (result-based)
- `idfxx::memory_type` - Memory region type (`internal`, `spiram`) — defined in `<idfxx/memory>`

### Send

- `send(item)` / `try_send(item)` - Send to back, blocking indefinitely
- `send(item, timeout)` / `try_send(item, timeout)` - Send to back with timeout
- `send_until(item, deadline)` / `try_send_until(item, deadline)` - Send to back with deadline

### Send to Front

- `send_to_front(item)` / `try_send_to_front(item)` - Send to front, blocking indefinitely
- `send_to_front(item, timeout)` / `try_send_to_front(item, timeout)` - Send to front with timeout
- `send_to_front_until(item, deadline)` / `try_send_to_front_until(item, deadline)` - Send to front with deadline

### Receive

- `receive()` / `try_receive()` - Receive item, blocking indefinitely
- `receive(timeout)` / `try_receive(timeout)` - Receive item with timeout
- `receive_until(deadline)` / `try_receive_until(deadline)` - Receive item with deadline

### Peek

- `peek()` / `try_peek()` - Peek at front item without removing, blocking indefinitely
- `peek(timeout)` / `try_peek(timeout)` - Peek with timeout
- `peek_until(deadline)` / `try_peek_until(deadline)` - Peek with deadline

### Overwrite

- `overwrite(item)` / `try_overwrite(item)` - Overwrite last item or send if not full (never blocks)

### ISR Operations

- `send_from_isr(item)` - Send to back from ISR, returns `isr_send_result{success, yield}`
- `send_to_front_from_isr(item)` - Send to front from ISR, returns `isr_send_result{success, yield}`
- `overwrite_from_isr(item)` - Overwrite from ISR, returns `bool yield`
- `receive_from_isr()` - Receive from ISR, returns `isr_receive_result{item, yield}`
- `peek_from_isr()` - Peek from ISR, returns `std::optional<T>`

### Query and State

- `size()` - Number of items currently in the queue
- `available()` - Number of free spaces remaining
- `empty()` - Check if queue contains no items
- `full()` - Check if queue has no free spaces
- `reset()` - Remove all items from the queue
- `idf_handle()` - Get underlying FreeRTOS QueueHandle_t

## Error Handling

Queue operations use error codes from `idfxx::errc`:

- `invalid_arg` - Queue length is 0
- `no_mem` - Memory allocation failed during queue creation
- `timeout` - Send or receive operation timed out (queue full or empty)

All `try_*` methods return `idfxx::result<T>`. Exception-based methods (without `try_` prefix) throw `std::system_error` when `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled.

## Important Notes

- **Trivially copyable**: The message type `T` must be trivially copyable (`std::is_trivially_copyable_v<T>`). This is enforced at compile time via a `requires` clause.
- **Non-copyable/movable**: Queue instances are non-copyable and non-movable to ensure handle stability.
- **RAII cleanup**: The destructor automatically deletes the queue and discards any remaining items.
- **ISR safety**: Use the `*_from_isr` methods in interrupt context. Pass the `yield` field to `idfxx::yield_from_isr()` to perform any necessary context switch.
- **Overwrite semantics**: `overwrite()` is most useful with a queue of length 1. With longer queues, it overwrites the most recently written item when the queue is full.

## License

Apache License 2.0 - see [LICENSE](LICENSE) for details.
