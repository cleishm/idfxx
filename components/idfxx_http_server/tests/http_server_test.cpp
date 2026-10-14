// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Unit tests for idfxx http server
// Uses ESP-IDF Unity test framework with compile-time static_asserts

#include "http_test_utils.hpp"
#include "idfxx/http/client"
#include "idfxx/http/server"
#include "unity.h"

#include <arpa/inet.h>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <esp_idf_version.h>
#include <memory>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <type_traits>
#include <unistd.h>
#include <utility>

#if defined(CONFIG_HTTPD_WS_SUPPORT) && ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
#include <algorithm>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <vector>
#endif

using namespace idfxx;
using namespace idfxx::http;

// =============================================================================
// Compile-time tests (static_assert)
// These verify correctness at compile time - if this file compiles, they pass.
// Note: Enum values are verified against ESP-IDF constants in server.cpp
// =============================================================================

// server is not default constructible (must use make() factory or explicit config)
static_assert(!std::is_default_constructible_v<server>);

// server is not copyable (unique ownership of handle)
static_assert(!std::is_copy_constructible_v<server>);
static_assert(!std::is_copy_assignable_v<server>);

// server is move-only
static_assert(std::is_move_constructible_v<server>);
static_assert(std::is_move_assignable_v<server>);

// request is not default constructible
static_assert(!std::is_default_constructible_v<request>);

// request is not copyable
static_assert(!std::is_copy_constructible_v<request>);
static_assert(!std::is_copy_assignable_v<request>);

// request is not movable
static_assert(!std::is_move_constructible_v<request>);
static_assert(!std::is_move_assignable_v<request>);

// server::errc is an error_code_enum
static_assert(std::is_error_code_enum_v<server::errc>);

// status_code values are the HTTP status numbers
static_assert(std::to_underlying(status_code::continue_) == 100);
static_assert(std::to_underlying(status_code::ok) == 200);
static_assert(std::to_underlying(status_code::permanent_redirect) == 308);
static_assert(std::to_underlying(status_code::not_found) == 404);
static_assert(std::to_underlying(status_code::request_header_fields_too_large) == 431);
static_assert(std::to_underlying(status_code::internal_server_error) == 500);
static_assert(std::to_underlying(status_code::network_authentication_required) == 511);

// error_status accepts only the codes the server can pass to an error handler
static_assert(error_status{status_code::not_found}.code() == status_code::not_found);
static_assert(error_status::from(status_code::not_found)->code() == status_code::not_found);
static_assert(error_status::from(status_code::http_version_not_supported).has_value());
static_assert(error_status::from(status_code::ok) == std::nullopt);
static_assert(error_status::from(status_code::service_unavailable) == std::nullopt);
static_assert(error_status::from(static_cast<status_code>(418)) == std::nullopt);
static_assert(error_status{status_code::forbidden} == error_status{status_code::forbidden});
static_assert(error_status{status_code::forbidden} != error_status{status_code::not_found});
static_assert(std::is_convertible_v<status_code, error_status>);

// error handlers receive the request and the status code
static_assert(std::is_same_v<server::error_handler_type,
                             std::move_only_function<result<void>(request&, status_code) const>>);
static_assert(std::is_same_v<decltype(std::declval<request&>().try_handle_error(status_code::not_found)), result<void>>);

// =============================================================================
// WebSocket compile-time tests
// =============================================================================

#ifdef CONFIG_HTTPD_WS_SUPPORT
// ws_frame_type enum values verified in server.cpp
static_assert(std::to_underlying(request::ws_frame_type::continuation) == 0x0);
static_assert(std::to_underlying(request::ws_frame_type::text) == 0x1);
static_assert(std::to_underlying(request::ws_frame_type::binary) == 0x2);
static_assert(std::to_underlying(request::ws_frame_type::close) == 0x8);
static_assert(std::to_underlying(request::ws_frame_type::ping) == 0x9);
static_assert(std::to_underlying(request::ws_frame_type::pong) == 0xA);

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 1, 0)
// ws_frame_reader is move-only and only obtained from a request
static_assert(!std::is_default_constructible_v<request::ws_frame_reader>);
static_assert(!std::is_copy_constructible_v<request::ws_frame_reader>);
static_assert(!std::is_copy_assignable_v<request::ws_frame_reader>);
static_assert(std::is_nothrow_move_constructible_v<request::ws_frame_reader>);
static_assert(std::is_nothrow_move_assignable_v<request::ws_frame_reader>);
static_assert(std::is_same_v<decltype(std::declval<request&>().try_ws_recv_begin()), result<request::ws_frame_reader>>);
static_assert(std::is_same_v<
              decltype(std::declval<request::ws_frame_reader&>().try_read(std::declval<std::span<uint8_t>>())),
              result<std::span<const uint8_t>>>);
static_assert(std::is_same_v<decltype(std::declval<request::ws_frame_reader&>().try_skip()), result<void>>);
#endif
#endif


#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 1, 0)
static_assert(std::is_same_v<decltype(server::config::bind_to_device), std::optional<std::string_view>>);
#endif

// Keep-alive tuning is optional: std::nullopt lets the driver choose
static_assert(std::is_same_v<decltype(server::config::keep_alive_idle), std::optional<std::chrono::seconds>>);
static_assert(std::is_same_v<decltype(server::config::keep_alive_interval), std::optional<std::chrono::seconds>>);
static_assert(std::is_same_v<decltype(server::config::keep_alive_count), std::optional<int>>);

// =============================================================================
// Runtime tests (Unity TEST_CASE)
// =============================================================================

// -- Error category tests ----------------------------------------------------

TEST_CASE("http server error_category has correct name", "[idfxx][http]") {
    TEST_ASSERT_EQUAL_STRING("http::server::Error", http_server_category().name());
}

TEST_CASE("http server error_category produces messages", "[idfxx][http]") {
    std::error_code ec = server::errc::handlers_full;
    TEST_ASSERT_TRUE(ec.message().length() > 0);

    ec = server::errc::handler_exists;
    TEST_ASSERT_TRUE(ec.message().length() > 0);

    ec = server::errc::resp_send;
    TEST_ASSERT_TRUE(ec.message().length() > 0);

    ec = server::errc::task;
    TEST_ASSERT_TRUE(ec.message().length() > 0);
}

