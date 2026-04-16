// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#include <idfxx/i2c/master>

#include <bit>
#include <driver/i2c_master.h>
#include <esp_log.h>
#include <utility>

namespace {

const char* TAG = "idfxx::i2c::master_bus";

using namespace idfxx::i2c;

i2c_clock_source_t to_idf(clk_source src) {
    switch (src) {
#if SOC_I2C_SUPPORT_APB
    case clk_source::apb:
        return I2C_CLK_SRC_APB;
#endif
#if SOC_I2C_SUPPORT_REF_TICK
    case clk_source::ref_tick:
        return I2C_CLK_SRC_REF_TICK;
#endif
#if SOC_I2C_SUPPORT_XTAL
    case clk_source::xtal:
        return I2C_CLK_SRC_XTAL;
#endif
#if SOC_I2C_SUPPORT_RTC
    case clk_source::rc_fast:
        return I2C_CLK_SRC_RC_FAST;
#endif
    default:
        return I2C_CLK_SRC_DEFAULT;
    }
}

#if SOC_LP_I2C_SUPPORTED
lp_i2c_clock_source_t to_idf(lp_clk_source src) {
    switch (src) {
    case lp_clk_source::lp_fast:
        return LP_I2C_SCLK_LP_FAST;
    case lp_clk_source::xtal_d2:
        return LP_I2C_SCLK_XTAL_D2;
    default:
        return LP_I2C_SCLK_DEFAULT;
    }
}
#endif

} // namespace

namespace idfxx::i2c {

static result<i2c_master_bus_handle_t> make_bus(enum port port, const master_bus::config& config) {
    if (!config.sda.is_connected()) {
        ESP_LOGD(TAG, "Field 'sda' has an invalid value");
        return error(errc::invalid_arg);
    }
    if (!config.scl.is_connected()) {
        ESP_LOGD(TAG, "Field 'scl' has an invalid value");
        return error(errc::invalid_arg);
    }
    if (config.frequency.count() == 0) {
        ESP_LOGD(TAG, "Field 'frequency' has an invalid value");
        return error(errc::invalid_arg);
    }

    i2c_master_bus_config_t bus_config{
        .i2c_port = std::to_underlying(port),
        .sda_io_num = config.sda.idf_num(),
        .scl_io_num = config.scl.idf_num(),
        .clk_source = to_idf(config.clk_source),
        .glitch_ignore_cnt = config.glitch_ignore_cnt,
        .intr_priority =
            config.intr_level ? std::countr_zero(static_cast<unsigned>(std::to_underlying(*config.intr_level))) : 0,
        .trans_queue_depth = config.trans_queue_depth,
        .flags = {
            .enable_internal_pullup = config.enable_internal_pullup ? 1u : 0u,
            .allow_pd = config.allow_pd ? 1u : 0u,
        },
    };
#if SOC_LP_I2C_SUPPORTED
    if (port == port::lp_i2c0) {
        bus_config.lp_source_clk = to_idf(config.lp_source_clk);
    }
#endif

    i2c_master_bus_handle_t handle;
    if (auto err = i2c_new_master_bus(&bus_config, &handle); err != ESP_OK) {
        ESP_LOGD(
            TAG,
            "Failed to create I2C master bus on port %d (SDA: GPIO%d, SCL: GPIO%d, Frequency: %d Hz): %s",
            std::to_underlying(port),
            config.sda.num(),
            config.scl.num(),
            static_cast<int>(config.frequency.count()),
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
        config.sda.num(),
        config.scl.num(),
        static_cast<int>(config.frequency.count())
    );
    return handle;
}

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
master_bus::master_bus(enum port port, const struct config& config)
    : master_bus(unwrap(make_bus(port, config)), port, config.frequency) {}

master_bus::master_bus(enum port port, gpio sda, gpio scl, freq::hertz frequency)
    : master_bus(port, config{.sda = sda, .scl = scl, .frequency = frequency}) {}
#endif

result<master_bus> master_bus::make(enum port port, const struct config& config) {
    return make_bus(port, config).transform([&](auto handle) { return master_bus{handle, port, config.frequency}; });
}

result<master_bus> master_bus::make(enum port port, gpio sda, gpio scl, freq::hertz frequency) {
    return make(port, config{.sda = sda, .scl = scl, .frequency = frequency});
}

master_bus::master_bus(i2c_master_bus_handle_t handle, enum port port, freq::hertz frequency)
    : _mux(std::make_unique<std::recursive_mutex>())
    , _handle(handle)
    , _port(port)
    , _frequency(frequency) {}

master_bus::master_bus(master_bus&& other) noexcept
    : _mux(std::move(other._mux))
    , _handle(std::exchange(other._handle, nullptr))
    , _port(other._port)
    , _frequency(other._frequency) {}

master_bus& master_bus::operator=(master_bus&& other) noexcept {
    if (this != &other) {
        _delete();
        _mux = std::move(other._mux);
        _handle = std::exchange(other._handle, nullptr);
        _port = other._port;
        _frequency = other._frequency;
    }
    return *this;
}

master_bus::~master_bus() {
    _delete();
}

void master_bus::_delete() noexcept {
    if (_handle != nullptr) {
        i2c_del_master_bus(_handle);
        _handle = nullptr;
    }
}

std::vector<uint8_t> master_bus::_scan_devices(std::chrono::milliseconds timeout) const {
    if (_handle == nullptr) {
        return {};
    }
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
                    auto msg = ec.message();
                    ESP_LOGD(TAG, "  Error probing address 0x%02X: %s", addr, msg.c_str());
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

result<void> master_bus::_probe(uint16_t address, std::chrono::milliseconds timeout) const {
    if (_handle == nullptr) {
        return error(errc::invalid_state);
    }
    esp_err_t err;
    {
        std::scoped_lock lock(*_mux);
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

result<void> master_bus::_try_wait_all_done(std::chrono::milliseconds timeout) const {
    if (_handle == nullptr) {
        return error(errc::invalid_state);
    }
    auto err = i2c_master_bus_wait_all_done(_handle, timeout.count());
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "wait_all_done on bus %d failed: %s", std::to_underlying(_port), esp_err_to_name(err));
    }
    return wrap(err);
}

} // namespace idfxx::i2c
