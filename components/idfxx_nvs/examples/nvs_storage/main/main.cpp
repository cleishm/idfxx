// SPDX-License-Identifier: Apache-2.0

#include <idfxx/log>
#include <idfxx/nvs>

#include <array>
#include <cstdint>
#include <vector>

using idfxx::nvs;

static constexpr idfxx::log::logger logger{"example"};

extern "C" void app_main() {
    // --- Initialize NVS flash ---
    logger.info("=== Initialize NVS Flash ===");
    nvs::flash::init();
    logger.info("NVS flash initialized");

    // --- Integer storage ---
    logger.info("=== Integer Storage ===");
    {
        nvs handle("demo_ns");
        logger.info("Opened namespace 'demo_ns' (writeable={})", handle.is_writeable());

        handle.set_value<uint32_t>("counter", 42);
        handle.set_value<int16_t>("temperature", -15);
        handle.set_value<uint8_t>("flags", 0xAB);
        handle.set_value<uint64_t>("timestamp", 1700000000000ULL);
        handle.commit();

        auto counter = handle.get_value<uint32_t>("counter");
        auto temperature = handle.get_value<int16_t>("temperature");
        auto flags = handle.get_value<uint8_t>("flags");
        auto timestamp = handle.get_value<uint64_t>("timestamp");
        logger.info("counter={}, temperature={}, flags=0x{:02X}, timestamp={}", counter, temperature, flags, timestamp);

        handle.erase_all();
        handle.commit();
    }

    // --- String storage ---
    logger.info("=== String Storage ===");
    {
        nvs handle("demo_ns");

        handle.set_string("device_name", "ESP32-Sensor");
        handle.set_string("wifi_ssid", "MyNetwork");
        handle.commit();

        auto device_name = handle.get_string("device_name");
        auto wifi_ssid = handle.get_string("wifi_ssid");
        logger.info("device_name='{}', wifi_ssid='{}'", device_name, wifi_ssid);

        handle.erase_all();
        handle.commit();
    }

    // --- Blob storage ---
    logger.info("=== Blob Storage ===");
    {
        nvs handle("demo_ns");

        std::array<uint8_t, 6> mac = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
        handle.set_blob("mac_addr", std::span<const uint8_t>(mac));
        handle.commit();

        auto stored_mac = handle.get_blob("mac_addr");
        logger.info(
            "MAC address: {:02X}:{:02X}:{:02X}:{:02X}:{:02X}:{:02X}",
            stored_mac[0],
            stored_mac[1],
            stored_mac[2],
            stored_mac[3],
            stored_mac[4],
            stored_mac[5]
        );

        handle.erase_all();
        handle.commit();
    }

    // --- Erase single key + error handling ---
    logger.info("=== Erase & Error Handling ===");
    {
        nvs handle("demo_ns");

        handle.set_value<uint32_t>("to_erase", 99);
        handle.commit();
        logger.info("Stored to_erase={}", handle.get_value<uint32_t>("to_erase"));

        handle.erase("to_erase");
        handle.commit();
        logger.info("Erased key 'to_erase'");

        try {
            handle.get_value<uint32_t>("to_erase");
        } catch (const std::system_error& e) {
            logger.info("Expected error reading erased key: {}", e.what());
        }

        handle.erase_all();
        handle.commit();
    }

    // --- Read-only handle ---
    logger.info("=== Read-Only Handle ===");
    {
        nvs writer("demo_ns");
        writer.set_value<uint32_t>("ro_test", 123);
        writer.commit();
    }
    {
        nvs reader("demo_ns", true);
        logger.info("Read-only handle: writeable={}", reader.is_writeable());
        auto val = reader.get_value<uint32_t>("ro_test");
        logger.info("Read ro_test={}", val);

        try {
            reader.set_value<uint32_t>("ro_test", 456);
        } catch (const std::system_error& e) {
            logger.info("Expected error writing to read-only handle: {}", e.what());
        }
    }
    {
        nvs cleanup("demo_ns");
        cleanup.erase_all();
        cleanup.commit();
    }

    // --- Deinitialize NVS flash ---
    logger.info("=== Deinitialize ===");
    nvs::flash::deinit();
    logger.info("NVS flash deinitialized. Done!");
}