TEST_CASE("make_error_code creates correct error codes", "[idfxx][http]") {
    std::error_code ec = make_error_code(server::errc::handlers_full);
    TEST_ASSERT_EQUAL(std::to_underlying(server::errc::handlers_full), ec.value());
    TEST_ASSERT_EQUAL_STRING("http::server::Error", ec.category().name());

    ec = make_error_code(server::errc::task);
    TEST_ASSERT_EQUAL(std::to_underlying(server::errc::task), ec.value());
}

// =============================================================================
// Integration tests (server + client over loopback)
// =============================================================================

static uint16_t next_test_port = 18080;
static uint16_t next_ctrl_port = 42768;

/// Returns a unique port pair for each test to avoid EADDRINUSE.
static uint16_t alloc_test_port() {
    return next_test_port++;
}

static uint16_t alloc_ctrl_port() {
    return next_ctrl_port++;
}

static void ensure_event_loop() { test::ensure_event_loop(); }
static std::string read_response_body(client& c) { return test::read_response_body(c); }

TEST_CASE("http server handles GET request", "[idfxx][http]") {
    ensure_event_loop();
    auto port = alloc_test_port();
    auto srv = server::make({.server_port = port, .ctrl_port = alloc_ctrl_port()});
    TEST_ASSERT_TRUE(srv.has_value());

    auto r = srv->try_on_get("/hello", [](request& req) -> result<void> {
        req.set_content_type("text/plain");
        return req.try_send("Hello, World!");
    });
    TEST_ASSERT_TRUE(r.has_value());

    auto url = "http://127.0.0.1:" + std::to_string(port) + "/hello";
    auto c = client::make({.url = url});
    TEST_ASSERT_TRUE(c.has_value());
    auto body = read_response_body(*c);

    TEST_ASSERT_EQUAL(200, c->status_code());
    TEST_ASSERT_EQUAL_STRING("Hello, World!", body.c_str());
}

TEST_CASE("http server handles POST with body", "[idfxx][http]") {
    ensure_event_loop();
    auto port = alloc_test_port();
    auto srv = server::make({.server_port = port, .ctrl_port = alloc_ctrl_port()});
    TEST_ASSERT_TRUE(srv.has_value());

    auto r = srv->try_on_post("/echo", [](request& req) -> result<void> {
        auto body = req.try_recv_body();
        if (!body) return idfxx::error(body.error());
        req.set_content_type("text/plain");
        return req.try_send(*body);
    });
    TEST_ASSERT_TRUE(r.has_value());

    auto url = "http://127.0.0.1:" + std::to_string(port) + "/echo";
    auto c = client::make({.url = url, .method = method::post});
    TEST_ASSERT_TRUE(c.has_value());

    std::string_view payload = "test payload";
    auto r2 = c->try_open(payload.size());
    TEST_ASSERT_TRUE(r2.has_value());
    auto written = c->try_write(payload);
    TEST_ASSERT_TRUE(written.has_value());
    auto r3 = c->try_fetch_headers();
    TEST_ASSERT_TRUE(r3.has_value());
    std::string body;
    std::array<char, 256> buf;
    while (true) {
        auto n = c->try_read(buf);
        TEST_ASSERT_TRUE(n.has_value());
        if (*n == 0) break;
        body.append(buf.data(), *n);
    }
    auto r4 = c->try_close();
    TEST_ASSERT_TRUE(r4.has_value());

    TEST_ASSERT_EQUAL(200, c->status_code());
    TEST_ASSERT_EQUAL_STRING("test payload", body.c_str());
}

TEST_CASE("http server sets custom status code", "[idfxx][http]") {
    ensure_event_loop();
    auto port = alloc_test_port();
    auto srv = server::make({.server_port = port, .ctrl_port = alloc_ctrl_port()});
    TEST_ASSERT_TRUE(srv.has_value());

    auto r = srv->try_on_get("/created", [](request& req) -> result<void> {
        req.set_status(201);
        return req.try_send("created");
    });
    TEST_ASSERT_TRUE(r.has_value());

    auto url = "http://127.0.0.1:" + std::to_string(port) + "/created";
    auto c = client::make({.url = url});
    TEST_ASSERT_TRUE(c.has_value());
    auto body = read_response_body(*c);

    TEST_ASSERT_EQUAL(201, c->status_code());
}

TEST_CASE("http server sets custom response headers", "[idfxx][http]") {
    ensure_event_loop();
    auto port = alloc_test_port();
    auto srv = server::make({.server_port = port, .ctrl_port = alloc_ctrl_port()});
    TEST_ASSERT_TRUE(srv.has_value());

    auto r = srv->try_on_get("/headers", [](request& req) -> result<void> {
        req.set_header("X-Custom", "test-value");
        return req.try_send("ok");
    });
    TEST_ASSERT_TRUE(r.has_value());

    std::string custom_hdr;
    auto url = "http://127.0.0.1:" + std::to_string(port) + "/headers";
    auto c = client::make({
        .url = url,
        .on_event =
            [&custom_hdr](const event_data& ev) {
                if (ev.id == event_id::on_header && ev.header_key == "X-Custom") {
                    custom_hdr = std::string(ev.header_value);
                }
            },
    });
    TEST_ASSERT_TRUE(c.has_value());
    auto body = read_response_body(*c);

    TEST_ASSERT_EQUAL(200, c->status_code());
    TEST_ASSERT_EQUAL_STRING("test-value", custom_hdr.c_str());
}

TEST_CASE("http server reads request headers", "[idfxx][http]") {
    ensure_event_loop();
    auto port = alloc_test_port();
    auto srv = server::make({.server_port = port, .ctrl_port = alloc_ctrl_port()});
    TEST_ASSERT_TRUE(srv.has_value());

    auto r = srv->try_on_get("/read-hdr", [](request& req) -> result<void> {
        auto val = req.header("X-Test-Input");
        if (val) {
            return req.try_send(*val);
        }
        return req.try_send("missing");
    });
    TEST_ASSERT_TRUE(r.has_value());

    auto url = "http://127.0.0.1:" + std::to_string(port) + "/read-hdr";
    auto c = client::make({.url = url});
    TEST_ASSERT_TRUE(c.has_value());
    c->set_header("X-Test-Input", "from-client");
    auto body = read_response_body(*c);

    TEST_ASSERT_EQUAL(200, c->status_code());
    TEST_ASSERT_EQUAL_STRING("from-client", body.c_str());
}

