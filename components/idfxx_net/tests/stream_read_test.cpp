// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Loopback tests for read / try_read on stream sockets and channels, the
// members that let them be used as idfxx::byte_source.

#include <idfxx/byte_source>
#include <idfxx/net/listener>
#include <idfxx/net/netconn/stream_channel>
#include <idfxx/net/stream_socket>

#include <array>
#include <chrono>
#include <cstdint>
#include <span>
#include <string_view>
#include <system_error>
#include <unity.h>
#include <vector>

using namespace idfxx::net;

static_assert(idfxx::byte_source<stream_socket>);
static_assert(idfxx::byte_source<netconn::stream_channel>);

namespace {

constexpr std::string_view payload = "0123456789abcdefghij";

// Reads any byte source in chunks of chunk_size until it reports end of stream.
std::vector<uint8_t> drain(idfxx::byte_source auto& src, size_t chunk_size) {
    std::vector<uint8_t> out;
    std::vector<uint8_t> buf(chunk_size);
    while (true) {
        auto n = src.try_read(buf);
        TEST_ASSERT_TRUE(n.has_value());
        TEST_ASSERT_TRUE(*n <= chunk_size);
        if (*n == 0) {
            break;
        }
        out.insert(out.end(), buf.begin(), buf.begin() + *n);
    }
    return out;
}

} // namespace

TEST_CASE("tcp loopback: try_read reads the stream until the peer closes", "[net]") {
    auto srv = listener::make({ipv4_addr(127, 0, 0, 1), 0}, {.reuse_address = true});
    TEST_ASSERT_TRUE(srv.has_value());
    auto local = srv->local_endpoint().value();

    auto client = stream_socket::connect_to(local);
    TEST_ASSERT_TRUE(client.has_value());

    auto server_side = srv->try_accept();
    TEST_ASSERT_TRUE(server_side.has_value());
    server_side->set_recv_timeout(std::chrono::seconds(1));

    TEST_ASSERT_TRUE(client->try_send_all(payload).has_value());

    // An empty buffer returns 0 straight away, whether or not data is pending
    auto none = server_side->try_read(std::span<uint8_t>{});
    TEST_ASSERT_TRUE(none.has_value());
    TEST_ASSERT_EQUAL_size_t(0, *none);

    TEST_ASSERT_TRUE(client->try_shutdown(direction::send).has_value());

    // Odd-sized chunks that do not divide the payload
    auto out = drain(*server_side, 7);
    TEST_ASSERT_EQUAL_size_t(payload.size(), out.size());
    TEST_ASSERT_EQUAL_MEMORY(payload.data(), out.data(), payload.size());

    // Once end of stream has been reported, further reads fail with not_connected
    std::array<uint8_t, 8> buf{};
    auto after = server_side->try_read(buf);
    TEST_ASSERT_FALSE(after.has_value());
    TEST_ASSERT_TRUE(after.error() == errc::not_connected);
}

TEST_CASE("tcp loopback: try_read passes a receive timeout through unchanged", "[net]") {
    auto srv = listener::make({ipv4_addr(127, 0, 0, 1), 0}, {.reuse_address = true});
    TEST_ASSERT_TRUE(srv.has_value());
    auto local = srv->local_endpoint().value();

    auto client = stream_socket::connect_to(local);
    TEST_ASSERT_TRUE(client.has_value());

    auto server_side = srv->try_accept();
    TEST_ASSERT_TRUE(server_side.has_value());
    server_side->set_recv_timeout(std::chrono::milliseconds(100));

    std::array<uint8_t, 8> buf{};
    auto r = server_side->try_read(buf);
    TEST_ASSERT_FALSE(r.has_value());

    auto expected = server_side->try_recv(std::as_writable_bytes(std::span{buf}));
    TEST_ASSERT_FALSE(expected.has_value());
    TEST_ASSERT_TRUE(r.error() == expected.error());
}

