// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#include <idfxx/button>
#include <idfxx/timer>

#include <esp_attr.h>
#include <esp_log.h>
#include <optional>
#include <utility>

namespace idfxx {

static const char* TAG = "idfxx_button";

namespace {

enum class button_state { released, pressed, pressed_long };

struct button_context {
    // Config
    button::config cfg;

    // Resources — destruction order matters (reverse of declaration)
    gpio::unique_isr_handle isr_handle;
    std::optional<timer> tmr;

    // State machine
    button_state state = button_state::released;
    std::chrono::microseconds pressed_time{0};
    std::chrono::microseconds repeating_time{0};
    bool monitoring = false;
};

void poll_button(button_context* ctx) {
    if (ctx->state == button_state::pressed && ctx->pressed_time < ctx->cfg.dead_time) {
        ctx->pressed_time += ctx->cfg.poll_interval;
        return;
    }

    if (ctx->cfg.pin.get_level() == ctx->cfg.pressed_level) {
        if (ctx->state == button_state::released) {
            ctx->state = button_state::pressed;
            ctx->pressed_time = std::chrono::microseconds{0};
            ctx->repeating_time = std::chrono::microseconds{0};
            ctx->cfg.callback(button::event_type::pressed);
            return;
        }

        ctx->pressed_time += ctx->cfg.poll_interval;

        if (ctx->cfg.autorepeat) {
            if (ctx->pressed_time < ctx->cfg.autorepeat_timeout) {
                return;
            }
            ctx->repeating_time += ctx->cfg.poll_interval;
            if (ctx->repeating_time >= ctx->cfg.autorepeat_interval) {
                ctx->repeating_time = std::chrono::microseconds{0};
                ctx->cfg.callback(button::event_type::clicked);
            }
            return;
        }

        if (ctx->state == button_state::pressed && ctx->pressed_time >= ctx->cfg.long_press_time) {
            ctx->state = button_state::pressed_long;
            ctx->cfg.callback(button::event_type::long_press);
        }
    } else if (ctx->state != button_state::released) {
        bool clicked = ctx->state == button_state::pressed &&
            !(ctx->cfg.autorepeat && ctx->pressed_time >= ctx->cfg.autorepeat_timeout);
        ctx->state = button_state::released;
        ctx->cfg.callback(button::event_type::released);
        if (clicked) {
            ctx->cfg.callback(button::event_type::clicked);
        }
    }
}

void timer_callback(void* arg) {
    auto* ctx = static_cast<button_context*>(arg);

    poll_button(ctx);

    if (ctx->cfg.mode == button::mode::interrupt) {
        if (ctx->state == button_state::released) {
            if (ctx->monitoring) {
                auto r = ctx->tmr->try_stop();
                if (!r) {
                    auto msg = r.error().message();
                    ESP_LOGE(TAG, "Failed to stop timer: %s", msg.c_str());
                }
                ctx->monitoring = false;
            }
            ctx->cfg.pin.intr_enable();
        } else if (!ctx->monitoring) {
            auto r = ctx->tmr->try_start_periodic(ctx->cfg.poll_interval);
            if (!r) {
                auto msg = r.error().message();
                ESP_LOGE(TAG, "Failed to start timer: %s", msg.c_str());
            }
            ctx->monitoring = true;
        }
    }
}

void IRAM_ATTR button_isr_handler(void* arg) {
    auto* ctx = static_cast<button_context*>(arg);
    ctx->cfg.pin.intr_disable();
    ctx->tmr->try_start_once_isr(0);
}

} // namespace

result<button> button::make(config cfg) {
    if (!cfg.pin.is_connected()) {
        ESP_LOGD(TAG, "Field 'pin' has an invalid value");
        return error(errc::invalid_arg);
    }
    if (!cfg.callback) {
        ESP_LOGD(TAG, "Field 'callback' has an invalid value");
        return error(errc::invalid_arg);
    }

    auto r = cfg.pin.try_set_direction(gpio::mode::input);
    if (!r) {
        auto msg = r.error().message();
        ESP_LOGD(TAG, "Failed to set pin direction: %s", msg.c_str());
        return error(r.error());
    }

    if (cfg.enable_pull) {
        auto pull = cfg.pressed_level == gpio::level::low ? gpio::pull_mode::pullup : gpio::pull_mode::pulldown;
        r = cfg.pin.try_set_pull_mode(pull);
        if (!r) {
            auto msg = r.error().message();
            ESP_LOGD(TAG, "Failed to set pin pull mode: %s", msg.c_str());
            return error(r.error());
        }
    }

    auto* ctx = new button_context{};
    ctx->cfg = std::move(cfg);

    auto t = timer::make({.name = "button"}, &timer_callback, ctx);
    if (!t) {
        auto msg = t.error().message();
        ESP_LOGD(TAG, "Failed to create timer: %s", msg.c_str());
        delete ctx;
        return error(t.error());
    }
    ctx->tmr.emplace(std::move(*t));

    if (ctx->cfg.mode == mode::poll) {
        auto sr = ctx->tmr->try_start_periodic(ctx->cfg.poll_interval);
        if (!sr) {
            auto msg = sr.error().message();
            ESP_LOGD(TAG, "Failed to start timer: %s", msg.c_str());
            delete ctx;
            return error(sr.error());
        }
    } else {
        ctx->cfg.pin.set_intr_type(gpio::intr_type::anyedge);

        auto ih = ctx->cfg.pin.try_isr_handler_add(&button_isr_handler, ctx);
        if (!ih) {
            auto msg = ih.error().message();
            ESP_LOGD(TAG, "Failed to add ISR handler: %s", msg.c_str());
            delete ctx;
            return error(ih.error());
        }
        ctx->isr_handle = gpio::unique_isr_handle(std::move(*ih));

        ctx->cfg.pin.intr_enable();
    }

    return button(ctx);
}

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
button::button(config cfg)
    : button(unwrap(make(std::move(cfg)))) {}
#endif

button::button(void* ctx)
    : _context(ctx) {}

button::button(button&& other) noexcept
    : _context(std::exchange(other._context, nullptr)) {}

button& button::operator=(button&& other) noexcept {
    if (this != &other) {
        _delete();
        _context = std::exchange(other._context, nullptr);
    }
    return *this;
}

button::~button() {
    _delete();
}

void button::_delete() noexcept {
    auto* ctx = static_cast<button_context*>(_context);
    if (ctx) {
        if (ctx->cfg.mode == mode::interrupt) {
            ctx->cfg.pin.intr_disable();
        }
        delete ctx;
    }
    _context = nullptr;
}

} // namespace idfxx
