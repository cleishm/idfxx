// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#include <idfxx/gpio>
#include <idfxx/memory>

#include <atomic>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>

namespace {
const char* TAG = "idfxx::gpio";
}

namespace idfxx {
namespace {

void trampoline(void* arg);

struct raw_entry {
    uint32_t id;
    void (*fn)(void*);
    void* arg;
    bool active;
};

struct functional_entry {
    uint32_t id;
    std::move_only_function<void() const> fn;
    bool active;
};

struct gpio_handlers {
    std::vector<raw_entry, dram_allocator<raw_entry>> raw;
    std::vector<functional_entry> functional;
    std::atomic<bool> activated{false};

    inline gpio_num_t num() const;
    result<void> activate();
    void compact();
};

constexpr size_t GPIO_MAX = GPIO_NUM_MAX;
gpio_handlers handlers[GPIO_MAX];

gpio_num_t gpio_handlers::num() const {
    return static_cast<gpio_num_t>(this - handlers);
}

result<void> gpio_handlers::activate() {
    if (activated.load(std::memory_order_relaxed)) {
        return {};
    }

    auto _num = num();
    auto err = gpio_isr_handler_add(_num, &trampoline, reinterpret_cast<void*>(static_cast<uintptr_t>(_num)));
    return wrap(err).transform([this]() { activated.store(true, std::memory_order_relaxed); });
}

void gpio_handlers::compact() {
    std::erase_if(raw, [](const raw_entry& e) { return !e.active; });
    std::erase_if(functional, [](const functional_entry& e) { return !e.active; });

    auto _num = num();
    if (raw.empty() && functional.empty()) {
        auto err = gpio_isr_handler_remove(_num);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to remove ISR handler for GPIO %d: %s", static_cast<int>(_num), esp_err_to_name(err));
        }
        activated.store(false, std::memory_order_relaxed);
    }
}

uint32_t next_id = 1;
bool iram_isr = false;
portMUX_TYPE handlers_mux = portMUX_INITIALIZER_UNLOCKED;
portMUX_TYPE active_mux = portMUX_INITIALIZER_UNLOCKED;

void IRAM_ATTR trampoline(void* arg) {
    auto pin = static_cast<gpio_num_t>(reinterpret_cast<uintptr_t>(arg));
    auto& handler = handlers[pin];

    portENTER_CRITICAL_ISR(&handlers_mux);

    for (auto& entry : handler.raw) {
        portENTER_CRITICAL_ISR(&active_mux);
        bool is_active = entry.active;
        portEXIT_CRITICAL_ISR(&active_mux);
        if (is_active) {
            entry.fn(entry.arg);
        }
    }

    if (!iram_isr) {
        for (auto& entry : handler.functional) {
            portENTER_CRITICAL_ISR(&active_mux);
            bool is_active = entry.active;
            portEXIT_CRITICAL_ISR(&active_mux);
            if (is_active) {
                entry.fn();
            }
        }
    }

    portEXIT_CRITICAL_ISR(&handlers_mux);
}

} // anonymous namespace

result<gpio> gpio::make(int num) {
    if (num == GPIO_NUM_NC) {
        return gpio{GPIO_NUM_NC};
    }
    // Add explicit range check before GPIO_IS_VALID_GPIO
    if (num < 0 || num >= GPIO_NUM_MAX || !GPIO_IS_VALID_GPIO(num)) {
        return error(errc::invalid_arg);
    }
    return gpio{static_cast<gpio_num_t>(num)};
}

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
gpio::gpio(int num) {
    if (num != GPIO_NUM_NC && (num < 0 || num >= GPIO_NUM_MAX || !GPIO_IS_VALID_GPIO(num))) {
        throw std::system_error(errc::invalid_arg);
    }
    _num = static_cast<gpio_num_t>(num);
}
#endif

result<void> gpio::try_install_isr_service(flags<intr_flag> intr_flags) {
    return wrap(gpio_install_isr_service(intr_flags.value())).transform([&]() {
        iram_isr = intr_flags.contains(intr_flag::iram);
    });
}

void gpio::uninstall_isr_service() {
    for (auto& handler : handlers) {
        if (handler.activated.load(std::memory_order_relaxed)) {
            gpio_isr_handler_remove(handler.num());
        }
        handler.raw.clear();
        handler.functional.clear();
        handler.activated.store(false, std::memory_order_relaxed);
    }
    gpio_uninstall_isr_service();
    iram_isr = false;
}

result<gpio::isr_handle> gpio::try_isr_handler_add(std::move_only_function<void() const> handler) {
    if (!is_connected()) {
        return error(errc::invalid_state);
    }
    if (iram_isr) {
        return error(errc::not_supported);
    }
    auto& gpio_handler = handlers[_num];
    return gpio_handler.activate().transform([&, handler = std::move(handler)]() mutable {
        portENTER_CRITICAL(&handlers_mux);
        portENTER_CRITICAL(&active_mux);

        uint32_t id = next_id++;
        gpio_handler.functional.push_back(functional_entry{
            .id = id,
            .fn = std::move(handler),
            .active = true,
        });

        portEXIT_CRITICAL(&active_mux);
        portEXIT_CRITICAL(&handlers_mux);
        return isr_handle{_num, id};
    });
}

