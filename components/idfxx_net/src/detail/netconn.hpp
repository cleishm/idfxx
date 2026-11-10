// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

// Internal helpers for allocating lwIP netconns.

#include <idfxx/net/endpoint>
#include <idfxx/net/error>

#include <cstdint>

// Forward declaration from lwIP.
struct netconn;

namespace idfxx::net::netconn::detail {

/**
 * @brief Allocates a netconn of the given base type (`NETCONN_TCP`, `NETCONN_UDP`, etc.)
 *        and address family. Returns `errc::no_buffer_space` on lwIP allocation failure.
 */
[[nodiscard]] result<::netconn*> make_netconn(int base_type, address_family fam);

/**
 * @brief Allocates a raw netconn of the given base type, IP protocol, and address family.
 *        Returns `errc::no_buffer_space` on lwIP allocation failure.
 */
[[nodiscard]] result<::netconn*> make_netconn_with_proto(int base_type, uint8_t proto, address_family fam);

} // namespace idfxx::net::netconn::detail
