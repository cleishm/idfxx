// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#include <idfxx/gpio>
#include <idfxx/memory>

#include <atomic>
#include <driver/gpio.h>
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
    bool compact();
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

    // Pre-allocate before the ISR is registered so that the first
    // handler additions don't allocate inside the critical section.
    raw.reserve(2);
    functional.reserve(2);

    auto _num = num();
    auto err = gpio_isr_handler_add(_num, &trampoline, reinterpret_cast<void*>(static_cast<uintptr_t>(_num)));
    return wrap(err).transform([this]() { activated.store(true, std::memory_order_relaxed); });
}

bool gpio_handlers::compact() {
    std::erase_if(raw, [](const raw_entry& e) { return !e.active; });
    std::erase_if(functional, [](const functional_entry& e) { return !e.active; });
    if (raw.empty() && functional.empty()) {
        activated.store(false, std::memory_order_relaxed);
        return true;
    }
    return false;
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
    if (!_is_valid_gpio_num(num)) {
        return error(errc::invalid_arg);
    }
    return gpio{static_cast<gpio_num_t>(num)};
}

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
gpio::gpio(int num)
    : gpio(unwrap(make(num))) {}
#endif

void gpio::reset() {
    if (is_connected()) {
        gpio_reset_pin(_num);
    }
}

result<void> gpio::try_set_direction(enum mode mode) {
    if (!is_connected()) {
        return error(errc::invalid_state);
    }
    return wrap(gpio_set_direction(_num, static_cast<gpio_mode_t>(mode)));
}

void gpio::input_enable() {
    if (is_connected()) {
        gpio_input_enable(_num);
    }
}

result<void> gpio::try_set_pull_mode(enum pull_mode mode) {
    if (!is_connected()) {
        return error(errc::invalid_state);
    }
    return wrap(gpio_set_pull_mode(_num, static_cast<gpio_pull_mode_t>(mode)));
}

result<void> gpio::try_pullup_enable() {
    if (!is_connected()) {
        return error(errc::invalid_state);
    }
    return wrap(gpio_pullup_en(_num));
}

result<void> gpio::try_pullup_disable() {
    if (!is_connected()) {
        return error(errc::invalid_state);
    }
    return wrap(gpio_pullup_dis(_num));
}

result<void> gpio::try_pulldown_enable() {
    if (!is_connected()) {
        return error(errc::invalid_state);
    }
    return wrap(gpio_pulldown_en(_num));
}

result<void> gpio::try_pulldown_disable() {
    if (!is_connected()) {
        return error(errc::invalid_state);
    }
    return wrap(gpio_pulldown_dis(_num));
}

void gpio::set_level(enum level level) {
    gpio_set_level(_num, std::to_underlying(level));
}

gpio::level gpio::get_level() const {
    return static_cast<enum level>(gpio_get_level(_num));
}

void gpio::toggle_level() {
    set_level(get_level() == level::high ? level::low : level::high);
}

result<void> gpio::try_set_drive_capability(enum drive_cap strength) {
    if (!is_connected()) {
        return error(errc::invalid_state);
    }
    return wrap(gpio_set_drive_capability(_num, static_cast<gpio_drive_cap_t>(strength)));
}

result<gpio::drive_cap> gpio::try_get_drive_capability() const {
    if (!is_connected()) {
        return error(errc::invalid_state);
    }
    gpio_drive_cap_t cap;
    auto err = gpio_get_drive_capability(_num, &cap);
    return wrap(err).transform([cap]() { return static_cast<drive_cap>(cap); });
}

void gpio::set_intr_type(enum intr_type intr_type) {
    if (is_connected()) {
        gpio_set_intr_type(_num, static_cast<gpio_int_type_t>(intr_type));
    }
}

void gpio::intr_enable() {
    if (is_connected()) {
        gpio_intr_enable(_num);
    }
}

void gpio::intr_disable() {
    if (is_connected()) {
        gpio_intr_disable(_num);
    }
}

result<void> gpio::try_wakeup_enable(enum intr_type intr_type) {
    if (!is_connected()) {
        return error(errc::invalid_state);
    }
    return wrap(gpio_wakeup_enable(_num, static_cast<gpio_int_type_t>(intr_type)));
}