result<gpio::isr_handle> gpio::try_isr_handler_add(void (*fn)(void*), void* arg) {
    if (!is_connected()) {
        return error(errc::invalid_state);
    }
    auto& gpio_handler = handlers[_num];
    return gpio_handler.activate().transform([&, fn, arg]() {
        portENTER_CRITICAL(&handlers_mux);
        portENTER_CRITICAL(&active_mux);

        uint32_t id = next_id++;
        gpio_handler.raw.push_back(raw_entry{
            .id = id,
            .fn = fn,
            .arg = arg,
            .active = true,
        });

        portEXIT_CRITICAL(&active_mux);
        portEXIT_CRITICAL(&handlers_mux);
        return isr_handle{_num, id};
    });
}

result<void> gpio::try_isr_handler_remove(isr_handle handle) {
    if (!is_connected()) {
        return error(errc::invalid_state);
    }
    if (handle._num != _num) {
        return error(errc::invalid_arg);
    }

    auto& gpio_handler = handlers[_num];

    if (xPortInIsrContext()) {
        // In ISR: handlers_mux may already be held, so only flag for removal
        portENTER_CRITICAL_ISR(&active_mux);
        for (auto& entry : gpio_handler.raw) {
            if (entry.id == handle._id) {
                entry.active = false;
                break;
            }
        }
        for (auto& entry : gpio_handler.functional) {
            if (entry.id == handle._id) {
                entry.active = false;
                break;
            }
        }
        portEXIT_CRITICAL_ISR(&active_mux);
    } else {
        // Non-ISR: acquire both locks, and remove directly
        portENTER_CRITICAL(&handlers_mux);
        portENTER_CRITICAL(&active_mux);

        std::erase_if(gpio_handler.raw, [&](auto& e) { return e.id == handle._id; });
        std::erase_if(gpio_handler.functional, [&](auto& e) { return e.id == handle._id; });
        gpio_handler.compact();

        portEXIT_CRITICAL(&active_mux);
        portEXIT_CRITICAL(&handlers_mux);
    }

    return {};
}

result<void> gpio::try_isr_handler_remove_all() {
    if (!is_connected()) {
        return error(errc::invalid_state);
    }
    auto& gpio_handler = handlers[_num];

    if (xPortInIsrContext()) {
        // In ISR: handlers_mux may already be held, so only flag for removal
        portENTER_CRITICAL_ISR(&active_mux);
        for (auto& entry : gpio_handler.raw) {
            entry.active = false;
        }
        for (auto& entry : gpio_handler.functional) {
            entry.active = false;
        }
        portEXIT_CRITICAL_ISR(&active_mux);
    } else {
        // Non-ISR: acquire both locks, and remove directly
        portENTER_CRITICAL(&handlers_mux);
        portENTER_CRITICAL(&active_mux);

        gpio_handler.raw.clear();
        gpio_handler.functional.clear();
        gpio_handler.compact();

        portEXIT_CRITICAL(&active_mux);
        portEXIT_CRITICAL(&handlers_mux);
    }

    return {};
}

result<void> try_configure_gpios(const gpio::config& cfg, std::vector<gpio> pins) {
    gpio_config_t gpio_config{
        .pin_bit_mask = 0,
        .mode = static_cast<gpio_mode_t>(cfg.mode),
        .pull_up_en = (cfg.pull_mode == gpio::pull_mode::pullup || cfg.pull_mode == gpio::pull_mode::pullup_pulldown)
            ? GPIO_PULLUP_ENABLE
            : GPIO_PULLUP_DISABLE,
        .pull_down_en =
            (cfg.pull_mode == gpio::pull_mode::pulldown || cfg.pull_mode == gpio::pull_mode::pullup_pulldown)
            ? GPIO_PULLDOWN_ENABLE
            : GPIO_PULLDOWN_DISABLE,
        .intr_type = static_cast<gpio_int_type_t>(cfg.intr_type),
#if SOC_GPIO_SUPPORT_PIN_HYS_FILTER
        .hys_ctrl_mode = static_cast<gpio_hys_ctrl_mode_t>(cfg.hys_ctrl_mode),
#endif
    };

    for (const auto& pin : pins) {
        if (!pin.is_connected()) {
            return error(errc::invalid_arg);
        }
        gpio_config.pin_bit_mask |= (1ULL << static_cast<uint32_t>(pin.idf_num()));
    }
    return wrap(::gpio_config(&gpio_config));
}

} // namespace idfxx
