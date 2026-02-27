// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

/**
 * @headerfile <idfxx/http/client>
 * @file client.hpp
 * @brief HTTP client class.
 *
 * @defgroup idfxx_http_client HTTP Client Component
 * @brief Type-safe HTTP/HTTPS client for ESP32.
 *
 * Provides blocking and streaming HTTP client operations with support for
 * TLS, authentication, redirects, and event-driven response handling.
 *
 * Depends on @ref idfxx_core for error handling.
 * @{
 */

#include <idfxx/error>
#include <idfxx/http/types>

#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>

typedef struct esp_http_client* esp_http_client_handle_t;
struct esp_http_client_event;
typedef int esp_err_t;

namespace idfxx::http {

/**
 * @headerfile <idfxx/http/client>
 * @brief HTTP client event identifiers.
 */
enum class event_id : int {
    // clang-format off
    error        = 0, ///< An error occurred during the HTTP operation
    on_connected = 1, ///< Connected to the server
    headers_sent = 2, ///< All request headers have been sent
    on_header    = 3, ///< Received a response header
    on_data      = 4, ///< Received response body data
    on_finish    = 5, ///< Finished a complete HTTP session
    disconnected = 6, ///< Disconnected from the server
    redirect     = 7, ///< Intercepting a redirect
    // clang-format on
};

/**
 * @headerfile <idfxx/http/client>
 * @brief Data passed to HTTP event callbacks.
 */
struct event_data {
    enum event_id id;              ///< Event type
    std::span<const uint8_t> data; ///< Payload for on_data events
    std::string_view header_key;   ///< Header name for on_header events
    std::string_view header_value; ///< Header value for on_header events
};

/**
 * @headerfile <idfxx/http/client>
 * @brief HTTP client with blocking and streaming request support.
 *
 * Manages an HTTP session with support for TLS, authentication, custom headers,
 * redirects, and event callbacks. Provides both a simple blocking perform() API
 * and a streaming open()/write()/read()/close() API.
 *
 * Not thread-safe — callers must manage their own synchronization.
 */
class client {
public:
    /**
     * @brief Error codes for HTTP client operations.
     */
    enum class errc : esp_err_t {
        // clang-format off
        max_redirect      = 0x7001, ///< Maximum number of redirects exceeded
        connect           = 0x7002, ///< Connection to server failed
        write_data        = 0x7003, ///< Error writing request data
        fetch_header      = 0x7004, ///< Error reading response headers
        invalid_transport = 0x7005, ///< Invalid transport type specified
        connecting        = 0x7006, ///< Connection in progress (async mode)
        eagain            = 0x7007, ///< Resource temporarily unavailable, try again
        connection_closed = 0x7008, ///< Connection closed by remote
        // clang-format on
    };

    /**
     * @brief Error category for HTTP client errors.
     */
    class error_category : public std::error_category {
    public:
        /** @brief Returns the name of the error category. */
        [[nodiscard]] const char* name() const noexcept override final;

        /** @brief Returns a human-readable message for the given error code. */
        [[nodiscard]] std::string message(int ec) const override final;
    };

    /**
     * @brief HTTP client configuration.
     */
    struct config {
        // Connection
        std::string url = {};                               ///< Full URL
        std::string host = {};                              ///< Host name
        int port = 0;                                       ///< Port number (0 = default)
        std::string path = {};                              ///< URL path
        std::string query = {};                             ///< URL query string
        enum method method = method::get;                   ///< HTTP method
        enum transport transport_type = transport::unknown; ///< Transport type
        std::chrono::milliseconds timeout{5000};            ///< Request timeout

        // Authentication
        std::string username = {};                  ///< Username for authentication
        std::string password = {};                  ///< Password for authentication
        enum auth_type auth_type = auth_type::none; ///< Authentication type

        // TLS (client stores owned copies to keep data alive for the handle)
        std::string cert_pem = {};        ///< Server CA certificate (PEM)
        std::string client_cert_pem = {}; ///< Client certificate (PEM)
        std::string client_key_pem = {};  ///< Client private key (PEM)

        // Headers
        std::string user_agent = {}; ///< Custom User-Agent string

        // Redirect
        bool disable_auto_redirect = false; ///< Disable automatic redirect following
        int max_redirection_count = 0;      ///< Maximum redirects (0 = default)
        int max_authorization_retries = 0;  ///< Maximum auth retries (0 = default)

        // Buffers
        int buffer_size = 0;    ///< Receive buffer size (0 = default)
        int buffer_size_tx = 0; ///< Transmit buffer size (0 = default)

