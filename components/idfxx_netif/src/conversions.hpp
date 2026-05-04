// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

#include <idfxx/net>

#include <array>
#include <esp_netif_ip_addr.h>
#include <esp_netif_types.h>

namespace idfxx::netif::detail {

inline esp_netif_ip_info_t to_native(const net::ipv4_info& info) {
    return {
        .ip = {.addr = info.ip.addr()},
        .netmask = {.addr = info.netmask.addr()},
        .gw = {.addr = info.gateway.addr()},
    };
}

inline net::ipv4_info ip4_from_native(const esp_netif_ip_info_t& native) {
    return {
        .ip = net::ipv4_addr(native.ip.addr),
        .netmask = net::ipv4_addr(native.netmask.addr),
        .gateway = net::ipv4_addr(native.gw.addr),
    };
}

inline net::ipv6_addr ip6_from_native(const esp_ip6_addr_t& native) {
    return net::ipv6_addr(
        std::array<uint32_t, 4>{native.addr[0], native.addr[1], native.addr[2], native.addr[3]}, native.zone
    );
}

} // namespace idfxx::netif::detail
