// SPDX-License-Identifier: Apache-2.0

// Reads a DHT22 (or DHT11 — change the model below) every few seconds and
// logs temperature and humidity.

#include <idfxx/dht>
#include <idfxx/log>
#include <idfxx/sched>

#include <chrono>
#include <thermo/thermo>

using namespace std::chrono_literals;

static constexpr idfxx::log::logger logger{"example"};

/// GPIO pin connected to the sensor's data line (change as needed).
static constexpr auto sensor_pin = idfxx::gpio_1;

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
        logger.info(
            "temperature {:.1f}, humidity {:.1f} %",
            thermo::temperature_cast<thermo::celsius_real>(r->temperature),
            r->humidity_pct
        );
    }
}