        // Keep-alive
        bool keep_alive_enable = false;              ///< Enable TCP keep-alive
        std::chrono::seconds keep_alive_idle{5};     ///< Keep-alive idle time
        std::chrono::seconds keep_alive_interval{5}; ///< Keep-alive probe interval
        int keep_alive_count = 3;                    ///< Keep-alive probe count

        // TLS options
        bool use_global_ca_store = false;         ///< Use global CA store
        bool skip_cert_common_name_check = false; ///< Skip CN verification
        std::string common_name = {};             ///< Expected server common name
        /**
         * @brief TLS certificate bundle attach callback.
         *
         * Called during the TLS handshake to attach a certificate bundle to the
         * underlying TLS configuration. The parameter is an opaque pointer to
         * the TLS configuration (e.g. mbedtls_ssl_config). Returns 0 on
         * success or a non-zero error code on failure.
         *
         * Typically set to `esp_crt_bundle_attach` from the ESP-IDF certificate
         * bundle component.
         */
        std::move_only_function<int(void*) const> crt_bundle_attach = {};

        // Event handling
        std::move_only_function<void(const event_data&) const> on_event = {}; ///< Event callback
    };

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Creates an HTTP client from configuration.
     *
     * @param cfg Client configuration.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    [[nodiscard]] explicit client(config cfg);
#endif

    /**
     * @brief Creates an HTTP client from configuration.
     *
     * @param cfg Client configuration.
     * @return The new client, or an error.
     */
    [[nodiscard]] static result<client> make(config cfg);

    /**
     * @brief Destroys the client and releases all resources.
     */
    ~client();

    client(const client&) = delete;
    client& operator=(const client&) = delete;

    /** @brief Move constructor. Transfers handle ownership. */
    client(client&&) noexcept;

    /** @brief Move assignment. Transfers handle ownership. */
    client& operator=(client&&) noexcept;

    // =========================================================================
    // Request Configuration
    // =========================================================================

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Sets the request URL.
     *
     * @param url The URL string.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    void set_url(std::string_view url) { unwrap(try_set_url(url)); }

#endif

    /**
     * @brief Sets the request URL.
     *
     * @param url The URL string.
     * @return Success, or an error.
     */
    [[nodiscard]] result<void> try_set_url(std::string_view url);

    /**
     * @brief Sets the HTTP method.
     *
     * @param m The HTTP method.
     */
    void set_method(enum method m);

    /**
     * @brief Sets a request header.
     *
     * Throws std::bad_alloc (or aborts) on allocation failure.
     *
     * @param key Header name.
     * @param value Header value.
     */
    void set_header(std::string_view key, std::string_view value);

    /**
     * @brief Deletes a request header.
     *
     * @param key Header name to remove.
     */
    void delete_header(std::string_view key);

    /**
     * @brief Sets the POST request body.
     *
     * The client takes ownership of a copy of the data, keeping it alive
     * for the duration of the request. Throws std::bad_alloc (or aborts)
     * on allocation failure.
     *
     * @param data POST body content.
     */
    void set_post_field(std::string_view data);

    /**
     * @brief Sets the username for authentication.
     *
     * @param username The username.
     */
    void set_username(std::string_view username);

    /**
     * @brief Sets the password for authentication.
     *
     * @param password The password.
     */
    void set_password(std::string_view password);

    /**
     * @brief Sets the authentication type.
     *
     * @param type The authentication type.
     */
    void set_auth_type(enum auth_type type);

    /**
     * @brief Sets the request timeout.
     *
     * @tparam Rep Duration representation type.
     * @tparam Period Duration period type.
     * @param timeout The timeout duration.
     */
    template<typename Rep, typename Period>
    void set_timeout(const std::chrono::duration<Rep, Period>& timeout) {
        _set_timeout(std::chrono::ceil<std::chrono::milliseconds>(timeout));
    }

    // =========================================================================
    // Simple Request (blocking)
    // =========================================================================

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Performs a blocking HTTP request.
     *
     * Sends the request and reads the complete response. Use event callbacks
     * or the streaming API to process response data.
     *
     * @code
     * idfxx::http::client c({.url = "https://example.com"});
     * c.perform();
     * auto status = c.status_code();
     * @endcode
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    void perform() { unwrap(try_perform()); }
#endif

    /**
     * @brief Performs a blocking HTTP request.
     *
     * Sends the request and reads the complete response. Use event callbacks
     * or the streaming API to process response data.
     *
     * @return Success, or an error.
     */
    [[nodiscard]] result<void> try_perform();