TEST_CASE("http server reads query parameters", "[idfxx][http]") {
    ensure_event_loop();
    auto port = alloc_test_port();
    auto srv = server::make({.server_port = port, .ctrl_port = alloc_ctrl_port()});
    TEST_ASSERT_TRUE(srv.has_value());

    auto r = srv->try_on_get("/query", [](request& req) -> result<void> {
        auto val = req.query_param("key");
        if (val) {
            return req.try_send(*val);
        }
        return req.try_send("missing");
    });
    TEST_ASSERT_TRUE(r.has_value());

    auto url = "http://127.0.0.1:" + std::to_string(port) + "/query?key=hello";
    auto c = client::make({.url = url});
    TEST_ASSERT_TRUE(c.has_value());
    auto body = read_response_body(*c);

    TEST_ASSERT_EQUAL(200, c->status_code());
    TEST_ASSERT_EQUAL_STRING("hello", body.c_str());
}

TEST_CASE("http server unregisters handler", "[idfxx][http]") {
    ensure_event_loop();
    auto port = alloc_test_port();
    auto srv = server::make({.server_port = port, .ctrl_port = alloc_ctrl_port()});
    TEST_ASSERT_TRUE(srv.has_value());

    auto r = srv->try_on_get("/temp", [](request& req) -> result<void> { return req.try_send("ok"); });
    TEST_ASSERT_TRUE(r.has_value());

    auto r2 = srv->try_unregister_handler(method::get, "/temp");
    TEST_ASSERT_TRUE(r2.has_value());

    // After unregistration, should get 404
    auto url = "http://127.0.0.1:" + std::to_string(port) + "/temp";
    auto c = client::make({.url = url});
    TEST_ASSERT_TRUE(c.has_value());
    auto r3 = c->try_open();
    TEST_ASSERT_TRUE(r3.has_value());
    auto r4 = c->try_fetch_headers();
    TEST_ASSERT_TRUE(r4.has_value());
    auto r5 = c->try_close();
    TEST_ASSERT_TRUE(r5.has_value());

    TEST_ASSERT_EQUAL(404, c->status_code());
}

TEST_CASE("http server handler_exists error on duplicate registration", "[idfxx][http]") {
    ensure_event_loop();
    auto port = alloc_test_port();
    auto srv = server::make({.server_port = port, .ctrl_port = alloc_ctrl_port()});
    TEST_ASSERT_TRUE(srv.has_value());

    auto handler = [](request& req) -> result<void> { return req.try_send("ok"); };

    auto r = srv->try_on_get("/dup", handler);
    TEST_ASSERT_TRUE(r.has_value());

    auto r2 = srv->try_on_get("/dup", handler);
    TEST_ASSERT_FALSE(r2.has_value());
    TEST_ASSERT_EQUAL(std::to_underlying(server::errc::handler_exists), r2.error().value());
}

TEST_CASE("http server config defaults", "[idfxx][http]") {
    server::config cfg{};
    TEST_ASSERT_EQUAL(CONFIG_HTTPD_MAX_REQ_HDR_LEN, cfg.max_req_hdr_len);
    TEST_ASSERT_EQUAL(CONFIG_HTTPD_MAX_URI_LEN, cfg.max_uri_len);
    TEST_ASSERT_FALSE(cfg.keep_alive_idle.has_value());
    TEST_ASSERT_FALSE(cfg.keep_alive_interval.has_value());
    TEST_ASSERT_FALSE(cfg.keep_alive_count.has_value());
}

TEST_CASE("http server starts with explicit limits and keep-alive settings", "[idfxx][http]") {
    ensure_event_loop();
    auto srv = server::make({
        .server_port = alloc_test_port(),
        .ctrl_port = alloc_ctrl_port(),
        .max_req_hdr_len = 2048,
        .max_uri_len = 256,
        .keep_alive_enable = true,
        .keep_alive_idle = std::chrono::seconds{10},
        .keep_alive_interval = std::chrono::seconds{2},
        .keep_alive_count = 4,
    });
    TEST_ASSERT_TRUE(srv.has_value());
}

TEST_CASE("http server rejects a zero max_req_hdr_len", "[idfxx][http]") {
    ensure_event_loop();
    auto srv = server::make({.server_port = alloc_test_port(), .ctrl_port = alloc_ctrl_port(), .max_req_hdr_len = 0});
    TEST_ASSERT_FALSE(srv.has_value());
    TEST_ASSERT_TRUE(srv.error() == idfxx::errc::invalid_arg);
}

TEST_CASE("http server rejects a zero max_uri_len", "[idfxx][http]") {
    ensure_event_loop();
    auto srv = server::make({.server_port = alloc_test_port(), .ctrl_port = alloc_ctrl_port(), .max_uri_len = 0});
    TEST_ASSERT_FALSE(srv.has_value());
    TEST_ASSERT_TRUE(srv.error() == idfxx::errc::invalid_arg);
}

TEST_CASE("http server rejects zero keep-alive settings", "[idfxx][http]") {
    ensure_event_loop();
    auto idle = server::make({
        .server_port = alloc_test_port(),
        .ctrl_port = alloc_ctrl_port(),
        .keep_alive_enable = true,
        .keep_alive_idle = std::chrono::seconds{0},
    });
    TEST_ASSERT_FALSE(idle.has_value());
    TEST_ASSERT_TRUE(idle.error() == idfxx::errc::invalid_arg);

    auto interval = server::make({
        .server_port = alloc_test_port(),
        .ctrl_port = alloc_ctrl_port(),
        .keep_alive_enable = true,
        .keep_alive_interval = std::chrono::seconds{0},
    });
    TEST_ASSERT_FALSE(interval.has_value());
    TEST_ASSERT_TRUE(interval.error() == idfxx::errc::invalid_arg);

    auto count = server::make({
        .server_port = alloc_test_port(),
        .ctrl_port = alloc_ctrl_port(),
        .keep_alive_enable = true,
        .keep_alive_count = 0,
    });
    TEST_ASSERT_FALSE(count.has_value());
    TEST_ASSERT_TRUE(count.error() == idfxx::errc::invalid_arg);
}

// =============================================================================
// Interface binding (ESP-IDF 6.1+)
// =============================================================================

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 1, 0)
TEST_CASE("http server bound to the loopback interface serves loopback requests", "[idfxx][http]") {
    ensure_event_loop();
    auto port = alloc_test_port();
    auto srv = server::make({.server_port = port, .ctrl_port = alloc_ctrl_port(), .bind_to_device = "lo0"});
    TEST_ASSERT_TRUE(srv.has_value());

    auto r = srv->try_on_get("/bound", [](request& req) -> result<void> { return req.try_send("bound"); });
    TEST_ASSERT_TRUE(r.has_value());

    auto url = "http://127.0.0.1:" + std::to_string(port) + "/bound";
    auto c = client::make({.url = url});
    TEST_ASSERT_TRUE(c.has_value());
    auto body = read_response_body(*c);

    TEST_ASSERT_EQUAL(200, c->status_code());
    TEST_ASSERT_EQUAL_STRING("bound", body.c_str());
}

