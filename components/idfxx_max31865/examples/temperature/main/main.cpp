// SPDX-License-Identifier: Apache-2.0

// Continuous RTD temperature readout from a MAX31865, with fault reporting and
// periodic register verification.

#include <idfxx/gpio>
#include <idfxx/log>
#include <idfxx/max31865>
#include <idfxx/sched>
#include <idfxx/spi/master>

#include <chrono>

using namespace quanta::electro_literals;
using namespace quanta::thermo_literals;
using namespace std::chrono_literals;

static constexpr idfxx::log::logger logger{"rtd"};

// Pin assignments — change to match your board.
static constexpr auto PIN_MOSI = idfxx::gpio_11;
static constexpr auto PIN_MISO = idfxx::gpio_13;
static constexpr auto PIN_SCLK = idfxx::gpio_12;
static constexpr auto PIN_CS = idfxx::gpio_10;
static constexpr auto PIN_DRDY = idfxx::gpio_14;

extern "C" void app_main() {
    idfxx::spi::bus_config bus_cfg{};
    bus_cfg.mosi = PIN_MOSI;
    bus_cfg.miso = PIN_MISO;
    bus_cfg.sclk = PIN_SCLK;
    idfxx::spi::master_bus bus(idfxx::spi::host_device::spi2, idfxx::spi::dma_chan::ch_auto, bus_cfg);

    idfxx::max31865::device rtd(
        bus,
        {
            .cs = PIN_CS,
            .r0 = 1000_Ohm, // PT1000
            // Placeholder: set this to the reference resistor fitted on your board.
            .rref = 4300_Ohm,
            .wiring = idfxx::max31865::wiring::three_wire,
            // Match your local mains frequency (hz_60 in the Americas).
            .notch = idfxx::max31865::notch_filter::hz_50,
            // Placeholder: set this to R x C of the filter on your board's RTDIN inputs.
            .filter_time_constant = 100us,
            .drdy = PIN_DRDY,
        }
    );

    // Check the RTD and its wiring once at startup.
    if (auto faults = rtd.detect_faults(); faults) {
        logger.warn("Fault detection found: {}", faults);
        rtd.clear_faults();
    } else {
        logger.info("Fault detection found no faults");
    }

    // Latch a fault if a conversion reads below -20 °C or above 150 °C.
    rtd.set_fault_thresholds(-20_c, 150_c);

    rtd.start_continuous();

    auto next = std::chrono::steady_clock::now();
    for (int seconds = 1;; ++seconds) {
        next += 1s;
        idfxx::delay_until(next);

        auto sample = rtd.read();
        if (sample.fault) {
            logger.warn("Fault: {}", rtd.read_faults());
            rtd.clear_faults();
        } else {
            logger.info("{:b.2f}  ({:b.3f})", rtd.to_temperature(sample.code), rtd.to_resistance(sample.code));
        }

        // A device reset silently restores its defaults, so check the registers now and then.
        if (seconds % 10 == 0 && !rtd.verify()) {
            logger.warn("Registers changed; restoring configuration");
            rtd.restore();
        }
    }
}
