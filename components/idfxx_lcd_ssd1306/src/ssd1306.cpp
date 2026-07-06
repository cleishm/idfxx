// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#include <idfxx/lcd/detail/panel_factory.hpp>
#include <idfxx/lcd/ssd1306>

#include <esp_lcd_panel_io.h>
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

    return idfxx::lcd::detail::make_panel_handle(
        esp_lcd_new_panel_ssd1306,
        io_handle,
        {
            .reset_gpio = config.reset_gpio,
            .bits_per_pixel = 1,
            .reset_active_level = config.reset_active_level,
            .vendor_config = &ssd1306_config,
        }
    );
}

} // namespace

namespace idfxx::lcd {

result<ssd1306> ssd1306::make(idfxx::lcd::panel_io& panel_io, ssd1306::config config) {
    return make_handle(panel_io.idf_handle(), config).transform([&](auto handle) {
        return ssd1306{panel_io.idf_handle(), handle, config.height};
    });
}

result<void> ssd1306::try_set_contrast(uint8_t level) {
    if (_io_handle == nullptr) {
        return error(errc::invalid_state);
    }
    return wrap(esp_lcd_panel_io_tx_param(_io_handle, 0x81, &level, 1));
}

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
ssd1306::ssd1306(idfxx::lcd::panel_io& panel_io, ssd1306::config config)
    : ssd1306(unwrap(make(panel_io, std::move(config)))) {}
#endif

ssd1306::ssd1306(ssd1306&& other) noexcept
    : panel(std::move(other))
    , _io_handle(std::exchange(other._io_handle, nullptr))
    , _handle(std::exchange(other._handle, nullptr)) {}

ssd1306& ssd1306::operator=(ssd1306&& other) noexcept {
    if (this != &other) {
        if (_handle != nullptr) {
            esp_lcd_panel_del(_handle);
        }

        panel::operator=(std::move(other));
        _io_handle = std::exchange(other._io_handle, nullptr);
        _handle = std::exchange(other._handle, nullptr);
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
