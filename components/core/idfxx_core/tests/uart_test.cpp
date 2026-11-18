// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Unit tests for idfxx uart types
// Uses ESP-IDF Unity test framework with compile-time static_asserts

#include "idfxx/uart.hpp"
#include "unity.h"

#include <type_traits>
#include <utility>

using namespace idfxx;

// =============================================================================
// Compile-time tests (static_assert)
// These verify correctness at compile time - if this file compiles, they pass.
// =============================================================================

static_assert(std::is_enum_v<uart_port>);
static_assert(std::is_same_v<std::underlying_type_t<uart_port>, int>);
static_assert(std::to_underlying(uart_port::uart0) == UART_NUM_0);
static_assert(std::to_underlying(uart_port::uart1) == UART_NUM_1);
#if SOC_UART_HP_NUM > 2
static_assert(std::to_underlying(uart_port::uart2) == UART_NUM_2);
#endif
#if SOC_UART_HP_NUM > 3
static_assert(std::to_underlying(uart_port::uart3) == UART_NUM_3);
#endif
#if SOC_UART_HP_NUM > 4
static_assert(std::to_underlying(uart_port::uart4) == UART_NUM_4);
#endif
#if SOC_UART_LP_NUM >= 1
static_assert(std::to_underlying(uart_port::lp_uart0) == LP_UART_NUM_0);
#endif

// =============================================================================
// Runtime tests (Unity TEST_CASE)
// =============================================================================

// to_string

TEST_CASE("to_string(uart_port) outputs UART0", "[idfxx][uart]") {
    auto s = to_string(uart_port::uart0);
    TEST_ASSERT_EQUAL_STRING("UART0", s.c_str());
}

TEST_CASE("to_string(uart_port) outputs UART1", "[idfxx][uart]") {
    auto s = to_string(uart_port::uart1);
    TEST_ASSERT_EQUAL_STRING("UART1", s.c_str());
}

#if SOC_UART_HP_NUM > 2
TEST_CASE("to_string(uart_port) outputs UART2", "[idfxx][uart]") {
    auto s = to_string(uart_port::uart2);
    TEST_ASSERT_EQUAL_STRING("UART2", s.c_str());
}
#endif

TEST_CASE("to_string(uart_port) handles unknown values", "[idfxx][uart]") {
    auto unknown = static_cast<uart_port>(99);
    auto s = to_string(unknown);
    TEST_ASSERT_EQUAL_STRING("unknown(99)", s.c_str());
}

#ifdef CONFIG_IDFXX_STD_FORMAT
// Formatter
static_assert(std::formattable<uart_port, char>);

TEST_CASE("uart_port formatter outputs UART0", "[idfxx][uart]") {
    auto s = std::format("{}", uart_port::uart0);
    TEST_ASSERT_EQUAL_STRING("UART0", s.c_str());
}

TEST_CASE("uart_port formatter outputs UART1", "[idfxx][uart]") {
    auto s = std::format("{}", uart_port::uart1);
    TEST_ASSERT_EQUAL_STRING("UART1", s.c_str());
}

TEST_CASE("uart_port formatter handles unknown values", "[idfxx][uart]") {
    auto unknown = static_cast<uart_port>(99);
    auto s = std::format("{}", unknown);
    TEST_ASSERT_EQUAL_STRING("unknown(99)", s.c_str());
}
#endif // CONFIG_IDFXX_STD_FORMAT
