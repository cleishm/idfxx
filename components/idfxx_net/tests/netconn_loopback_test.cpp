// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#include <idfxx/net/datagram_socket>
#include <idfxx/net/listener>
#include <idfxx/net/netconn/datagram_channel>
#include <idfxx/net/netconn/buffer>
#include <idfxx/net/netconn/listener>
#include <idfxx/net/netconn/stream_channel>

#include <array>
#include <chrono>
#include <cstring>
#include <ranges>
#include <span>
#include <string_view>
#include <type_traits>
#include <unity.h>

using namespace idfxx::net;

static_assert(!std::is_copy_constructible_v<netconn::stream_channel>);
static_assert(std::is_move_constructible_v<netconn::stream_channel>);
static_assert(!std::is_copy_constructible_v<netconn::datagram_channel>);
static_assert(std::is_move_constructible_v<netconn::datagram_channel>);
static_assert(!std::is_copy_constructible_v<netconn::buffer>);
static_assert(std::is_move_constructible_v<netconn::buffer>);
static_assert(std::ranges::forward_range<netconn::buffer::segment_view>);
static_assert(std::forward_iterator<netconn::buffer::segment_view::iterator>);

TEST_CASE("netconn: udp buffer attach reports attached length", "[net]") {
    auto rx = netconn::datagram_channel::make();
    TEST_ASSERT_TRUE(rx.has_value());
    rx->set_recv_timeout(std::chrono::seconds(1));
    rx->try_bind({ipv4_addr(127, 0, 0, 1), 0}).value();

    auto buf = netconn::buffer::make();
    TEST_ASSERT_TRUE(buf.has_value());
    constexpr std::string_view payload = "hello";
    auto attached = buf->try_attach(std::as_bytes(std::span(payload)));
    TEST_ASSERT_TRUE(attached.has_value());

    TEST_ASSERT_EQUAL_size_t(payload.size(), buf->len());
}

TEST_CASE("netconn: buffer segments() reconstructs received payload", "[net]") {
    auto srv = listener::make({ipv4_addr(127, 0, 0, 1), 0}, {.reuse_address = true});
    TEST_ASSERT_TRUE(srv.has_value());
    auto local = srv->local_endpoint().value();

    auto client = netconn::stream_channel::make();
    TEST_ASSERT_TRUE(client.has_value());
    client->set_recv_timeout(std::chrono::seconds(1));
    client->try_connect(local).value();

    auto server_side = srv->try_accept();
    TEST_ASSERT_TRUE(server_side.has_value());

    constexpr std::string_view payload = "hello segments";
    server_side->try_send(payload).value();

    auto rx = client->try_recv();
    TEST_ASSERT_TRUE(rx.has_value());
    TEST_ASSERT_TRUE(rx->is_open());

    // Walking segments() must reconstruct the full payload without disturbing
    // the buffer's own len() bookkeeping.
    std::array<std::byte, 64> gathered{};
    size_t total = 0;
    for (auto seg : rx->segments()) {
        TEST_ASSERT_TRUE(total + seg.size() <= gathered.size());
        std::memcpy(gathered.data() + total, seg.data(), seg.size());
        total += seg.size();
    }
    TEST_ASSERT_EQUAL_size_t(rx->len(), total);
    TEST_ASSERT_EQUAL_size_t(payload.size(), total);
    TEST_ASSERT_EQUAL_MEMORY(payload.data(), gathered.data(), payload.size());

    // The range is multi-pass: a second traversal yields the same bytes.
    size_t again = 0;
    for (auto seg : rx->segments()) {
        again += seg.size();
    }
    TEST_ASSERT_EQUAL_size_t(total, again);
}

