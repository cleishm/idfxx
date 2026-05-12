// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#include <idfxx/net/listener>
#include <idfxx/net/stream_socket>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <chrono>
#include <type_traits>
#include <unity.h>

using namespace idfxx::net;

static_assert(!std::is_copy_constructible_v<stream_socket>);
static_assert(!std::is_copy_assignable_v<stream_socket>);
static_assert(std::is_move_constructible_v<stream_socket>);
static_assert(std::is_move_assignable_v<stream_socket>);

static_assert(!std::is_copy_constructible_v<listener>);
static_assert(std::is_move_constructible_v<listener>);

TEST_CASE("tcp loopback: bind, accept, send, recv, close", "[net]") {
    auto srv = listener::make({ipv4_addr(127, 0, 0, 1), 0}, {.reuse_address = true});
    TEST_ASSERT_TRUE(srv.has_value());
    auto local = srv->local_endpoint();
    TEST_ASSERT_NOT_EQUAL(0, local.port());

    auto client = stream_socket::connect_to(local);
    TEST_ASSERT_TRUE(client.has_value());

    auto server_side = srv->try_accept();
    TEST_ASSERT_TRUE(server_side.has_value());

    constexpr std::string_view payload = "hello";
    auto sent = client->try_send(payload);
    TEST_ASSERT_TRUE(sent.has_value());
    TEST_ASSERT_EQUAL_size_t(payload.size(), *sent);

    std::array<std::byte, 16> buf{};
    auto received = server_side->try_recv(buf);
    TEST_ASSERT_TRUE(received.has_value());
    TEST_ASSERT_EQUAL_size_t(payload.size(), received->size());
    TEST_ASSERT_EQUAL_PTR(buf.data(), received->data());
    TEST_ASSERT_EQUAL_MEMORY(payload.data(), received->data(), payload.size());

    auto sd = client->try_shutdown(direction::send);
    TEST_ASSERT_TRUE(sd.has_value());

    auto eof = server_side->try_recv(buf);
    TEST_ASSERT_TRUE(eof.has_value());
    TEST_ASSERT_TRUE(eof->empty());
}

TEST_CASE("tcp loopback: connect_for to unbound port returns connection_refused", "[net]") {
    auto srv = listener::make({ipv4_addr(127, 0, 0, 1), 0}, {.reuse_address = true});
    TEST_ASSERT_TRUE(srv.has_value());
    auto local = srv->local_endpoint();
    srv.value().close();

    auto cli = stream_socket::make(family::ipv4);
    TEST_ASSERT_TRUE(cli.has_value());
    auto r = cli->try_connect_for(local, std::chrono::seconds(2));
    TEST_ASSERT_FALSE(r.has_value());
    // lwIP responds with RST when no listener is bound, which surfaces as
    // ECONNRESET via SO_ERROR rather than ECONNREFUSED. Accept either, plus
    // io_error as a fallback for unmapped errno values.
    TEST_ASSERT_TRUE(r.error() == idfxx::net::errc::connection_refused ||
                     r.error() == idfxx::net::errc::connection_reset ||
                     r.error() == idfxx::net::errc::io_error);
}

TEST_CASE("tcp loopback: try_accept_for times out", "[net]") {
    auto srv = listener::make({ipv4_addr(127, 0, 0, 1), 0}, {.reuse_address = true});
    TEST_ASSERT_TRUE(srv.has_value());
    auto r = srv->try_accept_for(std::chrono::milliseconds(100));
    TEST_ASSERT_FALSE(r.has_value());
    TEST_ASSERT_TRUE(r.error() == idfxx::net::errc::timed_out);
}