TEST_CASE("netconn: tcp try_read reads the stream until the peer closes", "[net]") {
    auto srv = listener::make({ipv4_addr(127, 0, 0, 1), 0}, {.reuse_address = true});
    TEST_ASSERT_TRUE(srv.has_value());
    auto local = srv->local_endpoint().value();

    auto client = netconn::stream_channel::make();
    TEST_ASSERT_TRUE(client.has_value());
    client->set_recv_timeout(std::chrono::seconds(1));
    TEST_ASSERT_TRUE(client->try_connect(local).has_value());

    auto server_side = srv->try_accept();
    TEST_ASSERT_TRUE(server_side.has_value());

    TEST_ASSERT_TRUE(server_side->try_send_all(payload).has_value());

    // An empty buffer returns 0 straight away, whether or not data is pending
    auto none = client->try_read(std::span<uint8_t>{});
    TEST_ASSERT_TRUE(none.has_value());
    TEST_ASSERT_EQUAL_size_t(0, *none);

    server_side->close();

    // Odd-sized chunks smaller than the segment lwIP delivers
    auto out = drain(*client, 3);
    TEST_ASSERT_EQUAL_size_t(payload.size(), out.size());
    TEST_ASSERT_EQUAL_MEMORY(payload.data(), out.data(), payload.size());

    // End of stream is reported again on later reads
    std::array<uint8_t, 8> buf{};
    auto eof = client->try_read(buf);
    TEST_ASSERT_TRUE(eof.has_value());
    TEST_ASSERT_EQUAL_size_t(0, *eof);
}

TEST_CASE("netconn: tcp try_read passes a receive timeout through unchanged", "[net]") {
    auto srv = listener::make({ipv4_addr(127, 0, 0, 1), 0}, {.reuse_address = true});
    TEST_ASSERT_TRUE(srv.has_value());
    auto local = srv->local_endpoint().value();

    auto client = netconn::stream_channel::make();
    TEST_ASSERT_TRUE(client.has_value());
    client->set_recv_timeout(std::chrono::milliseconds(100));
    TEST_ASSERT_TRUE(client->try_connect(local).has_value());

    auto server_side = srv->try_accept();
    TEST_ASSERT_TRUE(server_side.has_value());

    std::array<uint8_t, 8> buf{};
    auto r = client->try_read(buf);
    TEST_ASSERT_FALSE(r.has_value());

    auto expected = client->try_recv(std::as_writable_bytes(std::span{buf}));
    TEST_ASSERT_FALSE(expected.has_value());
    TEST_ASSERT_TRUE(r.error() == expected.error());
}

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS

TEST_CASE("tcp loopback: read returns byte counts and 0 at end of stream", "[net]") {
    auto srv = listener::make({ipv4_addr(127, 0, 0, 1), 0}, {.reuse_address = true});
    TEST_ASSERT_TRUE(srv.has_value());
    auto local = srv->local_endpoint().value();

    auto client = stream_socket::connect_to(local);
    TEST_ASSERT_TRUE(client.has_value());

    auto server_side = srv->try_accept();
    TEST_ASSERT_TRUE(server_side.has_value());
    server_side->set_recv_timeout(std::chrono::seconds(1));

    client->send_all(payload);
    client->shutdown(direction::send);

    std::vector<uint8_t> out;
    std::array<uint8_t, 6> buf{};
    while (size_t n = server_side->read(buf)) {
        out.insert(out.end(), buf.begin(), buf.begin() + n);
    }
    TEST_ASSERT_EQUAL_size_t(payload.size(), out.size());
    TEST_ASSERT_EQUAL_MEMORY(payload.data(), out.data(), payload.size());
}

TEST_CASE("netconn: tcp read throws on a receive timeout", "[net]") {
    auto srv = listener::make({ipv4_addr(127, 0, 0, 1), 0}, {.reuse_address = true});
    TEST_ASSERT_TRUE(srv.has_value());
    auto local = srv->local_endpoint().value();

    auto client = netconn::stream_channel::make();
    TEST_ASSERT_TRUE(client.has_value());
    client->set_recv_timeout(std::chrono::milliseconds(100));
    TEST_ASSERT_TRUE(client->try_connect(local).has_value());

    auto server_side = srv->try_accept();
    TEST_ASSERT_TRUE(server_side.has_value());

    std::array<uint8_t, 8> buf{};
    bool threw = false;
    try {
        (void)client->read(buf);
    } catch (const std::system_error&) {
        threw = true;
    }
    TEST_ASSERT_TRUE(threw);
}

#endif // CONFIG_COMPILER_CXX_EXCEPTIONS
