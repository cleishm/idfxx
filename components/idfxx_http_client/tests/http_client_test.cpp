// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Unit tests for idfxx http client
// Uses ESP-IDF Unity test framework with compile-time static_asserts

#include "http_test_utils.hpp"
#include "idfxx/http/client"
#include "idfxx/net/listener"
#include "idfxx/net/stream_socket"
#include "unity.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <esp_idf_version.h>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

using namespace idfxx;
using namespace idfxx::http;

// =============================================================================
// Compile-time tests (static_assert)
// These verify correctness at compile time - if this file compiles, they pass.
// Note: Enum values are verified against ESP-IDF constants in client.cpp
// =============================================================================

// client is not default constructible (must use make() factory)
static_assert(!std::is_default_constructible_v<client>);

// client is not copyable (unique ownership of handle)
static_assert(!std::is_copy_constructible_v<client>);
static_assert(!std::is_copy_assignable_v<client>);

// client is move-only
static_assert(std::is_move_constructible_v<client>);
static_assert(std::is_move_assignable_v<client>);

// client::errc is an error_code_enum
static_assert(std::is_error_code_enum_v<client::errc>);

// chunk_writer is only obtainable from client::open_chunked()
static_assert(!std::is_default_constructible_v<client::chunk_writer>);
static_assert(!std::is_constructible_v<client::chunk_writer, client&>);

// chunk_writer is not copyable
static_assert(!std::is_copy_constructible_v<client::chunk_writer>);
static_assert(!std::is_copy_assignable_v<client::chunk_writer>);

// chunk_writer is move-only
static_assert(std::is_nothrow_move_constructible_v<client::chunk_writer>);
static_assert(std::is_nothrow_move_assignable_v<client::chunk_writer>);

// =============================================================================
// Runtime tests (Unity TEST_CASE)
// =============================================================================

// -- Error category tests ----------------------------------------------------

TEST_CASE("http client error_category has correct name", "[idfxx][http]") {
    TEST_ASSERT_EQUAL_STRING("http::client::Error", http_client_category().name());
}

TEST_CASE("http client error_category produces messages", "[idfxx][http]") {
    std::error_code ec = client::errc::max_redirect;
    TEST_ASSERT_TRUE(ec.message().length() > 0);

    ec = client::errc::connect;
    TEST_ASSERT_TRUE(ec.message().length() > 0);

    ec = client::errc::connection_closed;
    TEST_ASSERT_TRUE(ec.message().length() > 0);

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 5, 2)
    ec = client::errc::read_timeout;
    TEST_ASSERT_EQUAL_STRING("Timed out reading response data", ec.message().c_str());

    ec = client::errc::incomplete_data;
    TEST_ASSERT_EQUAL_STRING("Response data incomplete", ec.message().c_str());
#endif

    ec = client::errc::not_modified;
    TEST_ASSERT_EQUAL_STRING("Not modified (HTTP 304)", ec.message().c_str());

    ec = client::errc::range_not_satisfiable;
    TEST_ASSERT_EQUAL_STRING("Range not satisfiable (HTTP 416)", ec.message().c_str());

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
    ec = client::errc::redirect_downgrade;
    TEST_ASSERT_EQUAL_STRING("Redirect from HTTPS to a non-HTTPS URL blocked", ec.message().c_str());

    ec = client::errc::header_too_long;
    TEST_ASSERT_EQUAL_STRING("Request header too long for the transmit buffer", ec.message().c_str());
#endif
}

TEST_CASE("make_error_code creates correct error codes", "[idfxx][http]") {
    std::error_code ec = make_error_code(client::errc::max_redirect);
    TEST_ASSERT_EQUAL(std::to_underlying(client::errc::max_redirect), ec.value());
    TEST_ASSERT_EQUAL_STRING("http::client::Error", ec.category().name());

    ec = make_error_code(client::errc::eagain);
    TEST_ASSERT_EQUAL(std::to_underlying(client::errc::eagain), ec.value());

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 5, 2)
    ec = make_error_code(client::errc::read_timeout);
    TEST_ASSERT_EQUAL(std::to_underlying(client::errc::read_timeout), ec.value());
    TEST_ASSERT_EQUAL_STRING("http::client::Error", ec.category().name());

    ec = make_error_code(client::errc::incomplete_data);
    TEST_ASSERT_EQUAL(std::to_underlying(client::errc::incomplete_data), ec.value());
    TEST_ASSERT_EQUAL_STRING("http::client::Error", ec.category().name());
