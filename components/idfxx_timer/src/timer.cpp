// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#include <idfxx/timer>

#include <esp_attr.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <utility>

#if CONFIG_ESP_TIMER_IN_IRAM
#define TIMER_ISR_ATTR IRAM_ATTR
#else
#define TIMER_ISR_ATTR
#endif

namespace idfxx {

struct timer::context {
    std::move_only_function<void()> callback;
    SemaphoreHandle_t mutex = nullptr;

    ~context() {
        if (mutex != nullptr) {
            vSemaphoreDelete(mutex);
        }
    }
};

void timer::trampoline(void* arg) {
    auto* ctx = static_cast<context*>(arg);
    xSemaphoreTake(ctx->mutex, portMAX_DELAY);
    if (ctx->callback) {
        ctx->callback();
    }
    xSemaphoreGive(ctx->mutex);
}

result<timer> timer::make(config cfg, std::move_only_function<void()> callback) {
#ifdef CONFIG_ESP_TIMER_SUPPORTS_ISR_DISPATCH_METHOD
    if (cfg.dispatch == dispatch_method::isr) {
        return error(ESP_ERR_INVALID_ARG);
    }
#endif

    auto* ctx = new context{};
    ctx->callback = std::move(callback);
    ctx->mutex = xSemaphoreCreateMutex();
    if (!ctx->mutex) {
        delete ctx;
        raise_no_mem();
    }

    timer t(nullptr, std::string{cfg.name}, ctx);

    esp_timer_create_args_t args{
        .callback = &trampoline,
        .arg = ctx,
        .dispatch_method = static_cast<esp_timer_dispatch_t>(cfg.dispatch),
        .name = t._name.empty() ? nullptr : t._name.c_str(),
        .skip_unhandled_events = cfg.skip_unhandled_events,
    };

    esp_timer_handle_t handle;
    auto err = esp_timer_create(&args, &handle);
    if (err != ESP_OK) {
        return error(err);
    }

    t._handle = handle;
    return t;
}

result<timer> timer::make(config cfg, void (*callback)(void*), void* arg) {
    std::string name{cfg.name};

    esp_timer_create_args_t args{
        .callback = callback,
        .arg = arg,
        .dispatch_method = static_cast<esp_timer_dispatch_t>(cfg.dispatch),
        .name = name.empty() ? nullptr : name.c_str(),
        .skip_unhandled_events = cfg.skip_unhandled_events,
    };

    esp_timer_handle_t handle;
    auto err = esp_timer_create(&args, &handle);
    if (err != ESP_OK) {
        return error(err);
    }

    return timer(handle, std::move(name), nullptr);
}

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
timer::timer(const config& cfg, std::move_only_function<void()> callback)
    : timer(unwrap(make(config{cfg}, std::move(callback)))) {}
#endif

timer::timer(esp_timer_handle_t handle, std::string name, context* ctx)
    : _handle(handle)
    , _name(std::move(name))
    , _context(ctx) {}

timer::timer(timer&& other) noexcept
    : _handle(std::exchange(other._handle, nullptr))
    , _name(std::move(other._name))
    , _context(std::exchange(other._context, nullptr)) {}

timer& timer::operator=(timer&& other) noexcept {
    if (this != &other) {
        _stop_and_delete();

        _handle = std::exchange(other._handle, nullptr);
        _name = std::move(other._name);
        _context = std::exchange(other._context, nullptr);
    }
    return *this;
}

timer::~timer() {
    _stop_and_delete();
}

void timer::_stop_and_delete() noexcept {
    if (_handle) {
        esp_timer_stop(_handle);
        if (_context) {
            xSemaphoreTake(_context->mutex, portMAX_DELAY);
            _context->callback = nullptr;
            xSemaphoreGive(_context->mutex);
        }
        esp_timer_delete(_handle);
    }
    delete _context;
}

esp_err_t TIMER_ISR_ATTR timer::try_start_once_isr(uint64_t timeout_us) {
    if (_handle == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    return esp_timer_start_once(_handle, timeout_us);
}

esp_err_t TIMER_ISR_ATTR timer::try_start_periodic_isr(uint64_t interval_us) {
    if (_handle == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    return esp_timer_start_periodic(_handle, interval_us);
}

esp_err_t TIMER_ISR_ATTR timer::try_restart_isr(uint64_t timeout_us) {
    if (_handle == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    auto err = esp_timer_restart(_handle, timeout_us);
    if (err == ESP_ERR_INVALID_STATE) {
        return esp_timer_start_once(_handle, timeout_us);
    }
    return err;
}

esp_err_t TIMER_ISR_ATTR timer::try_stop_isr() {
    if (_handle == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    return esp_timer_stop(_handle);
}

} // namespace idfxx
