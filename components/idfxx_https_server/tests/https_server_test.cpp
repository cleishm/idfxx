// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Unit tests for idfxx https server
// Uses ESP-IDF Unity test framework with compile-time static_asserts

#include "http_test_utils.hpp"
#include "idfxx/http/client"
#include "idfxx/http/ssl_server"
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
// =============================================================================

// ssl_server inherits from server
static_assert(std::is_base_of_v<server, ssl_server>);

// ssl_server is not default constructible
static_assert(!std::is_default_constructible_v<ssl_server>);

// ssl_server is not copyable
static_assert(!std::is_copy_constructible_v<ssl_server>);
static_assert(!std::is_copy_assignable_v<ssl_server>);

// ssl_server is move-only
static_assert(std::is_move_constructible_v<ssl_server>);
static_assert(std::is_move_assignable_v<ssl_server>);

// ssl_server::make returns result<ssl_server>
static_assert(std::is_same_v<decltype(ssl_server::make(std::declval<ssl_server::config>())), result<ssl_server>>);

// =============================================================================
// Embedded self-signed test certificates
// Generated for CN=127.0.0.1 with SAN IP:127.0.0.1, valid for 10 years.
// These are test-only certificates and carry no security value.
// =============================================================================

// clang-format off
static const char test_server_cert[] =
    "-----BEGIN CERTIFICATE-----\n"
    "MIIDGjCCAgKgAwIBAgIUHUQ76wpV7dJefKcvSonDJS3sZxwwDQYJKoZIhvcNAQEL\n"
    "BQAwFDESMBAGA1UEAwwJMTI3LjAuMC4xMB4XDTI2MDIyNzA1NDI0NVoXDTM2MDIy\n"
    "NTA1NDI0NVowFDESMBAGA1UEAwwJMTI3LjAuMC4xMIIBIjANBgkqhkiG9w0BAQEF\n"
    "AAOCAQ8AMIIBCgKCAQEAq8eHzyxqi3rKFH7Qa5TyDmde8WveT5z+n0WBQYOPrLvm\n"
    "3+oldSAnDPqc0J2o8aiVDXlpwFLSwVnqyQgqdszU45KJXFYXRJomPUyGsjAft4cA\n"
    "flG4PtmCIrwBbHCuxzOcCLF7eodqk+8r6xZL/TqBT381xFX1PGX08RCEK2YS+BZg\n"
    "Z3075dmM2HRr5tItxfsjMnnUKKUAWIEVkVn0jY6VavA66qVQbgwjLClcwBTDNRle\n"
    "M3llIHoUECadbRYAg3izs2cBq9fsapyAkPm7eX5oryYXSxEIf2fVI83xEAhWidEI\n"
    "LEGEkBIY9VnmYUNyaBUvM9UuxMeg+de5OsPCkw+tywIDAQABo2QwYjAdBgNVHQ4E\n"
    "FgQUcU2OeQ15U1BTc1pTeVT08UJjTHAwHwYDVR0jBBgwFoAUcU2OeQ15U1BTc1pT\n"
    "eVT08UJjTHAwDwYDVR0TAQH/BAUwAwEB/zAPBgNVHREECDAGhwR/AAABMA0GCSqG\n"
    "SIb3DQEBCwUAA4IBAQCokzwQHv9nVCQIB/OcXr0APjR9fSJz+P+pDBzKShjFkpbB\n"
    "X1clhCPtaYi/5bUv6xiUYLp45qriY00Opi4rX05G0GP7SywS6AFU7JaSUs+bf1IS\n"
    "qX8a6fVQYq4UbDEHN4uJQz9ueFHjiO3Jlg2dR7T7IJ/sykeNbCp2PHBI8Eyg06kD\n"
    "BCO9NdK6Q9a4MdiDOqMKeREcU7nnFJa+VRxyOgLcf3kObvQflFT6bbe3mNjrHm6L\n"
    "iAFKmJ8RzRWetlGM59+SlsxQmI5zzzSH846rEi8IqT59aOnToy7Y7pMMfQxIp03k\n"
    "nvpnLB0+wnMoEwIJS/V1IehVZunh2SkSZBcZvwHC\n"
    "-----END CERTIFICATE-----\n";

