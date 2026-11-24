// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

#include "sdkconfig.h"
#ifndef CONFIG_IDFXX_STD_FORMAT
#error                                                                                             \
    "idfxx_log requires CONFIG_IDFXX_STD_FORMAT to be enabled (menuconfig -> IDFXX -> Enable std::format support)"
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
 * - A @ref idfxx::log::logger "logger" class for repeated logging with a fixed tag
 *
 * Optional convenience macros provide zero-cost elimination of filtered
 * messages, including their argument evaluation.
 *
 * Sinks (@ref idfxx::log::sink "sink") receive log lines in addition to the console. Each sink
 * chooses which lines it wants. A sink can receive the lines logged through idfxx::log or, with
 * `CONFIG_IDFXX_LOG_ALL_LINES` enabled, every ESP-IDF log line.
 * @{
 */

#include <idfxx/error>

#include <algorithm>
#include <cstdint>
#include <esp_log.h>
#include <format>
#include <ranges>
#include <span>
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

/// @cond INTERNAL
namespace detail {

// Returns true if any added sink wants the line. log() uses this to format a line that the tag's
// runtime level would otherwise skip. Always false in an interrupt, while the flash cache is
// disabled, before the scheduler starts, and while the calling task is inside a sink.
[[nodiscard]] bool wanted_by_sink(level lvl, const char* tag) noexcept;

} // namespace detail
/// @endcond

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
 * The message is only formatted if the tag's runtime log level outputs the line, or if a sink (see
 * @ref add_sink) wants it. Format strings are validated at compile time.
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
    if (esp_log_level_get(tag) < static_cast<esp_log_level_t>(lvl) &&
        !detail::wanted_by_sink(lvl, tag)) {
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
void buffer_hex(level lvl, const char* tag, const void* buffer, uint16_t length);

/**
 * @headerfile <idfxx/log>
 * @brief Log a contiguous range as hexadecimal bytes.
 *
 * Outputs the buffer contents as hex values, 16 bytes per line.
 * Only the first 65535 bytes are logged; any remainder is silently truncated.
 *
 * @tparam R A contiguous range type (e.g., std::vector, std::span, std::array).
 * @param lvl The log severity level.
 * @param tag The log tag identifying the source.
 * @param data The data to log.
 */
template<std::ranges::contiguous_range R>
    requires(sizeof(std::ranges::range_value_t<R>) == 1)
void buffer_hex(level lvl, const char* tag, const R& data) {
    auto sp = std::span(data);
    buffer_hex(
        lvl,
        tag,
        sp.data(),
        static_cast<uint16_t>(std::min(sp.size(), static_cast<size_t>(UINT16_MAX)))
    );
}

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
void buffer_char(level lvl, const char* tag, const void* buffer, uint16_t length);

/**
 * @headerfile <idfxx/log>
 * @brief Log a contiguous range as printable characters.
 *
 * Outputs the buffer contents as characters, 16 per line. Non-printable
 * characters are not shown.
 * Only the first 65535 bytes are logged; any remainder is silently truncated.
 *
 * @tparam R A contiguous range type (e.g., std::vector, std::span, std::array).
 * @param lvl The log severity level.
 * @param tag The log tag identifying the source.
 * @param data The data to log.
 */
template<std::ranges::contiguous_range R>
    requires(sizeof(std::ranges::range_value_t<R>) == 1)
void buffer_char(level lvl, const char* tag, const R& data) {
    auto sp = std::span(data);
    buffer_char(
        lvl,
        tag,
        sp.data(),
        static_cast<uint16_t>(std::min(sp.size(), static_cast<size_t>(UINT16_MAX)))
    );
}

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
void buffer_hex_dump(level lvl, const char* tag, const void* buffer, uint16_t length);

/**
 * @headerfile <idfxx/log>
 * @brief Log a contiguous range as a formatted hex dump.
 *
 * Outputs a hex dump with memory addresses, hex values, and ASCII
 * representation, similar to the output of the `xxd` command.
 * Only the first 65535 bytes are logged; any remainder is silently truncated.
 *
 * @tparam R A contiguous range type (e.g., std::vector, std::span, std::array).
 * @param lvl The log severity level.
 * @param tag The log tag identifying the source.
 * @param data The data to log.
 */
template<std::ranges::contiguous_range R>
    requires(sizeof(std::ranges::range_value_t<R>) == 1)
void buffer_hex_dump(level lvl, const char* tag, const R& data) {
    auto sp = std::span(data);
    buffer_hex_dump(
        lvl,
        tag,
        sp.data(),
        static_cast<uint16_t>(std::min(sp.size(), static_cast<size_t>(UINT16_MAX)))
    );
}

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
    void buffer_hex(level lvl, const void* buffer, uint16_t length) const {
        idfxx::log::buffer_hex(lvl, _tag, buffer, length);
    }

    /**
     * @brief Log a contiguous range as hexadecimal bytes.
     *
     * Only the first 65535 bytes are logged; any remainder is silently truncated.
     *
     * @tparam R A contiguous range type (e.g., std::vector, std::span, std::array).
     * @param lvl The log severity level.
     * @param data The data to log.
     */
    template<std::ranges::contiguous_range R>
        requires(sizeof(std::ranges::range_value_t<R>) == 1)
    void buffer_hex(level lvl, const R& data) const {
        idfxx::log::buffer_hex(lvl, _tag, data);
    }

    /**
     * @brief Log a buffer as printable characters.
     *
     * @param lvl The log severity level.
     * @param buffer Pointer to the buffer data.
     * @param length Number of bytes to log.
     */
    void buffer_char(level lvl, const void* buffer, uint16_t length) const {
        idfxx::log::buffer_char(lvl, _tag, buffer, length);
    }

    /**
     * @brief Log a contiguous range as printable characters.
     *
     * Non-printable characters are not shown.
     * Only the first 65535 bytes are logged; any remainder is silently truncated.
     *
     * @tparam R A contiguous range type (e.g., std::vector, std::span, std::array).
     * @param lvl The log severity level.
     * @param data The data to log.
     */
    template<std::ranges::contiguous_range R>
        requires(sizeof(std::ranges::range_value_t<R>) == 1)
    void buffer_char(level lvl, const R& data) const {
        idfxx::log::buffer_char(lvl, _tag, data);
    }

    /**
     * @brief Log a buffer as a formatted hex dump.
     *
     * @param lvl The log severity level.
     * @param buffer Pointer to the buffer data.
     * @param length Number of bytes to log.
     */
    void buffer_hex_dump(level lvl, const void* buffer, uint16_t length) const {
        idfxx::log::buffer_hex_dump(lvl, _tag, buffer, length);
    }

    /**
     * @brief Log a contiguous range as a formatted hex dump.
     *
     * Only the first 65535 bytes are logged; any remainder is silently truncated.
     *
     * @tparam R A contiguous range type (e.g., std::vector, std::span, std::array).
     * @param lvl The log severity level.
     * @param data The data to log.
     */
    template<std::ranges::contiguous_range R>
        requires(sizeof(std::ranges::range_value_t<R>) == 1)
    void buffer_hex_dump(level lvl, const R& data) const {
        idfxx::log::buffer_hex_dump(lvl, _tag, data);
    }

private:
    const char* _tag;
};

