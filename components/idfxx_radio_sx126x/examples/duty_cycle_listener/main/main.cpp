// SPDX-License-Identifier: Apache-2.0

// Low-power duty-cycled LoRa listener with listen-before-talk beaconing.
//
// Demonstrates the event-driven, asynchronous radio surface:
//   * an event_loop receives rx_done / cad_done,
//   * start_receive(rx_duty_cycle_for(...)) puts the radio in periodic
//     low-power listen with windows computed from the modulation,
//   * scan_channel (blocking CAD) gates each transmit (listen-before-talk),
//   * start_transmit fires a beacon without blocking and returns a future,
//     awaited under a watchdog sized from the packet's time-on-air.

#include <idfxx/event>
#include <idfxx/gpio>
#include <idfxx/log>
#include <idfxx/radio/airtime>
#include <idfxx/radio/duty_cycle>
#include <idfxx/radio/sx126x>
#include <idfxx/sched>
#include <idfxx/spi/master>

#include <array>
#include <chrono>
#include <cinttypes>
#include <cstdio>
#include <span>
#include <string_view>

using namespace frequency_literals;
using namespace std::chrono_literals;

static constexpr idfxx::log::logger logger{"dutycycle"};

// Pin assignments — change to match your board (Heltec WiFi LoRa 32 V3 /
// LilyGo T3 layouts).
static constexpr auto PIN_MOSI = idfxx::gpio_10;
static constexpr auto PIN_MISO = idfxx::gpio_11;
static constexpr auto PIN_SCLK = idfxx::gpio_9;
static constexpr auto PIN_CS = idfxx::gpio_8;
static constexpr auto PIN_BUSY = idfxx::gpio_13;
static constexpr auto PIN_DIO1 = idfxx::gpio_14;
static constexpr auto PIN_NRESET = idfxx::gpio_12;

// The SX126x restarts its TCXO on every wake from sleep; sleep windows must
// be long enough to be worth that transition.
static constexpr auto TCXO_STARTUP = 5ms;

// A long preamble (64 symbols ≈ 262 ms at SF9/BW125) is what lets the
// receiver sleep between listen windows: the radio only has to catch the
// tail of the preamble, so senders pay air-time to buy the listener power.
static constexpr idfxx::radio::lora_modulation MODULATION{
    .sf = idfxx::radio::spreading_factor::sf9,
    .bw = idfxx::radio::bandwidth::bw_125,
    .cr = idfxx::radio::coding_rate::cr_4_5,
};
static constexpr idfxx::radio::lora_packet_params PACKET_PARAMS{.preamble_length = 64};

// Listen/sleep windows guaranteed to catch any packet using the preamble
// above (~37 ms listen / ~197 ms sleep at SF9/BW125). nullopt would mean
// duty-cycling is infeasible, and start_receive falls back to continuous.
static constexpr auto DUTY_CYCLE =
    idfxx::radio::rx_duty_cycle_for(MODULATION, PACKET_PARAMS.preamble_length, 8, 1016us + TCXO_STARTUP);

// How long to stay in duty-cycled listen between beacon attempts.
static constexpr auto LISTEN_INTERVAL = 5s;

// Slack added to the beacon's time-on-air when sizing the transmit watchdog.
static constexpr auto TX_WATCHDOG_SLACK = 500ms;

extern "C" void app_main() {
    try {
        idfxx::gpio::install_isr_service();

        idfxx::spi::bus_config bus_cfg{};
        bus_cfg.mosi = PIN_MOSI;
        bus_cfg.miso = PIN_MISO;
        bus_cfg.sclk = PIN_SCLK;
        idfxx::spi::master_bus bus(idfxx::spi::host_device::spi2, idfxx::spi::dma_chan::ch_auto, bus_cfg);

        // The radio posts rx_done / cad_done on this loop's task.
        idfxx::event_loop loop({.name = "radio_evt", .stack_size = 4096, .priority = 5});

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
                .tcxo = idfxx::radio::sx126x::tcxo_config{.voltage_mv = 1800, .startup = TCXO_STARTUP},
                .loop = &loop,
            }
        );

        radio.set_frequency(915_MHz);
        radio.set_output_power(14);
        radio.set_lora_modulation(MODULATION);
        radio.set_lora_packet_params(PACKET_PARAMS);

        // ---- Event listeners (dispatched on the loop's task) ----
        std::array<uint8_t, 256> rx_buf{};
        auto rx_handle = loop.listener_add(idfxx::radio::rx_done, [&](const idfxx::radio::rx_info& info) {
            auto r = radio.try_read_received(rx_buf);
            if (r) {
                std::string_view sv{reinterpret_cast<const char*>(rx_buf.data()), r->length};
                logger.info("rx [{}B rssi={}dBm snr_q4={}]: {}", r->length, info.rssi_dbm, info.snr_db_q4, sv);
            }
        });
        auto cad_handle = loop.listener_add(idfxx::radio::cad_done, [](const idfxx::radio::cad_info& ci) {
            logger.info("cad_done: channel {}", ci.detected ? "busy" : "clear");
        });

        if (DUTY_CYCLE) {
            logger.info(
                "duty-cycle listener on 915 MHz: listen {}us / sleep {}us",
                DUTY_CYCLE->rx_period.count(),
                DUTY_CYCLE->sleep_period.count()
            );
        } else {
            logger.info("duty-cycling infeasible for this modulation — listening continuously on 915 MHz");
        }

        uint32_t counter = 0;
        std::array<char, 32> tx_buf{};
        while (true) {
            // Low-power periodic listen for a while; packets surface via rx_done.
            radio.start_receive(DUTY_CYCLE);
            idfxx::delay(LISTEN_INTERVAL);

            // Stop listening and sound out the channel before talking (LBT).
            radio.standby();
            if (radio.scan_channel().detected) {
                logger.info("channel busy — deferring beacon");
                continue;
            }

            // Channel clear: fire a beacon without blocking, then await its
            // future under an air-time-sized watchdog.
            int n = std::snprintf(tx_buf.data(), tx_buf.size(), "beacon %04" PRIu32, counter++);
            if (n <= 0) {
                continue;
            }
            const auto len = static_cast<size_t>(n);
            auto tx_view = std::span{reinterpret_cast<const uint8_t*>(tx_buf.data()), len};

            auto tx = radio.start_transmit(tx_view);
            logger.info("beacon tx started: {}", std::string_view{tx_buf.data(), len});

            const auto watchdog = std::chrono::duration_cast<std::chrono::milliseconds>(
                                      idfxx::radio::time_on_air(MODULATION, PACKET_PARAMS, len)
                                  ) +
                TX_WATCHDOG_SLACK;
            if (!tx.try_wait_for(watchdog)) {
                // The transmit never reported done — recover the driver from
                // its "busy" state so the next cycle can proceed.
                logger.warn("tx watchdog expired — recovering to standby");
                radio.standby();
            }
        }
    } catch (const std::system_error& e) {
        logger.error("radio error: {}", e.what());
    }

    while (true) {
        idfxx::delay(1h);
    }
}
