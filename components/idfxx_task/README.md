# idfxx_task

Task lifecycle management for ESP32.

## Features

- **Join support** with optional timeout and deadline-based waiting
- **Cooperative stop signaling** via `request_stop()` / `stop_requested()`
- **Auto-join destructor** requests task to stop then blocks until the task completes (similar to `std::jthread`)
- **Fire-and-forget tasks** via spawn() and detach()
- **Suspend/resume control** with ISR-safe resume
- **Task notifications** for lightweight wake-up (binary and counting semaphore patterns)
- **Core affinity** for pinning tasks to specific cores

## Requirements

- ESP-IDF 5.5 or later
- C++23 compiler

## Installation

### ESP-IDF Component Manager

Add to your project's `idf_component.yml`:

```yaml
dependencies:
  idfxx_task:
    version: "^0.9.0"
```

Or add `idfxx_task` to the `REQUIRES` list in your component's `CMakeLists.txt`.

## Usage

### Exception-based API

If `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled:

```cpp
#include <idfxx/sched>
#include <idfxx/task>
#include <idfxx/log>

using namespace std::chrono_literals;

try {
    // Create a task that runs a lambda
    idfxx::task my_task(
        {.name = "worker", .stack_size = 4096, .priority = 5},
        [](idfxx::task::self&) {
            while (true) {
                idfxx::log::info("worker", "Working...");
                idfxx::delay(1s);
            }
        }
    );

    // Task runs until my_task goes out of scope

} catch (const std::system_error& e) {
    idfxx::log::error("main", "Error: {}", e.what());
}
```

### Result-based API

If `CONFIG_COMPILER_CXX_EXCEPTIONS` is *not* enabled:

```cpp
#include <idfxx/sched>
#include <idfxx/task>
#include <idfxx/log>

using namespace std::chrono_literals;

// Create task
auto result = idfxx::task::make(
    {.name = "worker", .stack_size = 4096, .priority = 5},
    [](idfxx::task::self&) {
        while (true) {
            idfxx::log::info("worker", "Working...");
            idfxx::delay(1s);
        }
    }
);

if (!result) {
    idfxx::log::error("main", "Failed to create task: {}", result.error().message());
    return;
}

auto& my_task = *result;

// Suspend and resume
if (auto r = my_task->try_suspend(); !r) {
    idfxx::log::error("main", "Failed to suspend");
}

idfxx::delay(5s);

if (auto r = my_task->try_resume(); !r) {
    idfxx::log::error("main", "Failed to resume");
}
```

### Fire-and-Forget Tasks

For tasks that manage their own lifecycle:

```cpp
#include <idfxx/task>

// spawn() creates a self-managing task
idfxx::task::spawn(
    {.name = "one_shot"},
    [](idfxx::task::self&) {
        // Do some work
        process_data();
        // Task automatically cleans up when done
    }
);

// Or detach an existing task
idfxx::task my_task({.name = "detachable"}, work_function);
my_task.detach();  // Task now self-manages
// my_task is now non-joinable
```

### Joining Tasks

Wait for a task to complete using `join()` with an optional timeout:

```cpp
#include <idfxx/task>

using namespace std::chrono_literals;

try {
    idfxx::task worker(
        {.name = "worker"},
        [](idfxx::task::self&) {
            // Do some work
            process_data();
        }
    );

    // Wait up to 500ms for the task to finish
    worker.join(500ms);

} catch (const std::system_error& e) {
    idfxx::log::error("main", "Error: {}", e.what());
}
```

Result-based API:

```cpp
auto result = idfxx::task::make(
    {.name = "worker"},
    [](idfxx::task::self&) { process_data(); }
);

if (!result) { /* handle error */ }
auto& worker = *result;

// Wait up to 500ms
if (auto r = worker->try_join(500ms); !r) {
    if (r.error() == idfxx::errc::timeout) {
        // Task didn't finish in time — still joinable
        worker->try_detach();  // or try again later
    }
}
```

### Stopping Tasks

Signal a task to stop cooperatively using `request_stop()` and `stop_requested()`:

```cpp
#include <idfxx/sched>
#include <idfxx/task>

using namespace std::chrono_literals;

// Exception-based
idfxx::task worker(
    {.name = "worker"},
    [](idfxx::task::self& self) {
        while (!self.stop_requested()) {
            do_work();
            idfxx::delay(100ms);
        }
    }
);

