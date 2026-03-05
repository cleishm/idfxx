// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

/**
 * @headerfile <idfxx/mac>
 * @file mac.hpp
 * @brief MAC address types and operations.
 *
 * @addtogroup idfxx_hw_support
 * @{
 * @defgroup idfxx_hw_support_mac MAC Addresses
 * @ingroup idfxx_hw_support
 * @brief MAC address value type, reading, and configuration.
 * @{
 */

#include <idfxx/error>

#include <array>
#include <cstdint>
#include <esp_mac.h>
#include <soc/soc_caps.h>
#include <string>

namespace idfxx {

/**
 * @headerfile <idfxx/mac>
 * @brief Identifies which network interface a MAC address belongs to.
 */
enum class mac_type : int {
    // clang-format off
    wifi_sta       = 0, ///< WiFi station interface.
    wifi_softap    = 1, ///< WiFi soft-AP interface.
    bt             = 2, ///< Bluetooth interface.
    ethernet       = 3, ///< Ethernet interface.
#if SOC_IEEE802154_SUPPORTED
    ieee802154     = 4, ///< IEEE 802.15.4 interface.
#endif
    base           = 5, ///< Base (factory) MAC address.
    efuse_factory  = 6, ///< Factory MAC stored in eFuse.
    efuse_custom   = 7, ///< Custom MAC stored in eFuse.
#if SOC_IEEE802154_SUPPORTED
    efuse_ext      = 8, ///< Extended eFuse MAC for 802.15.4 interface.
#endif
    // clang-format on
};

/**
 * @headerfile <idfxx/mac>
 * @brief A MAC-48 (6-byte) hardware address.
 *
 * Fixed-size value type for storing and comparing MAC addresses.
 *
 * @code
 * auto mac = idfxx::base_mac_address();
 * auto s = idfxx::to_string(mac); // "AA:BB:CC:DD:EE:FF"
 * @endcode
 */
class mac_address {
public:
    /**
     * @brief Constructs a zero-initialized MAC address (00:00:00:00:00:00).
     */
    constexpr mac_address() noexcept = default;

    /**
     * @brief Constructs a MAC address from a byte array.
     *
     * @param bytes The 6-byte MAC address.
     */
    constexpr explicit mac_address(std::array<uint8_t, 6> bytes) noexcept
        : _bytes(bytes) {}

    /**
     * @brief Constructs a MAC address from individual bytes.
     *
     * @param b0 First byte (most significant).
     * @param b1 Second byte.
     * @param b2 Third byte.
     * @param b3 Fourth byte.
     * @param b4 Fifth byte.
     * @param b5 Sixth byte (least significant).
     */
    constexpr mac_address(uint8_t b0, uint8_t b1, uint8_t b2, uint8_t b3, uint8_t b4, uint8_t b5) noexcept
        : _bytes{b0, b1, b2, b3, b4, b5} {}

    /**
     * @brief Returns a reference to the underlying byte array.
     *
     * @return The 6-byte array.
     */
    [[nodiscard]] constexpr const std::array<uint8_t, 6>& bytes() const noexcept { return _bytes; }

    /**
     * @brief Returns a pointer to the raw byte data.
     *
     * @return Pointer to the first byte.
     */
    [[nodiscard]] constexpr const uint8_t* data() const noexcept { return _bytes.data(); }

    /**
     * @brief Returns a mutable pointer to the raw byte data.
     *
     * @return Pointer to the first byte.
     */
    [[nodiscard]] constexpr uint8_t* data() noexcept { return _bytes.data(); }

    /**
     * @brief Accesses a byte by index.
     *
     * @param i Byte index (0-5).
     * @return The byte at the given index.
     */
    [[nodiscard]] constexpr uint8_t operator[](std::size_t i) const noexcept { return _bytes[i]; }

