// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Unit tests for idfxx http server
// Uses ESP-IDF Unity test framework with compile-time static_asserts

#include "http_test_utils.hpp"
#include "idfxx/http/client"
#include "idfxx/http/server"
#include "unity.h"

#include <array>
#include <string>
#include <type_traits>
#include <utility>

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
#endif

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
