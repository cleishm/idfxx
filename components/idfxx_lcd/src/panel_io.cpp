// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#include <idfxx/lcd/panel_io>

#include <esp_log.h>
#include <utility>

namespace {
const char* TAG = "idfxx::lcd::panel_io";
}

namespace idfxx::lcd {

bool panel_io::on_color_transfer_done(
    esp_lcd_panel_io_handle_t handle,
    esp_lcd_panel_io_event_data_t* edata,
    void* user_ctx
) {
    auto* state = static_cast<callback_state*>(user_ctx);
    if (state->on_color_transfer_done != nullptr) {
        return state->on_color_transfer_done(edata);
    }
    return false;
}

panel_io::panel_io(esp_lcd_panel_io_handle_t handle, std::unique_ptr<callback_state> callbacks)
    : _handle(handle)
    , _callbacks(std::move(callbacks)) {}

result<panel_io> panel_io::make(idfxx::spi::master_bus& spi_bus, panel_io::spi_config config) {
    std::unique_ptr<callback_state> cbs;
    if (config.on_color_transfer_done) {
        cbs = std::make_unique<callback_state>(std::move(config.on_color_transfer_done));
    }

    esp_lcd_panel_io_spi_config_t lcd_config{
        .cs_gpio_num = config.cs_gpio.idf_num(),
        .dc_gpio_num = config.dc_gpio.idf_num(),
        .spi_mode = config.spi_mode,
        .pclk_hz = static_cast<uint32_t>(config.pclk_freq.count()),
        .trans_queue_depth = config.trans_queue_depth,
        .on_color_trans_done = cbs ? on_color_transfer_done : nullptr,
        .user_ctx = cbs ? static_cast<void*>(cbs.get()) : nullptr,
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
    auto err = esp_lcd_new_panel_io_spi(static_cast<spi_host_device_t>(spi_bus.host()), &lcd_config, &handle);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "Failed to create panel IO: %s", esp_err_to_name(err));
        return error(err);
    }
    return panel_io{handle, std::move(cbs)};
}

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
panel_io::panel_io(idfxx::spi::master_bus& spi_bus, spi_config config)
    : panel_io(unwrap(make(spi_bus, std::move(config)))) {}
#endif

panel_io::panel_io(panel_io&& other) noexcept
    : _handle(std::exchange(other._handle, nullptr))
    , _callbacks(std::move(other._callbacks)) {}

panel_io& panel_io::operator=(panel_io&& other) noexcept {
    if (this != &other) {
        if (_handle != nullptr) {
            esp_lcd_panel_io_del(_handle);
        }
        _handle = std::exchange(other._handle, nullptr);
        _callbacks = std::move(other._callbacks);
    }
    return *this;
}

panel_io::~panel_io() {
    if (_handle != nullptr) {
        esp_lcd_panel_io_del(_handle);
    }
}

} // namespace idfxx::lcd
