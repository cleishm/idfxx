// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#include <idfxx/lcd/stmpe610>

#include <esp_lcd_touch.h>
#include <esp_lcd_touch_stmpe610.h>
#include <esp_log.h>
#include <utility>

namespace {
const char* TAG = "idfxx::lcd::stmpe610";
}

namespace idfxx::lcd {

void stmpe610::process_coordinates(
    esp_lcd_touch_handle_t tp,
    uint16_t* x,
    uint16_t* y,
    uint16_t* strength,
    uint8_t* point_num,
    uint8_t max_point_num
) {
    void* user_data = tp->config.user_data;
    if (user_data != nullptr) {
        auto* state = static_cast<callback_state*>(user_data);
        if (state->process_coordinates != nullptr) {
            auto res = invoke_guarded(TAG, "Coordinate processing callback", [&] {
                state->process_coordinates(x, y, strength, point_num, max_point_num);
            });
            if (!res) {
                // An exception (already logged) drops this sample: report no touch
                *point_num = 0;
            }
        }
    }
}

stmpe610::stmpe610(esp_lcd_touch_handle_t handle, std::unique_ptr<callback_state> callbacks)
    : _handle(handle)
    , _callbacks(std::move(callbacks)) {}

result<stmpe610> stmpe610::make(idfxx::panel_io& panel_io, config cfg) {
    if (cfg.x_max == 0) {
        ESP_LOGD(TAG, "Field 'x_max' has an invalid value");
        return error(errc::invalid_arg);
    }
    if (cfg.y_max == 0) {
        ESP_LOGD(TAG, "Field 'y_max' has an invalid value");
        return error(errc::invalid_arg);
    }

    std::unique_ptr<callback_state> cbs;
    if (cfg.process_coordinates) {
        cbs = std::make_unique<callback_state>(std::move(cfg.process_coordinates));
    }

    esp_lcd_touch_config_t touch_config{
        .x_max = cfg.x_max,
        .y_max = cfg.y_max,
        .rst_gpio_num = cfg.rst_gpio.idf_num(),
        .int_gpio_num = cfg.int_gpio.idf_num(),
        .levels =
            {
                .reset = static_cast<unsigned int>(std::to_underlying(cfg.reset_level)),
                .interrupt = static_cast<unsigned int>(std::to_underlying(cfg.interrupt_level)),
            },
        .flags =
            {
                .swap_xy = cfg.swap_xy,
                .mirror_x = cfg.mirror_x,
                .mirror_y = cfg.mirror_y,
            },
        .process_coordinates = cbs ? process_coordinates : nullptr,
        .interrupt_callback = nullptr,
        .user_data = cbs.get(),
        .driver_data = nullptr,
    };

    esp_lcd_touch_handle_t handle = nullptr;
    auto err = esp_lcd_touch_new_spi_stmpe610(panel_io.idf_handle(), &touch_config, &handle);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "Failed to create stmpe610 touch controller: %s", esp_err_to_name(err));
        return error(err);
    }
    return stmpe610{handle, std::move(cbs)};
}

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
stmpe610::stmpe610(idfxx::panel_io& panel_io, config cfg)
    : stmpe610(unwrap(make(panel_io, std::move(cfg)))) {}
#endif

stmpe610::stmpe610(stmpe610&& other) noexcept
    : _handle(std::exchange(other._handle, nullptr))
    , _callbacks(std::move(other._callbacks)) {}

stmpe610& stmpe610::operator=(stmpe610&& other) noexcept {
    if (this != &other) {
        if (_handle != nullptr) {
            esp_lcd_touch_del(_handle);
        }
        _handle = std::exchange(other._handle, nullptr);
        _callbacks = std::move(other._callbacks);
    }
    return *this;
}

stmpe610::~stmpe610() {
    if (_handle != nullptr) {
        esp_lcd_touch_del(_handle);
    }
}

esp_lcd_touch_handle_t stmpe610::do_idf_handle() const {
    return _handle;
}

} // namespace idfxx::lcd
