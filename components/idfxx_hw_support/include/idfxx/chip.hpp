// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

/**
 * @headerfile <idfxx/chip>
 * @file chip.hpp
 * @brief Chip identification and feature detection.
 *
 * @addtogroup idfxx_hw_support
 * @{
 * @defgroup idfxx_hw_support_chip Chip Info
 * @ingroup idfxx_hw_support
 * @brief Chip model, revision, and hardware feature detection.
 * @{
 */

#include <idfxx/flags>

#include <cstdint>
#include <esp_chip_info.h>
#include <string>

namespace idfxx {

/**
 * @headerfile <idfxx/chip>
 * @brief Identifies the ESP chip model.
 */
enum class chip_model : int {
    // clang-format off
    esp32   = 1,  ///< ESP32
    esp32s2 = 2,  ///< ESP32-S2
    esp32s3 = 9,  ///< ESP32-S3
    esp32c3 = 5,  ///< ESP32-C3
    esp32c2 = 12, ///< ESP32-C2
    esp32c6 = 13, ///< ESP32-C6
    esp32h2 = 16, ///< ESP32-H2
    esp32p4 = 18, ///< ESP32-P4
    esp32c5 = 23, ///< ESP32-C5
    esp32c61 = 20, ///< ESP32-C61
    // clang-format on
};

/**
 * @brief Returns a string representation of a chip model.
 *
 * @param m The chip model to convert.
 * @return A descriptive string, e.g. "ESP32-S3", or "unknown(N)" for unrecognized values.
 */
[[nodiscard]] std::string to_string(chip_model m);

/**
 * @headerfile <idfxx/chip>
 * @brief Hardware feature flags for chip capabilities.
 *
 * Use with `idfxx::flags<chip_feature>` and bitwise operators.
 *
 * @code
 * auto info = idfxx::chip_info::get();
 * if (info.features().contains(idfxx::chip_feature::wifi)) {
 *     // Chip has WiFi
 * }
 * @endcode
 */
enum class chip_feature : uint32_t {
    // clang-format off
    embedded_flash  = 1u << 0,  ///< Chip has embedded flash memory.
    wifi            = 1u << 1,  ///< Chip has 2.4GHz WiFi.
    ble             = 1u << 4,  ///< Chip has Bluetooth LE.
    bt_classic      = 1u << 5,  ///< Chip has Bluetooth Classic.
    ieee802154      = 1u << 6,  ///< Chip has IEEE 802.15.4 (Zigbee/Thread).
    embedded_psram  = 1u << 7,  ///< Chip has embedded PSRAM.
    // clang-format on
};

} // namespace idfxx

template<>
inline constexpr bool idfxx::enable_flags_operators<idfxx::chip_feature> = true;

namespace idfxx {

/**
 * @headerfile <idfxx/chip>
 * @brief Chip identification and hardware information.
 *
 * Provides model identification, silicon revision, core count,
 * and hardware feature detection.
 *
 * @code
 * auto info = idfxx::chip_info::get();
 * auto model = info.model();           // e.g. chip_model::esp32s3
 * auto cores = info.cores();           // e.g. 2
 * auto rev = info.revision();          // e.g. 100 (v1.0)
 * @endcode
 */
class chip_info {
public:
    /**
     * @brief Queries the chip and returns its information.
     *
     * @return A chip_info object populated with the current chip's data.
     */
    [[nodiscard]] static chip_info get() noexcept;

    /**
     * @brief Returns the chip model.
     *
     * @return The chip model identifier.
     */
    [[nodiscard]] chip_model model() const noexcept { return static_cast<chip_model>(_info.model); }

    /**
     * @brief Returns the chip's hardware feature flags.
     *
     * @return A flags set of chip_feature values.
     */
    [[nodiscard]] flags<chip_feature> features() const noexcept {
        return flags<chip_feature>::from_raw(_info.features);
    }

    /**
     * @brief Returns the number of CPU cores.
     *
     * @return The core count (e.g. 1 or 2).
     */
    [[nodiscard]] uint8_t cores() const noexcept { return _info.cores; }

    /**
     * @brief Returns the full chip revision number.
     *
     * The revision is encoded as major * 100 + minor. For example,
     * revision 100 means v1.0, and revision 301 means v3.1.
     *
     * @return The chip revision number.
     */
    [[nodiscard]] uint16_t revision() const noexcept { return _info.revision; }

    /**
     * @brief Returns the major part of the chip revision.
     *
     * @return The major revision number (revision / 100).
     */
    [[nodiscard]] uint8_t major_revision() const noexcept { return _info.revision / 100; }

    /**
     * @brief Returns the minor part of the chip revision.
     *
     * @return The minor revision number (revision % 100).
     */
    [[nodiscard]] uint8_t minor_revision() const noexcept { return _info.revision % 100; }

private:
    esp_chip_info_t _info{};
};

/** @} */ // end of idfxx_hw_support_chip
/** @} */ // end of idfxx_hw_support

} // namespace idfxx

#include "sdkconfig.h"
#ifdef CONFIG_IDFXX_STD_FORMAT
/** @cond INTERNAL */
#include <algorithm>
#include <format>
namespace std {
template<>
struct formatter<idfxx::chip_model> {
    constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }

    template<typename FormatContext>
    auto format(idfxx::chip_model m, FormatContext& ctx) const {
        auto s = to_string(m);
        return std::copy(s.begin(), s.end(), ctx.out());
    }
};
} // namespace std
/** @endcond */
#endif // CONFIG_IDFXX_STD_FORMAT
