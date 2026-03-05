// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

/**
 * @headerfile <idfxx/app>
 * @file app.hpp
 * @brief Running application metadata.
 *
 * @addtogroup idfxx_core
 * @{
 * @defgroup idfxx_core_app Application Info
 * @ingroup idfxx_core
 * @brief Accessors for the running application's embedded metadata.
 *
 * Provides information about the currently running firmware image, including
 * version, project name, build timestamps, and ELF hash. All string accessors
 * return views into static storage and do not allocate.
 *
 * @code
 * #include <idfxx/app>
 *
 * auto ver = idfxx::app::version();       // e.g. "1.0.0"
 * auto name = idfxx::app::project_name(); // e.g. "my_project"
 * @endcode
 * @{
 */

#include <cstdint>
#include <string>
#include <string_view>

namespace idfxx::app {

/**
 * @brief Returns the application version string.
 *
 * @return The version string embedded in the firmware image.
 */
[[nodiscard]] std::string_view version();

/**
 * @brief Returns the project name.
 *
 * @return The project name embedded in the firmware image.
 */
[[nodiscard]] std::string_view project_name();

/**
 * @brief Returns the compile time string.
 *
 * @return The time the firmware was compiled, e.g. "12:34:56".
 */
[[nodiscard]] std::string_view compile_time();

/**
 * @brief Returns the compile date string.
 *
 * @return The date the firmware was compiled, e.g. "Jan  1 2026".
 */
[[nodiscard]] std::string_view compile_date();

/**
 * @brief Returns the ESP-IDF version string used to build the firmware.
 *
 * @return The ESP-IDF version string.
 */
[[nodiscard]] std::string_view idf_version();

/**
 * @brief Returns the secure version counter.
 *
 * @return The secure version value embedded in the firmware image.
 */
[[nodiscard]] uint32_t secure_version();

/**
 * @brief Returns the ELF SHA-256 hash as a hex string.
 *
 * @return A hex-encoded SHA-256 string (typically 64 characters).
 */
[[nodiscard]] std::string elf_sha256_hex();

/** @} */ // end of idfxx_core_app
/** @} */ // end of idfxx_core

} // namespace idfxx::app
