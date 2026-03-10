// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#include <idfxx/rotary_encoder>

#include <encoder.h>
#include <esp_log.h>
#include <utility>

namespace idfxx {

static const char* TAG = "idfxx_rotary_encoder";

// Verify enum values match C library constants
static_assert(std::to_underlying(rotary_encoder::event_type::changed) == RE_ET_CHANGED);
static_assert(std::to_underlying(rotary_encoder::event_type::btn_released) == RE_ET_BTN_RELEASED);
static_assert(std::to_underlying(rotary_encoder::event_type::btn_pressed) == RE_ET_BTN_PRESSED);
static_assert(std::to_underlying(rotary_encoder::event_type::btn_long_pressed) == RE_ET_BTN_LONG_PRESSED);
static_assert(std::to_underlying(rotary_encoder::event_type::btn_clicked) == RE_ET_BTN_CLICKED);

namespace {

struct encoder_context {
    rotary_encoder_handle_t handle = nullptr;
    std::move_only_function<void(const rotary_encoder::event&)> callback;
};

void trampoline(const rotary_encoder_event_t* c_event, void* arg) {
    auto* ctx = static_cast<encoder_context*>(arg);
    rotary_encoder::event ev{
        .type = static_cast<rotary_encoder::event_type>(c_event->type),
        .diff = c_event->diff,
    };
    ctx->callback(ev);
}

} // namespace

result<rotary_encoder> rotary_encoder::make(config cfg) {
    if (!cfg.pin_a.is_connected()) {
        ESP_LOGD(TAG, "Field 'pin_a' has an invalid value");
        return error(errc::invalid_arg);
    }
    if (!cfg.pin_b.is_connected()) {
        ESP_LOGD(TAG, "Field 'pin_b' has an invalid value");
        return error(errc::invalid_arg);
    }
    if (!cfg.callback) {
        ESP_LOGD(TAG, "Field 'callback' has an invalid value");
        return error(errc::invalid_arg);
    }

    // Configure pull modes on encoder pins before creating the encoder
    if (cfg.encoder_pins_pull_mode) {
        auto r = cfg.pin_a.try_set_pull_mode(*cfg.encoder_pins_pull_mode);
        if (!r) {
            return error(r.error());
        }
        r = cfg.pin_b.try_set_pull_mode(*cfg.encoder_pins_pull_mode);
        if (!r) {
            return error(r.error());
        }
    }
    if (cfg.pin_btn.is_connected() && cfg.btn_pin_pull_mode) {
        auto r = cfg.pin_btn.try_set_pull_mode(*cfg.btn_pin_pull_mode);
        if (!r) {
            return error(r.error());
        }
    }

    auto* ctx = new encoder_context{};
    ctx->callback = std::move(cfg.callback);

    rotary_encoder_config_t c_cfg{
        .pin_a = cfg.pin_a.idf_num(),
        .pin_b = cfg.pin_b.idf_num(),
        .pin_btn = cfg.pin_btn.idf_num(),
        .btn_pressed_level = static_cast<uint8_t>(std::to_underlying(cfg.btn_active_level)),
        .enable_internal_pullup = false,
        .btn_dead_time_us = static_cast<uint32_t>(cfg.btn_dead_time.count()),
        .btn_long_press_time_us = static_cast<uint32_t>(cfg.btn_long_press_time.count()),
        .acceleration_threshold_ms = static_cast<uint32_t>(cfg.acceleration_threshold.count()),
        .acceleration_cap_ms = static_cast<uint32_t>(cfg.acceleration_cap.count()),
        .polling_interval_us = static_cast<uint32_t>(cfg.polling_interval.count()),
        .callback = &trampoline,
        .callback_ctx = ctx,
    };

    auto err = rotary_encoder_create(&c_cfg, &ctx->handle);
    if (err != ESP_OK) {
        delete ctx;
        return error(err);
    }

    return rotary_encoder(ctx);
}

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
rotary_encoder::rotary_encoder(config cfg)
    : rotary_encoder(unwrap(make(std::move(cfg)))) {}
#endif

rotary_encoder::rotary_encoder(void* ctx)
    : _context(ctx) {}

rotary_encoder::rotary_encoder(rotary_encoder&& other) noexcept
    : _context(std::exchange(other._context, nullptr)) {}

rotary_encoder& rotary_encoder::operator=(rotary_encoder&& other) noexcept {
    if (this != &other) {
        _delete();
        _context = std::exchange(other._context, nullptr);
    }
    return *this;
}

rotary_encoder::~rotary_encoder() {
    _delete();
}

void rotary_encoder::_delete() noexcept {
    auto* ctx = static_cast<encoder_context*>(_context);
    if (ctx) {
        rotary_encoder_delete(ctx->handle);
        delete ctx;
    }
    _context = nullptr;
}

void rotary_encoder::enable_acceleration(uint16_t coeff) {
    if (_context == nullptr) {
        return;
    }
    auto* ctx = static_cast<encoder_context*>(_context);
    rotary_encoder_enable_acceleration(ctx->handle, coeff);
}

void rotary_encoder::disable_acceleration() {
    if (_context == nullptr) {
        return;
    }
    auto* ctx = static_cast<encoder_context*>(_context);
    rotary_encoder_disable_acceleration(ctx->handle);
}

} // namespace idfxx
