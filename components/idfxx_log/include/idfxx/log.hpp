// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

#include "sdkconfig.h"
#ifndef CONFIG_IDFXX_STD_FORMAT
#error "idfxx_log requires CONFIG_IDFXX_STD_FORMAT to be enabled (menuconfig -> IDFXX -> Enable std::format support)"
#endif

/**
 * @headerfile <idfxx/log>
 * @file log.hpp
 * @brief Type-safe logging with std::format.
 *
 * @defgroup idfxx_log Log Component
 * @brief Type-safe logging with compile-time format validation for ESP32.
 *
 * Provides structured logging using std::format for type-safe, compile-time
 * validated format strings. Messages are output through ESP-IDF's logging
 * infrastructure, preserving timestamps, colors, level prefixes, and per-tag
 * runtime level control.
 *
 * Two API styles are available:
 * - Free functions for ad-hoc logging with an explicit tag
 * - A @ref logger class for repeated logging with a fixed tag
 *
 * Optional convenience macros provide zero-cost elimination of filtered
 * messages, including their argument evaluation.
 * @{
 */

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <esp_log.h>
#include <format>
#include <string>
#include <string_view>
#include <utility>

namespace idfxx::log {

/**
 * @headerfile <idfxx/log>
 * @brief Log severity level.
 *
 * Severity levels matching ESP-IDF's log levels, from most to least severe.
 * Messages are only output when their level is at or above the configured
 * threshold for the associated tag.
 */
enum class level : uint8_t {
    none = ESP_LOG_NONE,       ///< No log output.
    error = ESP_LOG_ERROR,     ///< Critical errors requiring immediate attention.
    warn = ESP_LOG_WARN,       ///< Warning conditions that may indicate problems.
    info = ESP_LOG_INFO,       ///< Informational messages about normal operation.
    debug = ESP_LOG_DEBUG,     ///< Detailed information for debugging.
    verbose = ESP_LOG_VERBOSE, ///< Highly detailed trace information.
};

/**
 * @headerfile <idfxx/log>
 * @brief Log a pre-formatted message at the specified level.
 *
 * The message is only output if the tag's runtime log level permits output
 * at the specified severity. No format processing is performed on the message.
 *
 * @param lvl The log severity level.
 * @param tag The log tag identifying the source.
 * @param msg The message string to log.
 */
void log(level lvl, const char* tag, std::string_view msg);

/**
 * @headerfile <idfxx/log>
 * @brief Log a message at the specified level.
 *
 * The message is only formatted if the tag's runtime log level permits output
 * at the specified severity. Format strings are validated at compile time.
 *
 * @tparam Args Format argument types, deduced from the arguments.
 * @param lvl The log severity level.
 * @param tag The log tag identifying the source.
 * @param fmt A std::format format string, validated at compile time.
 * @param args Arguments to format into the message.
 */
template<typename... Args>
void log(level lvl, const char* tag, std::format_string<Args...> fmt, Args&&... args) {
    if (static_cast<int>(lvl) > LOG_LOCAL_LEVEL) {
        return;
    }
    if (esp_log_level_get(tag) < static_cast<esp_log_level_t>(lvl)) {
        return;
    }
    log(lvl, tag, std::format(fmt, std::forward<Args>(args)...));
}

/**
 * @headerfile <idfxx/log>
 * @brief Log a message at error level.
 *
 * @tparam Args Format argument types, deduced from the arguments.
 * @param tag The log tag identifying the source.
 * @param fmt A std::format format string, validated at compile time.
 * @param args Arguments to format into the message.
 */
template<typename... Args>
void error(const char* tag, std::format_string<Args...> fmt, Args&&... args) {
    log(level::error, tag, fmt, std::forward<Args>(args)...);
}

/**
 * @headerfile <idfxx/log>
 * @brief Log a pre-formatted message at error level.
 *
 * @param tag The log tag identifying the source.
 * @param msg The message string to log.
 */
inline void error(const char* tag, std::string_view msg) {
    log(level::error, tag, msg);
}

/**
 * @headerfile <idfxx/log>
 * @brief Log a message at warning level.
 *
 * @tparam Args Format argument types, deduced from the arguments.
 * @param tag The log tag identifying the source.
 * @param fmt A std::format format string, validated at compile time.
 * @param args Arguments to format into the message.
 */
template<typename... Args>
void warn(const char* tag, std::format_string<Args...> fmt, Args&&... args) {
    log(level::warn, tag, fmt, std::forward<Args>(args)...);
}

/**
 * @headerfile <idfxx/log>
 * @brief Log a pre-formatted message at warning level.
 *
 * @param tag The log tag identifying the source.
 * @param msg The message string to log.
 */
inline void warn(const char* tag, std::string_view msg) {
    log(level::warn, tag, msg);
}

/**
 * @headerfile <idfxx/log>
 * @brief Log a message at info level.
 *
 * @tparam Args Format argument types, deduced from the arguments.
 * @param tag The log tag identifying the source.
 * @param fmt A std::format format string, validated at compile time.
 * @param args Arguments to format into the message.
 */
template<typename... Args>
void info(const char* tag, std::format_string<Args...> fmt, Args&&... args) {
    log(level::info, tag, fmt, std::forward<Args>(args)...);
}

/**
 * @headerfile <idfxx/log>
 * @brief Log a pre-formatted message at info level.
 *
 * @param tag The log tag identifying the source.
 * @param msg The message string to log.
 */
inline void info(const char* tag, std::string_view msg) {
    log(level::info, tag, msg);
}

/**
 * @headerfile <idfxx/log>
 * @brief Log a message at debug level.
 *
 * @tparam Args Format argument types, deduced from the arguments.
 * @param tag The log tag identifying the source.
 * @param fmt A std::format format string, validated at compile time.
 * @param args Arguments to format into the message.
 */
template<typename... Args>
void debug(const char* tag, std::format_string<Args...> fmt, Args&&... args) {
    log(level::debug, tag, fmt, std::forward<Args>(args)...);
}

/**
 * @headerfile <idfxx/log>
 * @brief Log a pre-formatted message at debug level.
 *
 * @param tag The log tag identifying the source.
 * @param msg The message string to log.
 */
inline void debug(const char* tag, std::string_view msg) {
    log(level::debug, tag, msg);
}

/**
 * @headerfile <idfxx/log>
 * @brief Log a message at verbose level.
 *
 * @tparam Args Format argument types, deduced from the arguments.
 * @param tag The log tag identifying the source.
 * @param fmt A std::format format string, validated at compile time.
 * @param args Arguments to format into the message.
 */
template<typename... Args>
void verbose(const char* tag, std::format_string<Args...> fmt, Args&&... args) {
    log(level::verbose, tag, fmt, std::forward<Args>(args)...);
}

/**
 * @headerfile <idfxx/log>
 * @brief Log a pre-formatted message at verbose level.
 *
 * @param tag The log tag identifying the source.
 * @param msg The message string to log.
 */
inline void verbose(const char* tag, std::string_view msg) {
    log(level::verbose, tag, msg);
}

/**
 * @headerfile <idfxx/log>
 * @brief Set the runtime log level for a specific tag.
 *
 * Messages with a severity below the configured level are suppressed at
 * runtime for the specified tag.
 *
 * @param tag The log tag to configure.
 * @param lvl The minimum severity level to output.
 */
void set_level(const char* tag, level lvl);

/**
 * @headerfile <idfxx/log>
 * @brief Set the default log level for all tags.
 *
 * Tags without a specific level configuration will use this default.
 *
 * @param lvl The minimum severity level to output.
 */
void set_default_level(level lvl);

/**
 * @headerfile <idfxx/log>
 * @brief Log a buffer as hexadecimal bytes.
 *
 * Outputs the buffer contents as hex values, 16 bytes per line.
 *
 * @param lvl The log severity level.
 * @param tag The log tag identifying the source.
 * @param buffer Pointer to the buffer data.
 * @param length Number of bytes to log.
 */
void buffer_hex(level lvl, const char* tag, const void* buffer, size_t length);

/**
 * @headerfile <idfxx/log>
 * @brief Log a buffer as printable characters.
 *
 * Outputs the buffer contents as characters, 16 per line. Non-printable
 * characters are not shown.
 *
 * @param lvl The log severity level.
 * @param tag The log tag identifying the source.
 * @param buffer Pointer to the buffer data.
 * @param length Number of bytes to log.
 */
void buffer_char(level lvl, const char* tag, const void* buffer, size_t length);

/**
 * @headerfile <idfxx/log>
 * @brief Log a buffer as a formatted hex dump.
 *
 * Outputs a hex dump with memory addresses, hex values, and ASCII
 * representation, similar to the output of the `xxd` command.
 *
 * @param lvl The log severity level.
 * @param tag The log tag identifying the source.
 * @param buffer Pointer to the buffer data.
 * @param length Number of bytes to log.
 */
void buffer_hex_dump(level lvl, const char* tag, const void* buffer, size_t length);

/**
 * @headerfile <idfxx/log>
 * @brief Lightweight logger bound to a specific tag.
 *
 * A value type that associates a tag string with logging methods, avoiding
 * repetition of the tag in every call. The tag pointer must remain valid
 * for the lifetime of the logger (string literals are recommended).
 *
 * @code
 * static constexpr idfxx::log::logger log{"my_component"};
 *
 * void do_work() {
 *     log.info("Starting work with {} items", count);
 *     // ...
 *     log.error("Failed to process item {}: {}", id, reason);
 * }
 * @endcode
 */
class logger {
public:
    /**
     * @brief Construct a logger with the given tag.
     *
     * @param tag The log tag identifying the source. Must remain valid for
     *            the lifetime of the logger. String literals are recommended.
     */
    constexpr explicit logger(const char* tag) noexcept
        : _tag(tag) {}

