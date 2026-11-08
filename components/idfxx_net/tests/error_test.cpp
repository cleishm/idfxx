// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#include <idfxx/net/error>

#include <system_error>
#include <unity.h>

TEST_CASE("net error category name", "[net]") {
    TEST_ASSERT_EQUAL_STRING("net::Error", idfxx::net_category().name());
}

TEST_CASE("net error category messages", "[net]") {
    auto ec = make_error_code(idfxx::net::errc::operation_would_block);
    TEST_ASSERT_EQUAL_STRING("Operation would block", ec.message().c_str());
}

TEST_CASE("net error code maps to std::errc", "[net]") {
    auto ec = make_error_code(idfxx::net::errc::operation_would_block);
    TEST_ASSERT_TRUE(ec == std::errc::operation_would_block);

    ec = make_error_code(idfxx::net::errc::timed_out);
    TEST_ASSERT_TRUE(ec == std::errc::timed_out);

    ec = make_error_code(idfxx::net::errc::connection_refused);
    TEST_ASSERT_TRUE(ec == std::errc::connection_refused);

    ec = make_error_code(idfxx::net::errc::address_in_use);
    TEST_ASSERT_TRUE(ec == std::errc::address_in_use);

    ec = make_error_code(idfxx::net::errc::broken_pipe);
    TEST_ASSERT_TRUE(ec == std::errc::broken_pipe);

    ec = make_error_code(idfxx::net::errc::message_too_long);
    TEST_ASSERT_TRUE(ec == std::errc::message_size);

    ec = make_error_code(idfxx::net::errc::too_many_files_open);
    TEST_ASSERT_TRUE(ec == std::errc::too_many_files_open);

    ec = make_error_code(idfxx::net::errc::invalid_state);
    TEST_ASSERT_TRUE(ec == std::errc::bad_file_descriptor);
}

TEST_CASE("net default_error_condition canonicalizes codes", "[net]") {
    // default_error_condition() yields the canonical std::errc condition, so the
    // comparison works in both directions (condition == code, not just code ==
    // condition) now that the mapping lives in default_error_condition rather
    // than a one-directional equivalent() override.
    auto cond = std::make_error_condition(std::errc::connection_reset);
    TEST_ASSERT_TRUE(cond == make_error_code(idfxx::net::errc::connection_reset));

    // Codes that share a synonym canonicalize to the same condition, so they
    // compare equal to each other's default_error_condition.
    auto sock = make_error_code(idfxx::net::errc::connection_reset);
    auto netc = make_error_code(idfxx::net::errc::netconn_reset);
    TEST_ASSERT_TRUE(sock.default_error_condition() == netc.default_error_condition());

    // A code without a synonym canonicalizes to its own category, so it never
    // collides with an unrelated std::errc condition.
    auto closed = make_error_code(idfxx::net::errc::netconn_closed);
    TEST_ASSERT_TRUE(closed.default_error_condition().category() == idfxx::net_category());
}

TEST_CASE("net netconn error codes map to std::errc", "[net]") {
    // Netconn-path codes that have a clean POSIX synonym must compare equal to
    // it, exactly like the socket-path code for the same logical condition —
    // so `ec == std::errc::connection_reset` holds regardless of which layer
    // produced the error.
    auto ec = make_error_code(idfxx::net::errc::netconn_reset);
    TEST_ASSERT_TRUE(ec == std::errc::connection_reset);

    ec = make_error_code(idfxx::net::errc::netconn_aborted);
    TEST_ASSERT_TRUE(ec == std::errc::connection_aborted);

    ec = make_error_code(idfxx::net::errc::netconn_routing);
    TEST_ASSERT_TRUE(ec == std::errc::network_unreachable);

    ec = make_error_code(idfxx::net::errc::netconn_illegal_value);
    TEST_ASSERT_TRUE(ec == std::errc::invalid_argument);

    // netconn_closed is the graceful-EOF signal and has no portable synonym, so
    // it must not spuriously compare equal to a connection error.
    ec = make_error_code(idfxx::net::errc::netconn_closed);
    TEST_ASSERT_FALSE(ec == std::errc::connection_reset);
}