#endif
}

TEST_CASE("http_client_error maps ESP-IDF HTTP codes to client::errc", "[idfxx][http]") {
    // The enum values equal the ESP_ERR_HTTP_* constants (checked in client.cpp),
    // so passing them as esp_err_t exercises the ESP-IDF code mapping.
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 5, 2)
    auto timeout = http_client_error(std::to_underlying(client::errc::read_timeout));
    TEST_ASSERT_TRUE(timeout.error() == client::errc::read_timeout);

    auto incomplete = http_client_error(std::to_underlying(client::errc::incomplete_data));
    TEST_ASSERT_TRUE(incomplete.error() == client::errc::incomplete_data);
#endif

    auto eagain = http_client_error(std::to_underlying(client::errc::eagain));
    TEST_ASSERT_TRUE(eagain.error() == client::errc::eagain);

    auto not_modified = http_client_error(std::to_underlying(client::errc::not_modified));
    TEST_ASSERT_TRUE(not_modified.error() == client::errc::not_modified);

    auto range = http_client_error(std::to_underlying(client::errc::range_not_satisfiable));
    TEST_ASSERT_TRUE(range.error() == client::errc::range_not_satisfiable);

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
    auto downgrade = http_client_error(std::to_underlying(client::errc::redirect_downgrade));
    TEST_ASSERT_TRUE(downgrade.error() == client::errc::redirect_downgrade);

    auto too_long = http_client_error(std::to_underlying(client::errc::header_too_long));
    TEST_ASSERT_TRUE(too_long.error() == client::errc::header_too_long);
#endif
}

// -- Streaming error reporting (loopback) --------------------------------------
//
// A plain TCP listener on loopback stands in for the HTTP server, so each test
// scripts exactly which bytes the client receives (or withholds).

namespace {

constexpr auto short_timeout = std::chrono::milliseconds(200);

net::listener make_loopback_server() {
    auto srv = net::listener::make({net::ipv4_addr(127, 0, 0, 1), 0}, {.reuse_address = true});
    TEST_ASSERT_TRUE(srv.has_value());
    // Bound the accept so a client that never connects fails the test instead of hanging it.
    srv->set_recv_timeout(std::chrono::seconds(2));
    return std::move(*srv);
}

std::string loopback_url(const net::listener& srv) {
    return "http://127.0.0.1:" + std::to_string(srv.local_endpoint().value().port()) + "/";
}

} // namespace

TEST_CASE("http client try_read reports body timeout as eagain and early close as connection_closed", "[idfxx][http]") {
    test::ensure_event_loop();
    auto srv = make_loopback_server();
    auto c = client::make({.url = loopback_url(srv), .timeout = short_timeout});
    TEST_ASSERT_TRUE(c.has_value());
    TEST_ASSERT_TRUE(c->try_open().has_value());
    auto conn = srv.try_accept();
    TEST_ASSERT_TRUE(conn.has_value());

    // Promise 10 body bytes but deliver only 3.
    TEST_ASSERT_TRUE(conn->try_send_all("HTTP/1.1 200 OK\r\nContent-Length: 10\r\n\r\nabc").has_value());
    auto length = c->try_fetch_headers();
    TEST_ASSERT_TRUE(length.has_value());
    TEST_ASSERT_EQUAL_INT32(10, static_cast<int32_t>(*length));

    // The bytes that did arrive are returned once the timeout ends the wait for more.
    std::array<char, 16> buf{};
    auto partial = c->try_read(buf);
    TEST_ASSERT_TRUE(partial.has_value());
    TEST_ASSERT_EQUAL_size_t(3, *partial);
    TEST_ASSERT_EQUAL_MEMORY("abc", buf.data(), 3);

    auto timed_out = c->try_read(buf);
    TEST_ASSERT_FALSE(timed_out.has_value());
    TEST_ASSERT_TRUE(timed_out.error() == client::errc::eagain);

    // The server ends the connection 7 bytes short of the promised length.
    TEST_ASSERT_TRUE(conn->try_shutdown(net::direction::send).has_value());
    auto closed = c->try_read(buf);
    TEST_ASSERT_FALSE(closed.has_value());
    TEST_ASSERT_TRUE(closed.error() == client::errc::connection_closed);

    std::ignore = c->try_close();
}

