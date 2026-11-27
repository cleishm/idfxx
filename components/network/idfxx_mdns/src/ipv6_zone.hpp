// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

#include <array>
#include <bit>
#include <cstdint>

namespace idfxx::mdns::internal {

// The zone for an IPv6 address found on an interface: the interface's lwIP
// index (as esp_netif_get_netif_impl_index() returns it, and as lwIP and
// idfxx::net use for zones and scope IDs) for a link-local (fe80::/10)
// address, and 0 otherwise or when the index is unusable. `words` holds the
// address bytes in network order.
[[nodiscard]] inline uint8_t
link_local_zone(const std::array<uint32_t, 4>& words, int netif_index) noexcept {
    const auto bytes = std::bit_cast<std::array<uint8_t, 16>>(words);
    if (bytes[0] != 0xfe || (bytes[1] & 0xc0) != 0x80 || netif_index <= 0 ||
        netif_index > UINT8_MAX) {
        return 0;
    }
    return static_cast<uint8_t>(netif_index);
}

// The zone for an IPv6 address in an answer. The C library does not set the
// zone of an address that it reads from a packet (mdns_receive.c), so the
// field holds whatever was on the stack. An answer that arrived on an
// interface therefore takes its zone from that interface. An answer from a
// local lookup keeps `c_zone`, the zone that the address was added with.
[[nodiscard]] inline uint8_t answer_zone(
    const std::array<uint32_t, 4>& words,
    uint8_t c_zone,
    bool arrived_on_netif,
    int netif_index
) noexcept {
    return arrived_on_netif ? link_local_zone(words, netif_index) : c_zone;
}

} // namespace idfxx::mdns::internal
