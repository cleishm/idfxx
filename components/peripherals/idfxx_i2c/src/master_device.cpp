// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#include <idfxx/i2c/master>

#include <algorithm>
#include <array>
#include <chrono>
#include <driver/i2c_master.h>
#include <esp_idf_version.h>
#include <esp_log.h>
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

// Returns true if reg can be sent with the given register address size.
static bool register_fits(register_address_size size, uint16_t reg) {
    return size == register_address_size::two_bytes || reg <= 0xFF;
}

// Encodes reg into out (big-endian for two-byte addresses) and returns the number of bytes used.
static size_t encode_register(register_address_size size, uint16_t reg, uint8_t* out) {
    if (size == register_address_size::two_bytes) {
        out[0] = static_cast<uint8_t>((reg >> 8) & 0xFF);
        out[1] = static_cast<uint8_t>(reg & 0xFF);
        return 2;
    }
    out[0] = static_cast<uint8_t>(reg & 0xFF);
    return 1;
}

// The SCL speed a device is configured with: config::scl_speed, or else the bus frequency.
static quanta::frequency::hertz
resolve_scl_speed(const master_bus& bus, const master_device::config& cfg) {
    return cfg.scl_speed.value_or(bus.frequency());
}

static result<i2c_master_dev_handle_t>
make_device(master_bus& bus, uint16_t address, const master_device::config& cfg) {
    if ((cfg.scl_speed && cfg.scl_speed->count() <= 0) || cfg.scl_wait_us == 0u) {
        return error(errc::invalid_arg);
    }
    auto scl_speed = resolve_scl_speed(bus, cfg);

    i2c_device_config_t dev_config{
#if SOC_I2C_SUPPORT_10BIT_ADDR
        .dev_addr_length = cfg.addr_10bit ? I2C_ADDR_BIT_LEN_10 : I2C_ADDR_BIT_LEN_7,
#else
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
#endif
        .device_address = address,
        .scl_speed_hz = static_cast<uint32_t>(scl_speed.count()),
        .scl_wait_us = cfg.scl_wait_us.value_or(0), // 0 selects the driver default
        .flags = {
            .disable_ack_check = cfg.disable_ack_check ? 1u : 0u,
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
        if (err == ESP_ERR_NO_MEM) {
            raise_no_mem();
        }
        return error(err);
    }

    ESP_LOGD(
        TAG,
        "I2C master device created at address 0x%04X on bus port %d",
        address,
        std::to_underlying(bus.port())
    );
    return handle;
}

// Returns true if the device was configured for 10-bit addressing.
static bool uses_10bit_addresses([[maybe_unused]] const master_device::config& cfg) {
#if SOC_I2C_SUPPORT_10BIT_ADDR
    return cfg.addr_10bit;
#else
    return false;
#endif
}

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
master_device::master_device(master_bus& bus, uint16_t address, const config& cfg)
    : _bus(&bus)
    , _handle(unwrap(make_device(bus, address, cfg)))
    , _address(address)
    , _addr_10bit(uses_10bit_addresses(cfg))
    , _register_address(cfg.register_address)
    , _scl_speed(resolve_scl_speed(bus, cfg)) {}

master_device::master_device(master_bus& bus, uint16_t address)
    : master_device(bus, address, config{}) {}
#endif

result<master_device> master_device::make(master_bus& bus, uint16_t address, const config& cfg) {
    return make_device(bus, address, cfg).transform([&](auto handle) {
        return master_device(
            &bus,
            handle,
            address,
            uses_10bit_addresses(cfg),
            cfg.register_address,
            resolve_scl_speed(bus, cfg)
        );
    });
}

result<master_device> master_device::make(master_bus& bus, uint16_t address) {
    return make(bus, address, config{});
}

master_device::master_device(
    master_bus* bus,
    i2c_master_dev_handle_t handle,
    uint16_t address,
    bool addr_10bit,
    register_address_size register_address,
    quanta::frequency::hertz scl_speed
)
    : _bus(bus)
    , _handle(handle)
    , _address(address)
    , _addr_10bit(addr_10bit)
    , _register_address(register_address)
    , _scl_speed(scl_speed) {}

master_device::master_device(master_device&& other) noexcept
    : _bus(std::exchange(other._bus, nullptr))
    , _handle(std::exchange(other._handle, nullptr))
    , _address(other._address)
    , _addr_10bit(other._addr_10bit)
    , _register_address(other._register_address)
    , _scl_speed(other._scl_speed) {}

master_device& master_device::operator=(master_device&& other) noexcept {
    if (this != &other) {
        _delete();
        _bus = std::exchange(other._bus, nullptr);
        _handle = std::exchange(other._handle, nullptr);
        _address = other._address;
        _addr_10bit = other._addr_10bit;
        _register_address = other._register_address;
        _scl_speed = other._scl_speed;
    }
    return *this;
}

master_device::~master_device() {
    _delete();
}

void master_device::_delete() noexcept {
    if (_handle != nullptr) {
        i2c_master_bus_rm_device(_handle);
        _handle = nullptr;
    }
}

// Maps a transfer result to idfxx errors. Driver errors pass through, except that
// ESP_ERR_INVALID_STATE is reported as errc::invalid_response. A transfer that fails on the bus (a
// NACK, a bus timeout, or not finishing within the caller's timeout) is reported as
// ESP_ERR_INVALID_RESPONSE by ESP-IDF 6.0+ and as ESP_ERR_INVALID_STATE by earlier versions, and a
// failed bus clear reports ESP_ERR_INVALID_STATE on every version. The transfer queue that would
// make ESP_ERR_INVALID_STATE mean "queue full" is never enabled, since buses are always created
// with a zero trans_queue_depth.
result<void> master_device::_map_xfer_error(esp_err_t err, const char* op) const {
    if (err == ESP_OK) {
        return {};
    }
    if (err == ESP_ERR_INVALID_STATE) {
        err = ESP_ERR_INVALID_RESPONSE;
    }
    ESP_LOGD(
        TAG,
        "I2C %s error on device at address 0x%04X on bus port %d: %s",
        op,
        _address,
        std::to_underlying(_bus->port()),
        esp_err_to_name(err)
    );
    return error(err);
}

// Takes the bus lock for a transfer, waiting at most the transfer's timeout. The driver call that
// follows is given the caller's timeout unchanged.
std::unique_lock<std::recursive_timed_mutex>
master_device::_lock_bus(std::chrono::milliseconds timeout) const {
    auto lock = _bus->_lock_for(timeout);
    if (!lock.owns_lock()) {
        ESP_LOGD(
            TAG,
            "Timed out acquiring bus port %d for device at address 0x%04X",
            std::to_underlying(_bus->port()),
            _address
        );
    }
    return lock;
}

// The driver takes byte buffers as uint8_t.
static const uint8_t* as_u8(const std::byte* p) {
    return reinterpret_cast<const uint8_t*>(p);
}

static uint8_t* as_u8(std::byte* p) {
    return reinterpret_cast<uint8_t*>(p);
}

result<void> master_device::_try_transmit(byte_view data, std::chrono::milliseconds timeout) {
    if (_handle == nullptr) {
        return error(errc::invalid_state);
    }
    auto lock = _lock_bus(timeout);
    if (!lock.owns_lock()) {
        return error(errc::timeout);
    }
    return _map_xfer_error(
        i2c_master_transmit(_handle, as_u8(data.data()), data.size(), timeout.count()), "transmit"
    );
}

result<void> master_device::_try_multi_buffer_transmit(
    std::span<const byte_view> buffers,
    std::chrono::milliseconds timeout
) {
    if (_handle == nullptr) {
        return error(errc::invalid_state);
    }
    auto lock = _lock_bus(timeout);
    if (!lock.owns_lock()) {
        return error(errc::timeout);
    }

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
        info[i].write_buffer = as_u8(buffers[i].data());
#else
        info[i].write_buffer = const_cast<uint8_t*>(as_u8(buffers[i].data()));
#endif
        info[i].buffer_size = buffers[i].size();
    }

    return _map_xfer_error(
        i2c_master_multi_buffer_transmit(_handle, info, buffers.size(), timeout.count()),
        "multi_buffer_transmit"
    );
}