TEST_CASE("http server fails to start on an unknown interface", "[idfxx][http]") {
    ensure_event_loop();
    auto srv = server::make({.server_port = alloc_test_port(), .ctrl_port = alloc_ctrl_port(), .bind_to_device = "zz9"});
    TEST_ASSERT_FALSE(srv.has_value());
}

TEST_CASE("http server rejects an over-long interface name", "[idfxx][http]") {
    ensure_event_loop();
    auto srv =
        server::make({.server_port = alloc_test_port(), .ctrl_port = alloc_ctrl_port(), .bind_to_device = "toolong"});
    TEST_ASSERT_FALSE(srv.has_value());
    TEST_ASSERT_TRUE(srv.error() == idfxx::errc::invalid_arg);
}
#endif

// =============================================================================
// Error handlers
// =============================================================================

namespace {

/// Minimal HTTP/1.1 client over a loopback TCP socket, for checking whether the
/// server keeps a connection open after a response.
class http_test_conn {
public:
    struct response {
        int status = 0;
        std::string body;
    };

    explicit http_test_conn(uint16_t port) {
        _fd = socket(AF_INET, SOCK_STREAM, 0);
        TEST_ASSERT_TRUE(_fd >= 0);

        timeval tv{.tv_sec = 5, .tv_usec = 0};
        setsockopt(_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        TEST_ASSERT_EQUAL(0, connect(_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)));
    }

    ~http_test_conn() {
        if (_fd >= 0) {
            close(_fd);
        }
    }

    http_test_conn(const http_test_conn&) = delete;
    http_test_conn& operator=(const http_test_conn&) = delete;

    /// Sends a GET request and receives the response, failing the test if
    /// no complete response arrives.
    response get(std::string_view uri) {
        std::string req = "GET " + std::string(uri) + " HTTP/1.1\r\n"
                          "Host: 127.0.0.1\r\n"
                          "\r\n";
        size_t sent = 0;
        while (sent < req.size()) {
            int n = send(_fd, req.data() + sent, req.size() - sent, 0);
            TEST_ASSERT_TRUE(n > 0);
            sent += static_cast<size_t>(n);
        }

        // Read the head one byte at a time so nothing past it is consumed
        std::string head;
        while (head.size() < 4 || head.compare(head.size() - 4, 4, "\r\n\r\n") != 0) {
            char ch;
            int n = recv(_fd, &ch, 1, 0);
            TEST_ASSERT_EQUAL(1, n);
            head.push_back(ch);
        }
        TEST_ASSERT_TRUE(head.starts_with("HTTP/1.1 "));

        response resp;
        resp.status = std::atoi(head.c_str() + 9);
        size_t len = 0;
        auto pos = head.find("Content-Length: ");
        if (pos != std::string::npos) {
            len = static_cast<size_t>(std::atoi(head.c_str() + pos + 16));
        }
        resp.body.resize(len);
        size_t got = 0;
        while (got < len) {
            int n = recv(_fd, resp.body.data() + got, len - got, 0);
            TEST_ASSERT_TRUE(n > 0);
            got += static_cast<size_t>(n);
        }
        return resp;
    }

    /// Returns whether the server has closed the connection, waiting for it
    /// up to the receive timeout.
    bool closed() {
        char ch;
        int n = recv(_fd, &ch, 1, 0);
        return n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK);
    }

private:
    int _fd = -1;
};

} // namespace

TEST_CASE("http server error handler serves a custom not-found response", "[idfxx][http]") {
    ensure_event_loop();
    auto port = alloc_test_port();
    auto srv = server::make({.server_port = port, .ctrl_port = alloc_ctrl_port()});
    TEST_ASSERT_TRUE(srv.has_value());

    auto r = srv->try_on_error(status_code::not_found, [](request& req, status_code code) -> result<void> {
        req.set_status(404);
        req.set_content_type("text/plain");
        return req.try_send("custom 404 for " + std::to_string(std::to_underlying(code)));
    });
    TEST_ASSERT_TRUE(r.has_value());

    auto url = "http://127.0.0.1:" + std::to_string(port) + "/missing";
    auto c = client::make({.url = url});
    TEST_ASSERT_TRUE(c.has_value());
    auto body = read_response_body(*c);

    TEST_ASSERT_EQUAL(404, c->status_code());
    TEST_ASSERT_EQUAL_STRING("custom 404 for 404", body.c_str());
}

TEST_CASE("http server error handler returning success keeps the connection open", "[idfxx][http]") {
    ensure_event_loop();
    auto port = alloc_test_port();
    auto srv = server::make({.server_port = port, .ctrl_port = alloc_ctrl_port()});
    TEST_ASSERT_TRUE(srv.has_value());

    auto r = srv->try_on_error(status_code::not_found, [](request& req, status_code) -> result<void> {
        req.set_status(404);
        return req.try_send("gone");
    });
    TEST_ASSERT_TRUE(r.has_value());
    auto r2 = srv->try_on_get("/ok", [](request& req) -> result<void> { return req.try_send("ok"); });
    TEST_ASSERT_TRUE(r2.has_value());

    http_test_conn conn{port};
    auto first = conn.get("/missing");
    TEST_ASSERT_EQUAL(404, first.status);
    TEST_ASSERT_EQUAL_STRING("gone", first.body.c_str());

    // The same connection serves further requests
    auto second = conn.get("/missing");
    TEST_ASSERT_EQUAL(404, second.status);
    TEST_ASSERT_EQUAL_STRING("gone", second.body.c_str());
    auto third = conn.get("/ok");
    TEST_ASSERT_EQUAL(200, third.status);
    TEST_ASSERT_EQUAL_STRING("ok", third.body.c_str());
}

TEST_CASE("http server error handler returning an error closes the connection", "[idfxx][http]") {
    ensure_event_loop();
    auto port = alloc_test_port();
    auto srv = server::make({.server_port = port, .ctrl_port = alloc_ctrl_port()});
    TEST_ASSERT_TRUE(srv.has_value());

    auto r = srv->try_on_error(status_code::not_found, [](request& req, status_code) -> result<void> {
        req.set_status(404);
        auto s = req.try_send("gone");
        if (!s) return s;
        return idfxx::error(idfxx::errc::fail);
    });
    TEST_ASSERT_TRUE(r.has_value());

    http_test_conn conn{port};
    auto resp = conn.get("/missing");
    TEST_ASSERT_EQUAL(404, resp.status);
    TEST_ASSERT_EQUAL_STRING("gone", resp.body.c_str());
    TEST_ASSERT_TRUE(conn.closed());
}

