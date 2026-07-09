// SPDX-License-Identifier: Apache-2.0

// Continuous LoRa receive — logs every received packet with RSSI and SNR.

#include <idfxx/gpio>
#include <idfxx/log>
#include <idfxx/radio/sx126x>
#include <idfxx/sched>
#include <idfxx/spi/master>

#include <array>
#include <chrono>
#include <span>
#include <string_view>

using namespace frequency_literals;
using namespace std::chrono_literals;

static constexpr idfxx::log::logger logger{"recv"};

// Pin assignments — change to match your board.
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
                .tcxo = idfxx::radio::sx126x::tcxo_config{.voltage_mv = 1800, .startup = 5ms},
            }
        );

        radio.set_frequency(915_MHz);
        radio.set_output_power(14);
        radio.set_lora_modulation({
            .sf = idfxx::radio::spreading_factor::sf9,
            .bw = idfxx::radio::bandwidth::bw_125,
            .cr = idfxx::radio::coding_rate::cr_4_5,
        });
        radio.set_lora_packet_params({});

        // Loop with blocking single-shot receive — simplest pattern.
        logger.info("Listening on 915 MHz");
        std::array<uint8_t, 256> buf{};
        while (true) {
            auto r = radio.try_receive(buf, 10s);
            if (!r) {
                if (r.error() == idfxx::errc::timeout) {
                    continue;
                }
                if (r.error() == idfxx::errc::invalid_crc) {
                    logger.warn("CRC error");
                    continue;
                }
                logger.error("receive failed: {}", r.error().message());
                continue;
            }
            std::string_view sv{reinterpret_cast<const char*>(buf.data()), r->length};
            logger.info("rx [{}B rssi={}dBm snr_q4={}]: {}", r->length, r->rssi_dbm, r->snr_db_q4, sv);
        }
    } catch (const std::system_error& e) {
        logger.error("radio error: {}", e.what());
    }

    while (true) {
        idfxx::delay(1h);
    }
}
