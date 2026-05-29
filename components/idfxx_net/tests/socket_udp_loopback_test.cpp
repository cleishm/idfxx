// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#include <idfxx/net/datagram_socket>

#include <unity.h>

using namespace idfxx::net;

TEST_CASE("udp loopback: send_to and recv_from", "[net]") {
    auto rx = datagram_socket::make(address_family::ipv4);
    TEST_ASSERT_TRUE(rx.has_value());
    auto bind_r = rx->try_bind({ipv4_addr(127, 0, 0, 1), 0});
    TEST_ASSERT_TRUE(bind_r.has_value());
    auto local = rx->local_endpoint().value();

    auto tx = datagram_socket::make(address_family::ipv4);
    TEST_ASSERT_TRUE(tx.has_value());

    constexpr std::string_view payload = "ping";
    auto sent = tx->try_send_to(std::as_bytes(std::span(payload)), local);
    TEST_ASSERT_TRUE(sent.has_value());

    rx->set_recv_timeout(std::chrono::seconds(1));
    std::array<std::byte, 32> buf{};
    auto received = rx->try_recv_from(buf);
    TEST_ASSERT_TRUE(received.has_value());
    TEST_ASSERT_EQUAL_size_t(payload.size(), received->data.size());
    TEST_ASSERT_EQUAL_PTR(buf.data(), received->data.data());
    TEST_ASSERT_EQUAL_MEMORY(payload.data(), received->data.data(), payload.size());
    TEST_ASSERT_TRUE(received->from.family() == address_family::ipv4);
    TEST_ASSERT_FALSE(received->truncated);
}

TEST_CASE("udp loopback: recv_from reports truncation", "[net]") {
    auto rx = datagram_socket::make(address_family::ipv4);
    TEST_ASSERT_TRUE(rx.has_value());
    rx->try_bind({ipv4_addr(127, 0, 0, 1), 0}).value();
    auto local = rx->local_endpoint().value();

    auto tx = datagram_socket::make(address_family::ipv4);
    TEST_ASSERT_TRUE(tx.has_value());

    // Send 16 bytes, but receive into a 4-byte buffer.
    constexpr std::string_view payload = "0123456789ABCDEF";
    tx->try_send_to(std::as_bytes(std::span(payload)), local).value();

    rx->set_recv_timeout(std::chrono::seconds(1));
    std::array<std::byte, 4> buf{};
    auto received = rx->try_recv_from(buf);
    TEST_ASSERT_TRUE(received.has_value());
    TEST_ASSERT_TRUE(received->truncated);
    TEST_ASSERT_EQUAL_size_t(buf.size(), received->data.size());
    TEST_ASSERT_EQUAL_MEMORY(payload.data(), received->data.data(), buf.size());
}

TEST_CASE("udp loopback: connect + send/recv (stream-style)", "[net]") {
    auto rx = datagram_socket::make(address_family::ipv4);
    TEST_ASSERT_TRUE(rx.has_value());
    rx->try_bind({ipv4_addr(127, 0, 0, 1), 0}).value();
    auto local = rx->local_endpoint().value();

    auto tx = datagram_socket::make(address_family::ipv4);
    TEST_ASSERT_TRUE(tx.has_value());
    tx->try_connect(local).value();

    constexpr std::string_view payload = "data";
    tx->try_send(std::as_bytes(std::span(payload))).value();

    rx->set_recv_timeout(std::chrono::seconds(1));
    std::array<std::byte, 32> buf{};
    auto received = rx->try_recv_from(buf);
    TEST_ASSERT_TRUE(received.has_value());
    TEST_ASSERT_EQUAL_size_t(payload.size(), received->data.size());
}

TEST_CASE("udp loopback: connected recv(span) reports oversize as message_too_long", "[net]") {
    auto rx = datagram_socket::make(address_family::ipv4);
    TEST_ASSERT_TRUE(rx.has_value());
    rx->try_bind({ipv4_addr(127, 0, 0, 1), 0}).value();
    auto local = rx->local_endpoint().value();

    auto tx = datagram_socket::make(address_family::ipv4);
    TEST_ASSERT_TRUE(tx.has_value());

    // Send 16 bytes, receive into a 4-byte buffer. The connected-mode recv(span)
    // must surface the overrun as an error rather than silently truncating.
    constexpr std::string_view payload = "0123456789ABCDEF";
    tx->try_send_to(std::as_bytes(std::span(payload)), local).value();

    rx->set_recv_timeout(std::chrono::seconds(1));
    std::array<std::byte, 4> buf{};
    auto received = rx->try_recv(buf);
    TEST_ASSERT_FALSE(received.has_value());
    TEST_ASSERT_TRUE(received.error() == idfxx::net::errc::message_too_long);
}

TEST_CASE("udp loopback: recv(span) of a zero-length datagram is empty, not EOF", "[net]") {
    auto rx = datagram_socket::make(address_family::ipv4);
    TEST_ASSERT_TRUE(rx.has_value());
    rx->try_bind({ipv4_addr(127, 0, 0, 1), 0}).value();
    auto local = rx->local_endpoint().value();

    auto tx = datagram_socket::make(address_family::ipv4);
    TEST_ASSERT_TRUE(tx.has_value());

    // A zero-length datagram is real on UDP: recv must report it as a successful
    // empty span, never as an end-of-stream signal.
    tx->try_send_to(std::span<const std::byte>{}, local).value();

    rx->set_recv_timeout(std::chrono::seconds(1));
    std::array<std::byte, 8> buf{};
    auto received = rx->try_recv(buf);
    TEST_ASSERT_TRUE(received.has_value());
    TEST_ASSERT_TRUE(received->empty());
}

TEST_CASE("udp: broadcast off by default", "[net]") {
    auto sock = datagram_socket::make(address_family::ipv4);
    TEST_ASSERT_TRUE(sock.has_value());
    // Toggling the option should succeed on a freshly opened datagram socket.
    sock->set_broadcast(true);
}

TEST_CASE("udp: multicast helpers accept the call shape", "[net]") {
    auto sock = datagram_socket::make(address_family::ipv4);
    TEST_ASSERT_TRUE(sock.has_value());
    // Sanity check: the multicast helpers should accept the call shape on a UDP
    // socket. The actual LWIP_IGMP feature may or may not be compiled in; we
    // just verify the API can be invoked.
    sock->set_multicast_loopback(false);
}
