// SPDX-License-Identifier: Apache-2.0

// Periodic LoRa beacon — sends "beacon NNNN" every two seconds.

#include <idfxx/gpio>
#include <idfxx/log>
#include <idfxx/radio/sx126x>
#include <idfxx/sched>
#include <idfxx/spi/master>

#include <array>
#include <chrono>
#include <cinttypes>
#include <cstdio>

using namespace electro_literals;
using namespace frequency_literals;
using namespace std::chrono_literals;

static constexpr idfxx::log::logger logger{"beacon"};

// Pin assignments — change to match your board (these match common Heltec
// WiFi LoRa 32 V3 / LilyGo T3 layouts).
static constexpr auto PIN_MOSI = idfxx::gpio_10;
static constexpr auto PIN_MISO = idfxx::gpio_11;
static constexpr auto PIN_SCLK = idfxx::gpio_9;
static constexpr auto PIN_CS = idfxx::gpio_8;
static constexpr auto PIN_BUSY = idfxx::gpio_13;
static constexpr auto PIN_DIO1 = idfxx::gpio_14;
static constexpr auto PIN_NRESET = idfxx::gpio_12;

extern "C" void app_main() {
    try {
        idfxx::gpio::install_isr_service();

        idfxx::spi::bus_config bus_cfg{};
        bus_cfg.mosi = PIN_MOSI;
        bus_cfg.miso = PIN_MISO;
        bus_cfg.sclk = PIN_SCLK;
        idfxx::spi::master_bus bus(idfxx::spi::host_device::spi2, idfxx::spi::dma_chan::ch_auto, bus_cfg);

        idfxx::radio::sx126x radio(
            bus,
            {
                .variant = idfxx::radio::sx126x::chip_variant::sx1262,
                .cs = PIN_CS,
                .busy = PIN_BUSY,
                .dio1 = PIN_DIO1,
                .nreset = PIN_NRESET,
                // Heltec V3 / LilyGo T3 drive a TCXO from DIO3 — required for the
                // chip to lock. Verify the voltage for your board.
                .tcxo = idfxx::radio::sx126x::tcxo_config{.voltage = 1800_mV, .startup = 5ms},
            }
        );

        radio.configure({
            .frequency = 915_MHz,
            .output_power = 14_dBm,
            .modulation = {
                .sf = idfxx::radio::spreading_factor::sf9,
                .bw = idfxx::radio::bandwidth::bw_125,
                .cr = idfxx::radio::coding_rate::cr_4_5,
            },
        });

        logger.info("Beacon starting on 915 MHz, SF9, 125 kHz BW");

        uint32_t counter = 0;
        std::array<char, 32> payload{};
        while (true) {
            int n = std::snprintf(payload.data(), payload.size(), "beacon %04" PRIu32, counter++);
            if (n > 0) {
                // transmit sizes its own timeout from the packet's air-time.
                radio.transmit(std::span{reinterpret_cast<const uint8_t*>(payload.data()), static_cast<size_t>(n)});
                logger.info("sent: {}", std::string_view{payload.data(), static_cast<size_t>(n)});
            }
            idfxx::delay(2s);
        }
    } catch (const std::system_error& e) {
        logger.error("radio error: {}", e.what());
    }

    while (true) {
        idfxx::delay(1h);
    }
}