TEST_CASE("http client try_fetch_headers reports a timeout as eagain", "[idfxx][http]") {
    test::ensure_event_loop();
    auto srv = make_loopback_server();
    auto c = client::make({.url = loopback_url(srv), .timeout = short_timeout});
    TEST_ASSERT_TRUE(c.has_value());
    TEST_ASSERT_TRUE(c->try_open().has_value());
    auto conn = srv.try_accept();
    TEST_ASSERT_TRUE(conn.has_value());

    // The server never responds.
    auto r = c->try_fetch_headers();
    TEST_ASSERT_FALSE(r.has_value());
    TEST_ASSERT_TRUE(r.error() == client::errc::eagain);

    std::ignore = c->try_close();
}

TEST_CASE("http client try_fetch_headers failure after a chunked response is still an error", "[idfxx][http]") {
    test::ensure_event_loop();
    auto srv = make_loopback_server();
    auto c = client::make({.url = loopback_url(srv), .timeout = short_timeout});
    TEST_ASSERT_TRUE(c.has_value());

    // First request: a complete, empty chunked response, reported as length 0.
    TEST_ASSERT_TRUE(c->try_open().has_value());
    auto first = srv.try_accept();
    TEST_ASSERT_TRUE(first.has_value());
    TEST_ASSERT_TRUE(first->try_send_all("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n\r\n").has_value());
    auto length = c->try_fetch_headers();
    TEST_ASSERT_TRUE(length.has_value());
    TEST_ASSERT_EQUAL_INT32(0, static_cast<int32_t>(*length));
    TEST_ASSERT_TRUE(c->try_close().has_value());

    // Second request on a fresh connection: the server never responds. The
    // failure must not be masked by the previous response having been chunked.
    TEST_ASSERT_TRUE(c->try_open().has_value());
    auto second = srv.try_accept();
    TEST_ASSERT_TRUE(second.has_value());
    auto r = c->try_fetch_headers();
    TEST_ASSERT_FALSE(r.has_value());
    TEST_ASSERT_TRUE(r.error() == client::errc::eagain);

    std::ignore = c->try_close();
}

TEST_CASE("http client try_write reports a timeout as eagain", "[idfxx][http]") {
    test::ensure_event_loop();
    auto srv = make_loopback_server();
    auto c = client::make({.url = loopback_url(srv), .method = method::post, .timeout = short_timeout});
    TEST_ASSERT_TRUE(c.has_value());

    // The server never reads, so the body stalls once the loopback TCP send
    // buffer and receive window are full (a few KiB each).
    std::vector<uint8_t> body(32 * 1024, 0x55);
    TEST_ASSERT_TRUE(c->try_open(body.size()).has_value());
    auto conn = srv.try_accept();
    TEST_ASSERT_TRUE(conn.has_value());

    auto r = c->try_write(body);
    TEST_ASSERT_FALSE(r.has_value());
    TEST_ASSERT_TRUE(r.error() == client::errc::eagain);

    std::ignore = c->try_close();
}

// -- to_string tests ---------------------------------------------------------

TEST_CASE("to_string(event_id) outputs correct names", "[idfxx][http]") {
    auto s = to_string(event_id::error);
    TEST_ASSERT_EQUAL_STRING("ERROR", s.c_str());
    s = to_string(event_id::on_connected);
    TEST_ASSERT_EQUAL_STRING("ON_CONNECTED", s.c_str());
    s = to_string(event_id::headers_sent);
    TEST_ASSERT_EQUAL_STRING("HEADERS_SENT", s.c_str());
    s = to_string(event_id::on_header);
    TEST_ASSERT_EQUAL_STRING("ON_HEADER", s.c_str());
    s = to_string(event_id::on_data);
    TEST_ASSERT_EQUAL_STRING("ON_DATA", s.c_str());
    s = to_string(event_id::on_finish);
    TEST_ASSERT_EQUAL_STRING("ON_FINISH", s.c_str());
    s = to_string(event_id::disconnected);
    TEST_ASSERT_EQUAL_STRING("DISCONNECTED", s.c_str());
    s = to_string(event_id::redirect);
    TEST_ASSERT_EQUAL_STRING("REDIRECT", s.c_str());
}

