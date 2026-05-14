// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#include <idfxx/net/datagram_socket>
#include <idfxx/net/listener>
#include <idfxx/net/netconn/datagram_connection>
#include <idfxx/net/netconn/netbuf>
#include <idfxx/net/netconn/stream_connection>

#include <array>
#include <chrono>
#include <span>
#include <string_view>
#include <type_traits>
#include <unity.h>

using namespace idfxx::net;

static_assert(!std::is_copy_constructible_v<netconn::stream_connection>);
static_assert(std::is_move_constructible_v<netconn::stream_connection>);
static_assert(!std::is_copy_constructible_v<netconn::datagram_connection>);
static_assert(std::is_move_constructible_v<netconn::datagram_connection>);
static_assert(!std::is_copy_constructible_v<netconn::netbuf>);
static_assert(std::is_move_constructible_v<netconn::netbuf>);

TEST_CASE("netconn: udp netbuf attach reports attached length", "[net]") {
    auto rx = netconn::datagram_connection::make();
    TEST_ASSERT_TRUE(rx.has_value());
    rx->set_recv_timeout(std::chrono::seconds(1));
    rx->try_bind({ipv4_addr(127, 0, 0, 1), 0}).value();

    auto buf = netconn::netbuf::make();
    TEST_ASSERT_TRUE(buf.has_value());
    constexpr std::string_view payload = "hello";
    auto attached = buf->try_attach(std::as_bytes(std::span(payload)));
    TEST_ASSERT_TRUE(attached.has_value());

    TEST_ASSERT_EQUAL_size_t(payload.size(), buf->len());
}

TEST_CASE("netconn: tcp connection construction and delete", "[net]") {
    auto c = netconn::stream_connection::make();
    TEST_ASSERT_TRUE(c.has_value());
    TEST_ASSERT_TRUE(c->is_open());

    netconn::stream_connection moved(std::move(*c));
    TEST_ASSERT_FALSE(c->is_open());
    TEST_ASSERT_TRUE(moved.is_open());
}

TEST_CASE("netconn: tcp recv returns empty netbuf on graceful close", "[net]") {
    auto srv = listener::make({ipv4_addr(127, 0, 0, 1), 0}, {.reuse_address = true});
    TEST_ASSERT_TRUE(srv.has_value());
    auto local = srv->local_endpoint();

    auto client = netconn::stream_connection::make();
    TEST_ASSERT_TRUE(client.has_value());
    client->set_recv_timeout(std::chrono::seconds(1));
    auto connected = client->try_connect(local);
    TEST_ASSERT_TRUE(connected.has_value());

    auto server_side = srv->try_accept();
    TEST_ASSERT_TRUE(server_side.has_value());

    constexpr std::string_view payload = "hi";
    auto sent = server_side->try_send(payload);
    TEST_ASSERT_TRUE(sent.has_value());
    server_side->close();

    // The first recv may return the payload or the close — drain until close.
    while (true) {
        auto rx = client->try_recv();
        TEST_ASSERT_TRUE(rx.has_value());
        if (!rx->is_open()) {
            break;
        }
    }

    // Subsequent recv on the closed connection must continue to report the
    // empty-netbuf close signal rather than throwing/erroring.
    auto eof = client->try_recv();
    TEST_ASSERT_TRUE(eof.has_value());
    TEST_ASSERT_FALSE(eof->is_open());

    // The recv(span) overload mirrors the BSD socket convention: empty span on close.
    std::array<std::byte, 16> buf{};
    auto eof_span = client->try_recv(buf);
    TEST_ASSERT_TRUE(eof_span.has_value());
    TEST_ASSERT_TRUE(eof_span->empty());
}

TEST_CASE("netconn: udp send_to(span) loopback to BSD socket", "[net]") {
    auto rx = datagram_socket::make(family::ipv4);
    TEST_ASSERT_TRUE(rx.has_value());
    rx->try_bind({ipv4_addr(127, 0, 0, 1), 0}).value();
    auto local = rx->local_endpoint();
    rx->set_recv_timeout(std::chrono::seconds(1));

    auto tx = netconn::datagram_connection::make();
    TEST_ASSERT_TRUE(tx.has_value());

    constexpr std::string_view payload = "ping";
    auto sent = tx->try_send_to(std::as_bytes(std::span(payload)), local);
    TEST_ASSERT_TRUE(sent.has_value());

    std::array<std::byte, 32> buf{};
    auto received = rx->try_recv_from(buf);
    TEST_ASSERT_TRUE(received.has_value());
    TEST_ASSERT_EQUAL_size_t(payload.size(), received->data.size());
    TEST_ASSERT_EQUAL_PTR(buf.data(), received->data.data());
    TEST_ASSERT_EQUAL_MEMORY(payload.data(), received->data.data(), payload.size());
}
