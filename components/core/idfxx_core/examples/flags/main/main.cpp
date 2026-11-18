// SPDX-License-Identifier: Apache-2.0

#include <idfxx/flags>
#include <idfxx/log>

#include <cstdint>

// Define a custom flag enum
enum class permissions : uint32_t {
    read = 1u << 0,
    write = 1u << 1,
    exec = 1u << 2,
    admin = 1u << 3,
};

// Opt in to idfxx::flags operators
template<>
inline constexpr bool idfxx::enable_flags_operators<permissions> = true;

static constexpr idfxx::log::logger logger{"example"};

extern "C" void app_main() {
    // Combining flags with operator|
    logger.info("=== Combining Flags ===");
    auto rw = permissions::read | permissions::write;
    logger.info("read | write = {}", rw);

    auto rwx = rw | permissions::exec;
    logger.info("read | write | exec = {}", rwx);

    // Testing flags with contains() and contains_any()
    logger.info("=== Testing Flags ===");
    logger.info("rwx contains read?    {}", rwx.contains(permissions::read));
    logger.info("rwx contains admin?   {}", rwx.contains(permissions::admin));
    logger.info("rwx contains r+w?     {}", rwx.contains(rw));
    logger.info("rw contains any exec? {}", rw.contains_any(permissions::exec));
    logger.info("rw contains any r|x?  {}", rw.contains_any(permissions::read | permissions::exec));

    // Empty check
    logger.info("=== Empty Check ===");
    idfxx::flags<permissions> empty;
    logger.info("default flags empty?  {}", empty.empty());
    logger.info("rwx empty?            {}", rwx.empty());

    // Set difference with operator-
    logger.info("=== Set Difference ===");
    auto ro = rwx - permissions::write - permissions::exec;
    logger.info("rwx - write - exec = {}", ro);
    logger.info("result contains read?  {}", ro.contains(permissions::read));
    logger.info("result contains write? {}", ro.contains(permissions::write));

    // to_underlying and to_string
    logger.info("=== Conversion ===");
    logger.info("to_underlying(rw):  {:#x}", idfxx::to_underlying(rw));
    logger.info("to_string(rw):      {}", idfxx::to_string(rw));
    logger.info("to_string(rwx):     {}", idfxx::to_string(rwx));

    // Using with std::format (via the formatter specialization)
    logger.info("=== Formatted Output ===");
    auto all = permissions::read | permissions::write | permissions::exec | permissions::admin;
    logger.info("All permissions: {}", all);
}