TEST_CASE("netconn: tcp send copies by default and honors no_copy", "[net]") {
    auto srv = listener::make({ipv4_addr(127, 0, 0, 1), 0}, {.reuse_address = true});
    TEST_ASSERT_TRUE(srv.has_value());
    auto local = srv->local_endpoint().value();

    auto client = netconn::stream_channel::make();
    TEST_ASSERT_TRUE(client.has_value());
    client->set_recv_timeout(std::chrono::seconds(1));
    client->try_connect(local).value();

    auto server_side = srv->try_accept();
    TEST_ASSERT_TRUE(server_side.has_value());
    server_side->set_recv_timeout(std::chrono::seconds(1));

    std::array<std::byte, 16> rbuf{};

    // Default send copies: the source buffer is destroyed immediately after
    // the call returns, yet the receiver still observes the original bytes.
    {
        std::array<std::byte, 5> src{};
        std::memcpy(src.data(), "hello", 5);
        auto written = client->try_send(src);
        TEST_ASSERT_TRUE(written.has_value());
        TEST_ASSERT_EQUAL_size_t(src.size(), *written);
    }
    auto got = server_side->try_recv(rbuf);
    TEST_ASSERT_TRUE(got.has_value());
    TEST_ASSERT_EQUAL_size_t(5, got->size());
    TEST_ASSERT_EQUAL_MEMORY("hello", got->data(), 5);

    // no_copy references the caller's buffer; a static-storage buffer satisfies
    // the lifetime contract.
    static constexpr std::string_view persistent = "world";
    auto written2 = client->try_send(std::as_bytes(std::span(persistent)), netconn::write_flag::no_copy);
    TEST_ASSERT_TRUE(written2.has_value());
    TEST_ASSERT_EQUAL_size_t(persistent.size(), *written2);

    auto got2 = server_side->try_recv(rbuf);
    TEST_ASSERT_TRUE(got2.has_value());
    TEST_ASSERT_EQUAL_size_t(persistent.size(), got2->size());
    TEST_ASSERT_EQUAL_MEMORY(persistent.data(), got2->data(), persistent.size());
}

TEST_CASE("netconn: tcp connection construction and delete", "[net]") {
    auto c = netconn::stream_channel::make();
    TEST_ASSERT_TRUE(c.has_value());
    TEST_ASSERT_TRUE(c->is_open());

    netconn::stream_channel moved(std::move(*c));
    TEST_ASSERT_FALSE(c->is_open());
    TEST_ASSERT_TRUE(moved.is_open());
}

TEST_CASE("netconn: tcp recv returns empty buffer on graceful close", "[net]") {
    auto srv = listener::make({ipv4_addr(127, 0, 0, 1), 0}, {.reuse_address = true});
    TEST_ASSERT_TRUE(srv.has_value());
    auto local = srv->local_endpoint().value();

    auto client = netconn::stream_channel::make();
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
    // empty-buffer close signal rather than throwing/erroring.
    auto eof = client->try_recv();
    TEST_ASSERT_TRUE(eof.has_value());
    TEST_ASSERT_FALSE(eof->is_open());

    // The recv(span) overload mirrors the BSD socket convention: empty span on close.
    std::array<std::byte, 16> buf{};
    auto eof_span = client->try_recv(buf);
    TEST_ASSERT_TRUE(eof_span.has_value());
    TEST_ASSERT_TRUE(eof_span->empty());
}

TEST_CASE("netconn: tcp recv(span) preserves bytes across small reads", "[net]") {
    // Regression test: when the caller buffer is smaller than the netbuf lwIP
    // delivered, the overflow bytes used to be silently dropped because the
    // netbuf was destroyed at the end of the call. The fix keeps the netbuf
    // alive in `_pending` and drains it on subsequent calls.
    auto srv = listener::make({ipv4_addr(127, 0, 0, 1), 0}, {.reuse_address = true});
    TEST_ASSERT_TRUE(srv.has_value());
    auto local = srv->local_endpoint().value();

    auto client = netconn::stream_channel::make();
    TEST_ASSERT_TRUE(client.has_value());
    client->set_recv_timeout(std::chrono::seconds(1));
    client->try_connect(local).value();

    auto server_side = srv->try_accept();
    TEST_ASSERT_TRUE(server_side.has_value());
    server_side->set_recv_timeout(std::chrono::seconds(1));

    // Send a 16-byte payload and read it back through 4-byte chunks. The
    // received chunks must reconstitute the original payload exactly.
    constexpr std::string_view payload = "0123456789abcdef";
    auto sent = client->try_send(payload);
    TEST_ASSERT_TRUE(sent.has_value());
    TEST_ASSERT_EQUAL_size_t(payload.size(), *sent);

    std::array<std::byte, 16> assembled{};
    size_t total = 0;
    while (total < payload.size()) {
        std::array<std::byte, 4> chunk{};
        auto got = server_side->try_recv(chunk);
        TEST_ASSERT_TRUE(got.has_value());
        TEST_ASSERT_FALSE(got->empty());
        std::memcpy(assembled.data() + total, got->data(), got->size());
        total += got->size();
    }
    TEST_ASSERT_EQUAL_size_t(payload.size(), total);
    TEST_ASSERT_EQUAL_MEMORY(payload.data(), assembled.data(), payload.size());
}

