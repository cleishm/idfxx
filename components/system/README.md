# System Services

Tasks, timers, queues, event loops and event groups, sleep, an interactive console, cryptographic
primitives, and a log that survives resets. See the [component catalog](../README.md) for every
area.

| Component | Description | Documentation |
|-----------|-------------|---------------|
| [idfxx_console](idfxx_console/README.md) | Interactive console REPL and command management | [API Docs](https://cleishm.github.io/idfxx/group__idfxx__console.html) |
| [idfxx_crypto](idfxx_crypto/README.md) | Cryptographic primitives: message digests (SHA-2, SHA-3, SHA-1, MD5) and MACs (HMAC, AES-CMAC), one-shot or incremental | [API Docs](https://cleishm.github.io/idfxx/group__idfxx__crypto.html) |
| [idfxx_event](idfxx_event/README.md) | Type-safe event loop for asynchronous events | [API Docs](https://cleishm.github.io/idfxx/group__idfxx__event.html) |
| [idfxx_event_group](idfxx_event_group/README.md) | Type-safe FreeRTOS event group for inter-task synchronization | [API Docs](https://cleishm.github.io/idfxx/group__idfxx__event__group.html) |
| [idfxx_persistent_log](idfxx_persistent_log/README.md) | Log in flash that keeps ESP-IDF's warnings and errors, the app's lines and its faults across resets and crashes | [API Docs](https://cleishm.github.io/idfxx/group__idfxx__persistent__log.html) |
| [idfxx_queue](idfxx_queue/README.md) | Type-safe FreeRTOS queue for inter-task communication | [API Docs](https://cleishm.github.io/idfxx/group__idfxx__queue.html) |
| [idfxx_sleep](idfxx_sleep/README.md) | Light and deep sleep with type-safe wake-up source configuration | [API Docs](https://cleishm.github.io/idfxx/group__idfxx__sleep.html) |
| [idfxx_task](idfxx_task/README.md) | FreeRTOS task management with join, cooperative stop, and fire-and-forget support | [API Docs](https://cleishm.github.io/idfxx/group__idfxx__task.html) |
| [idfxx_timer](idfxx_timer/README.md) | High-resolution timer (esp_timer) | [API Docs](https://cleishm.github.io/idfxx/group__idfxx__timer.html) |
