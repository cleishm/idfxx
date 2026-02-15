// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#include <idfxx/task>

#include <atomic>
#include <esp_attr.h>
#include <freertos/semphr.h>
#include <new>

#ifdef __INTELLISENSE__
#define IDFXX_NOTHROW_NEW new
#else
#define IDFXX_NOTHROW_NEW new (std::nothrow)
#endif

namespace idfxx {

struct task::context {
    std::move_only_function<void(task::self&)> func = nullptr;
    void (*func_ptr)(task::self&, void*) = nullptr;
    void* func_ptr_arg = nullptr;

    SemaphoreHandle_t join_sem = nullptr;
    std::atomic<bool> stop_flag{false};

    enum class state_t : int { running = 0, detached = 1, completed = 2, destroying = 3 };
    std::atomic<state_t> state{state_t::running};

    ~context() {
        if (join_sem != nullptr) {
            vSemaphoreDelete(join_sem);
        }
    }
};

void task::trampoline(void* arg) {
    auto* ctx = static_cast<context*>(arg);

    if (ctx->func_ptr) {
        self s{ctx};
        ctx->func_ptr(s, ctx->func_ptr_arg);
    } else if (ctx->func) {
        self s{ctx};
        ctx->func(s);
    }

    auto expected = context::state_t::running;
    if (ctx->state.compare_exchange_strong(
            expected, context::state_t::completed, std::memory_order_acq_rel, std::memory_order_acquire
        )) {
        // Owned task completed. Signal joiner, then suspend.
        // The owner (destructor/try_join/try_detach/try_kill) will vTaskDelete us.
        if (ctx->join_sem != nullptr) {
            xSemaphoreGive(ctx->join_sem);
        }
        for (;;) {
            vTaskSuspend(nullptr);
        }
    }

    if (expected == context::state_t::detached) {
        // Detached: self-clean.
        delete ctx;
        vTaskDeleteWithCaps(nullptr);
    }

    // Destroying: destructor will force-kill us. Suspend until killed.
    for (;;) {
        vTaskSuspend(nullptr);
    }
}

result<TaskHandle_t> task::_create(context* ctx, const config& cfg) {
    TaskHandle_t handle = nullptr;
    BaseType_t core =
        cfg.core_affinity ? static_cast<BaseType_t>(std::to_underlying(*cfg.core_affinity)) : tskNO_AFFINITY;

    BaseType_t ret = xTaskCreatePinnedToCoreWithCaps(
        trampoline,
        cfg.name.data(),
        cfg.stack_size,
        ctx,
        static_cast<UBaseType_t>(cfg.priority),
        &handle,
        core,
        std::to_underlying(cfg.stack_mem)
    );

    if (ret != pdPASS) {
        delete ctx;
        return error(errc::no_mem);
    }

    return handle;
}

result<std::unique_ptr<task>> task::make(config cfg, std::move_only_function<void(self&)> task_func) {
    auto* ctx = IDFXX_NOTHROW_NEW context{};
    if (!ctx) {
        return error(errc::no_mem);
    }
    ctx->func = std::move(task_func);
    ctx->join_sem = xSemaphoreCreateBinary();
    if (ctx->join_sem == nullptr) {
        delete ctx;
        return error(errc::no_mem);
    }
    return _create(ctx, cfg).transform([&](auto handle) {
        return std::unique_ptr<task>(new task(handle, std::string{cfg.name}, ctx));
    });
}

result<std::unique_ptr<task>> task::make(config cfg, void (*task_func)(self&, void*), void* arg) {
    auto* ctx = IDFXX_NOTHROW_NEW context{};
    if (!ctx) {
        return error(errc::no_mem);
    }
    ctx->func_ptr = task_func;
    ctx->func_ptr_arg = arg;
    ctx->join_sem = xSemaphoreCreateBinary();
    if (ctx->join_sem == nullptr) {
        delete ctx;
        return error(errc::no_mem);
    }
    return _create(ctx, cfg).transform([&](auto handle) {
        return std::unique_ptr<task>(new task(handle, std::string{cfg.name}, ctx));
    });
}

result<void> task::try_spawn(config cfg, std::move_only_function<void(self&)> task_func) {
    auto* ctx = IDFXX_NOTHROW_NEW context{};
    if (!ctx) {
        return error(errc::no_mem);
    }
    ctx->func = std::move(task_func);
    ctx->state.store(context::state_t::detached, std::memory_order_relaxed);
    return _create(ctx, cfg).transform([](auto) {});
}

result<void> task::try_spawn(config cfg, void (*task_func)(self&, void*), void* arg) {
    auto* ctx = IDFXX_NOTHROW_NEW context{};
    if (!ctx) {
        return error(errc::no_mem);
    }
    ctx->func_ptr = task_func;
    ctx->func_ptr_arg = arg;
    ctx->state.store(context::state_t::detached, std::memory_order_relaxed);
    return _create(ctx, cfg).transform([](auto) {});
}

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
task::task(const config& cfg, std::move_only_function<void(self&)> task_func)
    : _handle(nullptr)
    , _name(cfg.name)
    , _context(nullptr) {
    auto* ctx = new context{};
    ctx->func = std::move(task_func);
    ctx->join_sem = xSemaphoreCreateBinary();
    if (ctx->join_sem == nullptr) {
        delete ctx;
        throw std::system_error(errc::no_mem);
    }
    _handle = unwrap(_create(ctx, cfg));
    _context = ctx;
}

task::task(const config& cfg, void (*task_func)(self&, void*), void* arg)
    : _handle(nullptr)
    , _name(cfg.name)
    , _context(nullptr) {
    auto* ctx = new context{};
    ctx->func_ptr = task_func;
    ctx->func_ptr_arg = arg;
    ctx->join_sem = xSemaphoreCreateBinary();
    if (ctx->join_sem == nullptr) {
        delete ctx;
        throw std::system_error(errc::no_mem);
    }
    _handle = unwrap(_create(ctx, cfg));
    _context = ctx;
}
#endif

task::task(TaskHandle_t handle, std::string name, context* ctx)
    : _handle(handle)
    , _name(std::move(name))
    , _context(ctx) {}

task::~task() {
    if (_handle != nullptr) {
        _context->stop_flag.store(true, std::memory_order_release);
        // Resume the task repeatedly until it completes. Looping is necessary because vTaskResume
        // is a no-op if the task isn't suspended, so a single call can miss the window between the
        // task checking stop_flag and calling vTaskSuspend. Uses 1 tick (not pdMS_TO_TICKS(1))
        // to guarantee a non-zero wait between retries regardless of configTICK_RATE_HZ.
        do {
            vTaskResume(_handle);
            xTaskNotifyGive(_handle);
        } while (xSemaphoreTake(_context->join_sem, 1) != pdTRUE);
        vTaskDeleteWithCaps(_handle);
    }
    delete _context;
}

unsigned int task::priority() const noexcept {
    if (_handle == nullptr || _context->state.load(std::memory_order_acquire) != context::state_t::running) {
        return 0;
    }
    return uxTaskPriorityGet(_handle);
}

size_t task::stack_high_water_mark() const noexcept {
    if (_handle == nullptr || _context->state.load(std::memory_order_acquire) != context::state_t::running) {
        return 0;
    }
    return static_cast<size_t>(uxTaskGetStackHighWaterMark(_handle)) * sizeof(StackType_t);
}

bool task::is_completed() const noexcept {
    return _handle != nullptr && _context->state.load(std::memory_order_acquire) == context::state_t::completed;
}

bool task::request_stop() noexcept {
    if (_handle == nullptr) {
        return false;
    }
    return !_context->stop_flag.exchange(true, std::memory_order_acq_rel);
}

bool task::stop_requested() const noexcept {
    if (_handle == nullptr) {
        return false;
    }
    return _context->stop_flag.load(std::memory_order_acquire);
}

result<void> task::try_suspend() {
    if (_handle == nullptr || _context->state.load(std::memory_order_acquire) != context::state_t::running) {
        return error(errc::invalid_state);
    }
    vTaskSuspend(_handle);
    return {};
}

result<void> task::try_resume() {
    if (_handle == nullptr || _context->state.load(std::memory_order_acquire) != context::state_t::running) {
        return error(errc::invalid_state);
    }
    vTaskResume(_handle);
    return {};
}

result<void> task::try_set_priority(unsigned int new_priority) {
    if (_handle == nullptr || _context->state.load(std::memory_order_acquire) != context::state_t::running) {
        return error(errc::invalid_state);
    }
    vTaskPrioritySet(_handle, static_cast<UBaseType_t>(new_priority));
    return {};
}

result<void> task::try_detach() {
    if (_handle == nullptr) {
        return error(errc::invalid_state);
    }
    auto expected = context::state_t::running;
    if (!_context->state.compare_exchange_strong(
            expected, context::state_t::detached, std::memory_order_acq_rel, std::memory_order_acquire
        )) {
        // completed: task is suspended. Delete it and clean up context.
        vTaskDeleteWithCaps(_handle);
        delete _context;
    }
    _handle = nullptr;
    _context = nullptr;
    return {};
}

result<void> task::try_kill() {
    if (_handle == nullptr) {
        return error(errc::invalid_state);
    }
    if (xTaskGetCurrentTaskHandle() == _handle) {
        return error(errc::invalid_state);
    }

    auto expected = context::state_t::running;
    if (_context->state.compare_exchange_strong(
            expected, context::state_t::destroying, std::memory_order_acq_rel, std::memory_order_acquire
        )) {
        // Task hasn't completed yet. Force delete it.
        vTaskDeleteWithCaps(_handle);
    } else {
        // Task completed and is suspended. Take semaphore and delete.
        xSemaphoreTake(_context->join_sem, portMAX_DELAY);
        vTaskDeleteWithCaps(_handle);
    }

    _handle = nullptr;
    delete _context;
    _context = nullptr;
    return {};
}

result<void> task::try_join() {
    return _try_join(portMAX_DELAY);
}

result<void> task::_try_join(TickType_t ticks) {
    if (_handle == nullptr) {
        return error(errc::invalid_state);
    }
    if (xTaskGetCurrentTaskHandle() == _handle) {
        return error(std::errc::resource_deadlock_would_occur);
    }
    if (_context->state.load(std::memory_order_acquire) == context::state_t::completed) {
        vTaskDeleteWithCaps(_handle);
        _handle = nullptr;
        return {};
    }
    if (xSemaphoreTake(_context->join_sem, ticks) != pdTRUE) {
        return error(errc::timeout);
    }
    vTaskDeleteWithCaps(_handle);
    _handle = nullptr;
    return {};
}

bool IRAM_ATTR task::resume_from_isr() noexcept {
    if (_handle == nullptr || _context->state.load(std::memory_order_acquire) != context::state_t::running) {
        return false;
    }
    return xTaskResumeFromISR(_handle) == pdTRUE;
}

result<void> task::try_notify() {
    if (_handle == nullptr || _context->state.load(std::memory_order_acquire) != context::state_t::running) {
        return error(errc::invalid_state);
    }
    xTaskNotifyGive(_handle);
    return {};
}

bool IRAM_ATTR task::notify_from_isr() noexcept {
    if (_handle == nullptr || _context->state.load(std::memory_order_acquire) != context::state_t::running) {
        return false;
    }
    BaseType_t woken = pdFALSE;
    vTaskNotifyGiveFromISR(_handle, &woken);
    return woken == pdTRUE;
}

// =============================================================================
// task::self implementation
// =============================================================================

void task::self::suspend() noexcept {
    // Skip suspension if a stop has been requested
    if (_context->stop_flag.load(std::memory_order_acquire)) {
        return;
    }
    vTaskSuspend(nullptr);
}

unsigned int task::self::priority() const noexcept {
    return uxTaskPriorityGet(nullptr);
}

size_t task::self::stack_high_water_mark() const noexcept {
    return static_cast<size_t>(uxTaskGetStackHighWaterMark(nullptr)) * sizeof(StackType_t);
}

void task::self::set_priority(unsigned int new_priority) noexcept {
    vTaskPrioritySet(nullptr, static_cast<UBaseType_t>(new_priority));
}

std::string task::self::name() const {
    return pcTaskGetName(nullptr);
}

TaskHandle_t task::self::idf_handle() const noexcept {
    return xTaskGetCurrentTaskHandle();
}

bool task::self::is_detached() const noexcept {
    return _context->state.load(std::memory_order_acquire) == context::state_t::detached;
}

bool task::self::stop_requested() const noexcept {
    return _context->stop_flag.load(std::memory_order_acquire);
}

uint32_t task::self::_take(TickType_t ticks) noexcept {
    if (_context->stop_flag.load(std::memory_order_acquire)) {
        return 0;
    }
    return ulTaskNotifyTake(pdTRUE, ticks);
}

void task::self::wait() noexcept {
    _take(portMAX_DELAY);
}

uint32_t task::self::take() noexcept {
    return _take(portMAX_DELAY);
}

} // namespace idfxx
