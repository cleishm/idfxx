// SPDX-License-Identifier: Apache-2.0

// Reads a DHT22 (or DHT11 — change the model below) every few seconds and
// logs temperature and humidity.

#include <idfxx/dht>
#include <idfxx/log>
#include <idfxx/sched>

#include <chrono>

using namespace std::chrono_literals;

static constexpr idfxx::log::logger logger{"example"};

/// GPIO pin connected to the sensor's data line (change as needed). GPIO 1 is
/// the console UART's TX pin on the ESP32, so it uses GPIO 4 instead.
#if CONFIG_IDF_TARGET_ESP32
static constexpr auto sensor_pin = idfxx::gpio_4;
#else
static constexpr auto sensor_pin = idfxx::gpio_1;
#endif

extern "C" void app_main() {
    logger.info("=== DHT Sensor Example ===");

    idfxx::dht::sensor sensor({
        .pin = sensor_pin,
        .model = idfxx::dht::model::dht22,
    });

    logger.info("{} on {}", sensor.model(), sensor.pin());

    while (true) {
        // The sensor needs a fresh-measurement gap between reads.
        idfxx::delay(sensor.min_read_interval() + 500ms);

        auto r = sensor.try_read();
        if (!r) {
            logger.warn("read failed: {}", r.error().message());
            continue;
        }
        logger.info("temperature {:b.1f}, humidity {:.1f} %", r->temperature, r->humidity_pct);
    }
}
