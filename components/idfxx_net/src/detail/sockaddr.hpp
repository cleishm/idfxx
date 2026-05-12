// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

// Internal-only header. Provides conversions between the public idfxx::net
// endpoint type and lwIP's sockaddr_* structures. POSIX structs never appear
// in the public API.

#include <idfxx/net/endpoint>
#include <idfxx/net/error>

#include <lwip/sockets.h>

#include <cstddef>
#include <cstdint>
#include <optional>

namespace idfxx::net::detail {

/// Convert a populated sockaddr (and its size) to a public endpoint. Returns
/// std::nullopt if the family is not one we recognise (v4 or v6).
[[nodiscard]] std::optional<endpoint> from_sockaddr(const sockaddr* sa, socklen_t len) noexcept;

/// Convert an endpoint into a sockaddr_storage. Returns the populated length,
/// or 0 if the endpoint is default-constructed (holds no address).
[[nodiscard]] socklen_t to_sockaddr(const endpoint& ep, sockaddr_storage& out) noexcept;

/// Convert an idfxx::net::family to a POSIX AF_* constant. Returns AF_UNSPEC
/// for std::nullopt.
[[nodiscard]] int family_to_af(std::optional<family> f) noexcept;

/// Reads the local endpoint bound to the given fd. Used by both `socket` and
/// `listener`.
[[nodiscard]] result<endpoint> get_local_endpoint(int fd) noexcept;

/// Reads the peer endpoint connected to the given fd.
[[nodiscard]] result<endpoint> get_peer_endpoint(int fd) noexcept;

} // namespace idfxx::net::detail
