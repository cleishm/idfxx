// SPDX-License-Identifier: Apache-2.0

// Initializes an SD card over SPI, prints what the card reports about itself,
// then reads its first sector and prints the start of it.

#include <idfxx/gpio>
#include <idfxx/log>
#include <idfxx/sdcard>
#include <idfxx/spi/master>

#include <chrono>
#include <cstdint>
#include <span>
#include <system_error>
#include <vector>

using namespace quanta::frequency_literals;

static constexpr idfxx::log::logger logger{"card_info"};

// SD card wiring: the ESP32-S3's SPI2 IOMUX pins.
static constexpr auto PIN_MOSI = idfxx::gpio_11;
static constexpr auto PIN_SCLK = idfxx::gpio_12;
static constexpr auto PIN_MISO = idfxx::gpio_13;
static constexpr auto PIN_CS = idfxx::gpio_10;
// Card detect: a slot with a card-detect switch can wire it here (active low: low when a card is
// in).
static constexpr auto PIN_DETECT = idfxx::gpio::nc();

extern "C" void app_main() {
    try {
        // The card needs a bus with DMA.
        idfxx::spi::bus_config bus_cfg{};
        bus_cfg.mosi = PIN_MOSI;
        bus_cfg.miso = PIN_MISO;
        bus_cfg.sclk = PIN_SCLK;
        idfxx::spi::master_bus bus(
            idfxx::spi::host_device::spi2, idfxx::spi::dma_chan::ch_auto, bus_cfg
        );

        // Adds the card's own device to the bus and initializes the card.
        idfxx::sdcard::card card(
            bus,
            {
                .cs = PIN_CS,
                .card_detect = PIN_DETECT,
                .max_frequency = 20_MHz,
            }
        );

        // --- Type and identity ---
        logger.info("Type:     {}", card.type());
        auto id = card.id();
        logger.info(
            "Product:  {} (revision {}.{})",
            id.product_name(),
            id.revision() >> 4,
            id.revision() & 0xF
        );
        logger.info(
            "Maker:    manufacturer 0x{:02x}, OEM 0x{:04x}", id.manufacturer_id(), id.oem_id()
        );
        logger.info("Serial:   0x{:08x}", id.serial_number());
        if (auto date = id.manufacture_date()) {
            logger.info(
                "Made:     {}-{:02}",
                static_cast<int>(date->year()),
                static_cast<unsigned>(date->month())
            );
        }

        // --- Geometry and clock ---
        logger.info(
            "Capacity: {} sectors of {} bytes ({} MiB)",
            card.sector_count(),
            card.sector_size(),
            card.size() >> 20
        );
        logger.info("Bus:      {}, {} kHz", card.width(), card.frequency().count() / 1000);

        // --- First sector ---
        // On a formatted card this is the partition table (MBR) or the filesystem's boot sector;
        // both end in the signature 55 aa.
        std::vector<uint8_t> sector(card.sector_size());
        card.read_sectors(0, sector);
        logger.info("Sector 0:");
        logger.buffer_hex_dump(idfxx::log::level::info, std::span(sector).first(64));
        logger.info("Signature: {:02x} {:02x}", sector[510], sector[511]);
    } catch (const std::system_error& e) {
        // errc::not_found when a card-detect pin is wired and reports an empty slot.
        logger.error("SD card error: {}", e.what());
    }
}