static const char test_server_key[] =
    "-----BEGIN PRIVATE KEY-----\n"
    "MIIEvAIBADANBgkqhkiG9w0BAQEFAASCBKYwggSiAgEAAoIBAQCrx4fPLGqLesoU\n"
    "ftBrlPIOZ17xa95PnP6fRYFBg4+su+bf6iV1ICcM+pzQnajxqJUNeWnAUtLBWerJ\n"
    "CCp2zNTjkolcVhdEmiY9TIayMB+3hwB+Ubg+2YIivAFscK7HM5wIsXt6h2qT7yvr\n"
    "Fkv9OoFPfzXEVfU8ZfTxEIQrZhL4FmBnfTvl2YzYdGvm0i3F+yMyedQopQBYgRWR\n"
    "WfSNjpVq8DrqpVBuDCMsKVzAFMM1GV4zeWUgehQQJp1tFgCDeLOzZwGr1+xqnICQ\n"
    "+bt5fmivJhdLEQh/Z9UjzfEQCFaJ0QgsQYSQEhj1WeZhQ3JoFS8z1S7Ex6D517k6\n"
    "w8KTD63LAgMBAAECggEATkkvbCv+XJW3vfJzcuwdCpJyswzpcpgGdLi86QoXsu2p\n"
    "kPeJXaErGt+mEu8fPQ8K6uqf1t4IHcUoWrkFfUHpbdNtFW9IyGOGNN6I7nE/Kyl0\n"
    "AJ2mncIL7F+JOI38IV20aUPVlehcGpJgDhIJzJzarMu5ScKw8nc205wm2A57a6Mr\n"
    "nE1rponX9+GiFW0veI1SLgEZZEEacsU0Cl9VHgdoFgPG6Q17H7IjIJ7wTafPhYP+\n"
    "SUFuEGnKVPHlc3jqqTSHDI3RehvYEYTuuFklozvoSQ6FLzc8hKobIxd0Cb1k3EkT\n"
    "y+aqppZNTvUiYGJN7R8a3C3Wjw82lUx0hjwsclu2qQKBgQDddjj6WeAzxoKrv5C8\n"
    "EgHSvIPbqMUFPp01td4br+tiGW67/ldqeSd5qEXfFO0HFM+2H9BY2xiVmNVyI2FN\n"
    "0UdqQoyDTeOp+Shag0ZfDehMy2Jrvll3XWhdvuyRvrnuce65c++Ji6mR29HrYOCT\n"
    "Bwxa1LI73L+/IBR8rZuNyYhRAwKBgQDGkcHxwgc8y80XiCV24dyNO2VR5fU0p0mp\n"
    "yQ1yLgj5Sa54LuumRWrUGVOmXAeAWz6Y3XZ+CJHKYiTvH6KjLC9FPYEvk7SiU3Iq\n"
    "qZPRtISqma5RNRKythcBK4Ih6Fx4mHgzI1pwmmrZ9rlcSv0Bb/K2NztNRfbjfNVe\n"
    "0limJSzBmQKBgEC6dC7gJAKeC8VNKW1+yd+hT9zc+DDvOx3euTtYcLDshAwYa85n\n"
    "+Ny7DSkFwb2nHIq7w7ak2wumbwR8SM1o1Lm/F7itBFTCyUOjSOcxdmszquGY8idM\n"
    "OtvjmNuEZm3GCSNVOnb2RiqmmDV2zEzM65SExE9w1u5y3uoOCAAqHlinAoGAHB1B\n"
    "B3jAS5RTanSFUWqzLm/tbYYQjK7u2BI2TCdGb/1FrZB/HuCPOo6HcHNxQHQqzbv1\n"
    "bezKr4vrzMt+3HmCC9ykcNcJ6T3FWVL/Md7MNddife70wcbURP8jAqgCh7SWuC7W\n"
    "PEEwxcGQBwg7ADwYckIprEwuo5DmKPHBSWzUBGkCgYBMKE44Qv8cptwhkw59Ht6/\n"
    "7trzNJFLJBOankVOIqTa/gPUgH0IoWvComb+4EV5iOoiXmMCgyxPxZfkAOjTbKTg\n"
    "SRplT1jaZPpDqix6zzFQyF0uWlSfYhXnyoKAFMY2elCEi9/m3G3GA8L63PDepXaX\n"
    "O9yuAEzjapOyKoGVBmJaLA==\n"
    "-----END PRIVATE KEY-----\n";
// clang-format on

// =============================================================================
// Runtime tests (Unity TEST_CASE)
// =============================================================================

static uint16_t next_test_port = 19443;
static uint16_t next_ctrl_port = 43768;

/// Returns a unique port for each test to avoid EADDRINUSE.
static uint16_t alloc_test_port() {
    return next_test_port++;
}

static uint16_t alloc_ctrl_port() {
    return next_ctrl_port++;
}

static void ensure_event_loop() { test::ensure_event_loop(); }
static std::string read_response_body(client& c) { return test::read_response_body(c); }

