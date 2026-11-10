// SPDX-License-Identifier: Apache-2.0

// Mounts a read-only FAT image of the example's data/ directory, built at
// compile time and flashed with the app, then lists it and prints one of its files.

#include <idfxx/fatfs>
#include <idfxx/log>

#include <dirent.h>
#include <fstream>
#include <string>
#include <system_error>

static constexpr idfxx::log::logger logger{"flash_image"};

extern "C" void app_main() {
    try {
        // Mount the `assets` partition read-only at /assets. The FAT image in it comes from the
        // build: main/CMakeLists.txt packs the data/ directory into an image, and `idf.py flash`
        // writes that image to the partition along with this app.
        idfxx::fatfs::image assets("assets", "/assets");
        logger.info("Image of {} bytes mounted at {}", assets.size(), assets.mount_path());

        // List the top-level directory.
        if (DIR* dir = opendir("/assets")) {
            while (dirent* entry = readdir(dir)) {
                logger.info("  {}{}", entry->d_name, entry->d_type == DT_DIR ? "/" : "");
            }
            closedir(dir);
        }

        // Read a file, line by line.
        std::ifstream in("/assets/hello.txt");
        for (std::string line; std::getline(in, line);) {
            logger.info("hello.txt: {}", line);
        }

        // The image is read-only: opening a file for writing fails.
        if (!std::ofstream("/assets/new.txt")) {
            logger.info("As expected, /assets can't be written to");
        }
    } catch (const std::system_error& e) {
        logger.error("Image error: {}", e.what());
    }
}