    /**
     * @brief Get the tag associated with this logger.
     *
     * @return The log tag string.
     */
    [[nodiscard]] constexpr const char* tag() const noexcept { return _tag; }

    /**
     * @brief Log a message at the specified level.
     *
     * @tparam Args Format argument types, deduced from the arguments.
     * @param lvl The log severity level.
     * @param fmt A std::format format string, validated at compile time.
     * @param args Arguments to format into the message.
     */
    template<typename... Args>
    void log(level lvl, std::format_string<Args...> fmt, Args&&... args) const {
        idfxx::log::log(lvl, _tag, fmt, std::forward<Args>(args)...);
    }

    /**
     * @brief Log a pre-formatted message at the specified level.
     *
     * @param lvl The log severity level.
     * @param msg The message string to log.
     */
    void log(level lvl, std::string_view msg) const { idfxx::log::log(lvl, _tag, msg); }

    /**
     * @brief Log a message at error level.
     *
     * @tparam Args Format argument types, deduced from the arguments.
     * @param fmt A std::format format string, validated at compile time.
     * @param args Arguments to format into the message.
     */
    template<typename... Args>
    void error(std::format_string<Args...> fmt, Args&&... args) const {
        idfxx::log::error(_tag, fmt, std::forward<Args>(args)...);
    }

