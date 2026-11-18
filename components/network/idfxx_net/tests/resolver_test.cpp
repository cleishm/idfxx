// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#include <idfxx/net/resolver>

#include <unity.h>

TEST_CASE("resolver: numeric IPv4 loopback", "[net]") {
    idfxx::net::resolver_options opts{
        .family = idfxx::net::address_family::ipv4,
        .numeric_host = true,
    };
    auto r = idfxx::net::try_resolve("127.0.0.1", idfxx::net::port_number{53}, opts);
    TEST_ASSERT_TRUE(r.has_value());
    TEST_ASSERT_GREATER_OR_EQUAL_UINT(1, r->size());
    auto ep = r->front();
    TEST_ASSERT_TRUE(ep.family() == idfxx::net::address_family::ipv4);
    TEST_ASSERT_EQUAL_UINT16(53, ep.port());
}

TEST_CASE("resolver: numeric IPv6 loopback", "[net]") {
    idfxx::net::resolver_options opts{
        .family = idfxx::net::address_family::ipv6,
        .numeric_host = true,
    };
    auto r = idfxx::net::try_resolve("::1", idfxx::net::port_number{53}, opts);
    TEST_ASSERT_TRUE(r.has_value());
    TEST_ASSERT_GREATER_OR_EQUAL_UINT(1, r->size());
    auto ep = r->front();
    TEST_ASSERT_TRUE(ep.family() == idfxx::net::address_family::ipv6);
}

TEST_CASE("resolver: try_resolve_one returns first endpoint", "[net]") {
    idfxx::net::resolver_options opts{
        .family = idfxx::net::address_family::ipv4,
        .numeric_host = true,
    };
    auto r = idfxx::net::try_resolve_one("127.0.0.1", idfxx::net::port_number{80}, opts);
    TEST_ASSERT_TRUE(r.has_value());
    TEST_ASSERT_TRUE(r->family() == idfxx::net::address_family::ipv4);
    TEST_ASSERT_EQUAL_UINT16(80, r->port());
}

TEST_CASE("resolver: try_resolve_one with service name", "[net]") {
    idfxx::net::resolver_options opts{
        .family = idfxx::net::address_family::ipv4,
        .numeric_host = true,
        .numeric_port = true,
    };
    auto r = idfxx::net::try_resolve_one("127.0.0.1", "80", opts);
    TEST_ASSERT_TRUE(r.has_value());
    TEST_ASSERT_TRUE(r->family() == idfxx::net::address_family::ipv4);
    TEST_ASSERT_EQUAL_UINT16(80, r->port());
}

TEST_CASE("resolver: try_resolve_each visits every result", "[net]") {
    idfxx::net::resolver_options opts{
        .family = idfxx::net::address_family::ipv4,
        .numeric_host = true,
    };
    int count = 0;
    auto r = idfxx::net::try_resolve_each(
        "127.0.0.1", idfxx::net::port_number{80},
        [&count](const idfxx::net::endpoint& ep) {
            TEST_ASSERT_TRUE(ep.family() == idfxx::net::address_family::ipv4);
            TEST_ASSERT_EQUAL_UINT16(80, ep.port());
            ++count;
        },
        opts);
    TEST_ASSERT_TRUE(r.has_value());
    TEST_ASSERT_GREATER_OR_EQUAL_INT(1, count);
}

TEST_CASE("resolver: try_resolve_each stops early when visitor returns false", "[net]") {
    idfxx::net::resolver_options opts{
        .family = idfxx::net::address_family::ipv4,
        .numeric_host = true,
    };
    int count = 0;
    auto r = idfxx::net::try_resolve_each(
        "127.0.0.1", idfxx::net::port_number{80},
        [&count](const idfxx::net::endpoint&) {
            ++count;
            return false;
        },
        opts);
    TEST_ASSERT_TRUE(r.has_value());
    TEST_ASSERT_EQUAL_INT(1, count);
}

TEST_CASE("resolver: try_resolve_each with service name", "[net]") {
    idfxx::net::resolver_options opts{
        .family = idfxx::net::address_family::ipv4,
        .numeric_host = true,
        .numeric_port = true,
    };
    int count = 0;
    auto r = idfxx::net::try_resolve_each(
        "127.0.0.1", "80",
        [&count](const idfxx::net::endpoint& ep) {
            TEST_ASSERT_EQUAL_UINT16(80, ep.port());
            ++count;
        },
        opts);
    TEST_ASSERT_TRUE(r.has_value());
    TEST_ASSERT_GREATER_OR_EQUAL_INT(1, count);
}
