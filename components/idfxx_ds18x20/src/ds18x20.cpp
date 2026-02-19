// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#include <idfxx/ds18x20>

#include <cassert>
#include <cmath>
#include <ds18x20.h>
#include <esp_log.h>

namespace {
const char* TAG = "idfxx::ds18x20";
}

// Verify family enum values match C library constants
static_assert(std::to_underlying(idfxx::ds18x20::family::ds18s20) == DS18X20_FAMILY_DS18S20);
static_assert(std::to_underlying(idfxx::ds18x20::family::ds1822) == DS18X20_FAMILY_DS1822);
static_assert(std::to_underlying(idfxx::ds18x20::family::ds18b20) == DS18X20_FAMILY_DS18B20);
static_assert(std::to_underlying(idfxx::ds18x20::family::max31850) == DS18X20_FAMILY_MAX31850);

namespace idfxx::ds18x20 {

// -- device ------------------------------------------------------------------

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
device::device(idfxx::gpio pin, address addr)
    : _pin(pin)
    , _addr(addr) {
    if (!_pin.is_connected()) {
        throw std::system_error(make_error_code(errc::invalid_state), "GPIO pin is not connected");
    }
}
#endif

result<device> device::make(idfxx::gpio pin, address addr) {
    if (!pin.is_connected()) {
        ESP_LOGD(TAG, "Cannot create device: GPIO pin is not connected");
        return error(errc::invalid_state);
    }
    return device(pin, addr, validated{});
}

result<void> device::try_measure(bool wait) {
    return wrap(ds18x20_measure(_pin.idf_num(), _addr.raw(), wait));
}

result<thermo::millicelsius> device::try_read_temperature() const {
    float temp;
    return wrap(ds18x20_read_temperature(_pin.idf_num(), _addr.raw(), &temp)).transform([&]() {
        return thermo::millicelsius(static_cast<int64_t>(std::lround(temp * 1000.0f)));
    });
}

result<thermo::millicelsius> device::try_measure_and_read() {
    float temp;
    return wrap(ds18x20_measure_and_read(_pin.idf_num(), _addr.raw(), &temp)).transform([&]() {
        return thermo::millicelsius(static_cast<int64_t>(std::lround(temp * 1000.0f)));
    });
}

result<void> device::try_set_resolution(resolution res) {
    return try_read_scratchpad().and_then([&](auto scratchpad) -> result<void> {
        std::array<uint8_t, 3> data = {scratchpad[2], scratchpad[3], static_cast<uint8_t>(res)};
        return try_write_scratchpad(data);
    });
}

result<resolution> device::try_get_resolution() const {
    return try_read_scratchpad().transform([](auto scratchpad) {
        return static_cast<resolution>((scratchpad[4] & 0x60) | 0x1F);
    });
}

result<std::array<uint8_t, 9>> device::try_read_scratchpad() const {
    std::array<uint8_t, 9> buf{};
    return wrap(ds18x20_read_scratchpad(_pin.idf_num(), _addr.raw(), buf.data())).transform([&]() { return buf; });
}

result<void> device::try_write_scratchpad(std::span<const uint8_t, 3> data) {
    // C API takes non-const pointer but only reads the data
    return wrap(ds18x20_write_scratchpad(_pin.idf_num(), _addr.raw(), const_cast<uint8_t*>(data.data())));
}

result<void> device::try_copy_scratchpad() {
    return wrap(ds18x20_copy_scratchpad(_pin.idf_num(), _addr.raw()));
}

// -- Free functions ----------------------------------------------------------

result<std::vector<device>> try_scan_devices(idfxx::gpio pin, size_t max_devices) {
    if (!pin.is_connected()) {
        ESP_LOGD(TAG, "Cannot scan devices: GPIO pin is not connected");
        return error(errc::invalid_state);
    }

    std::vector<onewire_addr_t> addrs(max_devices);
    size_t found = 0;
    return wrap(ds18x20_scan_devices(pin.idf_num(), addrs.data(), max_devices, &found)).transform([&]() {
        std::vector<device> devices;
        devices.reserve(found);
        for (size_t i = 0; i < found; ++i) {
            devices.push_back(device(pin, address{addrs[i]}, device::validated{}));
        }
        return devices;
    });
}

result<std::vector<thermo::millicelsius>> try_measure_and_read_multi(std::span<const device> devices) {
    if (devices.empty()) {
        return std::vector<thermo::millicelsius>{};
    }

    std::vector<const device*> device_ptrs;
    device_ptrs.reserve(devices.size());
    for (const auto& d : devices) {
        device_ptrs.push_back(&d);
    }
    std::vector<onewire_addr_t> addrs;
    std::vector<thermo::millicelsius> temps(devices.size());

    for (size_t i = 0; i < device_ptrs.size(); ++i) {
        if (device_ptrs[i] == nullptr) {
            continue;
        }

        auto pin = device_ptrs[i]->pin();
        addrs.clear();
        addrs.push_back(device_ptrs[i]->addr().raw());

        for (size_t j = i + 1; j < device_ptrs.size(); ++j) {
            if (device_ptrs[j] == nullptr) {
                continue;
            }

            if (device_ptrs[j]->pin() == pin) {
                addrs.push_back(device_ptrs[j]->addr().raw());
            }
        }

        std::vector<float> raw_temps(addrs.size());
        auto result = wrap(ds18x20_measure_and_read_multi(pin.idf_num(), addrs.data(), addrs.size(), raw_temps.data()));
        if (!result) {
            return error(result.error());
        }

        std::vector<float>::iterator raw_temp = raw_temps.begin();

        for (size_t j = i; j < device_ptrs.size(); ++j) {
            if (device_ptrs[j] == nullptr) {
                continue;
            }

            if (device_ptrs[j]->pin() == pin) {
                temps[j] = thermo::millicelsius(static_cast<int64_t>(std::lround(*raw_temp * 1000.0f)));
                ++raw_temp;
                device_ptrs[j] = nullptr; // Mark as processed
            }
        }

        assert(raw_temp == raw_temps.end()); // Sanity check that we processed all temperatures
    }

    return temps;
}

} // namespace idfxx::ds18x20