TEST_CASE("request handle_error invokes the registered error handler", "[idfxx][http]") {
    ensure_event_loop();
    auto port = alloc_test_port();
    auto srv = server::make({.server_port = port, .ctrl_port = alloc_ctrl_port()});
    TEST_ASSERT_TRUE(srv.has_value());

    auto seen = std::make_shared<std::atomic<int>>(0);
    auto handled = std::make_shared<std::atomic<int>>(0);
    auto r = srv->try_on_error(status_code::forbidden, [seen](request& req, status_code code) -> result<void> {
        seen->store(std::to_underlying(code));
        req.set_status(403);
        return req.try_send("denied: " + std::string(req.uri()));
    });
    TEST_ASSERT_TRUE(r.has_value());
    auto r2 = srv->try_on_get("/secret", [handled](request& req) -> result<void> {
        auto res = req.try_handle_error(status_code::forbidden);
        handled->store(res ? 1 : -1);
        return res;
    });
    TEST_ASSERT_TRUE(r2.has_value());

    http_test_conn conn{port};
    auto resp = conn.get("/secret");
    TEST_ASSERT_EQUAL(403, resp.status);
    TEST_ASSERT_EQUAL_STRING("denied: /secret", resp.body.c_str());

    // The error handler succeeded, so the connection stays open
    auto again = conn.get("/secret");
    TEST_ASSERT_EQUAL(403, again.status);
    TEST_ASSERT_EQUAL(403, seen->load());
    TEST_ASSERT_EQUAL(1, handled->load());
}

TEST_CASE("request handle_error without an error handler sends the default response and closes", "[idfxx][http]") {
    ensure_event_loop();
    auto port = alloc_test_port();
    auto srv = server::make({.server_port = port, .ctrl_port = alloc_ctrl_port()});
    TEST_ASSERT_TRUE(srv.has_value());

    auto handled = std::make_shared<std::atomic<int>>(0);
    auto r = srv->try_on_get("/private", [handled](request& req) -> result<void> {
        auto res = req.try_handle_error(status_code::unauthorized);
        handled->store(res ? 1 : -1);
        return res;
    });
    TEST_ASSERT_TRUE(r.has_value());

    http_test_conn conn{port};
    auto resp = conn.get("/private");
    TEST_ASSERT_EQUAL(401, resp.status);
    TEST_ASSERT_TRUE(resp.body.size() > 0);
    TEST_ASSERT_TRUE(conn.closed());
    TEST_ASSERT_EQUAL(1, handled->load());
}

TEST_CASE("request handle_error with internal_server_error closes even when the handler succeeds", "[idfxx][http]") {
    ensure_event_loop();
    auto port = alloc_test_port();
    auto srv = server::make({.server_port = port, .ctrl_port = alloc_ctrl_port()});
    TEST_ASSERT_TRUE(srv.has_value());

    auto r = srv->try_on_error(status_code::internal_server_error, [](request& req, status_code) -> result<void> {
        req.set_status(500);
        return req.try_send("oops");
    });
    TEST_ASSERT_TRUE(r.has_value());
    auto r2 = srv->try_on_get(
        "/broken", [](request& req) -> result<void> { return req.try_handle_error(status_code::internal_server_error); }
    );
    TEST_ASSERT_TRUE(r2.has_value());

    http_test_conn conn{port};
    auto resp = conn.get("/broken");
    TEST_ASSERT_EQUAL(500, resp.status);
    TEST_ASSERT_EQUAL_STRING("oops", resp.body.c_str());
    TEST_ASSERT_TRUE(conn.closed());
}

TEST_CASE("http server handler_exists error on duplicate error handler", "[idfxx][http]") {
    ensure_event_loop();
    auto srv = server::make({.server_port = alloc_test_port(), .ctrl_port = alloc_ctrl_port()});
    TEST_ASSERT_TRUE(srv.has_value());

    auto handler = [](request& req, status_code) -> result<void> { return req.try_send("err"); };

    auto r = srv->try_on_error(status_code::not_found, handler);
    TEST_ASSERT_TRUE(r.has_value());

    auto r2 = srv->try_on_error(status_code::not_found, handler);
    TEST_ASSERT_FALSE(r2.has_value());
    TEST_ASSERT_EQUAL(std::to_underlying(server::errc::handler_exists), r2.error().value());
    TEST_ASSERT_TRUE(r2.error() == server::errc::handler_exists);

    // Other codes are independent
    auto r3 = srv->try_on_error(status_code::method_not_allowed, handler);
    TEST_ASSERT_TRUE(r3.has_value());
}

TEST_CASE("http server unregistering an error handler restores the default response", "[idfxx][http]") {
    ensure_event_loop();
    auto port = alloc_test_port();
    auto srv = server::make({.server_port = port, .ctrl_port = alloc_ctrl_port()});
    TEST_ASSERT_TRUE(srv.has_value());

    auto r = srv->try_on_error(status_code::not_found, [](request& req, status_code) -> result<void> {
        req.set_status(404);
        return req.try_send("custom");
    });
    TEST_ASSERT_TRUE(r.has_value());

    auto r2 = srv->try_unregister_error_handler(status_code::not_found);
    TEST_ASSERT_TRUE(r2.has_value());

    // Unregistering again finds no handler, as for URI handlers
    auto r3 = srv->try_unregister_error_handler(status_code::not_found);
    TEST_ASSERT_FALSE(r3.has_value());
    TEST_ASSERT_TRUE(r3.error() == idfxx::errc::not_found);

    http_test_conn conn{port};
    auto resp = conn.get("/missing");
    TEST_ASSERT_EQUAL(404, resp.status);
    TEST_ASSERT_TRUE(resp.body != "custom");
    TEST_ASSERT_TRUE(resp.body.size() > 0);
    TEST_ASSERT_TRUE(conn.closed());

    // A handler can be registered again
    auto r4 = srv->try_on_error(status_code::not_found, [](request& req, status_code) -> result<void> {
        return req.try_send("again");
    });
    TEST_ASSERT_TRUE(r4.has_value());
}

