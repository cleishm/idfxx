// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Unit tests for idfxx::net::raw_socket
// Compile-time checks of the configuration type

#include <idfxx/net/raw_socket>

#include <type_traits>

using namespace idfxx::net;

// =============================================================================
// Compile-time tests (static_assert)
// These verify correctness at compile time - if this file compiles, they pass.
// =============================================================================

// config has no default protocol
static_assert(!std::is_default_constructible_v<raw_socket::config>);

// Every other field has a default
static_assert(raw_socket::config{.protocol = ip_protocol::icmp}.protocol == ip_protocol::icmp);
static_assert(raw_socket::config{.protocol = ip_protocol::icmp}.family == address_family::ipv4);
static_assert(!raw_socket::config{.protocol = ip_protocol::icmp}.non_blocking);
