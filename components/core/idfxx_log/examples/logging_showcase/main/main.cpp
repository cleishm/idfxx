// SPDX-License-Identifier: Apache-2.0

#include <idfxx/log>

#include <array>
#include <cstdint>
#include <numeric>

static constexpr idfxx::log::logger logger{"example"};

extern "C" void app_main() {
    // --- Logger basics at each level ---
    logger.info("=== Logger Basics ===");
    logger.error("This is an error message");
    logger.warn("This is a warning message");
    logger.info("This is an info message");
    logger.debug("This is a debug message");
    logger.verbose("This is a verbose message");

    // --- Format arguments ---
    logger.info("=== Format Arguments ===");
    logger.info("Integer: {}, Float: {:.2f}, String: {}", 42, 3.14159, "hello");
    logger.info("Hex: {:#x}, Bool: {}, Char: {}", 255, true, 'A');

    // --- Free function API ---
    logger.info("=== Free Function API ===");
    idfxx::log::error("free_func", "Error via free function");
    idfxx::log::warn("free_func", "Warning with arg: {}", 99);
    idfxx::log::info("free_func", "Info via free function");
    idfxx::log::debug("free_func", "Debug via free function");
    idfxx::log::verbose("free_func", "Verbose via free function");

    // --- Per-tag level control ---
    logger.info("=== Per-Tag Level Control ===");
    constexpr idfxx::log::logger filtered{"filtered"};
    filtered.info("Before filter: this should appear");
    filtered.debug("Before filter: this debug should appear");

    idfxx::log::set_level("filtered", idfxx::log::level::error);
    filtered.info("After set to error: this should NOT appear");
    filtered.error("After set to error: this error SHOULD appear");

    idfxx::log::set_level("filtered", idfxx::log::level::verbose);
    filtered.info("After reset: info visible again");

    // --- Default level control ---
    logger.info("=== Default Level Control ===");
    idfxx::log::set_default_level(idfxx::log::level::warn);
    constexpr idfxx::log::logger new_tag{"new_tag"};
    new_tag.info("This info should NOT appear (default=warn)");
    new_tag.warn("This warn SHOULD appear (default=warn)");
    idfxx::log::set_default_level(idfxx::log::level::verbose);

    // --- Level to_string and formatting ---
    logger.info("=== Level to_string / Formatting ===");
    logger.info("to_string(error): {}", idfxx::to_string(idfxx::log::level::error));
    logger.info("to_string(info):  {}", idfxx::to_string(idfxx::log::level::info));
    logger.info("Formatted level:  {}", idfxx::log::level::debug);

    // --- Buffer logging ---
    logger.info("=== Buffer Logging ===");
    std::array<uint8_t, 16> buf{};
    std::iota(buf.begin(), buf.end(), static_cast<uint8_t>(0));

    logger.info("buffer_hex:");
    logger.buffer_hex(idfxx::log::level::info, buf);

    logger.info("buffer_char:");
    std::array<uint8_t, 16> ascii_buf{};
    for (int i = 0; i < 16; ++i) {
        ascii_buf[i] = static_cast<uint8_t>('A' + i);
    }
    logger.buffer_char(idfxx::log::level::info, ascii_buf);

    logger.info("buffer_hex_dump:");
    logger.buffer_hex_dump(idfxx::log::level::info, buf);

    logger.info("Done!");
}
