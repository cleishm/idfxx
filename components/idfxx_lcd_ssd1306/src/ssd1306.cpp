// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#include <idfxx/lcd/ssd1306>

#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_ssd1306.h>
#include <esp_log.h>
#include <utility>

namespace {

const char* TAG = "idfxx::lcd::ssd1306";

idfxx::result<esp_lcd_panel_handle_t>
make_handle(esp_lcd_panel_io_handle_t io_handle, const idfxx::lcd::ssd1306::config& config) {
    if (config.height != 64 && config.height != 32) {
        ESP_LOGD(TAG, "Field 'height' has an invalid value");
        return idfxx::error(idfxx::errc::invalid_arg);
    }

    // The driver copies this during panel creation, so a stack instance is fine.
    esp_lcd_panel_ssd1306_config_t ssd1306_config{
        .height = static_cast<uint8_t>(config.height),
    };

    // The field order of esp_lcd_panel_dev_config_t differs between IDF 5.5 and
    // 6.0, so assign member-wise rather than using designated initializers.
    esp_lcd_panel_dev_config_t panel_config{};
    panel_config.reset_gpio_num = config.reset_gpio.idf_num();
    panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
    panel_config.data_endian = LCD_RGB_DATA_ENDIAN_BIG;
    panel_config.bits_per_pixel = 1;
    panel_config.flags.reset_active_high = static_cast<unsigned int>(std::to_underlying(config.reset_active_level));
    panel_config.vendor_config = &ssd1306_config;

    esp_lcd_panel_handle_t handle = nullptr;
    if (auto err = esp_lcd_new_panel_ssd1306(io_handle, &panel_config, &handle); err != ESP_OK) {
        ESP_LOGD(TAG, "Failed to create ssd1306 panel: %s", esp_err_to_name(err));
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

result<ssd1306> ssd1306::make(idfxx::lcd::panel_io& panel_io, ssd1306::config config) {
    return make_handle(panel_io.idf_handle(), config).transform([&config](auto handle) {
        return ssd1306{handle, config.height};
    });
}

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
ssd1306::ssd1306(idfxx::lcd::panel_io& panel_io, ssd1306::config config)
    : ssd1306(unwrap(make(panel_io, std::move(config)))) {}
#endif

ssd1306::ssd1306(ssd1306&& other) noexcept
    : _handle(std::exchange(other._handle, nullptr))
    , _height(std::exchange(other._height, 0)) {}

ssd1306& ssd1306::operator=(ssd1306&& other) noexcept {
    if (this != &other) {
        if (_handle != nullptr) {
            esp_lcd_panel_del(_handle);
        }

        _handle = std::exchange(other._handle, nullptr);
        _height = std::exchange(other._height, 0);
    }
    return *this;
}

ssd1306::~ssd1306() {
    if (_handle != nullptr) {
        esp_lcd_panel_del(_handle);
    }
}

esp_lcd_panel_handle_t ssd1306::do_idf_handle() const {
    return _handle;
}

panel_io::i2c_config ssd1306::i2c_io_config(uint16_t device_address, freq::hertz scl_speed) noexcept {
    return {
        .device_address = device_address,
        .scl_speed = scl_speed,
        .control_phase_bytes = 1,
        .dc_bit_offset = 6,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
}

} // namespace idfxx::lcd