    /**
     * @brief Log a pre-formatted message at error level.
     *
     * @param msg The message string to log.
     */
    void error(std::string_view msg) const { idfxx::log::error(_tag, msg); }

    /**
     * @brief Log a message at warning level.
     *
     * @tparam Args Format argument types, deduced from the arguments.
     * @param fmt A std::format format string, validated at compile time.
     * @param args Arguments to format into the message.
     */
    template<typename... Args>
    void warn(std::format_string<Args...> fmt, Args&&... args) const {
        idfxx::log::warn(_tag, fmt, std::forward<Args>(args)...);
    }

    /**
     * @brief Log a pre-formatted message at warning level.
     *
     * @param msg The message string to log.
     */
    void warn(std::string_view msg) const { idfxx::log::warn(_tag, msg); }

    /**
     * @brief Log a message at info level.
     *
     * @tparam Args Format argument types, deduced from the arguments.
     * @param fmt A std::format format string, validated at compile time.
     * @param args Arguments to format into the message.
     */
    template<typename... Args>
    void info(std::format_string<Args...> fmt, Args&&... args) const {
        idfxx::log::info(_tag, fmt, std::forward<Args>(args)...);
    }

    /**
     * @brief Log a pre-formatted message at info level.
     *
     * @param msg The message string to log.
     */
    void info(std::string_view msg) const { idfxx::log::info(_tag, msg); }

