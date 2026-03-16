// SPDX-License-Identifier: Apache-2.0

#include <idfxx/ds18x20>
#include <idfxx/log>

#include <cstdlib>

using idfxx::ds18x20::device;
using idfxx::ds18x20::family;
using idfxx::ds18x20::resolution;

static constexpr idfxx::log::logger logger{"example"};

/// GPIO pin connected to the 1-Wire data bus (change as needed)
static constexpr auto sensor_pin = idfxx::gpio_4;

extern "C" void app_main() {
    logger.info("=== DS18x20 Temperature Sensor Example ===");
    logger.info("1-Wire bus on GPIO {}", sensor_pin);

    // --- Scan for devices ---
    logger.info("Scanning for DS18x20 sensors...");
    auto devices = idfxx::ds18x20::scan_devices(sensor_pin);

    if (devices.empty()) {
        logger.warn("No DS18x20 sensors found!");
        return;
    }

    logger.info("Found {} sensor(s):", devices.size());
    for (size_t i = 0; i < devices.size(); ++i) {
        auto fam = static_cast<family>(devices[i].addr().family());
        logger.info("  [{}] address={}, family={}", i, devices[i].addr(), fam);
    }

    // --- Read temperature from each sensor ---
    logger.info("=== Individual Readings ===");
    for (size_t i = 0; i < devices.size(); ++i) {
        auto temp = devices[i].measure_and_read();
        logger.info("  Sensor [{}]: {}", i, temp);
    }

    // --- Batch read ---
    logger.info("=== Batch Reading ===");
    auto temps = idfxx::ds18x20::measure_and_read_multi(devices);
    for (size_t i = 0; i < temps.size(); ++i) {
        logger.info("  Sensor [{}]: {}", i, temps[i]);
    }

    // --- Change resolution (DS18B20 only) ---
    logger.info("=== Resolution Change ===");
    for (size_t i = 0; i < devices.size(); ++i) {
        auto fam = static_cast<family>(devices[i].addr().family());
        if (fam != family::ds18b20) {
            logger.info("  Sensor [{}]: skipping (not DS18B20, family={})", i, fam);
            continue;
        }

        auto current_res = devices[i].get_resolution();
        logger.info("  Sensor [{}]: current resolution={}", i, current_res);

        devices[i].set_resolution(resolution::bits_10);
        logger.info("  Sensor [{}]: changed to 10-bit resolution", i);

        auto temp = devices[i].measure_and_read();
        logger.info("  Sensor [{}]: temperature at 10-bit: {}", i, temp);

        devices[i].set_resolution(resolution::bits_12);
        logger.info("  Sensor [{}]: restored to 12-bit resolution", i);
    }

    logger.info("Done!");
}
