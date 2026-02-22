// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#include <idfxx/lcd/stmpe610>

#include <esp_lcd_touch.h>
#include <esp_lcd_touch_stmpe610.h>
#include <esp_log.h>

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
            state->process_coordinates(x, y, strength, point_num, max_point_num);
        }
    }
}

result<esp_lcd_touch_handle_t> stmpe610::make_handle(esp_lcd_panel_io_handle_t io_handle, const config& config) {
    esp_lcd_touch_config_t touch_config{
        .x_max = config.x_max,
        .y_max = config.y_max,
        .rst_gpio_num = config.rst_gpio.idf_num(),
        .int_gpio_num = config.int_gpio.idf_num(),
        .levels =
            {
                .reset = config.levels.reset,
                .interrupt = config.levels.interrupt,
            },
        .flags =
            {
                .swap_xy = config.flags.swap_xy,
                .mirror_x = config.flags.mirror_x,
                .mirror_y = config.flags.mirror_y,
            },
        .process_coordinates = process_coordinates,
        .interrupt_callback = nullptr,
        .user_data = _callbacks.get(),
        .driver_data = nullptr,
    };

    esp_lcd_touch_handle_t handle = nullptr;
    auto err = esp_lcd_touch_new_spi_stmpe610(io_handle, &touch_config, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create stmpe610 touch controller: %s", esp_err_to_name(err));
        return error(err);
    }
    return handle;
}

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
stmpe610::stmpe610(idfxx::lcd::panel_io& panel_io, struct config config) {
    if (config.process_coordinates) {
        _callbacks = std::make_unique<callback_state>(std::move(config.process_coordinates));
    }
    _handle = unwrap(make_handle(panel_io.idf_handle(), config));
}
#endif

result<stmpe610> stmpe610::make(idfxx::lcd::panel_io& panel_io, struct config config) {
    stmpe610 self;
    if (config.process_coordinates) {
        self._callbacks = std::make_unique<callback_state>(std::move(config.process_coordinates));
    }

    auto handle_result = self.make_handle(panel_io.idf_handle(), config);
    if (!handle_result.has_value()) {
        return error(handle_result.error());
    }
    self._handle = *handle_result;
    return self;
}

stmpe610::stmpe610(stmpe610&& other) noexcept
    : _handle(other._handle)
    , _callbacks(std::move(other._callbacks)) {
    other._handle = nullptr;
}

stmpe610& stmpe610::operator=(stmpe610&& other) noexcept {
    if (this != &other) {
        if (_handle != nullptr) {
            esp_lcd_touch_del(_handle);
        }
        _handle = other._handle;
        _callbacks = std::move(other._callbacks);
        other._handle = nullptr;
    }
    return *this;
}

stmpe610::~stmpe610() {
    if (_handle != nullptr) {
        esp_lcd_touch_del(_handle);
    }
}

esp_lcd_touch_handle_t stmpe610::idf_handle() const {
    return _handle;
}

} // namespace idfxx::lcd
