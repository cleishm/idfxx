// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#include <idfxx/lcd/panel_io>

#include <esp_log.h>

namespace {
const char* TAG = "idfxx::lcd::panel_io";
}

namespace idfxx::lcd {

bool panel_io::on_color_transfer_done(
    esp_lcd_panel_io_handle_t handle,
    esp_lcd_panel_io_event_data_t* edata,
    void* user_ctx
) {
    auto* self = static_cast<panel_io*>(user_ctx);
    if (self->_on_color_transfer_done != nullptr) {
        return self->_on_color_transfer_done(edata);
    }
    return false;
}

result<esp_lcd_panel_io_handle_t>
panel_io::make_handle(idfxx::spi::host_device host, const panel_io::spi_config& config) {
    esp_lcd_panel_io_spi_config_t lcd_config{
        .cs_gpio_num = config.cs_gpio.idf_num(),
        .dc_gpio_num = config.dc_gpio.idf_num(),
        .spi_mode = config.spi_mode,
        .pclk_hz = static_cast<uint32_t>(config.pclk_freq.count()),
        .trans_queue_depth = config.trans_queue_depth,
        .on_color_trans_done = _on_color_transfer_done ? panel_io::on_color_transfer_done : nullptr,
        .user_ctx = _on_color_transfer_done ? static_cast<void*>(this) : nullptr,
        .lcd_cmd_bits = config.lcd_cmd_bits,
        .lcd_param_bits = config.lcd_param_bits,
        .cs_ena_pretrans = config.cs_enable_pretrans,
        .cs_ena_posttrans = config.cs_enable_posttrans,
        .flags{
            .dc_high_on_cmd = config.flags.dc_high_on_cmd,
            .dc_low_on_data = config.flags.dc_low_on_data,
            .dc_low_on_param = config.flags.dc_low_on_param,
            .octal_mode = config.flags.octal_mode,
            .quad_mode = config.flags.quad_mode,
            .sio_mode = config.flags.sio_mode,
            .lsb_first = config.flags.lsb_first,
            .cs_high_active = config.flags.cs_high_active,
        }
    };

    esp_lcd_panel_io_handle_t handle = nullptr;
    auto err = esp_lcd_new_panel_io_spi(static_cast<spi_host_device_t>(host), &lcd_config, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create panel IO: %s", esp_err_to_name(err));
        return error(err);
    }
    return handle;
}

result<std::unique_ptr<panel_io>>
panel_io::make(std::shared_ptr<idfxx::spi::master_bus> spi_bus, panel_io::spi_config config) {
    if (spi_bus == nullptr) {
        ESP_LOGE(TAG, "Cannot create panel_io: spi_bus is null");
        return error(errc::invalid_arg);
    }

    auto self = std::unique_ptr<panel_io>(new panel_io());
    self->_spi_bus = std::move(spi_bus);
    self->_on_color_transfer_done = std::move(config.on_color_transfer_done);

    return self->make_handle(self->_spi_bus->host(), config).transform([self = std::move(self)](auto handle) mutable {
        self->_handle = handle;
        return std::move(self);
    });
}

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
panel_io::panel_io(std::shared_ptr<idfxx::spi::master_bus> spi_bus, spi_config config) {
    if (spi_bus == nullptr) {
        throw std::system_error(errc::invalid_arg, "bus is null");
    }

    _spi_bus = std::move(spi_bus);
    _on_color_transfer_done = std::move(config.on_color_transfer_done);
    _handle = unwrap(make_handle(_spi_bus->host(), config));
}
#endif

panel_io::~panel_io() {
    if (_handle != nullptr) {
        esp_lcd_panel_io_del(_handle);
    }
}

} // namespace idfxx::lcd
