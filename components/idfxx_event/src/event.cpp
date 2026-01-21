// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#include <idfxx/event>

#include <esp_log.h>
#include <map>
#include <mutex>

namespace {
const char* TAG = "idfxx::event";
}

namespace idfxx {
namespace {

using internal_callback = std::move_only_function<void(esp_event_base_t, int32_t, void*) const>;

// Context stored for each typed handler
struct handler_context {
    internal_callback callback;
    esp_event_handler_instance_t instance;
};

// Storage for typed handler contexts
// Key: handler instance pointer
// Value: pointer to handler context (for cleanup)
struct handler_storage {
    std::mutex mutex;
    std::map<esp_event_handler_instance_t, handler_context*> contexts;
};

handler_storage storage;

// Trampoline function that ESP-IDF calls
void listener_trampoline(void* handler_arg, esp_event_base_t base, int32_t id, void* event_data) {
    auto* ctx = static_cast<handler_context*>(handler_arg);
    if (ctx != nullptr) {
        ctx->callback(base, id, event_data);
    }
}

// Clean up handler context storage (call AFTER ESP-IDF unregistration)
void cleanup_handler_context(esp_event_handler_instance_t instance) {
    std::lock_guard lock(storage.mutex);
    auto it = storage.contexts.find(instance);
    if (it != storage.contexts.end()) {
        delete it->second;
        storage.contexts.erase(it);
    }
}

} // namespace

result<event_loop::listener_handle> event_loop::register_listener(
    esp_event_loop_handle_t loop,
    esp_event_base_t base,
    int32_t id,
    internal_callback callback
) {
    // Allocate stable context for the callback
    auto* ctx = new handler_context{std::move(callback), nullptr};
    esp_err_t err;

    if (loop == nullptr) {
        // Default event loop
        err = esp_event_handler_instance_register(base, id, listener_trampoline, ctx, &ctx->instance);
    } else {
        err = esp_event_handler_instance_register_with(loop, base, id, listener_trampoline, ctx, &ctx->instance);
    }

    if (err != ESP_OK) {
        delete ctx;
        return error(err);
    }

    // Store context for cleanup on unregister
    {
        std::lock_guard lock(storage.mutex);
        storage.contexts[ctx->instance] = ctx;
    }

    return listener_handle{loop, ctx->instance, base, id};
}

// Unregisters from ESP-IDF first, then cleans up storage.
// This order is critical to avoid use-after-free if an event fires during unregistration.
result<void> event_loop::unregister_listener(
    esp_event_loop_handle_t loop,
    esp_event_handler_instance_t instance,
    esp_event_base_t base,
    int32_t id
) {
    // Step 1: Unregister from ESP-IDF FIRST (stops trampoline from being called)
    esp_err_t err;
    if (loop == nullptr) {
        err = esp_event_handler_instance_unregister(base, id, instance);
    } else {
        err = esp_event_handler_instance_unregister_with(loop, base, id, instance);
    }

    // Step 2: Clean up storage (safe now - ESP-IDF won't call trampoline)
    cleanup_handler_context(instance);

    return wrap(err);
}

// =============================================================================
// event_loop implementation
// =============================================================================

result<void> event_loop::try_create_system() {
    return wrap(esp_event_loop_create_default());
}

result<void> event_loop::try_destroy_system() {
    return wrap(esp_event_loop_delete_default());
}

event_loop& event_loop::system() {
    static event_loop instance{nullptr};
    return instance;
}

result<void> event_loop::try_listener_remove(listener_handle handle) {
    if (handle._instance == nullptr) {
        return error(errc::invalid_arg);
    }

    return unregister_listener(handle._loop, handle._instance, handle._base, handle._id);
}

event_loop::~event_loop() {
    if (_handle != nullptr) {
        esp_err_t err = esp_event_loop_delete(_handle);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to delete event loop: %s", make_error_code(err).message().c_str());
        }
    }
}

// =============================================================================
// unique_listener_handle implementation
// =============================================================================

void event_loop::unique_listener_handle::reset() noexcept {
    if (_handle._instance != nullptr) {
        auto result = unregister_listener(_handle._loop, _handle._instance, _handle._base, _handle._id);
        if (!result) {
            ESP_LOGE(TAG, "Failed to unregister event handler: %s", result.error().message().c_str());
        }
        _handle = listener_handle{};
    }
}

// =============================================================================
// User event loop factory implementations
// =============================================================================

result<std::unique_ptr<user_event_loop>> event_loop::try_make_user(size_t queue_size) {
    esp_event_loop_args_t args{
        .queue_size = static_cast<int32_t>(queue_size),
        .task_name = nullptr,
        .task_priority = 0,
        .task_stack_size = 0,
        .task_core_id = 0,
    };

    esp_event_loop_handle_t handle = nullptr;
    esp_err_t err = esp_event_loop_create(&args, &handle);
    if (err != ESP_OK) {
        return error(err);
    }

    return std::unique_ptr<user_event_loop>(new user_event_loop(handle));
}

result<std::unique_ptr<event_loop>> event_loop::try_make_user(task_config task, size_t queue_size) {
    auto core_id_to_freertos = [](std::optional<core_id> affinity) -> BaseType_t {
        return affinity.has_value() ? static_cast<BaseType_t>(*affinity) : tskNO_AFFINITY;
    };

    esp_event_loop_args_t args{
        .queue_size = static_cast<int32_t>(queue_size),
        .task_name = task.name.data(),
        .task_priority = static_cast<UBaseType_t>(task.priority),
        .task_stack_size = task.stack_size,
        .task_core_id = core_id_to_freertos(task.core_affinity),
    };

    esp_event_loop_handle_t handle = nullptr;
    esp_err_t err = esp_event_loop_create(&args, &handle);
    if (err != ESP_OK) {
        return error(err);
    }

    return std::unique_ptr<event_loop>(new user_event_loop(handle));
}

} // namespace idfxx
