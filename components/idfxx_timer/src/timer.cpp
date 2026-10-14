// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#include <idfxx/timer>

#include <algorithm>
#include <esp_attr.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <utility>

#if CONFIG_ESP_TIMER_IN_IRAM
#define TIMER_ISR_ATTR IRAM_ATTR
#else
#define TIMER_ISR_ATTR
#endif

namespace {
const char* TAG = "idfxx::timer";
}

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
        // An exception is logged and the timer keeps running
        (void)invoke_guarded(TAG, "Timer callback", [&] { ctx->callback(); });
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
        _handle = nullptr;
    }
    delete _context;
}

namespace {

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 1, 0)
// How many times an absolute-time call clamped to now is retried when the clamped time slips
// into the past before the call checks it.
constexpr int max_clamp_attempts = 3;

// Microseconds since boot of a time point, as taken by the esp_timer absolute-time API.
uint64_t to_alarm_us(timer::clock::time_point time) {
    return static_cast<uint64_t>(time.time_since_epoch().count());
}
#else
// Microseconds remaining until a time point, or zero if it has passed.
uint64_t remaining_us(timer::clock::time_point time) {
    return static_cast<uint64_t>(std::max(int64_t{0}, (time - timer::clock::now()).count()));
}
#endif

} // namespace

result<void> timer::try_start_once(clock::time_point time) {
    if (_handle == nullptr) {
        return error(errc::invalid_state);
    }
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 1, 0)
    // esp_timer_start_once_at() rejects a time in the past with ESP_ERR_INVALID_ARG. Fire at
    // once instead, both when the time has already passed and when it slips by before the call
    // checks it.
    if (time > clock::now()) {
        auto err = esp_timer_start_once_at(_handle, to_alarm_us(time));
        if (err != ESP_ERR_INVALID_ARG) {
            return wrap(err);
        }
    }
    return wrap(esp_timer_start_once(_handle, 0));
#else
    return wrap(esp_timer_start_once(_handle, remaining_us(time)));
#endif
}

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 1, 0)
result<void> timer::try_restart(clock::time_point next) {
    if (_handle == nullptr) {
        return error(errc::invalid_state);
    }
    // A one-shot timer reports a period of zero, which esp_timer_restart_at() ignores for it.
    uint64_t period_us = 0;
    esp_timer_get_period(_handle, &period_us);
    return _try_restart_at(next, period_us);
}

result<void> timer::_try_restart_at(clock::time_point next, uint64_t period_us) {
    if (_handle == nullptr) {
        return error(errc::invalid_state);
    }
    // A relative restart would also replace the period of a periodic timer, so clamp a time in
    // the past to now instead; see _try_start_periodic_at() for why ESP_ERR_INVALID_ARG means
    // the clamped time slipped further into the past and is retried. The past check comes before
    // the not-running check, so ESP_ERR_INVALID_STATE means the timer must be started instead.
    esp_err_t err = ESP_ERR_INVALID_ARG;
    for (int attempt = 0; attempt < max_clamp_attempts && err == ESP_ERR_INVALID_ARG; ++attempt) {
        auto at = std::max(next, clock::now());
        err = esp_timer_restart_at(_handle, period_us, to_alarm_us(at));
    }
    if (err == ESP_ERR_INVALID_STATE) {
        return try_start_once(next);
    }
    return wrap(err);
}

result<void> timer::_try_start_periodic_at(clock::time_point first, uint64_t interval_us) {
    if (_handle == nullptr) {
        return error(errc::invalid_state);
    }
    // There is no relative call that fires a periodic timer at once, so clamp a first time in
    // the past to now. esp_timer_start_periodic_at() accepts a time no more than the minimum
    // timer period in the past, and with the handle checked above ESP_ERR_INVALID_ARG can only
    // mean the time slipped further than that before the call checked it; take a fresh now and
    // try again, a bounded number of times.
    esp_err_t err = ESP_ERR_INVALID_ARG;
    for (int attempt = 0; attempt < max_clamp_attempts && err == ESP_ERR_INVALID_ARG; ++attempt) {
        auto at = std::max(first, clock::now());
        err = esp_timer_start_periodic_at(_handle, interval_us, to_alarm_us(at));
    }
    return wrap(err);
}
#endif

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