/**
 * @headerfile <idfxx/log>
 * @brief A destination for log lines, in addition to the console.
 *
 * A sink added with @ref add_sink receives each line that it wants. For example, a sink can store
 * the log in flash or send it over the network. A sink's choice of lines is independent of the
 * console's log levels: a sink can receive a line that `set_level()` hides from the console.
 *
 * Both functions are called from the task that logged the line, and several tasks can call them
 * at the same time. They are never called from an interrupt, while the flash cache is disabled, or
 * before the scheduler starts. Lines logged in those conditions are not passed to any sink. Lines
 * that a task logs while it is inside a sink are not passed to any sink either, so a sink can log
 * safely. A sink must not add or remove sinks from inside its own functions.
 *
 * A sink must remain valid for as long as it is added. Remove it with @ref remove_sink before
 * destroying it. See @ref remove_sink also for the restriction on deleting a task that may be
 * logging.
 *
 * @code
 * class counter final : public idfxx::log::sink {
 *     bool do_wants(idfxx::log::level lvl, std::string_view) const noexcept override {
 *         return lvl <= idfxx::log::level::warn;
 *     }
 *     void do_write(idfxx::log::level, std::string_view, std::string_view) noexcept override {
 *         ++_lines;
 *     }
 *     std::atomic<unsigned> _lines{0};
 * };
 *
 * static counter warnings;
 * idfxx::log::add_sink(warnings);
 * @endcode
 */