// Request cooperative stop
worker.request_stop();
worker.join(1s);

// Or just let the destructor handle it — it calls request_stop() + join()
```

Result-based API:

```cpp
auto result = idfxx::task::make(
    {.name = "worker"},
    [](idfxx::task::self& self) {
        while (!self.stop_requested()) {
            do_work();
            idfxx::delay(100ms);
        }
    }
);

if (!result) { /* handle error */ }
auto& worker = *result;

worker->request_stop();
if (auto r = worker->try_join(1s); !r) {
    // handle timeout
}
```

### Core Affinity

Pin tasks to specific cores:

```cpp
#include <idfxx/task>
#include <idfxx/cpu>

auto task = idfxx::task::make(
    {.name = "pinned", .core_affinity = idfxx::core_id::core_0},
    my_task_function
);

// Use std::nullopt (default) to allow the task to run on any core
```

### Task Control from ISR

```cpp
#include <idfxx/task>
#include <idfxx/sched>

idfxx::task* worker_task;  // Set up elsewhere

void IRAM_ATTR my_isr() {
    bool need_yield = worker_task->resume_from_isr();
    idfxx::yield_from_isr(need_yield);
}
```

### Task Notifications

Task notifications provide a lightweight signaling mechanism using FreeRTOS
direct-to-task notifications (index 0). Unlike `suspend()`/`resume()`,
notifications latch — a notification sent before the task waits is not lost.

**Binary semaphore pattern** — simple "wake up and do work":

```cpp
#include <idfxx/sched>
#include <idfxx/task>

using namespace std::chrono_literals;

idfxx::task worker(
    {.name = "worker"},
    [](idfxx::task::self& self) {
        for (;;) {
            self.wait();  // blocks until notified
            if (self.stop_requested()) {
                break;
            }
            process_data();
        }
    }
);

// From another task or context:
worker.notify();  // wakes the worker
```

**Counting semaphore pattern** — accumulate notifications:

```cpp
idfxx::task worker(
    {.name = "counter"},
    [](idfxx::task::self& self) {
        for (;;) {
            uint32_t count = self.take();  // blocks until notified
            if (self.stop_requested()) {
                break;
            }
            for (uint32_t i = 0; i < count; ++i) {
                process_item();
            }
        }
    }
);

// Multiple notifications accumulate:
worker.notify();
worker.notify();
worker.notify();
// worker.take() returns 3 (or however many accumulated)
```

**Timed wait** — with timeout or deadline:

```cpp
// Wait up to 100ms for a notification
bool got_it = self.wait_for(100ms);

// Take with deadline
auto deadline = idfxx::chrono::tick_clock::now() + 500ms;
uint32_t count = self.take_until(deadline);
```

Result-based API:

```cpp
auto result = idfxx::task::make(
    {.name = "worker"},
    [](idfxx::task::self& self) {
        while (!self.stop_requested()) {
            self.wait();
            if (self.stop_requested()) break;
            process_data();
        }
    }
);

if (!result) { /* handle error */ }
auto& worker = *result;

if (auto r = worker->try_notify(); !r) {
    // handle error (task detached or completed)
}
```

**ISR-safe notification:**

```cpp
idfxx::task* worker_task;  // Set up elsewhere

void IRAM_ATTR my_isr() {
    bool need_yield = worker_task->notify_from_isr();
    idfxx::yield_from_isr(need_yield);
}
```

> **Note:** `idfxx::task` reserves notification index 0 for `wait()`/`take()`/`notify()`.
> If you need to use other FreeRTOS notification indices, access them directly via `idf_handle()`.

### Static Utility Methods

```cpp
#include <idfxx/chrono>
#include <idfxx/sched>
#include <idfxx/task>

// Delay current task
idfxx::delay(100ms);

// Precise periodic timing
auto next = idfxx::chrono::tick_clock::now() + 100ms;
while (true) {
    idfxx::delay_until(next);
    next += 100ms;
    // Runs every 100ms regardless of execution time
}

// Yield to other tasks
idfxx::yield();