TEST_CASE("http server rejects an empty error handler", "[idfxx][http]") {
    ensure_event_loop();
    auto srv = server::make({.server_port = alloc_test_port(), .ctrl_port = alloc_ctrl_port()});
    TEST_ASSERT_TRUE(srv.has_value());

    auto r = srv->try_on_error(status_code::not_found, server::error_handler_type{});
    TEST_ASSERT_FALSE(r.has_value());
    TEST_ASSERT_TRUE(r.error() == idfxx::errc::invalid_arg);
}

// =============================================================================
// WebSocket (over a raw loopback socket)
// =============================================================================

// ESP-IDF 5.5 also passes the upgrade request itself to the WebSocket handler,
// which these tests don't account for.
#if defined(CONFIG_HTTPD_WS_SUPPORT) && ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)

namespace {

/// Minimal WebSocket client over a loopback TCP socket.
class ws_test_client {
public:
    enum class recv_status { frame, closed, timeout };

    struct frame {
        uint8_t opcode = 0;
        std::vector<uint8_t> payload;
    };

    /// Masking key used for every frame sent.
    static constexpr std::array<uint8_t, 4> mask = {0x12, 0x34, 0x56, 0x78};

    explicit ws_test_client(uint16_t port) {
        _fd = socket(AF_INET, SOCK_STREAM, 0);
        TEST_ASSERT_TRUE(_fd >= 0);

        timeval tv{.tv_sec = 5, .tv_usec = 0};
        setsockopt(_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        TEST_ASSERT_EQUAL(0, connect(_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)));

        std::string handshake = "GET /ws HTTP/1.1\r\n"
                                "Host: 127.0.0.1\r\n"
                                "Upgrade: websocket\r\n"
                                "Connection: Upgrade\r\n"
                                "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
                                "Sec-WebSocket-Version: 13\r\n"
                                "\r\n";
        send_all(reinterpret_cast<const uint8_t*>(handshake.data()), handshake.size());

        // Read the upgrade response one byte at a time so no frame data is consumed
        std::string response;
        while (response.size() < 4 || response.compare(response.size() - 4, 4, "\r\n\r\n") != 0) {
            char ch;
            int n = recv(_fd, &ch, 1, 0);
            TEST_ASSERT_EQUAL(1, n);
            response.push_back(ch);
        }
        TEST_ASSERT_TRUE(response.starts_with("HTTP/1.1 101"));
    }

    ~ws_test_client() {
        if (_fd >= 0) {
            close(_fd);
        }
    }

    ws_test_client(const ws_test_client&) = delete;
    ws_test_client& operator=(const ws_test_client&) = delete;

    /// Sends a single masked frame with the FIN flag set.
    void send_frame(uint8_t opcode, std::span<const uint8_t> payload) {
        auto out = encode_frame(opcode, payload);
        send_all(out.data(), out.size());
    }

    /// Encodes a single masked frame with the FIN flag set.
    static std::vector<uint8_t> encode_frame(uint8_t opcode, std::span<const uint8_t> payload) {
        std::vector<uint8_t> out;
        out.push_back(0x80 | opcode);
        if (payload.size() < 126) {
            out.push_back(0x80 | static_cast<uint8_t>(payload.size()));
        } else {
            out.push_back(0x80 | 126);
            out.push_back(static_cast<uint8_t>(payload.size() >> 8));
            out.push_back(static_cast<uint8_t>(payload.size() & 0xff));
        }
        out.insert(out.end(), mask.begin(), mask.end());
        for (size_t i = 0; i < payload.size(); ++i) {
            out.push_back(payload[i] ^ mask[i % 4]);
        }
        return out;
    }

    /// Sends bytes as they are, e.g. a truncated frame.
    void send_raw(std::span<const uint8_t> data) { send_all(data.data(), data.size()); }

    void send_text(std::string_view text) {
        send_frame(0x1, std::span<const uint8_t>{reinterpret_cast<const uint8_t*>(text.data()), text.size()});
    }

    /// Receives one unmasked frame from the server.
    recv_status recv_frame(frame& f) {
        std::array<uint8_t, 2> hdr;
        auto st = recv_exact(hdr.data(), hdr.size());
        if (st != recv_status::frame) {
            return st;
        }
        f.opcode = hdr[0] & 0x0f;
        size_t len = hdr[1] & 0x7f;
        if (len == 126) {
            std::array<uint8_t, 2> ext;
            st = recv_exact(ext.data(), ext.size());
            if (st != recv_status::frame) {
                return st;
            }
            len = (static_cast<size_t>(ext[0]) << 8) | ext[1];
        }
        f.payload.resize(len);
        return recv_exact(f.payload.data(), len);
    }

    /// Receives one text frame and returns its payload.
    std::string recv_text() {
        frame f;
        TEST_ASSERT_TRUE(recv_frame(f) == recv_status::frame);
        TEST_ASSERT_EQUAL(0x1, f.opcode);
        return std::string(f.payload.begin(), f.payload.end());
    }

private:
    void send_all(const uint8_t* data, size_t len) {
        while (len > 0) {
            int n = send(_fd, data, len, 0);
            TEST_ASSERT_TRUE(n > 0);
            data += n;
            len -= static_cast<size_t>(n);
        }
    }

    recv_status recv_exact(uint8_t* data, size_t len) {
        while (len > 0) {
            int n = recv(_fd, data, len, 0);
            if (n == 0) {
                return recv_status::closed;
            }
            if (n < 0) {
                return (errno == EAGAIN || errno == EWOULDBLOCK) ? recv_status::timeout : recv_status::closed;
            }
            data += n;
            len -= static_cast<size_t>(n);
        }
        return recv_status::frame;
    }

    int _fd = -1;
};

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 1, 0)
std::vector<uint8_t> make_pattern(size_t len) {
    std::vector<uint8_t> v(len);
    for (size_t i = 0; i < len; ++i) {
        v[i] = static_cast<uint8_t>((i * 7 + 3) & 0xff);
    }
    return v;
}
#endif

result<server> make_ws_server(uint16_t port, server::handler_type handler) {
    auto srv = server::make({.stack_size = 6144, .server_port = port, .ctrl_port = alloc_ctrl_port()});
    if (!srv) {
        return srv;
    }
    auto r = srv->try_on_ws("/ws", std::move(handler));
    if (!r) {
        return idfxx::error(r.error());
    }
    return srv;
}