TEST_CASE("to_string(event_id) handles unknown values", "[idfxx][http]") {
    auto unknown = static_cast<event_id>(99);
    auto s = to_string(unknown);
    TEST_ASSERT_EQUAL_STRING("unknown(99)", s.c_str());
}

// -- Chunked request body tests ----------------------------------------------

/// Starts a loopback listener for the client to connect to.
static net::listener make_test_listener() {
    auto srv = net::listener::make({net::ipv4_addr(127, 0, 0, 1), 0}, {.reuse_address = true});
    TEST_ASSERT_TRUE(srv.has_value());
    return std::move(*srv);
}

static std::string test_url(net::listener& srv) {
    return "http://127.0.0.1:" + std::to_string(srv.local_endpoint().value().port()) + "/upload";
}

/// Accepts the client's connection and receives its request up to and including the given suffix.
static net::stream_socket accept_request(net::listener& srv, std::string& request, std::string_view suffix) {
    auto conn = srv.try_accept_for(std::chrono::seconds(5));
    TEST_ASSERT_TRUE(conn.has_value());
    conn->set_recv_timeout(std::chrono::seconds(5));

    request.clear();
    std::array<std::byte, 256> buf;
    while (!request.ends_with(suffix)) {
        auto received = conn->try_recv(buf);
        TEST_ASSERT_TRUE(received.has_value());
        TEST_ASSERT_FALSE(received->empty());
        request.append(reinterpret_cast<const char*>(received->data()), received->size());
        TEST_ASSERT_TRUE(request.size() < 4096);
    }
    return std::move(*conn);
}

/// Sends a fixed response and reads it back through the client.
static void complete_exchange(client& c, net::stream_socket& conn) {
    auto sent = conn.try_send_all(std::string_view{"HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: close\r\n\r\nok"});
    TEST_ASSERT_TRUE(sent.has_value());

    auto length = c.try_fetch_headers();
    TEST_ASSERT_TRUE(length.has_value());
    TEST_ASSERT_EQUAL(2, static_cast<int>(*length));
    TEST_ASSERT_EQUAL(200, c.status_code());

    std::string body;
    std::array<char, 16> buf;
    while (true) {
        auto n = c.try_read(buf);
        TEST_ASSERT_TRUE(n.has_value());
        if (*n == 0) break;
        body.append(buf.data(), *n);
    }
    TEST_ASSERT_EQUAL_STRING("ok", body.c_str());
    TEST_ASSERT_TRUE(c.try_close().has_value());
}

static std::string_view request_headers(std::string_view request) {
    auto end = request.find("\r\n\r\n");
    TEST_ASSERT_TRUE(end != std::string_view::npos);
    return request.substr(0, end + 2);
}

static std::string request_body(std::string_view request) {
    auto end = request.find("\r\n\r\n");
    TEST_ASSERT_TRUE(end != std::string_view::npos);
    return std::string{request.substr(end + 4)};
}

static bool contains(std::string_view haystack, std::string_view needle) {
    return haystack.find(needle) != std::string_view::npos;
}

