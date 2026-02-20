// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#include <idfxx/onewire>

#include <esp_log.h>
#include <onewire.h>

namespace {
const char* TAG = "idfxx::onewire";
}

// Verify ONEWIRE_NONE matches address::none()
static_assert(idfxx::onewire::address::none().raw() == ONEWIRE_NONE);

namespace idfxx::onewire {

// -- CRC utilities -----------------------------------------------------------

uint8_t crc8(std::span<const uint8_t> data) {
    return onewire_crc8(data.data(), static_cast<uint8_t>(data.size()));
}

uint16_t crc16(std::span<const uint8_t> data, uint16_t crc_iv) {
    return onewire_crc16(data.data(), data.size(), crc_iv);
}

bool check_crc16(std::span<const uint8_t> data, std::span<const uint8_t, 2> inverted_crc, uint16_t crc_iv) {
    return onewire_check_crc16(data.data(), data.size(), inverted_crc.data(), crc_iv);
}

// -- bus ---------------------------------------------------------------------

result<std::unique_ptr<bus>> bus::make(gpio pin) {
    if (!pin.is_connected()) {
        ESP_LOGD(TAG, "Cannot create bus: GPIO pin is not connected");
        return error(errc::invalid_state);
    }
    return std::unique_ptr<bus>(new bus(pin, validated{}));
}

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
bus::bus(gpio pin)
    : _pin(pin) {
    if (!_pin.is_connected()) {
        throw std::system_error(make_error_code(errc::invalid_state), "GPIO pin is not connected");
    }
}
#endif

bool bus::reset() {
    std::scoped_lock lock(_mux);
    return onewire_reset(_pin.idf_num());
}

result<void> bus::try_select(address addr) {
    std::scoped_lock lock(_mux);
    if (!onewire_select(_pin.idf_num(), addr.raw())) {
        ESP_LOGD(TAG, "Failed to select device");
        return error(errc::fail);
    }
    return {};
}

result<void> bus::try_skip_rom() {
    std::scoped_lock lock(_mux);
    if (!onewire_skip_rom(_pin.idf_num())) {
        ESP_LOGD(TAG, "Failed to skip ROM");
        return error(errc::fail);
    }
    return {};
}

result<void> bus::try_write(uint8_t value) {
    std::scoped_lock lock(_mux);
    if (!onewire_write(_pin.idf_num(), value)) {
        ESP_LOGD(TAG, "Failed to write byte");
        return error(errc::fail);
    }
    return {};
}

result<void> bus::try_write(std::span<const uint8_t> data) {
    std::scoped_lock lock(_mux);
    if (!onewire_write_bytes(_pin.idf_num(), data.data(), data.size())) {
        ESP_LOGD(TAG, "Failed to write %zu bytes", data.size());
        return error(errc::fail);
    }
    return {};
}

result<uint8_t> bus::try_read() {
    std::scoped_lock lock(_mux);
    int value = onewire_read(_pin.idf_num());
    if (value < 0) {
        ESP_LOGD(TAG, "Failed to read byte");
        return error(errc::fail);
    }
    return static_cast<uint8_t>(value);
}

result<void> bus::try_read(std::span<uint8_t> buf) {
    std::scoped_lock lock(_mux);
    if (!onewire_read_bytes(_pin.idf_num(), buf.data(), buf.size())) {
        ESP_LOGD(TAG, "Failed to read %zu bytes", buf.size());
        return error(errc::fail);
    }
    return {};
}

result<void> bus::try_power() {
    std::scoped_lock lock(_mux);
    if (!onewire_power(_pin.idf_num())) {
        ESP_LOGD(TAG, "Failed to power bus");
        return error(errc::fail);
    }
    return {};
}

void bus::depower() noexcept {
    std::scoped_lock lock(_mux);
    onewire_depower(_pin.idf_num());
}

result<std::vector<address>> bus::try_search(size_t max_devices) {
    std::scoped_lock lock(_mux);
    onewire_search_t search;
    onewire_search_start(&search);

    std::vector<address> devices;
    devices.reserve(max_devices);
    for (size_t i = 0; i < max_devices; ++i) {
        onewire_addr_t addr = onewire_search_next(&search, _pin.idf_num());
        if (addr == ONEWIRE_NONE) {
            break;
        }
        devices.emplace_back(addr);
    }

    ESP_LOGD(TAG, "Found %zu device(s) on GPIO%d", devices.size(), _pin.num());
    return devices;
}

result<std::vector<address>> bus::try_search(uint8_t family_code, size_t max_devices) {
    std::scoped_lock lock(_mux);
    onewire_search_t search;
    onewire_search_prefix(&search, family_code);

    std::vector<address> devices;
    devices.reserve(max_devices);
    for (size_t i = 0; i < max_devices; ++i) {
        onewire_addr_t addr = onewire_search_next(&search, _pin.idf_num());
        if (addr == ONEWIRE_NONE) {
            break;
        }
        devices.emplace_back(addr);
    }

    ESP_LOGD(TAG, "Found %zu device(s) on GPIO%d", devices.size(), _pin.num());
    return devices;
}

} // namespace idfxx::onewire