TEST_CASE("netconn: udp send_to(span) loopback to BSD socket", "[net]") {
    auto rx = datagram_socket::make(address_family::ipv4);
    TEST_ASSERT_TRUE(rx.has_value());
    rx->try_bind({ipv4_addr(127, 0, 0, 1), 0}).value();
    auto local = rx->local_endpoint().value();
    rx->set_recv_timeout(std::chrono::seconds(1));

    auto tx = netconn::datagram_channel::make();
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

TEST_CASE("netconn: listener accept_with_peer reports the connecting endpoint", "[net]") {
    // The netconn listener can now report the peer, so it can serve as the
    // server side directly rather than borrowing the BSD listener.
    auto srv = netconn::listener::make();
    TEST_ASSERT_TRUE(srv.has_value());
    srv->try_bind({ipv4_addr(127, 0, 0, 1), 0}).value();
    srv->try_listen().value();
    srv->set_recv_timeout(std::chrono::seconds(1)); // also bounds accept

    // local_endpoint reflects the bound + listening address.
    auto local = srv->local_endpoint();
    TEST_ASSERT_TRUE(local.has_value());
    TEST_ASSERT_NOT_EQUAL(0, local->port());

    auto client = netconn::stream_channel::make();
    TEST_ASSERT_TRUE(client.has_value());
    client->set_recv_timeout(std::chrono::seconds(1));
    client->try_connect(*local).value();

    auto accepted = srv->try_accept_with_peer();
    TEST_ASSERT_TRUE(accepted.has_value());

    // The peer the server reports is the client's local endpoint.
    auto client_local = client->local_endpoint();
    TEST_ASSERT_TRUE(client_local.has_value());
    TEST_ASSERT_TRUE(accepted->peer == *client_local);

    // The accepted channel's own peer_endpoint agrees with the reported peer.
    auto server_side_peer = accepted->channel.try_peer_endpoint();
    TEST_ASSERT_TRUE(server_side_peer.has_value());
    TEST_ASSERT_TRUE(*server_side_peer == accepted->peer);

    // From the client's side, the peer is the listener's bound address.
    auto client_peer = client->try_peer_endpoint();
    TEST_ASSERT_TRUE(client_peer.has_value());
    TEST_ASSERT_TRUE(*client_peer == *local);
}

TEST_CASE("netconn: datagram_channel local/peer endpoint accessors", "[net]") {
    auto ch = netconn::datagram_channel::make();
    TEST_ASSERT_TRUE(ch.has_value());

    // No default peer set yet: querying the peer reports not_connected.
    auto unconnected = ch->try_peer_endpoint();
    TEST_ASSERT_FALSE(unconnected.has_value());
    TEST_ASSERT_TRUE(unconnected.error() == idfxx::net::errc::not_connected);

    // Bind assigns a concrete local address; local_endpoint reflects it.
    ch->try_bind({ipv4_addr(127, 0, 0, 1), 0}).value();
    auto local = ch->local_endpoint();
    TEST_ASSERT_TRUE(local.has_value());
    TEST_ASSERT_TRUE(local->address<ipv4_addr>() == ipv4_addr(127, 0, 0, 1));
    TEST_ASSERT_NOT_EQUAL(0, local->port());

    // A second bound socket provides a concrete peer to connect to.
    auto peer_sock = datagram_socket::make(address_family::ipv4);
    TEST_ASSERT_TRUE(peer_sock.has_value());
    peer_sock->try_bind({ipv4_addr(127, 0, 0, 1), 0}).value();
    auto peer_addr = peer_sock->local_endpoint().value();

    // After connect, peer_endpoint reports that default peer.
    ch->try_connect(peer_addr).value();
    auto peer = ch->try_peer_endpoint();
    TEST_ASSERT_TRUE(peer.has_value());
    TEST_ASSERT_TRUE(*peer == peer_addr);
}
