// SPDX-License-Identifier: Apache-2.0

#include "boot_count.hpp"
#include "sensor.hpp"

#include <idfxx/log>
#include <idfxx/persistent_log>
#include <idfxx/sched>

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <print>

namespace pl = idfxx::persistent_log;
using namespace std::chrono_literals;

static constexpr idfxx::log::logger logger{"crash_report"};

// The number of sensor reads before the crash. The example reads the sensor once a second.
static constexpr int sensor_reads = 10;

// Prints what the log kept from earlier boots: each fault with the entries before it, then the
// log's newest entries.
static void print_report() {
    std::println("===== {} fault(s) kept =====", pl::fault_count());
    auto faults = pl::read_faults();
    for (const auto& f : faults) {
        std::println("{}", f);
        for (const auto& e : faults.context()) {
            std::println("{}", e);
        }
        std::println("-----");
    }

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

    // The log captures ESP-IDF's warnings and errors, and also this example's info lines. If a
    // panic caused this boot, start() records it here as a fault, with the lines that led up to
    // it. If a panic was already recorded, start() logs this one but does not record it. That
    // continues until a reset that is not caused by a fault.
    pl::start({
        .boot_count = boot,
        .capture = pl::capture_config{.tag_levels = {{logger.tag(), idfxx::log::level::info}}},
    });
    print_report();

    logger.info("started; reading the sensor once a second");
    // A sensor that stays down would fill the fault slots with copies of the same fault, one for
    // each failed read. So the example records the first failure with its repeats suppressed. The
    // repeats are only logged, and the suppression is lifted once the sensor answers again.
    for (int read = 0; read < sensor_reads; ++read) {
        if (sensor_answers(boot, read)) {
            logger.info("sensor read {}: ok", read);
            // The sensor answers again, so its next failure is recorded.
            pl::lift_suppression("sensor");
        } else {
            const bool recorded = pl::record_fault("sensor", "no reply", pl::suppress_repeats);
            if (recorded) {
                logger.warn("sensor read {} failed: recorded", read);
            } else {
                logger.warn("sensor read {} failed: a repeat, only logged", read);
            }
        }
        idfxx::delay(1s);
    }

    // The lines above are still in RTC memory, which survives the reset. The next boot keeps
    // them, and records the panic with them as its context (or, for a repeat, only logs it).
    logger.error("giving up");
    std::abort();
}
