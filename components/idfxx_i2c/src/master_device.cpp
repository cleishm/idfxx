// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#include <idfxx/i2c/master>

#include <driver/i2c_master.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <utility>

namespace {
const char* TAG = "idfxx::i2c::master_device";
}

namespace idfxx::i2c {

static result<i2c_master_dev_handle_t> make_device(master_bus& bus, uint8_t address) {
    i2c_device_config_t dev_config{
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = address,
        .scl_speed_hz = static_cast<uint32_t>(bus.frequency().count()),
        .scl_wait_us = 0,
        .flags =
            {
                .disable_ack_check = 0,
            },
    };

    i2c_master_dev_handle_t handle;
    if (auto err = i2c_master_bus_add_device(bus.handle(), &dev_config, &handle); err != ESP_OK) {
        ESP_LOGD(
            TAG,
            "Failed to create I2C master device at address 0x%02X on bus port %d: %s",
            address,
            std::to_underlying(bus.port()),
            esp_err_to_name(err)
        );
        switch (err) {
        case ESP_ERR_NO_MEM:
            raise_no_mem();
        default:
            return error(errc::invalid_arg);
        }
    }

    ESP_LOGD(
        TAG, "I2C master device created at address 0x%02X on bus port %d", address, std::to_underlying(bus.port())
    );
    return handle;
}

result<master_device> master_device::make(master_bus& bus, uint8_t address) {
    return make_device(bus, address).transform([&](auto handle) { return master_device(&bus, handle, address); });
}

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
master_device::master_device(master_bus& bus, uint8_t address)
    : _bus(&bus)
    , _handle(unwrap(make_device(bus, address)))
    , _address(address) {}
#endif

master_device::master_device(master_bus* bus, i2c_master_dev_handle_t handle, uint8_t address)
    : _bus(bus)
    , _handle(handle)
    , _address(address) {}

master_device::master_device(master_device&& other) noexcept
    : _bus(other._bus)
    , _handle(other._handle)
    , _address(other._address) {
    other._bus = nullptr;
    other._handle = nullptr;
}

master_device& master_device::operator=(master_device&& other) noexcept {
    if (this != &other) {
        if (_handle != nullptr) {
            i2c_master_bus_rm_device(_handle);
        }
        _bus = other._bus;
        _handle = other._handle;
        _address = other._address;
        other._bus = nullptr;
        other._handle = nullptr;
    }
    return *this;
}

master_device::~master_device() {
    if (_handle != nullptr) {
        i2c_master_bus_rm_device(_handle);
    }
}

result<void> master_device::_try_transmit(const uint8_t* buf, size_t size, std::chrono::milliseconds timeout) {
    if (_handle == nullptr) {
        return error(errc::invalid_state);
    }
    std::scoped_lock lock(*_bus);

    esp_err_t err = i2c_master_transmit(_handle, buf, size, timeout.count());
    if (err == ESP_OK) {
        return {};
    } else if (err == ESP_ERR_TIMEOUT) {
        ESP_LOGD(
            TAG,
            "I2C transmit timeout on device at address 0x%02X on bus port %d",
            _address,
            std::to_underlying(_bus->port())
        );
        return error(errc::timeout);
    } else {
        ESP_LOGD(
            TAG,
            "I2C transmit error on device at address 0x%02X on bus port %d: %s",
            _address,
            std::to_underlying(_bus->port()),
            esp_err_to_name(err)
        );
        return error(errc::invalid_arg);
    }
}

result<void> master_device::_try_receive(uint8_t* buf, size_t size, std::chrono::milliseconds timeout) {
    if (_handle == nullptr) {
        return error(errc::invalid_state);
    }
    std::scoped_lock lock(*_bus);

    esp_err_t err = i2c_master_receive(_handle, buf, size, timeout.count());
    if (err == ESP_OK) {
        return {};
    } else if (err == ESP_ERR_TIMEOUT) {
        ESP_LOGD(
            TAG,
            "I2C receive timeout on device at address 0x%02X on bus port %d",
            _address,
            std::to_underlying(_bus->port())
        );
        return error(errc::timeout);
    } else {
        ESP_LOGD(
            TAG,
            "I2C receive error on device at address 0x%02X on bus port %d: %s",
            _address,
            std::to_underlying(_bus->port()),
            esp_err_to_name(err)
        );
        return error(errc::invalid_arg);
    }
}

result<void>
master_device::_try_write_register(uint16_t reg, const uint8_t* buf, size_t size, std::chrono::milliseconds timeout) {
    if (_handle == nullptr) {
        return error(errc::invalid_state);
    }
    // Combine register address and data into single buffer
    uint8_t buffer[2 + size];
    buffer[0] = (reg >> 8) & 0xFF;
    buffer[1] = reg & 0xFF;
    if (buf && size > 0) {
        memcpy(buffer + 2, buf, size);
    }
    return _try_transmit(buffer, 2 + size, timeout);
}

result<void> master_device::_try_write_register(
    uint8_t regHigh,
    uint8_t regLow,
    const uint8_t* buf,
    size_t size,
    std::chrono::milliseconds timeout
) {
    if (_handle == nullptr) {
        return error(errc::invalid_state);
    }
    // Combine register address and data into single buffer
    uint8_t buffer[2 + size];
    buffer[0] = regHigh;
    buffer[1] = regLow;
    if (buf && size > 0) {
        memcpy(buffer + 2, buf, size);
    }
    return _try_transmit(buffer, 2 + size, timeout);
}

result<void> master_device::_try_write_registers(
    std::span<const uint16_t> registers,
    const uint8_t* buf,
    size_t size,
    std::chrono::milliseconds timeout
) {
    if (_handle == nullptr) {
        return error(errc::invalid_state);
    }
    std::scoped_lock lock(*_bus);

    for (const auto& reg : registers) {
        auto result = _try_write_register(reg, buf, size, timeout);
        if (!result.has_value()) {
            return result;
        }
        vTaskDelay(pdMS_TO_TICKS(4)); // Small delay between writes
    }
    return {};
}

result<void>
master_device::_try_read_register(uint16_t reg, uint8_t* buf, size_t size, std::chrono::milliseconds timeout) {
    if (_handle == nullptr) {
        return error(errc::invalid_state);
    }
    std::scoped_lock lock(*_bus);

    uint8_t buffer[2]{(uint8_t)(reg >> 8), (uint8_t)(reg & 0xFF)};
    return _try_transmit(buffer, 2, timeout).and_then([&]() {
        vTaskDelay(pdMS_TO_TICKS(20)); // Delay between write and read for device processing
        return _try_receive(buf, size, timeout);
    });
}

result<void> master_device::_try_read_register(
    uint8_t regHigh,
    uint8_t regLow,
    uint8_t* buf,
    size_t size,
    std::chrono::milliseconds timeout
) {
    if (_handle == nullptr) {
        return error(errc::invalid_state);
    }
    std::scoped_lock lock(*_bus);

    uint8_t buffer[2]{regHigh, regLow};
    return _try_transmit(buffer, 2, timeout).and_then([&]() {
        vTaskDelay(pdMS_TO_TICKS(20)); // Delay between write and read for device processing
        return _try_receive(buf, size, timeout);
    });
}

} // namespace idfxx::i2c