class sink {
public:
    virtual ~sink() = default;

    /**
     * @brief Returns whether the sink wants a line.
     *
     * Always called before `write()`, which only receives lines that the sink wants. Where
     * possible, it is also called before the line is formatted, so that a line is only formatted
     * if the console or a sink will use it. It may be called more than once for the same line.
     *
     * @param lvl The line's severity level.
     * @param tag The line's tag, or an empty string if the line has no tag.
     * @return True if the sink wants the line.
     */
    [[nodiscard]] bool wants(level lvl, std::string_view tag) const noexcept {
        return do_wants(lvl, tag);
    }

    /**
     * @brief Receives a line that the sink wants.
     *
     * @param lvl The line's severity level.
     * @param tag The line's tag, or an empty string if the line has no tag.
     * @param message The line's message. It excludes the level, timestamp and tag that the console
     *                prints before the message, and the newline that ends the line. The message
     *                itself may contain newlines. Valid only during the call.
     */
    void write(level lvl, std::string_view tag, std::string_view message) noexcept {
        do_write(lvl, tag, message);
    }

protected:
    sink() = default;
    sink(const sink&) = default;
    sink& operator=(const sink&) = default;

private:
    virtual bool do_wants(level lvl, std::string_view tag) const noexcept = 0;
    virtual void do_write(level lvl, std::string_view tag, std::string_view message) noexcept = 0;
};

#if defined(CONFIG_IDFXX_LOG_ALL_LINES) || defined(__DOXYGEN__)
/// @cond INTERNAL
// The type of `all_lines`. It selects the add_sink() overloads that add a sink for every log line.
struct all_lines_t {
    explicit all_lines_t() = default;
};
/// @endcond

/**
 * @headerfile <idfxx/log>
 * @brief Tag for adding a sink that receives every log line, as in
 *        `idfxx::log::add_sink(idfxx::log::all_lines, s)`.
 *
 * @note Only available when `CONFIG_IDFXX_LOG_ALL_LINES` is enabled in menuconfig.
 */
inline constexpr all_lines_t all_lines{};
#endif

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
/**
 * @headerfile <idfxx/log>
 * @brief Adds a sink for the lines logged through idfxx::log.
 *
 * The sink receives the lines logged with @ref log, a @ref logger and the `IDFXX_LOGx` macros.
 * Each line is passed to the sink complete and as formatted, after the console has printed it. A
 * line that the console's log level hides is still passed to the sink.
 *
 * The sink does not receive:
 * - buffer dumps (@ref buffer_hex and the related functions),
 * - lines that the formatting functions and macros skip because they are above the compile-time
 *   maximum level (`LOG_LOCAL_LEVEL`),
 * - lines that other code logs through ESP-IDF's log.
 *
 * To receive buffer dumps and other code's lines as well, add the sink with @ref all_lines.
 *
 * A sink must not add or remove sinks from inside its own functions.
 *
 * @param s The sink. It must remain valid until it is removed.
 * @throws std::system_error with `errc::invalid_arg` if the sink is already added (for idfxx::log
 *         lines or for every line), or `errc::invalid_state` if `CONFIG_IDFXX_LOG_MAX_SINKS` sinks
 *         are already added. A sink that is being removed counts toward that limit until the calls
 *         into it have finished.
 * @note Only available when `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled in menuconfig.
 */
