// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#include <idfxx/i2c/master>

#include <array>
#include <driver/i2c_master.h>
#include <esp_idf_version.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <utility>
#include <vector>

namespace {
const char* TAG = "idfxx::i2c::master_device";
}

namespace idfxx::i2c {

// Verify operation_command values match ESP-IDF constants
static_assert(std::to_underlying(operation_command::start) == I2C_MASTER_CMD_START);
static_assert(std::to_underlying(operation_command::write) == I2C_MASTER_CMD_WRITE);
static_assert(std::to_underlying(operation_command::read) == I2C_MASTER_CMD_READ);
static_assert(std::to_underlying(operation_command::stop) == I2C_MASTER_CMD_STOP);

// Verify ack_value values match ESP-IDF constants
static_assert(std::to_underlying(ack_value::ack) == I2C_ACK_VAL);
static_assert(std::to_underlying(ack_value::nack) == I2C_NACK_VAL);

static result<void> map_xfer_error(esp_err_t err, const char* op, uint16_t address, enum port port) {
    if (err == ESP_OK) {
        return {};
    }
    if (err == ESP_ERR_TIMEOUT) {
        ESP_LOGD(
            TAG, "I2C %s timeout on device at address 0x%04X on bus port %d", op, address, std::to_underlying(port)
        );
        return error(errc::timeout);
    }
    ESP_LOGD(
        TAG,
        "I2C %s error on device at address 0x%04X on bus port %d: %s",
        op,
        address,
        std::to_underlying(port),
        esp_err_to_name(err)
    );
    return error(errc::invalid_arg);
}

static result<i2c_master_dev_handle_t>
make_device(master_bus& bus, uint16_t address, const master_device::config& config) {
    auto scl_speed = config.scl_speed.count() > 0 ? config.scl_speed : bus.frequency();

    i2c_device_config_t dev_config{
#if SOC_I2C_SUPPORT_10BIT_ADDR
        .dev_addr_length = config.addr_10bit ? I2C_ADDR_BIT_LEN_10 : I2C_ADDR_BIT_LEN_7,
#else
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
#endif
        .device_address = address,
        .scl_speed_hz = static_cast<uint32_t>(scl_speed.count()),
        .scl_wait_us = config.scl_wait_us,
        .flags = {
            .disable_ack_check = config.disable_ack_check ? 1u : 0u,
        },
    };

    i2c_master_dev_handle_t handle;
    if (auto err = i2c_master_bus_add_device(bus.handle(), &dev_config, &handle); err != ESP_OK) {
        ESP_LOGD(
            TAG,
            "Failed to create I2C master device at address 0x%04X on bus port %d: %s",
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
        TAG, "I2C master device created at address 0x%04X on bus port %d", address, std::to_underlying(bus.port())
    );
    return handle;
}

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
master_device::master_device(master_bus& bus, uint16_t address, const struct config& config)
    : _bus(&bus)
    , _handle(unwrap(make_device(bus, address, config)))
    , _address(address) {}

master_device::master_device(master_bus& bus, uint16_t address)
    : master_device(bus, address, config{}) {}
#endif

result<master_device> master_device::make(master_bus& bus, uint16_t address, const struct config& config) {
    return make_device(bus, address, config).transform([&](auto handle) {
        return master_device(&bus, handle, address);
    });
}

result<master_device> master_device::make(master_bus& bus, uint16_t address) {
    return make(bus, address, config{});
}

master_device::master_device(master_bus* bus, i2c_master_dev_handle_t handle, uint16_t address)
    : _bus(bus)
    , _handle(handle)
    , _address(address) {}

master_device::master_device(master_device&& other) noexcept
    : _bus(std::exchange(other._bus, nullptr))
    , _handle(std::exchange(other._handle, nullptr))
    , _address(other._address)
    , _on_trans_done(std::move(other._on_trans_done)) {}

master_device& master_device::operator=(master_device&& other) noexcept {
    if (this != &other) {
        _delete();
        _bus = std::exchange(other._bus, nullptr);
        _handle = std::exchange(other._handle, nullptr);
        _address = other._address;
        _on_trans_done = std::move(other._on_trans_done);
    }
    return *this;
}

master_device::~master_device() {
    _delete();
}

void master_device::_delete() noexcept {
    if (_handle != nullptr) {
        if (_on_trans_done) {
            i2c_master_event_callbacks_t cbs{.on_trans_done = nullptr};
            (void)i2c_master_register_event_callbacks(_handle, &cbs, nullptr);
        }
        i2c_master_bus_rm_device(_handle);
        _handle = nullptr;
    }
    _on_trans_done.reset();
}

result<void> master_device::_try_transmit(const uint8_t* buf, size_t size, std::chrono::milliseconds timeout) {
    if (_handle == nullptr) {
        return error(errc::invalid_state);
    }
    esp_err_t err;
    {
        std::scoped_lock lock(*_bus);
        err = i2c_master_transmit(_handle, buf, size, timeout.count());
    }
    return map_xfer_error(err, "transmit", _address, _bus->port());
}

result<void> master_device::_try_multi_buffer_transmit(
    std::span<const std::span<const uint8_t>> buffers,
    std::chrono::milliseconds timeout
) {
    if (_handle == nullptr) {
        return error(errc::invalid_state);
    }
    std::scoped_lock lock(*_bus);

    constexpr size_t SBO_SIZE = 8;
    std::array<i2c_master_transmit_multi_buffer_info_t, SBO_SIZE> stack_info;
    std::vector<i2c_master_transmit_multi_buffer_info_t> heap_info;
    i2c_master_transmit_multi_buffer_info_t* info;
    if (buffers.size() <= SBO_SIZE) {
        info = stack_info.data();
    } else {
        heap_info.resize(buffers.size());
        info = heap_info.data();
    }
    for (size_t i = 0; i < buffers.size(); ++i) {
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
        info[i].write_buffer = buffers[i].data();
#else
        info[i].write_buffer = const_cast<uint8_t*>(buffers[i].data());
#endif
        info[i].buffer_size = buffers[i].size();
    }

    return map_xfer_error(
        i2c_master_multi_buffer_transmit(_handle, info, buffers.size(), timeout.count()),
        "multi_buffer_transmit",
        _address,
        _bus->port()
    );
}

result<void> master_device::_try_change_address(uint16_t new_address, std::chrono::milliseconds timeout) {
    if (_handle == nullptr) {
        return error(errc::invalid_state);
    }
    std::scoped_lock lock(*_bus);
    auto r = map_xfer_error(
        i2c_master_device_change_address(_handle, new_address, timeout.count()),
        "change_address",
        _address,
        _bus->port()
    );
    if (r) {
        _address = new_address;
    }
    return r;
}

result<void> master_device::_try_execute_operations(std::span<const operation> ops, std::chrono::milliseconds timeout) {
    if (_handle == nullptr) {
        return error(errc::invalid_state);
    }
    std::scoped_lock lock(*_bus);

    constexpr size_t SBO_SIZE = 8;
    std::array<i2c_operation_job_t, SBO_SIZE> stack_jobs{};
    std::vector<i2c_operation_job_t> heap_jobs;
    i2c_operation_job_t* jobs;
    if (ops.size() <= SBO_SIZE) {
        jobs = stack_jobs.data();
    } else {
        heap_jobs.resize(ops.size());
        jobs = heap_jobs.data();
    }
    for (size_t i = 0; i < ops.size(); ++i) {
        auto& job = jobs[i];
        job.command = static_cast<i2c_master_command_t>(std::to_underlying(ops[i].command));
        switch (ops[i].command) {
        case operation_command::write:
            job.write.ack_check = ops[i].ack_check;
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
            job.write.data = ops[i].write_data.data();
#else
            job.write.data = const_cast<uint8_t*>(ops[i].write_data.data());
#endif
            job.write.total_bytes = ops[i].write_data.size();
            break;
        case operation_command::read:
            job.read.ack_value = static_cast<i2c_ack_value_t>(std::to_underlying(ops[i].ack_type));
            job.read.data = ops[i].read_data.data();
            job.read.total_bytes = ops[i].read_data.size();
            break;
        default:
            break;
        }
    }

    return map_xfer_error(
        i2c_master_execute_defined_operations(_handle, jobs, ops.size(), timeout.count()),
        "execute_operations",
        _address,
        _bus->port()
    );
}

namespace {

bool IRAM_ATTR trans_done_trampoline(i2c_master_dev_handle_t, const i2c_master_event_data_t*, void* user_arg) {
    auto* cb = static_cast<const std::move_only_function<bool() const>*>(user_arg);
    return (*cb)();
}

} // namespace

result<void> master_device::try_register_event_callbacks(std::move_only_function<bool() const> on_trans_done) {
    if (_handle == nullptr) {
        return error(errc::invalid_state);
    }
    auto new_cb = std::make_unique<std::move_only_function<bool() const>>(std::move(on_trans_done));
    i2c_master_event_callbacks_t cbs{.on_trans_done = trans_done_trampoline};
    if (auto err = i2c_master_register_event_callbacks(_handle, &cbs, new_cb.get()); err != ESP_OK) {
        return error(err);
    }
    _on_trans_done = std::move(new_cb);
    return {};
}

result<void> master_device::_try_receive(uint8_t* buf, size_t size, std::chrono::milliseconds timeout) {
    if (_handle == nullptr) {
        return error(errc::invalid_state);
    }
    esp_err_t err;
    {
        std::scoped_lock lock(*_bus);
        err = i2c_master_receive(_handle, buf, size, timeout.count());
    }
    return map_xfer_error(err, "receive", _address, _bus->port());
}

result<void>
master_device::_try_write_register(uint16_t reg, const uint8_t* buf, size_t size, std::chrono::milliseconds timeout) {
    return _try_write_register(
        static_cast<uint8_t>((reg >> 8) & 0xFF), static_cast<uint8_t>(reg & 0xFF), buf, size, timeout
    );
}

result<void> master_device::_try_write_register(
    uint8_t high,
    uint8_t low,
    const uint8_t* buf,
    size_t size,
    std::chrono::milliseconds timeout
) {
    // Combine register address and data into single buffer
#ifdef __GNUC__
    uint8_t buffer[2 + size];
#else
    std::vector<uint8_t> buf_storage(2 + size);
    auto* buffer = buf_storage.data();
#endif
    buffer[0] = high;
    buffer[1] = low;
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
    return _try_read_register(
        static_cast<uint8_t>((reg >> 8) & 0xFF), static_cast<uint8_t>(reg & 0xFF), buf, size, timeout
    );
}

result<void> master_device::_try_read_register(
    uint8_t high,
    uint8_t low,
    uint8_t* buf,
    size_t size,
    std::chrono::milliseconds timeout
) {
    if (_handle == nullptr) {
        return error(errc::invalid_state);
    }
    std::scoped_lock lock(*_bus);

    uint8_t buffer[2]{high, low};
    return _try_transmit(buffer, 2, timeout).and_then([&]() {
        vTaskDelay(pdMS_TO_TICKS(20)); // Delay between write and read for device processing
        return _try_receive(buf, size, timeout);
    });
}

} // namespace idfxx::i2c
