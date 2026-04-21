// SPDX-License-Identifier: Apache-2.0

#include <idfxx/app>
#include <idfxx/chrono>
#include <idfxx/log>
#include <idfxx/memory>
#include <idfxx/random>
#include <idfxx/system>

#include <random>

static constexpr idfxx::log::logger logger{"example"};

extern "C" void app_main() {
    namespace memory = idfxx::memory;

    // Application metadata
    logger.info("=== Application Info ===");
    logger.info("Project:      {}", idfxx::app::project_name());
    logger.info("Version:      {}", idfxx::app::version());
    logger.info("IDF version:  {}", idfxx::app::idf_version());
    logger.info("Compiled:     {} {}", idfxx::app::compile_date(), idfxx::app::compile_time());
    logger.info("ELF SHA256:   {}", idfxx::app::elf_sha256_hex());

    // System information
    logger.info("=== System Info ===");
    logger.info("Last reset reason: {}", idfxx::last_reset_reason());

    // Heap information
    logger.info("=== Heap Info ===");
    logger.info("Free heap:         {} bytes", memory::free_size(memory::capabilities::default_heap));
    logger.info("Min free heap:     {} bytes", memory::minimum_free_size(memory::capabilities::default_heap));
    logger.info("Free internal:     {} bytes", memory::free_size(memory::capabilities::internal));

    // Random number generation
    logger.info("=== Random Numbers ===");
    logger.info("random():          {:#010x}", idfxx::random());

    // Using random_device with std::uniform_int_distribution
    idfxx::random_device rng;
    std::uniform_int_distribution<int> dist(1, 100);
    logger.info("dice roll (1-100): {}", dist(rng));

    // Tick clock
    logger.info("=== Tick Clock ===");
    auto now = idfxx::chrono::tick_clock::now();
    auto ticks = now.time_since_epoch().count();
    logger.info("Current ticks:     {}", ticks);
}