void add_sink(sink& s);
#endif

/**
 * @headerfile <idfxx/log>
 * @brief Adds a sink for the lines logged through idfxx::log.
 *
 * The sink receives the lines logged with @ref log, a @ref logger and the `IDFXX_LOGx` macros.
 * Each line is passed to the sink complete and as formatted, after the console has printed it. A
 * line that the console's log level hides is still passed to the sink.
 *
 * The sink does not receive:
 * - buffer dumps (@ref buffer_hex and the related functions),
 * - lines that the formatting functions and macros skip because they are above the compile-time
 *   maximum level (`LOG_LOCAL_LEVEL`),
 * - lines that other code logs through ESP-IDF's log.
 *
 * To receive buffer dumps and other code's lines as well, add the sink with @ref all_lines.
 *
 * A sink must not add or remove sinks from inside its own functions.
 *
 * @param s The sink. It must remain valid until it is removed.
 * @return Success, or an error.
 * @retval errc::invalid_arg The sink is already added (for idfxx::log lines or for every line).
 * @retval errc::invalid_state `CONFIG_IDFXX_LOG_MAX_SINKS` sinks are already added. A sink that is
 *         being removed counts toward that limit until the calls into it have finished.
 */
[[nodiscard]] result<void> try_add_sink(sink& s);

#if defined(CONFIG_IDFXX_LOG_ALL_LINES) || defined(__DOXYGEN__)
#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
/**
 * @headerfile <idfxx/log>
 * @brief Adds a sink for every log line.
 *
 * The sink receives every line logged through ESP-IDF's log: `ESP_LOGx`, `ESP_LOG_BUFFER_*`,
 * `esp_log_write`, and the libraries that log through them. It also receives the lines logged
 * through idfxx::log. Each line is passed to the sink after the console has printed it. A line
 * that the console's log level hides is still passed to the sink.
 *
 * The sink does not receive these ESP-IDF log lines:
 * - early and DRAM lines (`ESP_EARLY_LOGx`, `ESP_DRAM_LOGx`),
 * - lines logged in ESP-IDF's binary log mode,
 * - lines above the compile-time maximum level (`CONFIG_LOG_MAXIMUM_LEVEL`, or `LOG_LOCAL_LEVEL`
 *   in a source file),
 * - with `CONFIG_LOG_MASTER_LEVEL` under log v1, `ESP_LOGx` lines above the master level. The
 *   macros skip these lines before logging them.
 *
 * Lines logged through idfxx::log (with @ref log, a @ref logger and the `IDFXX_LOGx` macros) are
 * passed to the sink complete and as formatted, whatever their length. This is the same as for a
 * sink added without @ref all_lines. Buffer dumps (@ref buffer_hex and the related functions) are
 * different: they are logged through ESP-IDF's log one line at a time, so they are handled, and
 * shortened, like any other ESP-IDF log line.
 *
 * Each call to `esp_log` is one line, whether or not its format string ends in a newline.
 * `ESP_LOGx` and `ESP_LOG_BUFFER_*` call `esp_log`. Some libraries log one line in several calls
 * to `esp_log_write`: a prefix, then the message, then a newline. Such a line is passed to the
 * sink once, complete, when its newline arrives, with the level and tag of its first call. A line
 * logged through idfxx::log in the meantime does not end or extend the unfinished line.
 *
 * A line longer than `CONFIG_IDFXX_LOG_SINK_MAX_LINE` bytes is shortened to that limit, at a UTF-8
 * character boundary. The limit includes the prefix that the line's caller formatted before the
 * message, even though the sink does not receive the prefix. The prefix is log v1's level,
 * timestamp and tag, or whatever a caller of `esp_log_write` formats itself. For a line logged in
 * several calls, the limit also includes the line's tag. The prefix is located using the line's
 * tag, so a line with an empty tag is passed to the sink with its prefix still attached.
 *
 * A sink must not add or remove sinks from inside its own functions.
 *
 * @param s The sink. It must remain valid until it is removed.
 * @throws std::system_error with `errc::invalid_arg` if the sink is already added (for idfxx::log
 *         lines or for every line), or `errc::invalid_state` if `CONFIG_IDFXX_LOG_MAX_SINKS` sinks
 *         are already added. A sink that is being removed counts toward that limit until the calls
 *         into it have finished.
 * @note Only available when `CONFIG_IDFXX_LOG_ALL_LINES` and `CONFIG_COMPILER_CXX_EXCEPTIONS` are
 *       enabled in menuconfig. To receive every line, idfxx_log wraps `esp_log` and `esp_log_va`
 *       at link time (`-Wl,--wrap`). Nothing else in the app may wrap them: a second definition of
 *       `__wrap_esp_log` or `__wrap_esp_log_va` makes the link fail.
 */
