// SPDX-License-Identifier: Apache-2.0

// Mounts the FAT filesystem on an SD card connected over SPI, saves a file
// safely, reads it back and reports the free space, then unmounts it.

#include <idfxx/fatfs>
#include <idfxx/gpio>
#include <idfxx/log>
#include <idfxx/sdcard>
#include <idfxx/spi/master>

#include <cstdio>
#include <fstream>
#include <string>
#include <system_error>

using namespace quanta::frequency_literals;

static constexpr idfxx::log::logger logger{"sd_card"};

// SD card wiring: the ESP32-S3's SPI2 IOMUX pins.
static constexpr auto PIN_MOSI = idfxx::gpio_11;
static constexpr auto PIN_SCLK = idfxx::gpio_12;
static constexpr auto PIN_MISO = idfxx::gpio_13;
static constexpr auto PIN_CS = idfxx::gpio_10;
// Card detect: a slot with a card-detect switch can wire it here (active low: low when a card is
// in).
static constexpr auto PIN_DETECT = idfxx::gpio::nc();

// The file the example saves.
static const std::string FILE_PATH = "/sdcard/hello.txt";

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

        idfxx::sdcard::card card(
            bus, {.cs = PIN_CS, .card_detect = PIN_DETECT, .max_frequency = 20_MHz}
        );

        // Mounts the card's FAT filesystem at /sdcard. A card without one (or with exFAT, which
        // isn't supported) is formatted, which erases it.
        idfxx::fatfs::volume vol(card, "/sdcard", {.format_if_missing = true});

        // The files are now ordinary files under /sdcard. First finish or discard any replace of
        // the file that a reset interrupted, then replace it, so that a reset at any point leaves
        // the old contents or the new.
        vol.recover_file(FILE_PATH);
        vol.replace_file(FILE_PATH, [](FILE* f) -> idfxx::result<void> {
            std::fputs("Hello from idfxx_fatfs!\n", f);
            return {};
        });

        {
            std::ifstream in(FILE_PATH);
            std::string line;
            std::getline(in, line);
            logger.info("hello.txt says: {}", line);
        }

        logger.info("{} MiB free of {} MiB", vol.free_size() >> 20, vol.size() >> 20);

        // With every file closed, unmount before the card goes away (the destructor would otherwise
        // do it at the end of this scope, but could only log an error).
        vol.unmount();
    } catch (const std::system_error& e) {
        // idfxx::errc::not_found when a card-detect pin is wired and reports an empty slot.
        logger.error("SD card error: {}", e.what());
    }
}
