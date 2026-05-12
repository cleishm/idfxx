// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#include <idfxx/net/error>

#include <system_error>
#include <unity.h>

TEST_CASE("net error category name", "[net]") {
    TEST_ASSERT_EQUAL_STRING("net::Error", idfxx::net_category().name());
}

TEST_CASE("net error category messages", "[net]") {
    auto ec = make_error_code(idfxx::net::errc::would_block);
    TEST_ASSERT_EQUAL_STRING("Operation would block", ec.message().c_str());
}

TEST_CASE("net error code maps to std::errc", "[net]") {
    auto ec = make_error_code(idfxx::net::errc::would_block);
    TEST_ASSERT_TRUE(ec == std::errc::operation_would_block);

    ec = make_error_code(idfxx::net::errc::timed_out);
    TEST_ASSERT_TRUE(ec == std::errc::timed_out);

    ec = make_error_code(idfxx::net::errc::connection_refused);
    TEST_ASSERT_TRUE(ec == std::errc::connection_refused);

    ec = make_error_code(idfxx::net::errc::address_in_use);
    TEST_ASSERT_TRUE(ec == std::errc::address_in_use);

    ec = make_error_code(idfxx::net::errc::pipe_broken);
    TEST_ASSERT_TRUE(ec == std::errc::broken_pipe);

    ec = make_error_code(idfxx::net::errc::message_too_long);
    TEST_ASSERT_TRUE(ec == std::errc::message_size);

    ec = make_error_code(idfxx::net::errc::invalid_state);
    TEST_ASSERT_TRUE(ec == std::errc::bad_file_descriptor);
}