    /**
     * @brief Log a message at debug level.
     *
     * @tparam Args Format argument types, deduced from the arguments.
     * @param fmt A std::format format string, validated at compile time.
     * @param args Arguments to format into the message.
     */
    template<typename... Args>
    void debug(std::format_string<Args...> fmt, Args&&... args) const {
        idfxx::log::debug(_tag, fmt, std::forward<Args>(args)...);
    }

    /**
     * @brief Log a pre-formatted message at debug level.
     *
     * @param msg The message string to log.
     */
    void debug(std::string_view msg) const { idfxx::log::debug(_tag, msg); }

    /**
     * @brief Log a message at verbose level.
     *
     * @tparam Args Format argument types, deduced from the arguments.
     * @param fmt A std::format format string, validated at compile time.
     * @param args Arguments to format into the message.
     */
    template<typename... Args>
    void verbose(std::format_string<Args...> fmt, Args&&... args) const {
        idfxx::log::verbose(_tag, fmt, std::forward<Args>(args)...);
    }

    /**
     * @brief Log a pre-formatted message at verbose level.
     *
     * @param msg The message string to log.
     */
    void verbose(std::string_view msg) const { idfxx::log::verbose(_tag, msg); }

    /**
     * @brief Set the runtime log level for this logger's tag.
     *
     * Messages with a severity below the configured level are suppressed
     * at runtime.
     *
     * @param lvl The minimum severity level to output.
     */
    void set_level(level lvl) const;

    /**
     * @brief Log a buffer as hexadecimal bytes.
     *
     * @param lvl The log severity level.
     * @param buffer Pointer to the buffer data.
     * @param length Number of bytes to log.
     */
    void buffer_hex(level lvl, const void* buffer, size_t length) const {
        idfxx::log::buffer_hex(lvl, _tag, buffer, length);
    }

    /**
     * @brief Log a buffer as printable characters.
     *
     * @param lvl The log severity level.
     * @param buffer Pointer to the buffer data.
     * @param length Number of bytes to log.
     */
    void buffer_char(level lvl, const void* buffer, size_t length) const {
        idfxx::log::buffer_char(lvl, _tag, buffer, length);
    }

    /**
     * @brief Log a buffer as a formatted hex dump.
     *
     * @param lvl The log severity level.
     * @param buffer Pointer to the buffer data.
     * @param length Number of bytes to log.
     */
    void buffer_hex_dump(level lvl, const void* buffer, size_t length) const {
        idfxx::log::buffer_hex_dump(lvl, _tag, buffer, length);
    }

private:
    const char* _tag;
};

} // namespace idfxx::log

namespace idfxx {

/**
 * @headerfile <idfxx/log>
 * @brief Returns a string representation of a log level.
 *
 * @param lvl The log level to convert.
 * @return "NONE", "ERROR", "WARN", "INFO", "DEBUG", "VERBOSE", or "unknown(N)" for unrecognized values.
 */
[[nodiscard]] inline std::string to_string(log::level lvl) {
    switch (lvl) {
    case log::level::none:
        return "NONE";
    case log::level::error:
        return "ERROR";
    case log::level::warn:
        return "WARN";
    case log::level::info:
        return "INFO";
    case log::level::debug:
        return "DEBUG";
    case log::level::verbose:
        return "VERBOSE";
    default:
        return "unknown(" + std::to_string(static_cast<unsigned int>(lvl)) + ")";
    }
}

} // namespace idfxx

