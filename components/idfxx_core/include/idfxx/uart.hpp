// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

/**
 * @headerfile <idfxx/uart>
 * @file uart.hpp
 * @brief UART port identification types.
 *
 * @defgroup idfxx_uart UART Types
 * @brief UART port identification.
 * @{
 */

#include <hal/uart_types.h>
#include <soc/soc_caps.h>
#include <string>

namespace idfxx {

/**
 * @headerfile <idfxx/uart>
 * @brief Identifies a UART port.
 *
 * @note Only ports available on the target chip are defined.
 *
 * @code
 * // Use UART port 0
 * .port = idfxx::uart_port::uart0
 * @endcode
 */
enum class uart_port : int {
    uart0 = UART_NUM_0,
    uart1 = UART_NUM_1,
#if SOC_UART_HP_NUM > 2
    uart2 = UART_NUM_2,
#endif
#if SOC_UART_HP_NUM > 3
    uart3 = UART_NUM_3,
#endif
#if SOC_UART_HP_NUM > 4
    uart4 = UART_NUM_4,
#endif
#if SOC_UART_LP_NUM >= 1
    lp_uart0 = LP_UART_NUM_0,
#endif
};

/**
 * @brief Returns a string representation of a UART port identifier.
 *
 * @param p The port identifier to convert.
 * @return "UART0", "UART1", etc., or "unknown(N)" for unrecognized values.
 */
[[nodiscard]] inline std::string to_string(uart_port p) {
    switch (p) {
    case uart_port::uart0:
        return "UART0";
    case uart_port::uart1:
        return "UART1";
#if SOC_UART_HP_NUM > 2
    case uart_port::uart2:
        return "UART2";
#endif
#if SOC_UART_HP_NUM > 3
    case uart_port::uart3:
        return "UART3";
#endif
#if SOC_UART_HP_NUM > 4
    case uart_port::uart4:
        return "UART4";
#endif
#if SOC_UART_LP_NUM >= 1
    case uart_port::lp_uart0:
        return "LP_UART0";
#endif
    default:
        return "unknown(" + std::to_string(static_cast<int>(p)) + ")";
    }
}

/** @} */ // end of idfxx_uart

} // namespace idfxx

#include "sdkconfig.h"
#ifdef CONFIG_IDFXX_STD_FORMAT
/** @cond INTERNAL */
#include <algorithm>
#include <format>
namespace std {
template<>
struct formatter<idfxx::uart_port> {
    constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }

    template<typename FormatContext>
    auto format(idfxx::uart_port p, FormatContext& ctx) const {
        auto s = to_string(p);
        return std::copy(s.begin(), s.end(), ctx.out());
    }
};
} // namespace std
/** @endcond */
#endif // CONFIG_IDFXX_STD_FORMAT
