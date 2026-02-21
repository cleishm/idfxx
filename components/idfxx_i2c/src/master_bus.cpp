// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#include <idfxx/i2c/master>

#include <driver/i2c_master.h>
#include <esp_log.h>
#include <utility>

namespace {
const char* TAG = "idfxx::i2c::master_bus";
}

namespace idfxx::i2c {

static result<i2c_master_bus_handle_t> make_bus(enum port port, gpio sda, gpio scl, freq::hertz frequency) {
    i2c_master_bus_config_t bus_config{
        .i2c_port = std::to_underlying(port),
        .sda_io_num = sda.idf_num(),
        .scl_io_num = scl.idf_num(),
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .intr_priority = 0,     // auto
        .trans_queue_depth = 0, // only valid in asynchronous transaction
        .flags =
            {
                .enable_internal_pullup = 1,
                .allow_pd = 0, // do not allow power down
            },
    };

    i2c_master_bus_handle_t handle;
    if (auto err = i2c_new_master_bus(&bus_config, &handle); err != ESP_OK) {
        ESP_LOGD(
            TAG,
            "Failed to create I2C master bus on port %d (SDA: GPIO%d, SCL: GPIO%d, Frequency: %d Hz): %s",
            std::to_underlying(port),
            sda.num(),
            scl.num(),
            static_cast<int>(frequency.count()),
            esp_err_to_name(err)
        );
        switch (err) {
        case ESP_ERR_NO_MEM:
            raise_no_mem();
        case ESP_ERR_NOT_FOUND:
            return error(errc::not_found);
        default:
            return error(errc::invalid_arg);
        }
    }

    ESP_LOGD(
        TAG,
        "I2C master bus created on port %d (SDA: GPIO%d, SCL: GPIO%d, Frequency: %d Hz)",
        std::to_underlying(port),
        sda.num(),
        scl.num(),
        static_cast<int>(frequency.count())
    );
    return handle;
}

result<std::unique_ptr<master_bus>> master_bus::make(enum port port, gpio sda, gpio scl, freq::hertz frequency) {
    return make_bus(port, sda, scl, frequency).transform([&](auto handle) {
        return std::unique_ptr<master_bus>(new master_bus{handle, port, frequency});
    });
}

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
master_bus::master_bus(enum port port, gpio sda, gpio scl, freq::hertz frequency)
    : master_bus(unwrap(make_bus(port, sda, scl, frequency)), port, frequency) {}
#endif

master_bus::master_bus(i2c_master_bus_handle_t handle, enum port port, freq::hertz frequency)
    : _handle(handle)
    , _port(port)
    , _frequency(frequency) {}

master_bus::~master_bus() {
    i2c_del_master_bus(_handle);
}

std::vector<uint8_t> master_bus::_scan_devices(std::chrono::milliseconds timeout) const {
    std::vector<uint8_t> devices;

    ESP_LOGD(TAG, "Scanning I2C bus %d...", std::to_underlying(_port));

    // Scan addresses 0x08 to 0x77 (standard 7-bit address range, excluding reserved addresses)
    for (uint8_t addr = 0x08; addr <= 0x77; addr++) {
        _probe(addr, timeout)
            .transform([&] {
                devices.push_back(addr);
                ESP_LOGD(TAG, "  Found device at address 0x%02X", addr);
            })
            .transform_error([&](auto ec) {
                if (ec != errc::not_found) {
                    ESP_LOGD(TAG, "  Error probing address 0x%02X: %s", addr, ec.message().c_str());
                }
                return ec;
            });
    }

    if (devices.empty()) {
        ESP_LOGD(TAG, "  No devices found on I2C bus %d", std::to_underlying(_port));
    } else {
        ESP_LOGD(TAG, "  Found %zu device(s) on I2C bus %d", devices.size(), std::to_underlying(_port));
    }
    return devices;
}

result<void> master_bus::_probe(uint8_t address, std::chrono::milliseconds timeout) const {
    esp_err_t err;
    {
        std::scoped_lock lock(_mux);
        err = i2c_master_probe(_handle, address, timeout.count());
    }
    if (err == ESP_OK) {
        return {};
    }
    if (err == ESP_ERR_NOT_FOUND) {
        return error(errc::not_found);
    }
    if (err == ESP_ERR_TIMEOUT) {
        ESP_LOGD(TAG, "Timeout probing address 0x%02X on I2C bus %d", address, std::to_underlying(_port));
        return error(errc::timeout);
    }
    ESP_LOGD(
        TAG, "Error probing address 0x%02X on I2C bus %d: %s", address, std::to_underlying(_port), esp_err_to_name(err)
    );
    return error(errc::invalid_arg);
}

} // namespace idfxx::i2c