/** @} */

/** @cond INTERNAL */
namespace std {
template<>
struct formatter<idfxx::log::level> {
    constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }

    template<typename FormatContext>
    auto format(idfxx::log::level lvl, FormatContext& ctx) const {
        auto s = idfxx::to_string(lvl);
        return std::copy(s.begin(), s.end(), ctx.out());
    }
};
} // namespace std
/** @endcond */

// NOLINTBEGIN(cppcoreguidelines-macro-usage)

/**
 * @brief Log at error level with zero-cost elimination when compile-time filtered.
 *
 * When `LOG_LOCAL_LEVEL` is set below `ESP_LOG_ERROR`, the compiler eliminates
 * the entire log call including argument evaluation.
 *
 * @param tag The log tag (a `const char*`).
 * @param fmt A std::format format string.
 * @param ... Arguments to format into the message.
 */
#define IDFXX_LOGE(tag, fmt, ...)                                                                                      \
    do {                                                                                                               \
        if (LOG_LOCAL_LEVEL >= ESP_LOG_ERROR) {                                                                        \
            idfxx::log::error(tag, fmt __VA_OPT__(, ) __VA_ARGS__);                                                    \
        }                                                                                                              \
    } while (0)

/**
 * @brief Log at warning level with zero-cost elimination when compile-time filtered.
 *
 * @param tag The log tag (a `const char*`).
 * @param fmt A std::format format string.
 * @param ... Arguments to format into the message.
 */
#define IDFXX_LOGW(tag, fmt, ...)                                                                                      \
    do {                                                                                                               \
        if (LOG_LOCAL_LEVEL >= ESP_LOG_WARN) {                                                                         \
            idfxx::log::warn(tag, fmt __VA_OPT__(, ) __VA_ARGS__);                                                     \
        }                                                                                                              \
    } while (0)

/**
 * @brief Log at info level with zero-cost elimination when compile-time filtered.
 *
 * @param tag The log tag (a `const char*`).
 * @param fmt A std::format format string.
 * @param ... Arguments to format into the message.
 */
#define IDFXX_LOGI(tag, fmt, ...)                                                                                      \
    do {                                                                                                               \
        if (LOG_LOCAL_LEVEL >= ESP_LOG_INFO) {                                                                         \
            idfxx::log::info(tag, fmt __VA_OPT__(, ) __VA_ARGS__);                                                     \
        }                                                                                                              \
    } while (0)

/**
 * @brief Log at debug level with zero-cost elimination when compile-time filtered.
 *
 * @param tag The log tag (a `const char*`).
 * @param fmt A std::format format string.
 * @param ... Arguments to format into the message.
 */
#define IDFXX_LOGD(tag, fmt, ...)                                                                                      \
    do {                                                                                                               \
        if (LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG) {                                                                        \
            idfxx::log::debug(tag, fmt __VA_OPT__(, ) __VA_ARGS__);                                                    \
        }                                                                                                              \
    } while (0)

/**
 * @brief Log at verbose level with zero-cost elimination when compile-time filtered.
 *
 * @param tag The log tag (a `const char*`).
 * @param fmt A std::format format string.
 * @param ... Arguments to format into the message.
 */
#define IDFXX_LOGV(tag, fmt, ...)                                                                                      \
    do {                                                                                                               \
        if (LOG_LOCAL_LEVEL >= ESP_LOG_VERBOSE) {                                                                      \
            idfxx::log::verbose(tag, fmt __VA_OPT__(, ) __VA_ARGS__);                                                  \
        }                                                                                                              \
    } while (0)

// NOLINTEND(cppcoreguidelines-macro-usage)