/// Returns a payload that, once masked by ws_test_client, appears on the wire
/// as @p wire. Lets a test place arbitrary bytes, such as a complete frame,
/// inside a frame's payload as they will be seen by the server.
std::vector<uint8_t> payload_for_wire(std::span<const uint8_t> wire) {
    std::vector<uint8_t> payload;
    for (size_t i = 0; i < wire.size(); ++i) {
        payload.push_back(wire[i] ^ ws_test_client::mask[i % 4]);
    }
    return payload;
}

} // namespace

TEST_CASE("ws_recv keeps the connection in sync after an empty frame", "[idfxx][http]") {
    ensure_event_loop();
    auto port = alloc_test_port();
    // Replies with the payload length and the payload
    auto srv = make_ws_server(port, [](request& req) -> result<void> {
        std::array<uint8_t, 64> buf;
        auto f = req.try_ws_recv(buf);
        if (!f) return idfxx::error(f.error());
        return req.try_ws_send_text(
            "len=" + std::to_string(f->data.size()) + ":" + std::string(f->data.begin(), f->data.end())
        );
    });
    TEST_ASSERT_TRUE(srv.has_value());

    ws_test_client ws{port};

    ws.send_text("");
    TEST_ASSERT_EQUAL_STRING("len=0:", ws.recv_text().c_str());
    ws.send_text("hi");
    TEST_ASSERT_EQUAL_STRING("len=2:hi", ws.recv_text().c_str());
}

TEST_CASE("ws_recv closes the connection after a frame larger than its buffer", "[idfxx][http]") {
    ensure_event_loop();
    auto port = alloc_test_port();
    auto calls = std::make_shared<std::atomic<int>>(0);
    auto too_large = std::make_shared<std::atomic<int>>(0);
    // Ignores a receive error and reports success
    auto srv = make_ws_server(port, [calls, too_large](request& req) -> result<void> {
        calls->fetch_add(1);
        std::array<uint8_t, 16> buf;
        auto f = req.try_ws_recv(buf);
        if (!f && f.error() == idfxx::errc::invalid_size) {
            too_large->fetch_add(1);
        }
        return {};
    });
    TEST_ASSERT_TRUE(srv.has_value());

    ws_test_client ws{port};

    // The payload is crafted to appear on the wire as a complete masked text
    // frame. If the connection were kept, httpd would parse the unread payload
    // as the next frame and call the handler again.
    std::string_view smuggled = "smuggled-frame-text";
    auto inner = ws_test_client::encode_frame(
        0x1, std::span<const uint8_t>{reinterpret_cast<const uint8_t*>(smuggled.data()), smuggled.size()}
    );
    ws.send_frame(0x2, payload_for_wire(inner));

    ws_test_client::frame f;
    TEST_ASSERT_TRUE(ws.recv_frame(f) == ws_test_client::recv_status::closed);
    vTaskDelay(pdMS_TO_TICKS(100));
    TEST_ASSERT_EQUAL(1, calls->load());
    TEST_ASSERT_EQUAL(1, too_large->load());
}

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 1, 0)

TEST_CASE("ws_frame_reader reads a frame larger than its buffer in parts", "[idfxx][http]") {
    ensure_event_loop();
    auto port = alloc_test_port();
    // Replies with a summary of the frame, then echoes the reassembled payload
    auto srv = make_ws_server(port, [](request& req) -> result<void> {
        auto rd = req.try_ws_recv_begin();
        if (!rd) return idfxx::error(rd.error());

        bool ok = true;
        std::array<uint8_t, 64> buf;
        if (rd->remaining() > 0) {
            auto e = rd->try_read(std::span<uint8_t>{});
            ok = ok && !e && e.error() == idfxx::errc::invalid_arg;
        }
        std::vector<uint8_t> payload;
        int parts = 0;
        while (rd->remaining() > 0) {
            size_t expect = std::min(buf.size(), rd->remaining());
            auto part = rd->try_read(buf);
            if (!part) return idfxx::error(part.error());
            ok = ok && part->size() == expect && part->data() == buf.data();
            payload.insert(payload.end(), part->begin(), part->end());
            ++parts;
        }
        auto tail = rd->try_read(buf);
        ok = ok && tail && tail->empty() && payload.size() == rd->size();

        std::string summary = "t=" + std::to_string(std::to_underlying(rd->type())) +
            " f=" + std::to_string(rd->final()) + " n=" + std::to_string(rd->size()) +
            " p=" + std::to_string(parts) + " ok=" + std::to_string(ok);
        auto s = req.try_ws_send_text(summary);
        if (!s) return s;
        return req.try_ws_send_binary(payload);
    });
    TEST_ASSERT_TRUE(srv.has_value());

    ws_test_client ws{port};

    // 1000 bytes read through a 64-byte buffer: 15 full parts and one of 40
    auto big = make_pattern(1000);
    ws.send_frame(0x2, big);
    TEST_ASSERT_EQUAL_STRING("t=2 f=1 n=1000 p=16 ok=1", ws.recv_text().c_str());
    ws_test_client::frame echo;
    TEST_ASSERT_TRUE(ws.recv_frame(echo) == ws_test_client::recv_status::frame);
    TEST_ASSERT_EQUAL(0x2, echo.opcode);
    TEST_ASSERT_EQUAL(big.size(), echo.payload.size());
    TEST_ASSERT_TRUE(echo.payload == big);

    // An empty frame has nothing to read, and reading it does not touch the connection
    ws.send_text("");
    TEST_ASSERT_EQUAL_STRING("t=1 f=1 n=0 p=0 ok=1", ws.recv_text().c_str());
    TEST_ASSERT_TRUE(ws.recv_frame(echo) == ws_test_client::recv_status::frame);
    TEST_ASSERT_EQUAL(0, echo.payload.size());

    // The connection stays in sync for later frames
    ws.send_text("hi");
    TEST_ASSERT_EQUAL_STRING("t=1 f=1 n=2 p=1 ok=1", ws.recv_text().c_str());
    TEST_ASSERT_TRUE(ws.recv_frame(echo) == ws_test_client::recv_status::frame);
    TEST_ASSERT_EQUAL_STRING("hi", std::string(echo.payload.begin(), echo.payload.end()).c_str());
}