    // =========================================================================
    // Streaming API
    // =========================================================================

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Opens a connection for streaming.
     *
     * Opens the HTTP connection and sends request headers. If write_len is
     * greater than zero, the connection is opened for writing that many bytes
     * before reading the response.
     *
     * @param write_len Number of bytes to write (0 for read-only requests).
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error if write_len exceeds INT_MAX or the connection fails.
     */
    void open(size_t write_len = 0) { unwrap(try_open(write_len)); }

    /// @overload
    void open(int write_len) { open(static_cast<size_t>(write_len)); }

    /**
     * @brief Writes data to the open connection.
     *
     * @param data Binary data to write.
     * @return Number of bytes written.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    [[nodiscard]] size_t write(std::span<const uint8_t> data) { return unwrap(try_write(data)); }

    /**
     * @brief Writes string data to the open connection.
     *
     * @param data String data to write.
     * @return Number of bytes written.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    [[nodiscard]] size_t write(std::string_view data) { return unwrap(try_write(data)); }

    /**
     * @brief Reads and processes response headers.
     *
     * Must be called after open() (and any write() calls) before reading
     * response body data.
     *
     * @return The content length, or -1 if chunked/unknown.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    int64_t fetch_headers() { return unwrap(try_fetch_headers()); }

    /**
     * @brief Reads response body data.
     *
     * @param buf Buffer to read into.
     * @return Number of bytes read, or 0 at end of response.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    [[nodiscard]] size_t read(std::span<uint8_t> buf) { return unwrap(try_read(buf)); }

    /**
     * @brief Reads response body data into a character buffer.
     *
     * @param buf Buffer to read into.
     * @return Number of bytes read, or 0 at end of response.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    [[nodiscard]] size_t read(std::span<char> buf) { return unwrap(try_read(buf)); }

    /**
     * @brief Discards any remaining response data.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    void flush_response() { unwrap(try_flush_response()); }

    /**
     * @brief Closes the connection.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    void close() { unwrap(try_close()); }
#endif

    /**
     * @brief Opens a connection for streaming.
     *
     * Opens the HTTP connection and sends request headers. If write_len is
     * greater than zero, the connection is opened for writing that many bytes
     * before reading the response.
     *
     * @param write_len Number of bytes to write (0 for read-only requests).
     * @return Success, or an error.
     * @retval idfxx::errc::invalid_arg if write_len exceeds INT_MAX.
     */
    [[nodiscard]] result<void> try_open(size_t write_len = 0);

    /// @overload
    [[nodiscard]] result<void> try_open(int write_len) { return try_open(static_cast<size_t>(write_len)); }

    /**
     * @brief Writes data to the open connection.
     *
     * @param data Binary data to write.
     * @return Number of bytes written, or an error.
     */
    [[nodiscard]] result<size_t> try_write(std::span<const uint8_t> data);

    /**
     * @brief Writes string data to the open connection.
     *
     * @param data String data to write.
     * @return Number of bytes written, or an error.
     */
    [[nodiscard]] result<size_t> try_write(std::string_view data);

    /**
     * @brief Reads and processes response headers.
     *
     * Must be called after open() (and any write() calls) before reading
     * response body data.
     *
     * @return The content length (-1 if chunked/unknown), or an error.
     */
    // [[nodiscard]] intentionally omitted: callers may fetch headers for their
    // side effects without needing the returned content-length value.
    result<int64_t> try_fetch_headers();

    /**
     * @brief Reads response body data.
     *
     * @param buf Buffer to read into.
     * @return Number of bytes read (0 at end of response), or an error.
     */
    [[nodiscard]] result<size_t> try_read(std::span<uint8_t> buf);

    /**
     * @brief Reads response body data into a character buffer.
     *
     * @param buf Buffer to read into.
     * @return Number of bytes read (0 at end of response), or an error.
     */
    [[nodiscard]] result<size_t> try_read(std::span<char> buf);

    /**
     * @brief Discards any remaining response data.
     *
     * @return Success, or an error.
     */
    [[nodiscard]] result<void> try_flush_response();

    /**
     * @brief Closes the connection.
     *
     * @return Success, or an error.
     */
    [[nodiscard]] result<void> try_close();

    // =========================================================================
    // Response Accessors
    // =========================================================================

    /**
     * @brief Returns the HTTP response status code.
     * @return The status code (e.g. 200, 404).
     */
    [[nodiscard]] int status_code() const;

    /**
     * @brief Returns the response content length.
     * @return Content length in bytes, or -1 if unknown/chunked.
     */
    [[nodiscard]] int64_t content_length() const;

    /**
     * @brief Checks whether the response uses chunked transfer encoding.
     * @return true if the response is chunked.
     */
    [[nodiscard]] bool is_chunked_response() const;