result<void> gpio::try_wakeup_disable() {
    if (!is_connected()) {
        return error(errc::invalid_state);
    }
    return wrap(gpio_wakeup_disable(_num));
}

result<void> gpio::try_hold_enable() {
    if (!is_connected()) {
        return error(errc::invalid_state);
    }
    return wrap(gpio_hold_en(_num));
}

result<void> gpio::try_hold_disable() {
    if (!is_connected()) {
        return error(errc::invalid_state);
    }
    return wrap(gpio_hold_dis(_num));
}

void gpio::deep_sleep_hold_enable() {
    gpio_deep_sleep_hold_en();
}

void gpio::deep_sleep_hold_disable() {
    gpio_deep_sleep_hold_dis();
}

void gpio::sleep_sel_enable() {
    if (is_connected()) {
        gpio_sleep_sel_en(_num);
    }
}

void gpio::sleep_sel_disable() {
    if (is_connected()) {
        gpio_sleep_sel_dis(_num);
    }
}

result<void> gpio::try_sleep_set_direction(enum mode mode) {
    if (!is_connected()) {
        return error(errc::invalid_state);
    }
    return wrap(gpio_sleep_set_direction(_num, static_cast<gpio_mode_t>(mode)));
}

void gpio::sleep_set_pull_mode(enum pull_mode pull) {
    if (is_connected()) {
        gpio_sleep_set_pull_mode(_num, static_cast<gpio_pull_mode_t>(pull));
    }
}

result<void> gpio::try_install_isr_service(intr_levels levels, flags<intr_flag> intr_flags) {
    int raw = to_underlying(levels) | to_underlying(intr_flags);
    return wrap(gpio_install_isr_service(raw)).transform([&]() { iram_isr = intr_flags.contains(intr_flag::iram); });
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
        gpio_handler.functional.reserve(gpio_handler.functional.size() + 1);

        portENTER_CRITICAL(&active_mux);
        uint32_t id = next_id++;
        gpio_handler.functional.push_back(
            functional_entry{
                .id = id,
                .fn = std::move(handler),
                .active = true,
            }
        );
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
        gpio_handler.raw.reserve(gpio_handler.raw.size() + 1);

        portENTER_CRITICAL(&active_mux);
        uint32_t id = next_id++;
        gpio_handler.raw.push_back(
            raw_entry{
                .id = id,
                .fn = fn,
                .arg = arg,
                .active = true,
            }
        );
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
        bool needs_remove = false;
        portENTER_CRITICAL(&handlers_mux);
        portENTER_CRITICAL(&active_mux);

        std::erase_if(gpio_handler.raw, [&](auto& e) { return e.id == handle._id; });
        std::erase_if(gpio_handler.functional, [&](auto& e) { return e.id == handle._id; });
        needs_remove = gpio_handler.compact();

        portEXIT_CRITICAL(&active_mux);
        portEXIT_CRITICAL(&handlers_mux);

        if (needs_remove) {
            auto err = gpio_isr_handler_remove(_num);
            if (err != ESP_OK) {
                ESP_LOGE(
                    TAG, "Failed to remove ISR handler for GPIO %d: %s", static_cast<int>(_num), esp_err_to_name(err)
                );
            }
        }
    }

    return {};
}

void gpio::isr_handler_remove_all() {
    if (!is_connected()) {
        return;
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
        bool needs_remove = false;
        portENTER_CRITICAL(&handlers_mux);
        portENTER_CRITICAL(&active_mux);

        gpio_handler.raw.clear();
        gpio_handler.functional.clear();
        needs_remove = gpio_handler.compact();

        portEXIT_CRITICAL(&active_mux);
        portEXIT_CRITICAL(&handlers_mux);

        if (needs_remove) {
            auto err = gpio_isr_handler_remove(_num);
            if (err != ESP_OK) {
                ESP_LOGE(
                    TAG, "Failed to remove ISR handler for GPIO %d: %s", static_cast<int>(_num), esp_err_to_name(err)
                );
            }
        }
    }
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