TEST_CASE("ws_frame_reader skip discards the rest of a frame", "[idfxx][http]") {
    ensure_event_loop();
    auto port = alloc_test_port();
    // Reads the first 10 bytes of long frames and skips the rest; echoes short frames
    auto srv = make_ws_server(port, [](request& req) -> result<void> {
        auto begun = req.try_ws_recv_begin();
        if (!begun) return idfxx::error(begun.error());
        // Use the reader after a move: the moved-from reader must not affect the connection
        auto rd = std::move(*begun);

        // A frame can only be begun once per handler invocation
        auto again = req.try_ws_recv_begin();
        if (again || again.error() != idfxx::errc::invalid_state) {
            return req.try_ws_send_text("begin-twice");
        }

        std::array<uint8_t, 16> buf;
        if (rd.size() > buf.size()) {
            auto head = rd.try_read(std::span<uint8_t>{buf.data(), 10});
            if (!head) return idfxx::error(head.error());
            auto sk = rd.try_skip();
            if (!sk) return sk;
            // Skipping again once everything is consumed does nothing
            auto sk2 = rd.try_skip();
            if (!sk2) return sk2;
            std::string reply = "skip:" + std::string(head->begin(), head->end()) + ":" + std::to_string(rd.remaining());
            return req.try_ws_send_text(reply);
        }
        auto part = rd.try_read(buf);
        if (!part) return idfxx::error(part.error());
        return req.try_ws_send_text("echo:" + std::string(part->begin(), part->end()));
    });
    TEST_ASSERT_TRUE(srv.has_value());

    ws_test_client ws{port};

    std::string long_text;
    for (int i = 0; i < 300; ++i) {
        long_text.push_back(static_cast<char>('a' + i % 26));
    }
    ws.send_text(long_text);
    TEST_ASSERT_EQUAL_STRING("skip:abcdefghij:0", ws.recv_text().c_str());

    ws.send_text("next");
    TEST_ASSERT_EQUAL_STRING("echo:next", ws.recv_text().c_str());
}

TEST_CASE("ws handler leaving frame payload unread closes the connection", "[idfxx][http]") {
    ensure_event_loop();
    auto port = alloc_test_port();
    auto calls = std::make_shared<std::atomic<int>>(0);
    // Reads only part of the payload and reports success
    auto srv = make_ws_server(port, [calls](request& req) -> result<void> {
        calls->fetch_add(1);
        auto rd = req.try_ws_recv_begin();
        if (!rd) return idfxx::error(rd.error());
        std::array<uint8_t, 10> buf;
        auto part = rd->try_read(buf);
        if (!part) return idfxx::error(part.error());
        return {};
    });
    TEST_ASSERT_TRUE(srv.has_value());

    ws_test_client ws{port};

    // The handler reads only the first 10 payload bytes. The rest is crafted so
    // that, on the wire, it is a complete masked text frame: if the connection
    // were kept, httpd would parse it as the next frame and call the handler
    // again, and the connection would stay open.
    std::string_view smuggled = "smuggled";
    auto inner = ws_test_client::encode_frame(
        0x1, std::span<const uint8_t>{reinterpret_cast<const uint8_t*>(smuggled.data()), smuggled.size()}
    );
    std::vector<uint8_t> payload(10, 'x');
    for (size_t i = 0; i < inner.size(); ++i) {
        payload.push_back(inner[i] ^ ws_test_client::mask[(payload.size()) % 4]);
    }
    ws.send_frame(0x2, payload);

    ws_test_client::frame f;
    TEST_ASSERT_TRUE(ws.recv_frame(f) == ws_test_client::recv_status::closed);
    vTaskDelay(pdMS_TO_TICKS(100));
    TEST_ASSERT_EQUAL(1, calls->load());
}

/// Creates a server whose WebSocket handler reads each frame with a
/// ws_frame_reader, counts receive failures, and reports success regardless.
static result<server> make_failure_server(uint16_t port, std::shared_ptr<std::atomic<int>> failures) {
    auto srv = server::make({
        .stack_size = 6144,
        .server_port = port,
        .ctrl_port = alloc_ctrl_port(),
        .recv_wait_timeout = std::chrono::seconds{1},
    });
    if (!srv) {
        return srv;
    }
    auto r = srv->try_on_ws("/ws", [failures](request& req) -> result<void> {
        auto rd = req.try_ws_recv_begin();
        if (!rd) {
            failures->fetch_add(1);
            return {};
        }
        std::array<uint8_t, 64> buf;
        while (rd->remaining() > 0) {
            auto part = rd->try_read(buf);
            if (!part) {
                failures->fetch_add(1);
                // A failed reader stays unusable
                auto again = rd->try_skip();
                if (again || again.error() != idfxx::errc::invalid_state) {
                    failures->fetch_add(100);
                }
                return {};
            }
        }
        return {};
    });
    if (!r) {
        return idfxx::error(r.error());
    }
    return srv;
}

TEST_CASE("ws_frame_reader read failure closes the connection", "[idfxx][http]") {
    ensure_event_loop();
    auto port = alloc_test_port();
    auto failures = std::make_shared<std::atomic<int>>(0);
    auto srv = make_failure_server(port, failures);
    TEST_ASSERT_TRUE(srv.has_value());

    ws_test_client ws{port};

    // Send the header and only part of the payload, then go quiet. The read
    // times out on the server, leaving the position in the frame unknown. If
    // the connection were kept, the client would see no close before its own
    // receive timeout.
    auto bytes = ws_test_client::encode_frame(0x2, make_pattern(300));
    ws.send_raw(std::span<const uint8_t>{bytes.data(), 4 + 4 + 20});

    ws_test_client::frame f;
    TEST_ASSERT_TRUE(ws.recv_frame(f) == ws_test_client::recv_status::closed);
    TEST_ASSERT_EQUAL(1, failures->load());
}

TEST_CASE("ws_recv_begin failure closes the connection", "[idfxx][http]") {
    ensure_event_loop();
    auto port = alloc_test_port();
    auto failures = std::make_shared<std::atomic<int>>(0);
    auto srv = make_failure_server(port, failures);
    TEST_ASSERT_TRUE(srv.has_value());

    ws_test_client ws{port};

    // Send only the first header byte, then go quiet. Receiving the rest of
    // the header times out on the server.
    auto bytes = ws_test_client::encode_frame(0x2, make_pattern(300));
    ws.send_raw(std::span<const uint8_t>{bytes.data(), 1});

    ws_test_client::frame f;
    TEST_ASSERT_TRUE(ws.recv_frame(f) == ws_test_client::recv_status::closed);
    TEST_ASSERT_EQUAL(1, failures->load());
}

#endif // ESP_IDF_VERSION >= 6.1.0

#endif // CONFIG_HTTPD_WS_SUPPORT && ESP_IDF_VERSION >= 6.0.0