result<void>
master_device::_try_change_address(uint16_t new_address, std::chrono::milliseconds timeout) {
    if (_handle == nullptr) {
        return error(errc::invalid_state);
    }
    // The driver checks this too, but returns without releasing its own bus lock, which stalls
    // every later transfer on the bus, so it's checked here first.
    if (_addr_10bit ? new_address <= 0x7F : new_address > 0x7F) {
        ESP_LOGD(
            TAG,
            "Address 0x%04X doesn't fit the %d-bit addressing of the device at address 0x%04X",
            new_address,
            _addr_10bit ? 10 : 7,
            _address
        );
        return error(errc::invalid_arg);
    }
    auto lock = _lock_bus(timeout);
    if (!lock.owns_lock()) {
        return error(errc::timeout);
    }
    auto r = _map_xfer_error(
        i2c_master_device_change_address(_handle, new_address, timeout.count()), "change_address"
    );
    if (r) {
        _address = new_address;
    }
    return r;
}

result<void> master_device::_try_execute_operations(
    std::span<const operation> ops,
    std::chrono::milliseconds timeout
) {
    if (_handle == nullptr) {
        return error(errc::invalid_state);
    }
    auto lock = _lock_bus(timeout);
    if (!lock.owns_lock()) {
        return error(errc::timeout);
    }

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
            job.write.data = as_u8(ops[i].write_data.data());
#else
            job.write.data = const_cast<uint8_t*>(as_u8(ops[i].write_data.data()));
#endif
            job.write.total_bytes = ops[i].write_data.size();
            break;
        case operation_command::read:
            job.read.ack_value = static_cast<i2c_ack_value_t>(std::to_underlying(ops[i].ack_type));
            job.read.data = as_u8(ops[i].read_data.data());
            job.read.total_bytes = ops[i].read_data.size();
            break;
        default:
            break;
        }
    }

    return _map_xfer_error(
        i2c_master_execute_defined_operations(_handle, jobs, ops.size(), timeout.count()),
        "execute_operations"
    );
}

