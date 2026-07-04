// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#include <idfxx/lcd/detail/panel_factory.hpp>

#include <esp_lcd_panel_ops.h>
#include <esp_log.h>
#include <utility>

namespace {

const char* TAG = "idfxx::lcd";

} // namespace

namespace idfxx::lcd::detail {

result<esp_lcd_panel_handle_t>
make_panel_handle(panel_factory factory, esp_lcd_panel_io_handle_t io_handle, const panel::config& config) {
    // The field order of esp_lcd_panel_dev_config_t differs between IDF 5.5
    // and 6.0, so assign member-wise rather than using designated initializers.
    esp_lcd_panel_dev_config_t panel_config{};
    panel_config.reset_gpio_num = config.reset_gpio.idf_num();
    panel_config.rgb_ele_order = static_cast<lcd_rgb_element_order_t>(config.rgb_element_order);
    panel_config.data_endian = static_cast<lcd_rgb_data_endian_t>(config.data_endian);
    panel_config.bits_per_pixel = config.bits_per_pixel;
    panel_config.flags.reset_active_high = static_cast<unsigned int>(std::to_underlying(config.reset_active_level));
    panel_config.vendor_config = config.vendor_config;

    esp_lcd_panel_handle_t handle = nullptr;
    if (auto err = factory(io_handle, &panel_config, &handle); err != ESP_OK) {
        ESP_LOGD(TAG, "Failed to create panel: %s", esp_err_to_name(err));
        return error(err);
    }

    if (auto err = esp_lcd_panel_reset(handle); err != ESP_OK) {
        ESP_LOGD(TAG, "Failed to reset panel: %s", esp_err_to_name(err));
        esp_lcd_panel_del(handle);
        return error(err);
    }

    if (auto err = esp_lcd_panel_init(handle); err != ESP_OK) {
        ESP_LOGD(TAG, "Failed to initialize panel: %s", esp_err_to_name(err));
        esp_lcd_panel_del(handle);
        return error(err);
    }

    return handle;
}

} // namespace idfxx::lcd::detail
