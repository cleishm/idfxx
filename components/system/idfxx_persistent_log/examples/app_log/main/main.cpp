// SPDX-License-Identifier: Apache-2.0

#include "boot_count.hpp"

#include <idfxx/log>
#include <idfxx/persistent_log>

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <print>

namespace pl = idfxx::persistent_log;

static constexpr idfxx::log::logger logger{"app_log"};

// A reading above this temperature, in degrees Celsius, is logged as a warning.
static constexpr int temperature_limit = 30;

// Prints the newest entries in the log. These are the entries from earlier boots, and the entry
// that start() added with the reason for this boot's reset. read_newest() writes the lines that
// were still in RTC memory at the reset to flash first, so the newest entries include them.
static void print_stored_log() {
    std::println("===== the log's newest entries =====");
    for (const auto& e : pl::read_newest(20)) {
        std::println("{}", e);
    }
    std::println("=====");
}

extern "C" void app_main() {
    // Each line in the log has a stamp with the boot count, which shows the boot that logged the
    // line. The app keeps the count. This example keeps it in NVS.
    const uint32_t boot = boot_count::next();

    // By default, the log stores only warnings and errors. This example sets the capture level to
    // info, so that the log also stores the example's info lines.
    //
    // In this configuration, the log captures only the lines that are logged through idfxx::log.
    // The lines that ESP-IDF and other libraries log with ESP_LOGx are not stored. With
    // CONFIG_IDFXX_PERSISTENT_LOG_CAPTURE_ALL_LINES enabled, the log also stores those lines.
    //
    // The example records no faults, so it starts the log without fault slots (std::nullopt). The
    // log then uses the whole `log` partition for its lines.
    pl::start({
        .boot_count = boot,
        .capture = pl::capture_config{.level = idfxx::log::level::info},
        .faults = std::nullopt,
    });
    print_stored_log();

    // The log stores these lines, and the console also prints them.
    logger.info("started on boot {}", boot);
    static constexpr std::array readings{21, 22, 34, 23};
    for (int temperature : readings) {
        if (temperature > temperature_limit) {
            logger.warn(
                "temperature {} C is above the limit of {} C", temperature, temperature_limit
            );
        } else {
            logger.info("temperature {} C", temperature);
        }
    }

    // append() formats a line and adds it to the log without printing it on the console. An app
    // uses it for the records that it keeps for itself, such as this summary of the readings.
    const int highest = std::ranges::max(readings);
    pl::append("readings={} highest={} C", readings.size(), highest);

    // The writer task writes these lines to flash within 5 seconds. Lines that are still in RTC
    // memory at a reset are written to flash after the next start.
    std::println("Reset the board to see these lines in the stored log.");
}