TEST_CASE("http client sends a chunked request body", "[idfxx][http]") {
    test::ensure_event_loop();
    auto srv = make_test_listener();
    auto c = client::make({.url = test_url(srv), .method = method::post});
    TEST_ASSERT_TRUE(c.has_value());

    auto w = c->try_open_chunked();
    TEST_ASSERT_TRUE(w.has_value());
    TEST_ASSERT_TRUE(w->try_write(std::string_view{"hello"}).has_value());
    // Empty input sends nothing (a zero-length chunk would end the body)
    TEST_ASSERT_TRUE(w->try_write(std::string_view{}).has_value());
    constexpr std::string_view bytes = "0123456789abcdefg";
    TEST_ASSERT_TRUE(
        w->try_write(std::span<const uint8_t>{reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size()})
            .has_value()
    );
    TEST_ASSERT_TRUE(w->try_finish().has_value());

    std::string request;
    auto conn = accept_request(srv, request, "\r\n0\r\n\r\n");
    auto headers = request_headers(request);
    TEST_ASSERT_TRUE(headers.starts_with("POST /upload HTTP/1.1\r\n"));
    TEST_ASSERT_TRUE(contains(headers, "Transfer-Encoding: chunked\r\n"));
    TEST_ASSERT_FALSE(contains(headers, "Content-Length"));
    TEST_ASSERT_EQUAL_STRING(
        "5\r\nhello\r\n11\r\n0123456789abcdefg\r\n0\r\n\r\n", request_body(request).c_str()
    );

    complete_exchange(*c, conn);
}

TEST_CASE("http client chunk_writer rejects use after finish", "[idfxx][http]") {
    test::ensure_event_loop();
    auto srv = make_test_listener();
    auto c = client::make({.url = test_url(srv), .method = method::post});
    TEST_ASSERT_TRUE(c.has_value());

    auto w = c->try_open_chunked();
    TEST_ASSERT_TRUE(w.has_value());
    TEST_ASSERT_TRUE(w->try_finish().has_value());

    auto late_write = w->try_write(std::string_view{"late"});
    TEST_ASSERT_FALSE(late_write.has_value());
    TEST_ASSERT_TRUE(late_write.error() == idfxx::errc::invalid_state);
    auto second_finish = w->try_finish();
    TEST_ASSERT_FALSE(second_finish.has_value());
    TEST_ASSERT_TRUE(second_finish.error() == idfxx::errc::invalid_state);

    std::string request;
    auto conn = accept_request(srv, request, "\r\n\r\n0\r\n\r\n");
    TEST_ASSERT_EQUAL_STRING("0\r\n\r\n", request_body(request).c_str());

    complete_exchange(*c, conn);
}

TEST_CASE("http client moved chunk_writer continues the request body", "[idfxx][http]") {
    test::ensure_event_loop();
    auto srv = make_test_listener();
    auto c = client::make({.url = test_url(srv), .method = method::put});
    TEST_ASSERT_TRUE(c.has_value());

    auto opened = c->try_open_chunked();
    TEST_ASSERT_TRUE(opened.has_value());
    client::chunk_writer w = std::move(*opened);
    TEST_ASSERT_TRUE(w.try_write(std::string_view{"abc"}).has_value());
    client::chunk_writer w2 = std::move(w);
    TEST_ASSERT_TRUE(w2.try_write(std::string_view{"defghijklmnopqrstuvwxyz"}).has_value());
    TEST_ASSERT_TRUE(w2.try_finish().has_value());

    std::string request;
    auto conn = accept_request(srv, request, "\r\n0\r\n\r\n");
    TEST_ASSERT_TRUE(request_headers(request).starts_with("PUT /upload HTTP/1.1\r\n"));
    TEST_ASSERT_EQUAL_STRING(
        "3\r\nabc\r\n17\r\ndefghijklmnopqrstuvwxyz\r\n0\r\n\r\n", request_body(request).c_str()
    );

    complete_exchange(*c, conn);
}