    /**
     * @brief Equality comparison.
     *
     * @return True if both MAC addresses have the same bytes.
     */
    [[nodiscard]] constexpr bool operator==(const mac_address&) const noexcept = default;

private:
    std::array<uint8_t, 6> _bytes{};
};

/**
 * @brief Returns a colon-separated string representation of a MAC address.
 *
 * @param addr The MAC address to convert.
 * @return A string in "AA:BB:CC:DD:EE:FF" format.
 */
[[nodiscard]] std::string to_string(const mac_address& addr);

// =============================================================================
// MAC address reading
// =============================================================================

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
/**
 * @brief Reads the MAC address for a specific interface.
 *
 * @param type The interface type to read.
 *
 * @return The MAC address.
 *
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
 * @throws std::system_error on failure.
 */
[[nodiscard]] mac_address read_mac(mac_type type);

/**
 * @brief Returns the base MAC address.
 *
 * The base MAC address is used to derive per-interface MAC addresses.
 *
 * @return The base MAC address.
 *
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
 * @throws std::system_error on failure.
 */
[[nodiscard]] mac_address base_mac_address();
#endif

/**
 * @brief Reads the MAC address for a specific interface.
 *
 * @param type The interface type to read.
 *
 * @return The MAC address, or an error.
 * @retval idfxx::errc::invalid_arg if the type is not valid.
 */
[[nodiscard]] result<mac_address> try_read_mac(mac_type type);

/**
 * @brief Returns the base MAC address.
 *
 * The base MAC address is used to derive per-interface MAC addresses.
 *
 * @return The base MAC address, or an error.
 */
[[nodiscard]] result<mac_address> try_base_mac_address();

// =============================================================================
// MAC address configuration
// =============================================================================

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
/**
 * @brief Sets the base MAC address.
 *
 * The base MAC address is used to derive per-interface MAC addresses.
 * This overrides the factory-programmed address until the next reboot.
 *
 * @param addr The new base MAC address.
 *
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
 * @throws std::system_error on failure.
 */
void set_base_mac_address(const mac_address& addr);

/**
 * @brief Sets the MAC address for a specific interface.
 *
 * @param addr The MAC address to set.
 * @param type The interface type.
 *
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
 * @throws std::system_error on failure.
 */
void set_interface_mac_address(const mac_address& addr, mac_type type);
#endif

/**
 * @brief Sets the base MAC address.
 *
 * The base MAC address is used to derive per-interface MAC addresses.
 * This overrides the factory-programmed address until the next reboot.
 *
 * @param addr The new base MAC address.
 *
 * @return Success, or an error.
 * @retval idfxx::errc::invalid_arg if the address is invalid.
 */
[[nodiscard]] result<void> try_set_base_mac_address(const mac_address& addr);

/**
 * @brief Sets the MAC address for a specific interface.
 *
 * @param addr The MAC address to set.
 * @param type The interface type.
 *
 * @return Success, or an error.
 * @retval idfxx::errc::invalid_arg if the address or type is invalid.
 */
[[nodiscard]] result<void> try_set_interface_mac_address(const mac_address& addr, mac_type type);

// =============================================================================
// eFuse MAC helpers
// =============================================================================

/**
 * @brief Returns the factory MAC address from eFuse.
 *
 * @return The factory MAC address, or an error.
 */
[[nodiscard]] result<mac_address> try_default_mac();

/**
 * @brief Returns the custom MAC address from eFuse.
 *
 * @return The custom MAC address, or an error.
 * @retval idfxx::errc::invalid_mac if no custom MAC has been programmed.
 */
[[nodiscard]] result<mac_address> try_custom_mac();

/**
 * @brief Derives a local MAC address from a universal MAC address.
 *
 * Creates a locally-administered unicast MAC address by modifying
 * the U/L bit and adjusting the last byte.
 *
 * @param universal_mac The universal MAC address to derive from.
 *
 * @return The derived local MAC address, or an error.
 */
[[nodiscard]] result<mac_address> try_derive_local_mac(const mac_address& universal_mac);

/** @} */ // end of idfxx_hw_support_mac
/** @} */ // end of idfxx_hw_support

} // namespace idfxx

#include "sdkconfig.h"
#ifdef CONFIG_IDFXX_STD_FORMAT
/** @cond INTERNAL */
#include <algorithm>
#include <format>
namespace std {
template<>
struct formatter<idfxx::mac_address> {
    constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }

    template<typename FormatContext>
    auto format(const idfxx::mac_address& addr, FormatContext& ctx) const {
        auto s = to_string(addr);
        return std::copy(s.begin(), s.end(), ctx.out());
    }
};
} // namespace std
/** @endcond */
#endif // CONFIG_IDFXX_STD_FORMAT
