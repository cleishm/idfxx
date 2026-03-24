// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#include <idfxx/lcd/ili9341>

#include <esp_idf_version.h>
#include <esp_lcd_ili9341.h>
#include <esp_lcd_panel_ops.h>
#include <esp_log.h>
#include <utility>

namespace {

const char* TAG = "idfxx::lcd::ili9341";

idfxx::result<esp_lcd_panel_handle_t>
make_handle(esp_lcd_panel_io_handle_t io_handle, const idfxx::lcd::panel::config& config) {
    esp_lcd_panel_dev_config_t panel_config{
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
        .rgb_ele_order = static_cast<lcd_rgb_element_order_t>(config.rgb_element_order),
        .data_endian = static_cast<lcd_rgb_data_endian_t>(config.data_endian),
        .bits_per_pixel = config.bits_per_pixel,
        .reset_gpio_num = config.reset_gpio.idf_num(),
        .vendor_config = config.vendor_config,
        .flags = {
            .reset_active_high = static_cast<unsigned int>(std::to_underlying(config.flags.reset_active_level)),
        },
#else
        .reset_gpio_num = config.reset_gpio.idf_num(),
        .rgb_ele_order = static_cast<lcd_rgb_element_order_t>(config.rgb_element_order),
        .data_endian = static_cast<lcd_rgb_data_endian_t>(config.data_endian),
        .bits_per_pixel = config.bits_per_pixel,
        .flags =
            {
                .reset_active_high = static_cast<unsigned int>(std::to_underlying(config.flags.reset_active_level)),
            },
        .vendor_config = config.vendor_config,
#endif
    };

    esp_lcd_panel_handle_t handle = nullptr;
    if (auto err = esp_lcd_new_panel_ili9341(io_handle, &panel_config, &handle); err != ESP_OK) {
        ESP_LOGD(TAG, "Failed to create ili9341 panel: %s", esp_err_to_name(err));
        return idfxx::error(err);
    }

    if (auto err = esp_lcd_panel_reset(handle); err != ESP_OK) {
        ESP_LOGD(TAG, "Failed to reset panel: %s", esp_err_to_name(err));
        esp_lcd_panel_del(handle);
        return idfxx::error(err);
    }

    if (auto err = esp_lcd_panel_init(handle); err != ESP_OK) {
        ESP_LOGD(TAG, "Failed to initialize panel: %s", esp_err_to_name(err));
        esp_lcd_panel_del(handle);
        return idfxx::error(err);
    }

    return handle;
}

} // namespace

namespace idfxx::lcd {

result<ili9341> ili9341::make(idfxx::lcd::panel_io& panel_io, panel::config config) {
    return make_handle(panel_io.idf_handle(), config).transform([](auto handle) { return ili9341{handle}; });
}

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
ili9341::ili9341(idfxx::lcd::panel_io& panel_io, panel::config config)
    : ili9341(unwrap(make(panel_io, std::move(config)))) {}
#endif

ili9341::ili9341(ili9341&& other) noexcept
    : _handle(std::exchange(other._handle, nullptr)) {}

ili9341& ili9341::operator=(ili9341&& other) noexcept {
    if (this != &other) {
        if (_handle != nullptr) {
            esp_lcd_panel_del(_handle);
        }

        _handle = std::exchange(other._handle, nullptr);
    }
    return *this;
}

ili9341::~ili9341() {
    if (_handle != nullptr) {
        esp_lcd_panel_del(_handle);
    }
}

esp_lcd_panel_handle_t ili9341::idf_handle() const {
    return _handle;
}

result<void> ili9341::try_swap_xy(bool swap) {
    if (_handle == nullptr) {
        return error(errc::invalid_state);
    }
    return wrap(esp_lcd_panel_swap_xy(_handle, swap));
}

result<void> ili9341::try_mirror(bool mirror_x, bool mirror_y) {
    if (_handle == nullptr) {
        return error(errc::invalid_state);
    }
    return wrap(esp_lcd_panel_mirror(_handle, mirror_x, mirror_y));
}

result<void> ili9341::try_display_on(bool on) {
    if (_handle == nullptr) {
        return error(errc::invalid_state);
    }
    return wrap(esp_lcd_panel_disp_on_off(_handle, on));
}

} // namespace idfxx::lcd
