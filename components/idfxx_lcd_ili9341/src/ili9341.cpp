// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#include <idfxx/lcd/ili9341>

#include <esp_lcd_ili9341.h>
#include <esp_lcd_panel_ops.h>
#include <esp_log.h>
#include <utility>

namespace {
const char* TAG = "idfxx::lcd::ili9341";
}

namespace idfxx::lcd {

result<esp_lcd_panel_handle_t> ili9341::make_handle(esp_lcd_panel_io_handle_t io_handle, const panel::config& config) {
    esp_lcd_panel_dev_config_t panel_config{
        .reset_gpio_num = config.reset_gpio.idf_num(),
        .rgb_ele_order = static_cast<lcd_rgb_element_order_t>(config.rgb_element_order),
        .data_endian = static_cast<lcd_rgb_data_endian_t>(config.data_endian),
        .bits_per_pixel = config.bits_per_pixel,
        .flags =
            {
                .reset_active_high = config.flags.reset_active_high,
            },
        .vendor_config = config.vendor_config,
    };

    esp_lcd_panel_handle_t handle = nullptr;
    if (auto err = esp_lcd_new_panel_ili9341(io_handle, &panel_config, &handle); err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create ili9341 panel: %s", esp_err_to_name(err));
        return error(err);
    }

    // Initialize panel
    if (auto err = esp_lcd_panel_reset(handle); err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to reset panel: %s", esp_err_to_name(err));
        esp_lcd_panel_del(handle);
        return error(err);
    }

    if (auto err = esp_lcd_panel_init(handle); err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize panel: %s", esp_err_to_name(err));
        esp_lcd_panel_del(handle);
        return error(err);
    }

    return handle;
}

result<std::unique_ptr<ili9341>> ili9341::make(std::shared_ptr<idfxx::lcd::panel_io> panel_io, panel::config config) {
    if (panel_io == nullptr) {
        ESP_LOGE(TAG, "Cannot create ili9341 panel: panel_io is null");
        return error(errc::invalid_arg);
    }

    auto self = std::unique_ptr<ili9341>(new ili9341());
    self->_panel_io = std::move(panel_io);

    return self->make_handle(self->_panel_io->idf_handle(), config).transform([&](auto handle) {
        self->_handle = handle;
        return std::move(self);
    });
}

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
ili9341::ili9341(std::shared_ptr<idfxx::lcd::panel_io> panel_io, panel::config config) {
    if (panel_io == nullptr) {
        throw std::system_error(errc::invalid_arg, "panel_io is null");
    }

    _panel_io = std::move(panel_io);
    _handle = unwrap(make_handle(_panel_io->idf_handle(), config));
}
#endif

ili9341::~ili9341() {
    if (_handle != nullptr) {
        esp_lcd_panel_del(_handle);
    }
}

esp_lcd_panel_handle_t ili9341::idf_handle() const {
    return _handle;
}

result<void> ili9341::try_swap_xy(bool swap) {
    if (auto err = esp_lcd_panel_swap_xy(_handle, swap); err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set swap_xy: %s", esp_err_to_name(err));
        return error(err);
    }
    return {};
}

result<void> ili9341::try_mirror(bool mirror_x, bool mirror_y) {
    if (auto err = esp_lcd_panel_mirror(_handle, mirror_x, mirror_y); err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set mirror: %s", esp_err_to_name(err));
        return error(err);
    }
    return {};
}

result<void> ili9341::try_display_on(bool on) {
    if (auto err = esp_lcd_panel_disp_on_off(_handle, on); err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to turn %s display: %s", on ? "on" : "off", esp_err_to_name(err));
        return error(err);
    }
    return {};
}

} // namespace idfxx::lcd