TEST_CASE("http client chunked and fixed-length requests keep their own framing headers", "[idfxx][http]") {
    test::ensure_event_loop();
    auto srv = make_test_listener();
    auto c = client::make({.url = test_url(srv), .method = method::post});
    TEST_ASSERT_TRUE(c.has_value());
    std::string request;

    // Fixed-length request first, which sets a Content-Length header
    TEST_ASSERT_TRUE(c->try_open(3).has_value());
    auto written = c->try_write(std::string_view{"abc"});
    TEST_ASSERT_TRUE(written.has_value());
    {
        auto conn = accept_request(srv, request, "\r\n\r\nabc");
        TEST_ASSERT_TRUE(contains(request_headers(request), "Content-Length: 3\r\n"));
        TEST_ASSERT_FALSE(contains(request_headers(request), "Transfer-Encoding"));
        complete_exchange(*c, conn);
    }

    // A chunked request carries no Content-Length
    {
        auto w = c->try_open_chunked();
        TEST_ASSERT_TRUE(w.has_value());
        TEST_ASSERT_TRUE(w->try_write(std::string_view{"xyz"}).has_value());
        TEST_ASSERT_TRUE(w->try_finish().has_value());
    }
    {
        auto conn = accept_request(srv, request, "\r\n0\r\n\r\n");
        TEST_ASSERT_TRUE(contains(request_headers(request), "Transfer-Encoding: chunked\r\n"));
        TEST_ASSERT_FALSE(contains(request_headers(request), "Content-Length"));
        complete_exchange(*c, conn);
    }

    // A following fixed-length request carries no Transfer-Encoding
    TEST_ASSERT_TRUE(c->try_open(2).has_value());
    written = c->try_write(std::string_view{"de"});
    TEST_ASSERT_TRUE(written.has_value());
    {
        auto conn = accept_request(srv, request, "\r\n\r\nde");
        TEST_ASSERT_TRUE(contains(request_headers(request), "Content-Length: 2\r\n"));
        TEST_ASSERT_FALSE(contains(request_headers(request), "Transfer-Encoding"));
        complete_exchange(*c, conn);
    }
}

TEST_CASE("http client chunk_writer stops sending after a failed write", "[idfxx][http]") {
    test::ensure_event_loop();
    auto srv = make_test_listener();
    auto c = client::make({.url = test_url(srv), .method = method::post});
    TEST_ASSERT_TRUE(c.has_value());

    auto w = c->try_open_chunked();
    TEST_ASSERT_TRUE(w.has_value());

    // Close the server side with the request unread, so the connection is reset
    auto conn = srv.try_accept_for(std::chrono::seconds(5));
    TEST_ASSERT_TRUE(conn.has_value());
    conn->close();

    // Writes start failing once the reset reaches the client
    result<void> r{};
    for (int i = 0; i < 50 && r.has_value(); ++i) {
        vTaskDelay(pdMS_TO_TICKS(20));
        r = w->try_write(std::string_view{"data"});
    }
    TEST_ASSERT_FALSE(r.has_value());
    TEST_ASSERT_TRUE(r.error() == client::errc::write_data);

    // The failure is latched: nothing more is sent
    auto after = w->try_write(std::string_view{"more"});
    TEST_ASSERT_FALSE(after.has_value());
    TEST_ASSERT_TRUE(after.error() == idfxx::errc::invalid_state);
    auto fin = w->try_finish();
    TEST_ASSERT_FALSE(fin.has_value());
    TEST_ASSERT_TRUE(fin.error() == idfxx::errc::invalid_state);

    c->try_close();
}

TEST_CASE("http client chunk_writer reports a timeout as eagain and stops sending", "[idfxx][http]") {
    test::ensure_event_loop();
    auto srv = make_loopback_server();
    auto c = client::make({.url = loopback_url(srv), .method = method::post, .timeout = short_timeout});
    TEST_ASSERT_TRUE(c.has_value());

    auto w = c->try_open_chunked();
    TEST_ASSERT_TRUE(w.has_value());
    auto conn = srv.try_accept();
    TEST_ASSERT_TRUE(conn.has_value());

    // The server never reads, so the chunk stalls once the loopback TCP send
    // buffer and receive window are full (a few KiB each).
    std::vector<uint8_t> body(32 * 1024, 0x55);
    auto r = w->try_write(body);
    TEST_ASSERT_FALSE(r.has_value());
    TEST_ASSERT_TRUE(r.error() == client::errc::eagain);

    // The failure is latched: nothing more is sent
    auto after = w->try_write(std::string_view{"more"});
    TEST_ASSERT_FALSE(after.has_value());
    TEST_ASSERT_TRUE(after.error() == idfxx::errc::invalid_state);
    auto fin = w->try_finish();
    TEST_ASSERT_FALSE(fin.has_value());
    TEST_ASSERT_TRUE(fin.error() == idfxx::errc::invalid_state);

    std::ignore = c->try_close();
}

