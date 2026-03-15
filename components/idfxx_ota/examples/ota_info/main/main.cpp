// SPDX-License-Identifier: Apache-2.0

#include <idfxx/log>
#include <idfxx/ota>

static constexpr idfxx::log::logger logger{"example"};

static const char* state_name(idfxx::ota::image_state s) {
    switch (s) {
    case idfxx::ota::image_state::new_image:
        return "new_image";
    case idfxx::ota::image_state::pending_verify:
        return "pending_verify";
    case idfxx::ota::image_state::valid:
        return "valid";
    case idfxx::ota::image_state::invalid:
        return "invalid";
    case idfxx::ota::image_state::aborted:
        return "aborted";
    case idfxx::ota::image_state::undefined:
        return "undefined";
    }
    return "unknown";
}

extern "C" void app_main() {
    logger.info("=== OTA Info Example ===");

    // --- Running partition ---
    logger.info("=== Running Partition ===");
    auto running = idfxx::ota::running_partition();
    logger.info("Label:   {}", running.label());
    logger.info("Address: 0x{:08X}", running.address());
    logger.info("Size:    {} bytes", running.size());

    // --- App description ---
    logger.info("=== App Description ===");
    auto desc = idfxx::ota::partition_description(running);
    logger.info("Version:      {}", desc.version());
    logger.info("Project name: {}", desc.project_name());
    logger.info("IDF version:  {}", desc.idf_ver());
    logger.info("Build date:   {}", desc.date());
    logger.info("Build time:   {}", desc.time());

    // --- Boot partition ---
    logger.info("=== Boot Partition ===");
    auto boot = idfxx::ota::boot_partition();
    logger.info("Label:   {}", boot.label());
    logger.info("Address: 0x{:08X}", boot.address());

    // --- Next update partition ---
    logger.info("=== Next Update Partition ===");
    auto next = idfxx::ota::next_update_partition();
    logger.info("Label:   {}", next.label());
    logger.info("Address: 0x{:08X}", next.address());
    logger.info("Size:    {} bytes", next.size());

    // --- OTA partition count ---
    logger.info("=== OTA Partitions ===");
    auto count = idfxx::ota::app_partition_count();
    logger.info("OTA app partition count: {}", count);

    // --- Partition states ---
    logger.info("=== Partition States ===");
    auto running_state = idfxx::ota::partition_state(running);
    logger.info("Running partition state: {}", state_name(running_state));

    auto next_state_result = idfxx::ota::try_partition_state(next);
    if (next_state_result) {
        logger.info("Next update partition state: {}", state_name(*next_state_result));
    } else {
        logger.info("Next update partition state: (no state recorded)");
    }

    // --- Rollback info ---
    logger.info("=== Rollback ===");
    logger.info("Rollback possible: {}", idfxx::ota::rollback_possible());

    auto last_invalid = idfxx::ota::try_last_invalid_partition();
    if (last_invalid) {
        logger.info("Last invalid partition: {}", last_invalid->label());
    } else {
        logger.info("No invalid partitions found");
    }

    logger.info("Done!");
}
