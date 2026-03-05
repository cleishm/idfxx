// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#include <idfxx/mac>

#include <cstdio>
#include <esp_mac.h>
#include <utility>

// Verify mac_type values match ESP-IDF constants
static_assert(std::to_underlying(idfxx::mac_type::wifi_sta) == ESP_MAC_WIFI_STA);
static_assert(std::to_underlying(idfxx::mac_type::wifi_softap) == ESP_MAC_WIFI_SOFTAP);
static_assert(std::to_underlying(idfxx::mac_type::bt) == ESP_MAC_BT);
static_assert(std::to_underlying(idfxx::mac_type::ethernet) == ESP_MAC_ETH);
#if SOC_IEEE802154_SUPPORTED
static_assert(std::to_underlying(idfxx::mac_type::ieee802154) == ESP_MAC_IEEE802154);
#endif
static_assert(std::to_underlying(idfxx::mac_type::base) == ESP_MAC_BASE);
static_assert(std::to_underlying(idfxx::mac_type::efuse_factory) == ESP_MAC_EFUSE_FACTORY);
static_assert(std::to_underlying(idfxx::mac_type::efuse_custom) == ESP_MAC_EFUSE_CUSTOM);
#if SOC_IEEE802154_SUPPORTED
static_assert(std::to_underlying(idfxx::mac_type::efuse_ext) == ESP_MAC_EFUSE_EXT);
#endif

namespace idfxx {

std::string to_string(const mac_address& addr) {
    char buf[18]; // "AA:BB:CC:DD:EE:FF\0"
    const auto& b = addr.bytes();
    snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X", b[0], b[1], b[2], b[3], b[4], b[5]);
    return buf;
}

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
mac_address read_mac(mac_type type) {
    return unwrap(try_read_mac(type));
}

mac_address base_mac_address() {
    return unwrap(try_base_mac_address());
}

void set_base_mac_address(const mac_address& addr) {
    unwrap(try_set_base_mac_address(addr));
}

void set_interface_mac_address(const mac_address& addr, mac_type type) {
    unwrap(try_set_interface_mac_address(addr, type));
}
#endif

result<mac_address> try_read_mac(mac_type type) {
    mac_address addr;
    auto err = esp_read_mac(addr.data(), static_cast<esp_mac_type_t>(type));
    if (err) {
        return error(err);
    }
    return addr;
}

result<mac_address> try_base_mac_address() {
    mac_address addr;
    auto err = esp_base_mac_addr_get(addr.data());
    if (err) {
        return error(err);
    }
    return addr;
}

result<void> try_set_base_mac_address(const mac_address& addr) {
    return wrap(esp_base_mac_addr_set(addr.data()));
}

result<void> try_set_interface_mac_address(const mac_address& addr, mac_type type) {
    return wrap(esp_iface_mac_addr_set(addr.data(), static_cast<esp_mac_type_t>(type)));
}

result<mac_address> try_default_mac() {
    mac_address addr;
    auto err = esp_efuse_mac_get_default(addr.data());
    if (err) {
        return error(err);
    }
    return addr;
}

result<mac_address> try_custom_mac() {
    mac_address addr;
    auto err = esp_efuse_mac_get_custom(addr.data());
    if (err) {
        return error(err);
    }
    return addr;
}

result<mac_address> try_derive_local_mac(const mac_address& universal_mac) {
    mac_address local;
    auto err = esp_derive_local_mac(local.data(), universal_mac.data());
    if (err) {
        return error(err);
    }
    return local;
}

} // namespace idfxx
