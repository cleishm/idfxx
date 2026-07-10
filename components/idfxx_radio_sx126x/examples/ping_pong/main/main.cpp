// SPDX-License-Identifier: Apache-2.0

// LoRa ping-pong — two boards flash identical firmware and bounce a counter
// back and forth. Whichever board sees its initial-receive window time out
// first becomes the initiator and sends seq=0.
//
// On every received packet:
//   * parse the counter,
//   * log it with the measured RSSI / SNR,
//   * increment and transmit it back.

#include <idfxx/gpio>
#include <idfxx/log>
#include <idfxx/radio/sx126x>
#include <idfxx/sched>
#include <idfxx/spi/master>

#include <array>
#include <chrono>
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <span>
#include <string_view>

using namespace electro_literals;
using namespace frequency_literals;
using namespace std::chrono_literals;

static constexpr idfxx::log::logger logger{"pingpong"};

// Pin assignments — change to match your board (Heltec WiFi LoRa 32 V3 /
// LilyGo T3 layouts).
static constexpr auto PIN_MOSI = idfxx::gpio_10;
static constexpr auto PIN_MISO = idfxx::gpio_11;
static constexpr auto PIN_SCLK = idfxx::gpio_9;
static constexpr auto PIN_CS = idfxx::gpio_8;
static constexpr auto PIN_BUSY = idfxx::gpio_13;
static constexpr auto PIN_DIO1 = idfxx::gpio_14;
static constexpr auto PIN_NRESET = idfxx::gpio_12;

// How long to wait for a peer packet before electing ourselves the initiator.
// A modest random jitter inside try_receive's poll loop is enough to break
// symmetry when both boards boot together.
static constexpr auto LISTEN_TIMEOUT = 5s;

// Maximum acceptable round-trip latency. If we send a packet and don't see a
// reply by then, restart the election.
static constexpr auto REPLY_TIMEOUT = 3s;

// LoRa configuration, identical on both ends of the link.
static constexpr idfxx::radio::lora_modulation MODULATION{
    .sf = idfxx::radio::spreading_factor::sf9,
    .bw = idfxx::radio::bandwidth::bw_125,
    .cr = idfxx::radio::coding_rate::cr_4_5,
};
static constexpr idfxx::radio::lora_packet_params PACKET_PARAMS{};

// Try to parse "PING NNNN" or "PONG NNNN"; return the counter or -1.
static int32_t parse_counter(std::string_view sv) {
    if (sv.size() < 5) {
        return -1;
    }
    uint32_t value = 0;
    for (size_t i = 5; i < sv.size(); ++i) {
        char c = sv[i];
        if (c < '0' || c > '9') {
            return -1;
        }
        value = value * 10 + static_cast<uint32_t>(c - '0');
    }
    return static_cast<int32_t>(value);
}

static size_t format_packet(std::span<char> out, const char* tag, uint32_t counter) {
    int n = std::snprintf(out.data(), out.size(), "%s %04" PRIu32, tag, counter);
    return n > 0 ? static_cast<size_t>(n) : 0;
}

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
            .modulation = MODULATION,
            .packet_params = PACKET_PARAMS,
        });

        std::array<uint8_t, 32> rx_buf{};
        std::array<char, 32> tx_buf{};

        logger.info("ping-pong starting on 915 MHz, SF9, 125 kHz BW");
        logger.info(
            "listening up to {}s for a peer before electing as initiator",
            std::chrono::duration_cast<std::chrono::seconds>(LISTEN_TIMEOUT).count()
        );

        bool initiator = false;
        uint32_t counter = 0;

        // ---- Election: listen briefly; whoever times out first sends seq=0.
        auto first = radio.try_receive(rx_buf, LISTEN_TIMEOUT);
        if (first) {
            std::string_view sv{reinterpret_cast<const char*>(rx_buf.data()), first->length};
            int32_t parsed = parse_counter(sv);
            if (parsed < 0) {
                logger.warn("election rx: unparseable [{}B]: {}", first->length, sv);
                counter = 0;
            } else {
                logger.info("election rx: {} (rssi={} snr={}) — we are the responder", sv, first->rssi, first->snr);
                counter = static_cast<uint32_t>(parsed) + 1;
            }
        } else if (first.error() == idfxx::errc::timeout) {
            logger.info("no peer heard — we are the initiator");
            initiator = true;
            counter = 0;
        } else {
            logger.error("election rx failed: {}", first.error().message());
            return;
        }

        // ---- Main bounce loop: TX, then RX, then TX, ...
        while (true) {
            const char* tag = initiator ? "PING" : "PONG";
            size_t n = format_packet(tx_buf, tag, counter);
            if (n == 0) {
                logger.error("format failed");
                return;
            }
            auto tx_view = std::span{reinterpret_cast<const uint8_t*>(tx_buf.data()), n};

            // try_transmit sizes its own timeout from the packet's air-time
            // under the configured link parameters.
            if (auto e = radio.try_transmit(tx_view); !e) {
                logger.error("tx failed: {}", e.error().message());
                idfxx::delay(500ms);
                continue;
            }
            logger.info("tx: {}", std::string_view{tx_buf.data(), n});

            auto r = radio.try_receive(rx_buf, REPLY_TIMEOUT);
            if (!r) {
                if (r.error() == idfxx::errc::timeout) {
                    logger.warn("no reply for seq {} — restarting election", counter);
                    // Restart the election: become a responder again and listen.
                    initiator = false;
                    counter = 0;
                    auto re = radio.try_receive(rx_buf, LISTEN_TIMEOUT);
                    if (!re) {
                        if (re.error() == idfxx::errc::timeout) {
                            logger.info("no peer heard — promoting to initiator");
                            initiator = true;
                            counter = 0;
                        } else {
                            logger.error("re-election rx failed: {}", re.error().message());
                            return;
                        }
                    } else {
                        std::string_view sv{reinterpret_cast<const char*>(rx_buf.data()), re->length};
                        int32_t parsed = parse_counter(sv);
                        counter = parsed < 0 ? 0 : static_cast<uint32_t>(parsed) + 1;
                        logger.info("re-elected as responder, resuming at {}", counter);
                    }
                    continue;
                }
                if (r.error() == idfxx::errc::invalid_crc) {
                    logger.warn("rx crc error");
                    continue;
                }
                logger.error("rx failed: {}", r.error().message());
                continue;
            }

            std::string_view sv{reinterpret_cast<const char*>(rx_buf.data()), r->length};
            int32_t parsed = parse_counter(sv);
            if (parsed < 0) {
                logger.warn("rx: unparseable [{}B rssi={} snr={}]: {}", r->length, r->rssi, r->snr, sv);
                continue;
            }
            logger.info("rx: {} (rssi={} snr={})", sv, r->rssi, r->snr);
            counter = static_cast<uint32_t>(parsed) + 1;
        }
    } catch (const std::system_error& e) {
        logger.error("radio error: {}", e.what());
    }

    while (true) {
        idfxx::delay(1h);
    }
}