void add_sink(all_lines_t, sink& s);
#endif

/**
 * @headerfile <idfxx/log>
 * @brief Adds a sink for every log line.
 *
 * The sink receives every line logged through ESP-IDF's log: `ESP_LOGx`, `ESP_LOG_BUFFER_*`,
 * `esp_log_write`, and the libraries that log through them. It also receives the lines logged
 * through idfxx::log. Each line is passed to the sink after the console has printed it. A line
 * that the console's log level hides is still passed to the sink.
 *
 * The sink does not receive these ESP-IDF log lines:
 * - early and DRAM lines (`ESP_EARLY_LOGx`, `ESP_DRAM_LOGx`),
 * - lines logged in ESP-IDF's binary log mode,
 * - lines above the compile-time maximum level (`CONFIG_LOG_MAXIMUM_LEVEL`, or `LOG_LOCAL_LEVEL`
 *   in a source file),
 * - with `CONFIG_LOG_MASTER_LEVEL` under log v1, `ESP_LOGx` lines above the master level. The
 *   macros skip these lines before logging them.
 *
 * Lines logged through idfxx::log (with @ref log, a @ref logger and the `IDFXX_LOGx` macros) are
 * passed to the sink complete and as formatted, whatever their length. This is the same as for a
 * sink added without @ref all_lines. Buffer dumps (@ref buffer_hex and the related functions) are
 * different: they are logged through ESP-IDF's log one line at a time, so they are handled, and
 * shortened, like any other ESP-IDF log line.
 *
 * Each call to `esp_log` is one line, whether or not its format string ends in a newline.
 * `ESP_LOGx` and `ESP_LOG_BUFFER_*` call `esp_log`. Some libraries log one line in several calls
 * to `esp_log_write`: a prefix, then the message, then a newline. Such a line is passed to the
 * sink once, complete, when its newline arrives, with the level and tag of its first call. A line
 * logged through idfxx::log in the meantime does not end or extend the unfinished line.
 *
 * A line longer than `CONFIG_IDFXX_LOG_SINK_MAX_LINE` bytes is shortened to that limit, at a UTF-8
 * character boundary. The limit includes the prefix that the line's caller formatted before the
 * message, even though the sink does not receive the prefix. The prefix is log v1's level,
 * timestamp and tag, or whatever a caller of `esp_log_write` formats itself. For a line logged in
 * several calls, the limit also includes the line's tag. The prefix is located using the line's
 * tag, so a line with an empty tag is passed to the sink with its prefix still attached.
 *
 * A sink must not add or remove sinks from inside its own functions.
 *
 * @param s The sink. It must remain valid until it is removed.
 * @return Success, or an error.
 * @retval errc::invalid_arg The sink is already added (for idfxx::log lines or for every line).
 * @retval errc::invalid_state `CONFIG_IDFXX_LOG_MAX_SINKS` sinks are already added. A sink that is
 *         being removed counts toward that limit until the calls into it have finished.
 * @note Only available when `CONFIG_IDFXX_LOG_ALL_LINES` is enabled in menuconfig. To receive
 *       every line, idfxx_log wraps `esp_log` and `esp_log_va` at link time (`-Wl,--wrap`). Nothing
 *       else in the app may wrap them: a second definition of `__wrap_esp_log` or
 *       `__wrap_esp_log_va` makes the link fail.
 */
