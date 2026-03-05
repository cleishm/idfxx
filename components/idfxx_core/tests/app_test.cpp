// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Unit tests for idfxx app info
// Uses ESP-IDF Unity test framework

#include "idfxx/app.hpp"
#include "unity.h"

using namespace idfxx;

// =============================================================================
// Runtime tests (Unity TEST_CASE)
// =============================================================================

TEST_CASE("app::version returns non-empty string", "[idfxx][app]") {
    auto v = app::version();
    TEST_ASSERT_FALSE(v.empty());
}

TEST_CASE("app::project_name returns non-empty string", "[idfxx][app]") {
    auto name = app::project_name();
    TEST_ASSERT_FALSE(name.empty());
}

TEST_CASE("app::compile_time returns non-empty string", "[idfxx][app]") {
    auto t = app::compile_time();
    TEST_ASSERT_FALSE(t.empty());
}

TEST_CASE("app::compile_date returns non-empty string", "[idfxx][app]") {
    auto d = app::compile_date();
    TEST_ASSERT_FALSE(d.empty());
}

TEST_CASE("app::idf_version returns non-empty string", "[idfxx][app]") {
    auto v = app::idf_version();
    TEST_ASSERT_FALSE(v.empty());
}

TEST_CASE("app::elf_sha256_hex returns hex string", "[idfxx][app]") {
    auto sha = app::elf_sha256_hex();
    TEST_ASSERT_GREATER_THAN(0, sha.size());
    for (char c : sha) {
        TEST_ASSERT_TRUE((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'));
    }
}
