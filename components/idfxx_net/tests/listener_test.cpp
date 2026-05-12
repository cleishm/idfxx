// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#include <idfxx/net/listener>

#include <unity.h>

using namespace idfxx::net;

TEST_CASE("listener: bind to port 0 yields ephemeral port", "[net]") {
    auto a = listener::make({ipv4_addr(127, 0, 0, 1), 0}, {.reuse_address = true});
    TEST_ASSERT_TRUE(a.has_value());
    auto ep = a->local_endpoint().value();
    TEST_ASSERT_NOT_EQUAL(0, ep.port());
}

TEST_CASE("listener: reuse_address allows immediate rebind", "[net]") {
    constexpr port_number desired = 0;
    auto a = listener::make({ipv4_addr(127, 0, 0, 1), desired}, {.reuse_address = true});
    TEST_ASSERT_TRUE(a.has_value());
    auto ep = a->local_endpoint().value();
    a.value().close();

    auto a2 = listener::make({ipv4_addr(127, 0, 0, 1), ep.port()}, {.reuse_address = true});
    TEST_ASSERT_TRUE(a2.has_value());
}

TEST_CASE("listener: move-from invalidates", "[net]") {
    auto a = listener::make({ipv4_addr(127, 0, 0, 1), 0}, {.reuse_address = true});
    TEST_ASSERT_TRUE(a.has_value());
    listener moved(std::move(*a));
    TEST_ASSERT_FALSE(a->is_open());
    TEST_ASSERT_TRUE(moved.is_open());
}

TEST_CASE("listener: port-only make binds to wildcard address", "[net]") {
    auto a = listener::make(0);
    TEST_ASSERT_TRUE(a.has_value());
    auto ep = a->local_endpoint().value();
    TEST_ASSERT_NOT_EQUAL(0, ep.port());
#ifdef CONFIG_LWIP_IPV6
    TEST_ASSERT_EQUAL(static_cast<int>(address_family::ipv6), static_cast<int>(ep.family()));
    auto v6 = ep.address<ipv6_addr>();
    TEST_ASSERT_TRUE(v6.has_value());
    TEST_ASSERT_TRUE(v6->is_any());
#else
    TEST_ASSERT_EQUAL(static_cast<int>(address_family::ipv4), static_cast<int>(ep.family()));
    auto v4 = ep.address<ipv4_addr>();
    TEST_ASSERT_TRUE(v4.has_value());
    TEST_ASSERT_TRUE(v4->is_any());
#endif
}

TEST_CASE("listener: port-only make with explicit ipv4 family binds to ipv4 wildcard", "[net]") {
    auto a = listener::make(0, {.family = address_family::ipv4});
    TEST_ASSERT_TRUE(a.has_value());
    auto ep = a->local_endpoint().value();
    TEST_ASSERT_EQUAL(static_cast<int>(address_family::ipv4), static_cast<int>(ep.family()));
    auto v4 = ep.address<ipv4_addr>();
    TEST_ASSERT_TRUE(v4.has_value());
    TEST_ASSERT_TRUE(v4->is_any());
    TEST_ASSERT_NOT_EQUAL(0, ep.port());
}

TEST_CASE("listener: port-only make with ipv6 family binds to ipv6 wildcard", "[net]") {
    auto a = listener::make(0, {.family = address_family::ipv6});
    TEST_ASSERT_TRUE(a.has_value());
    auto ep = a->local_endpoint().value();
    TEST_ASSERT_EQUAL(static_cast<int>(address_family::ipv6), static_cast<int>(ep.family()));
    auto v6 = ep.address<ipv6_addr>();
    TEST_ASSERT_TRUE(v6.has_value());
    TEST_ASSERT_TRUE(v6->is_any());
    TEST_ASSERT_NOT_EQUAL(0, ep.port());
}