// Get current task info
TaskHandle_t handle = idfxx::task::current_handle();
std::string name = idfxx::task::current_name();
```

## API Overview

### Factory Methods

- `task::make(config, callback)` - Create task with std::move_only_function callback
- `task::make(config, fn, arg)` - Create task with raw function pointer callback
- `task::spawn(config, callback)` / `task::try_spawn(config, callback)` - Create fire-and-forget task (std::move_only_function)
- `task::spawn(config, fn, arg)` / `task::try_spawn(config, fn, arg)` - Create fire-and-forget task (raw function pointer)

### Constructors (exception-based)

- `task(config, callback)` - Create task with std::move_only_function callback
- `task(config, fn, arg)` - Create task with raw function pointer callback

### Task Control

- `suspend()` / `try_suspend()` - Suspend the task
- `resume()` / `try_resume()` - Resume a suspended task
- `notify()` / `try_notify()` - Send a notification (increments notification count)
- `notify_from_isr()` - Send a notification from ISR context (returns yield hint)
- `set_priority(p)` / `try_set_priority(p)` - Change task priority
- `request_stop()` - Request cooperative stop (sets internal flag)
- `detach()` / `try_detach()` - Release ownership (task becomes self-managing)
- `kill()` / `try_kill()` - Immediately terminate the task (**dangerous**, see below)
- `join()` / `try_join()` - Block until the task completes
- `join(duration)` / `try_join(duration)` - Block with timeout
- `join_until(time_point)` / `try_join_until(time_point)` - Block until deadline
- `resume_from_isr()` - Resume from ISR context (returns yield hint)

### Task Self-Notification (within task function)

- `self.wait()` - Block until notified (binary semaphore pattern)
- `self.wait_for(duration)` - Block until notified or timeout
- `self.wait_until(time_point)` - Block until notified or deadline
- `self.take()` - Block until notified, returns accumulated count (counting semaphore pattern)
- `self.take_for(duration)` - Block until notified or timeout, returns count
- `self.take_until(time_point)` - Block until notified or deadline, returns count

### Query Methods

- `idf_handle()` - Get underlying TaskHandle_t (nullptr if detached)
- `name()` - Get task name
- `priority()` - Get current priority
- `stop_requested()` - Check if stop has been requested
- `is_completed()` - Check if task function returned (move_only_function only)
- `joinable()` - Check if task is owned

### Static Methods (current task operations)

- `task::current_handle()` - Get current task handle
- `task::current_name()` - Get current task name

## Error Handling

Task operations use error codes from `idfxx::errc`:

- `no_mem` - Memory allocation failed or insufficient stack
- `invalid_state` - Task has been detached, already joined, or operation invalid for current state
- `timeout` - Join did not complete within the specified duration

All `try_*` methods return `idfxx::result<T>`. Exception-based methods (without `try_` prefix) throw `std::system_error` when `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled.

## Important Notes

- **Auto-join destructor**: The destructor calls `request_stop()` then blocks until the task function completes, similar to `std::jthread`. Tasks that check `stop_requested()` in their loops will exit cleanly. For tasks that ignore the stop request and may not return promptly, call `join()` with a timeout or `detach()` before the task goes out of scope.
- **Cooperative stop**: `request_stop()` only sets a flag — the task must poll `self.stop_requested()` to observe it. It does not forcibly terminate the task.
- **Force kill**: `kill()` / `try_kill()` immediately terminates the task. This is dangerous: C++ destructors will not run, held mutexes will not be released, and allocated memory will not be freed. Only use as a last resort when a task cannot be stopped cooperatively.
- **Single-join semantics**: A task can only be joined once. After a successful join, the task becomes non-joinable. A timed-out join does not consume the join — the task remains joinable for a subsequent attempt.
- **Prefer timeouts**: Use `join(duration)` or `try_join(duration)` over the no-argument versions to avoid blocking indefinitely on tasks that may not return.
- **Completed tasks**: Operations on a completed task (suspend, resume, set_priority) return `invalid_state`. Use `is_completed()` to check.
- **Detached/spawned tasks**: Detached and spawned tasks clean up automatically when their function returns.
- **Non-copyable/movable**: Task is non-copyable and non-movable to ensure handle stability.
- **Notification index 0 reserved**: `wait()`/`take()`/`notify()` use FreeRTOS notification index 0. If you need additional notification indices, access them directly via `idf_handle()`.

## License

Apache License 2.0 - see [LICENSE](LICENSE) for details.