    /**
     * @brief Retrieves a response header value.
     *
     * @param key Header name to look up.
     * @return The header value, or std::nullopt if the header is not present.
     */
    [[nodiscard]] std::optional<std::string> get_header(std::string_view key) const;

    /**
     * @brief Retrieves the current effective URL.
     *
     * May differ from the originally configured URL after redirects.
     *
     * @return The current URL.
     */
    [[nodiscard]] std::string get_url() const;

    // =========================================================================
    // Redirect Control
    // =========================================================================

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Applies the current redirect URL.
     *
     * Sets the client URL to the redirect target from the Location header.
     * Typically called from a redirect event callback when automatic
     * redirect is disabled.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    void set_redirection() { unwrap(try_set_redirection()); }
#endif

    /**
     * @brief Applies the current redirect URL.
     *
     * Sets the client URL to the redirect target from the Location header.
     * Typically called from a redirect event callback when automatic
     * redirect is disabled.
     *
     * @return Success, or an error.
     */
    [[nodiscard]] result<void> try_set_redirection();

    /**
     * @brief Resets the redirect counter to zero.
     *
     * Allows additional redirects up to max_redirection_count.
     */
    void reset_redirect_counter();

    /**
     * @brief Returns the underlying ESP-IDF HTTP client handle.
     * @return The esp_http_client handle.
     */
    [[nodiscard]] esp_http_client_handle_t idf_handle() const noexcept { return _handle; }

private:
    client(
        esp_http_client_handle_t handle,
        std::move_only_function<void(const event_data&) const> on_event,
        std::move_only_function<int(void*) const> crt_bundle_attach,
        std::string post_data,
        std::string cert_pem,
        std::string client_cert_pem,
        std::string client_key_pem
    );

    void _set_timeout(std::chrono::milliseconds timeout);

    static result<esp_http_client_handle_t> _init_client(
        config& cfg,
        const std::string& cert_pem,
        const std::string& client_cert_pem,
        const std::string& client_key_pem
    );
    static esp_err_t _http_event_handler(esp_http_client_event* evt);
    static esp_err_t _crt_bundle_trampoline(void* conf);

    esp_http_client_handle_t _handle;
    std::move_only_function<void(const event_data&) const> _on_event;
    std::move_only_function<int(void*) const> _crt_bundle_attach;
    std::string _post_data;
    std::string _cert_pem;
    std::string _client_cert_pem;
    std::string _client_key_pem;
};

/**
 * @headerfile <idfxx/http/client>
 * @brief Returns a reference to the HTTP client error category singleton.
 *
 * @return Reference to the singleton client::error_category instance.
 */
[[nodiscard]] const client::error_category& http_client_category() noexcept;

/**
 * @headerfile <idfxx/http/client>
 * @brief Creates an error code from an idfxx::http::client::errc value.
 */
[[nodiscard]] inline std::error_code make_error_code(client::errc e) noexcept {
    return {std::to_underlying(e), http_client_category()};
}

/**
 * @headerfile <idfxx/http/client>
 * @brief Creates an unexpected error from an ESP-IDF error code, mapping to HTTP client
 *        error codes where possible.
 *
 * Converts the ESP-IDF error code to an HTTP client-specific error code if a mapping
 * exists, otherwise falls back to the default IDFXX error category.
 *
 * @param e The ESP-IDF error code.
 * @return An unexpected value suitable for returning from result-returning functions.
 */
[[nodiscard]] std::unexpected<std::error_code> http_client_error(esp_err_t e);

} // namespace idfxx::http

namespace idfxx {

/**
 * @headerfile <idfxx/http/client>
 * @brief Returns a string representation of an HTTP event identifier.
 *
 * @param id The event identifier to convert.
 * @return "ERROR", "ON_CONNECTED", etc., or "unknown(N)" for unrecognized values.
 */
[[nodiscard]] std::string to_string(http::event_id id);

} // namespace idfxx

/** @cond INTERNAL */
namespace std {
template<>
struct is_error_code_enum<idfxx::http::client::errc> : true_type {};
} // namespace std
/** @endcond */

#include "sdkconfig.h"
#ifdef CONFIG_IDFXX_STD_FORMAT
/** @cond INTERNAL */
#include <algorithm>
#include <format>
namespace std {

template<>
struct formatter<idfxx::http::event_id> {
    constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }

    template<typename FormatContext>
    auto format(idfxx::http::event_id id, FormatContext& ctx) const {
        auto s = idfxx::to_string(id);
        return std::copy(s.begin(), s.end(), ctx.out());
    }
};

} // namespace std
/** @endcond */
#endif // CONFIG_IDFXX_STD_FORMAT

/** @} */ // end of idfxx_http_client