[[nodiscard]] result<void> try_add_sink(all_lines_t, sink& s);
#endif

/**
 * @headerfile <idfxx/log>
 * @brief Removes a sink.
 *
 * Returns once no call into the sink is in progress, so the sink can be destroyed afterward. If
 * two tasks remove the same sink at the same time, both calls return only then. While a removal
 * waits, other sinks can be added and removed, but the sink being removed still counts toward
 * `CONFIG_IDFXX_LOG_MAX_SINKS` until the calls into it have finished. Does nothing if the sink is
 * not added.
 *
 * A sink must not add or remove sinks from inside its own functions: removing a sink from inside
 * that sink would wait for itself. If a task is deleted while it is inside the sink, this function
 * never returns, so do not delete a task that may be logging.
 *
 * @param s The sink to remove.
 */
void remove_sink(sink& s) noexcept;

} // namespace idfxx::log

namespace idfxx {

/**
 * @headerfile <idfxx/log>
 * @brief Returns a string representation of a log level.
 *
 * @param lvl The log level to convert.
 * @return "NONE", "ERROR", "WARN", "INFO", "DEBUG", "VERBOSE", or "unknown(N)" for unrecognized
 * values.
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
#define IDFXX_LOGE(tag, fmt, ...)                                                                  \
    do {                                                                                           \
        if (LOG_LOCAL_LEVEL >= ESP_LOG_ERROR) {                                                    \
            idfxx::log::error(tag, fmt __VA_OPT__(, ) __VA_ARGS__);                                \
        }                                                                                          \
    } while (0)

/**
 * @brief Log at warning level with zero-cost elimination when compile-time filtered.
 *
 * @param tag The log tag (a `const char*`).
 * @param fmt A std::format format string.
 * @param ... Arguments to format into the message.
 */
#define IDFXX_LOGW(tag, fmt, ...)                                                                  \
    do {                                                                                           \
        if (LOG_LOCAL_LEVEL >= ESP_LOG_WARN) {                                                     \
            idfxx::log::warn(tag, fmt __VA_OPT__(, ) __VA_ARGS__);                                 \
        }                                                                                          \
    } while (0)

/**
 * @brief Log at info level with zero-cost elimination when compile-time filtered.
 *
 * @param tag The log tag (a `const char*`).
 * @param fmt A std::format format string.
 * @param ... Arguments to format into the message.
 */
#define IDFXX_LOGI(tag, fmt, ...)                                                                  \
    do {                                                                                           \
        if (LOG_LOCAL_LEVEL >= ESP_LOG_INFO) {                                                     \
            idfxx::log::info(tag, fmt __VA_OPT__(, ) __VA_ARGS__);                                 \
        }                                                                                          \
    } while (0)

/**
 * @brief Log at debug level with zero-cost elimination when compile-time filtered.
 *
 * @param tag The log tag (a `const char*`).
 * @param fmt A std::format format string.
 * @param ... Arguments to format into the message.
 */
#define IDFXX_LOGD(tag, fmt, ...)                                                                  \
    do {                                                                                           \
        if (LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG) {                                                    \
            idfxx::log::debug(tag, fmt __VA_OPT__(, ) __VA_ARGS__);                                \
        }                                                                                          \
    } while (0)

/**
 * @brief Log at verbose level with zero-cost elimination when compile-time filtered.
 *
 * @param tag The log tag (a `const char*`).
 * @param fmt A std::format format string.
 * @param ... Arguments to format into the message.
 */
#define IDFXX_LOGV(tag, fmt, ...)                                                                  \
    do {                                                                                           \
        if (LOG_LOCAL_LEVEL >= ESP_LOG_VERBOSE) {                                                  \
            idfxx::log::verbose(tag, fmt __VA_OPT__(, ) __VA_ARGS__);                              \
        }                                                                                          \
    } while (0)

// NOLINTEND(cppcoreguidelines-macro-usage)
