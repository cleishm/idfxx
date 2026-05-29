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
    auto local = srv->local_endpoint().value();
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
    auto local = srv->local_endpoint().value();
    srv.value().close();

    auto cli = stream_socket::make(address_family::ipv4);
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

TEST_CASE("tcp loopback: connect_to (resolving, bare) resolves and connects", "[net]") {
    auto srv = listener::make({ipv4_addr(127, 0, 0, 1), 0}, {.reuse_address = true});
    TEST_ASSERT_TRUE(srv.has_value());
    auto local = srv->local_endpoint().value();

    // Bare overload — no cfg, no opts. The string_view host selects the
    // resolving overload. "127.0.0.1" is a numeric address so resolution
    // succeeds without DNS even with default (non-numeric_host) opts.
    auto client = stream_socket::connect_to("127.0.0.1", local.port());
    TEST_ASSERT_TRUE(client.has_value());

    auto server_side = srv->try_accept();
    TEST_ASSERT_TRUE(server_side.has_value());

    constexpr std::string_view payload = "hi";
    TEST_ASSERT_TRUE(client->try_send(payload).has_value());

    std::array<std::byte, 4> buf{};
    auto received = server_side->try_recv(buf);
    TEST_ASSERT_TRUE(received.has_value());
    TEST_ASSERT_EQUAL_size_t(payload.size(), received->size());
    TEST_ASSERT_EQUAL_MEMORY(payload.data(), received->data(), payload.size());
}

TEST_CASE("tcp loopback: connect_to (resolving) returns last connect error when all candidates fail", "[net]") {
    // Bind, capture the port, then close — connect attempts will be refused (or RST).
    auto srv = listener::make({ipv4_addr(127, 0, 0, 1), 0}, {.reuse_address = true});
    TEST_ASSERT_TRUE(srv.has_value());
    auto local = srv->local_endpoint().value();
    srv.value().close();

    auto r = stream_socket::connect_to("127.0.0.1", local.port());
    TEST_ASSERT_FALSE(r.has_value());
    // The error is the last connect error, not a synthetic name_not_found —
    // resolution succeeded and exactly one candidate was tried.
    TEST_ASSERT_TRUE(
        r.error() == idfxx::net::errc::connection_refused || r.error() == idfxx::net::errc::connection_reset ||
        r.error() == idfxx::net::errc::io_error
    );
}

TEST_CASE("tcp loopback: connect_to_for honors per-attempt timeout", "[net]") {
    auto srv = listener::make({ipv4_addr(127, 0, 0, 1), 0}, {.reuse_address = true});
    TEST_ASSERT_TRUE(srv.has_value());
    auto local = srv->local_endpoint().value();

    auto client = stream_socket::connect_to_for("127.0.0.1", local.port(), std::chrono::seconds(2));
    TEST_ASSERT_TRUE(client.has_value());

    auto server_side = srv->try_accept();
    TEST_ASSERT_TRUE(server_side.has_value());
}