result<void> master_device::_try_receive(byte_span buf, std::chrono::milliseconds timeout) {
    if (_handle == nullptr) {
        return error(errc::invalid_state);
    }
    auto lock = _lock_bus(timeout);
    if (!lock.owns_lock()) {
        return error(errc::timeout);
    }
    return _map_xfer_error(
        i2c_master_receive(_handle, as_u8(buf.data()), buf.size(), timeout.count()), "receive"
    );
}

result<void>
master_device::_try_write_register(uint16_t reg, byte_view buf, std::chrono::milliseconds timeout) {
    return _try_write_registers(std::span<const uint16_t>(&reg, 1), buf, timeout);
}

result<void> master_device::_try_write_registers(
    std::span<const uint16_t> registers,
    byte_view buf,
    std::chrono::milliseconds timeout
) {
    if (_handle == nullptr) {
        return error(errc::invalid_state);
    }
    if (!std::ranges::all_of(registers, [this](uint16_t reg) {
            return register_fits(_register_address, reg);
        })) {
        return error(errc::invalid_arg);
    }
    // Held across every register so the writes go out back to back
    auto lock = _lock_bus(timeout);
    if (!lock.owns_lock()) {
        return error(errc::timeout);
    }

    // Each register is sent with the data as one write transaction. The driver skips zero-length
    // buffers, so a register-only write sends just the register address.
    for (const auto& reg : registers) {
        uint8_t reg_bytes[2];
        auto reg_size = encode_register(_register_address, reg, reg_bytes);
        const std::array<byte_view, 2> buffers{byte_view{reg_bytes, reg_size}, buf};
        if (auto r = _try_multi_buffer_transmit(buffers, timeout); !r) {
            return r;
        }
    }
    return {};
}

result<void>
master_device::_try_read_register(uint16_t reg, byte_span buf, std::chrono::milliseconds timeout) {
    if (_handle == nullptr) {
        return error(errc::invalid_state);
    }
    if (!register_fits(_register_address, reg) || buf.empty()) {
        return error(errc::invalid_arg);
    }

    uint8_t reg_bytes[2];
    auto reg_size = encode_register(_register_address, reg, reg_bytes);
    auto lock = _lock_bus(timeout);
    if (!lock.owns_lock()) {
        return error(errc::timeout);
    }
    return _map_xfer_error(
        i2c_master_transmit_receive(
            _handle, reg_bytes, reg_size, as_u8(buf.data()), buf.size(), timeout.count()
        ),
        "read_register"
    );
}

} // namespace idfxx::i2c
