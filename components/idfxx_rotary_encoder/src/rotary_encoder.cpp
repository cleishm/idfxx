// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#include <idfxx/rotary_encoder>
#include <idfxx/timer>

#include <atomic>
#include <esp_log.h>
#include <optional>
#include <utility>

namespace idfxx {

static const char* TAG = "idfxx_rotary_encoder";

namespace {

// Gray code validation lookup table
constexpr uint8_t valid_states[] = {0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0};

struct encoder_context {
    rotary_encoder::config cfg;

    // Resources — destruction order matters (reverse of declaration)
    std::optional<timer> tmr;

    // Gray code decoder state
    uint8_t code = 0;
    uint16_t store = 0;

    // Acceleration state
    std::atomic<uint16_t> accel_coeff{0};
    timer::clock::time_point last_motion_time{};
};

void poll_encoder(encoder_context* ctx) {
    // Read current pin states and build 4-bit gray code
    ctx->code <<= 2;
    if (ctx->cfg.pin_a.get_level() == gpio::level::high) {
        ctx->code |= 0x02;
    }
    if (ctx->cfg.pin_b.get_level() == gpio::level::high) {
        ctx->code |= 0x01;
    }
    ctx->code &= 0x0f;

    // Validate state transition
    if (!valid_states[ctx->code]) {
        return;
    }

    // Build 16-bit pattern for direction detection
    ctx->store = (ctx->store << 4) | ctx->code;

    int32_t diff = 0;
    if (ctx->store == 0xe817 || ctx->store == 0x17e8) {
        diff = 1;
    } else if (ctx->store == 0xd42b || ctx->store == 0x2bd4) {
        diff = -1;
    } else {
        return;
    }

    // Apply acceleration if enabled
    uint16_t coeff = ctx->accel_coeff.load(std::memory_order_relaxed);
    if (coeff > 0) {
        auto now = timer::clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - ctx->last_motion_time);
        ctx->last_motion_time = now;

        if (elapsed < ctx->cfg.acceleration_threshold) {
            auto capped = std::max(elapsed, ctx->cfg.acceleration_cap);
            diff *= static_cast<int32_t>(ctx->cfg.acceleration_threshold / capped * coeff);
        }
    }

    ctx->cfg.callback(diff);
}

void timer_callback(void* arg) {
    auto* ctx = static_cast<encoder_context*>(arg);
    poll_encoder(ctx);
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

    // Set pin direction to input
    auto r = cfg.pin_a.try_set_direction(gpio::mode::input);
    if (!r) {
        auto msg = r.error().message();
        ESP_LOGD(TAG, "Failed to set pin_a direction: %s", msg.c_str());
        return error(r.error());
    }
    r = cfg.pin_b.try_set_direction(gpio::mode::input);
    if (!r) {
        auto msg = r.error().message();
        ESP_LOGD(TAG, "Failed to set pin_b direction: %s", msg.c_str());
        return error(r.error());
    }

    // Configure pull modes on encoder pins
    if (cfg.encoder_pins_pull_mode) {
        r = cfg.pin_a.try_set_pull_mode(*cfg.encoder_pins_pull_mode);
        if (!r) {
            auto msg = r.error().message();
            ESP_LOGD(TAG, "Failed to set pin_a pull mode: %s", msg.c_str());
            return error(r.error());
        }
        r = cfg.pin_b.try_set_pull_mode(*cfg.encoder_pins_pull_mode);
        if (!r) {
            auto msg = r.error().message();
            ESP_LOGD(TAG, "Failed to set pin_b pull mode: %s", msg.c_str());
            return error(r.error());
        }
    }

    auto* ctx = new encoder_context{};
    ctx->cfg = std::move(cfg);

    auto t = timer::make({.name = "rotary_encoder"}, &timer_callback, ctx);
    if (!t) {
        auto msg = t.error().message();
        ESP_LOGD(TAG, "Failed to create timer: %s", msg.c_str());
        delete ctx;
        return error(t.error());
    }
    ctx->tmr.emplace(std::move(*t));

    auto sr = ctx->tmr->try_start_periodic(ctx->cfg.polling_interval);
    if (!sr) {
        auto msg = sr.error().message();
        ESP_LOGD(TAG, "Failed to start timer: %s", msg.c_str());
        delete ctx;
        return error(sr.error());
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
        delete ctx;
    }
    _context = nullptr;
}

void rotary_encoder::enable_acceleration(uint16_t coeff) {
    if (_context == nullptr) {
        return;
    }
    auto* ctx = static_cast<encoder_context*>(_context);
    ctx->last_motion_time = timer::clock::now();
    ctx->accel_coeff.store(coeff, std::memory_order_relaxed);
}

void rotary_encoder::disable_acceleration() {
    if (_context == nullptr) {
        return;
    }
    auto* ctx = static_cast<encoder_context*>(_context);
    ctx->accel_coeff.store(0, std::memory_order_relaxed);
}

} // namespace idfxx