TEST_CASE("http client refuses an abandoned chunked request and starts the next one afresh", "[idfxx][http]") {
    test::ensure_event_loop();
    auto srv = make_test_listener();
    auto c = client::make({.url = test_url(srv), .method = method::post});
    TEST_ASSERT_TRUE(c.has_value());

    {
        auto w = c->try_open_chunked();
        TEST_ASSERT_TRUE(w.has_value());
        TEST_ASSERT_TRUE(w->try_write(std::string_view{"hello"}).has_value());
        // Discarded without finish(): the body is left unterminated
    }
    auto first = srv.try_accept_for(std::chrono::seconds(5));
    TEST_ASSERT_TRUE(first.has_value());
    first->set_recv_timeout(std::chrono::seconds(5));

    // The abandoned request is refused rather than waiting on a response the
    // server will never send (which would block until the 5 s timeout)
    auto headers = c->try_fetch_headers();
    TEST_ASSERT_FALSE(headers.has_value());
    TEST_ASSERT_TRUE(headers.error() == idfxx::errc::invalid_state);
    std::array<char, 16> buf;
    auto body_read = c->try_read(buf);
    TEST_ASSERT_FALSE(body_read.has_value());
    TEST_ASSERT_TRUE(body_read.error() == idfxx::errc::invalid_state);
    auto raw_write = c->try_write(std::string_view{"0\r\n\r\n"});
    TEST_ASSERT_FALSE(raw_write.has_value());
    TEST_ASSERT_TRUE(raw_write.error() == idfxx::errc::invalid_state);

    // The next request closes the abandoned connection and uses a new one
    TEST_ASSERT_TRUE(c->try_open(2).has_value());
    auto written = c->try_write(std::string_view{"ok"});
    TEST_ASSERT_TRUE(written.has_value());

    // The first connection carries only the unterminated chunked request, then ends
    std::string abandoned;
    std::array<std::byte, 256> raw;
    while (true) {
        auto received = first->try_recv(raw);
        TEST_ASSERT_TRUE(received.has_value());
        if (received->empty()) break;
        abandoned.append(reinterpret_cast<const char*>(received->data()), received->size());
        TEST_ASSERT_TRUE(abandoned.size() < 4096);
    }
    TEST_ASSERT_TRUE(contains(request_headers(abandoned), "Transfer-Encoding: chunked\r\n"));
    TEST_ASSERT_EQUAL_STRING("5\r\nhello\r\n", request_body(abandoned).c_str());

    // The second connection carries a clean fixed-length request
    std::string request;
    auto second = accept_request(srv, request, "\r\n\r\nok");
    TEST_ASSERT_TRUE(request_headers(request).starts_with("POST /upload HTTP/1.1\r\n"));
    TEST_ASSERT_TRUE(contains(request_headers(request), "Content-Length: 2\r\n"));
    TEST_ASSERT_FALSE(contains(request_headers(request), "Transfer-Encoding"));
    TEST_ASSERT_EQUAL_STRING("ok", request_body(request).c_str());

    complete_exchange(*c, second);
}

TEST_CASE("http client try_open_chunked fails when the server is unreachable", "[idfxx][http]") {
    test::ensure_event_loop();
    auto srv = make_test_listener();
    auto url = test_url(srv);
    srv.close();

    auto c = client::make({.url = url, .method = method::post});
    TEST_ASSERT_TRUE(c.has_value());
    auto w = c->try_open_chunked();
    TEST_ASSERT_FALSE(w.has_value());
    TEST_ASSERT_TRUE(w.error() == client::errc::connect);
}

// -- Formatter tests ---------------------------------------------------------

#ifdef CONFIG_IDFXX_STD_FORMAT
static_assert(std::formattable<event_id, char>);

TEST_CASE("event_id formatter outputs correct names", "[idfxx][http]") {
    auto s = std::format("{}", event_id::error);
    TEST_ASSERT_EQUAL_STRING("ERROR", s.c_str());
    s = std::format("{}", event_id::on_connected);
    TEST_ASSERT_EQUAL_STRING("ON_CONNECTED", s.c_str());
    s = std::format("{}", event_id::redirect);
    TEST_ASSERT_EQUAL_STRING("REDIRECT", s.c_str());
}
#endif // CONFIG_IDFXX_STD_FORMAT