TEST_CASE("https server handles GET request", "[idfxx][https][hw]") {
    ensure_event_loop();
    auto port = alloc_test_port();
    auto srv = ssl_server::make({
        .server_port = port,
        .ctrl_port = alloc_ctrl_port(),
        .server_cert = test_server_cert,
        .private_key = test_server_key,
    });
    TEST_ASSERT_TRUE(srv.has_value());

    auto r = srv->try_on_get("/hello", [](request& req) -> result<void> {
        req.set_content_type("text/plain");
        return req.try_send("Hello, TLS!");
    });
    TEST_ASSERT_TRUE(r.has_value());

    auto url = "https://127.0.0.1:" + std::to_string(port) + "/hello";
    auto c = client::make({
        .url = url,
        .cert_pem = test_server_cert,
        .skip_cert_common_name_check = true,
    });
    TEST_ASSERT_TRUE(c.has_value());
    auto body = read_response_body(*c);

    TEST_ASSERT_EQUAL(200, c->status_code());
    TEST_ASSERT_EQUAL_STRING("Hello, TLS!", body.c_str());
}

TEST_CASE("https server handles POST with body", "[idfxx][https][hw]") {
    ensure_event_loop();
    auto port = alloc_test_port();
    auto srv = ssl_server::make({
        .server_port = port,
        .ctrl_port = alloc_ctrl_port(),
        .server_cert = test_server_cert,
        .private_key = test_server_key,
    });
    TEST_ASSERT_TRUE(srv.has_value());

    auto r = srv->try_on_post("/echo", [](request& req) -> result<void> {
        auto body = req.try_recv_body();
        if (!body) return idfxx::error(body.error());
        req.set_content_type("text/plain");
        return req.try_send(*body);
    });
    TEST_ASSERT_TRUE(r.has_value());

    auto url = "https://127.0.0.1:" + std::to_string(port) + "/echo";
    auto c = client::make({
        .url = url,
        .method = method::post,
        .cert_pem = test_server_cert,
        .skip_cert_common_name_check = true,
    });
    TEST_ASSERT_TRUE(c.has_value());

    std::string_view payload = "tls payload";
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
    TEST_ASSERT_EQUAL_STRING("tls payload", body.c_str());
}

TEST_CASE("https server sets custom status code", "[idfxx][https][hw]") {
    ensure_event_loop();
    auto port = alloc_test_port();
    auto srv = ssl_server::make({
        .server_port = port,
        .ctrl_port = alloc_ctrl_port(),
        .server_cert = test_server_cert,
        .private_key = test_server_key,
    });
    TEST_ASSERT_TRUE(srv.has_value());

    auto r = srv->try_on_get("/created", [](request& req) -> result<void> {
        req.set_status(201);
        return req.try_send("created");
    });
    TEST_ASSERT_TRUE(r.has_value());

    auto url = "https://127.0.0.1:" + std::to_string(port) + "/created";
    auto c = client::make({
        .url = url,
        .cert_pem = test_server_cert,
        .skip_cert_common_name_check = true,
    });
    TEST_ASSERT_TRUE(c.has_value());
    auto body = read_response_body(*c);

    TEST_ASSERT_EQUAL(201, c->status_code());
}

TEST_CASE("https server unregisters handler", "[idfxx][https][hw]") {
    ensure_event_loop();
    auto port = alloc_test_port();
    auto srv = ssl_server::make({
        .server_port = port,
        .ctrl_port = alloc_ctrl_port(),
        .server_cert = test_server_cert,
        .private_key = test_server_key,
    });
    TEST_ASSERT_TRUE(srv.has_value());

    auto r = srv->try_on_get("/temp", [](request& req) -> result<void> { return req.try_send("ok"); });
    TEST_ASSERT_TRUE(r.has_value());

    auto r2 = srv->try_unregister_handler(method::get, "/temp");
    TEST_ASSERT_TRUE(r2.has_value());

    // After unregistration, should get 404
    auto url = "https://127.0.0.1:" + std::to_string(port) + "/temp";
    auto c = client::make({
        .url = url,
        .cert_pem = test_server_cert,
        .skip_cert_common_name_check = true,
    });
    TEST_ASSERT_TRUE(c.has_value());
    auto r3 = c->try_open();
    TEST_ASSERT_TRUE(r3.has_value());
    auto r4 = c->try_fetch_headers();
    TEST_ASSERT_TRUE(r4.has_value());
    auto r5 = c->try_close();
    TEST_ASSERT_TRUE(r5.has_value());

    TEST_ASSERT_EQUAL(404, c->status_code());
}
