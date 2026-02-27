// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Shared test utilities for HTTP server and HTTPS server tests.

#pragma once

#include "idfxx/http/client"
#include "unity.h"

#include <array>
#include <esp_event.h>
#include <string>

namespace idfxx::http::test {

/// Ensures the default event loop exists (idempotent).
inline void ensure_event_loop() {
    static bool created = false;
    if (!created) {
        esp_event_loop_create_default();
        created = true;
    }
}

/// Read the entire response body via the streaming API.
inline std::string read_response_body(client& c) {
    auto r1 = c.try_open();
    TEST_ASSERT_TRUE(r1.has_value());
    auto r2 = c.try_fetch_headers();
    TEST_ASSERT_TRUE(r2.has_value());
    std::string body;
    std::array<char, 256> buf;
    while (true) {
        auto n = c.try_read(buf);
        TEST_ASSERT_TRUE(n.has_value());
        if (*n == 0) break;
        body.append(buf.data(), *n);
    }
    auto r3 = c.try_close();
    TEST_ASSERT_TRUE(r3.has_value());
    return body;
}

} // namespace idfxx::http::test
