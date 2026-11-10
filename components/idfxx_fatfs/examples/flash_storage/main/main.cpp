// SPDX-License-Identifier: Apache-2.0

// Keeps a boot counter in a file on a FAT filesystem in the `storage` flash
// partition, so that it counts every reset and power cycle.

#include <idfxx/fatfs>
#include <idfxx/log>

#include <fstream>
#include <string>
#include <system_error>

static constexpr idfxx::log::logger logger{"flash_storage"};

extern "C" void app_main() {
    try {
        // Mount the `storage` partition (see partitions.csv) read-write at /data. A volume on flash
        // spreads its writes across the partition (wear leveling), so rewriting a file on every
        // boot doesn't wear out one spot. The partition is empty on the first boot, so it gets
        // formatted.
        idfxx::fatfs::volume vol("storage", "/data", {.format_if_missing = true});

        // Finish or discard a save that a reset interrupted, then read the count left by the
        // previous boot: none on the first boot, or after the partition is erased.
        vol.recover_file("/data/boots.txt");
        unsigned boots = 0;
        if (std::ifstream in("/data/boots.txt"); in) {
            in >> boots;
        }
        ++boots;

        // Save the new count so that a reset or power loss part-way through leaves either the old
        // count or the new.
        vol.replace_file("/data/boots.txt", std::to_string(boots) + "\n");

        logger.info("Boot number {}", boots);
        logger.info("{} KiB free of {} KiB", vol.free_size() / 1024, vol.size() / 1024);

        // Unmounting saves the wear-leveling state; the destructor would do it too, but could only
        // log an error.
        vol.unmount();
        logger.info("Press the reset button to count another boot");
    } catch (const std::system_error& e) {
        logger.error("Storage error: {}", e.what());
    }
}
