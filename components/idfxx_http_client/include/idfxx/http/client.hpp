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

#include "sdkconfig.h"

#include <idfxx/byte_source>
#include <idfxx/error>
#include <idfxx/http/types>

#include <chrono>
#include <cstdint>
#include <esp_idf_version.h>
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
    error                = 0, ///< An error occurred during the HTTP operation
    on_connected         = 1, ///< Connected to the server
    headers_sent         = 2, ///< All request headers have been sent
    on_header            = 3, ///< Received a response header
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
    on_headers_complete  = 4, ///< All response headers have been received
    on_status_code       = 5, ///< Received the HTTP status code from the server
    on_data              = 6, ///< Received response body data
    on_finish            = 7, ///< Finished a complete HTTP session
    disconnected         = 8, ///< Disconnected from the server
    redirect             = 9, ///< Intercepting a redirect
#else
    on_data              = 4, ///< Received response body data
    on_finish            = 5, ///< Finished a complete HTTP session
    disconnected         = 6, ///< Disconnected from the server
    redirect             = 7, ///< Intercepting a redirect
#endif
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
 * and a streaming open()/write()/read()/close() API, including chunked request
 * bodies via open_chunked().
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
        max_redirect          = 0x7001, ///< Maximum number of redirects exceeded
        connect               = 0x7002, ///< Connection to server failed
        write_data            = 0x7003, ///< Error writing request data
        fetch_header          = 0x7004, ///< Error reading response headers
        invalid_transport     = 0x7005, ///< Invalid transport type specified
        connecting            = 0x7006, ///< Connection in progress (async mode)
        eagain                = 0x7007, ///< Resource temporarily unavailable, try again
        connection_closed     = 0x7008, ///< Connection closed by remote
        not_modified          = 0x7009, ///< Server responded 304 Not Modified
        range_not_satisfiable = 0x700A, ///< Server responded 416 Range Not Satisfiable
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 5, 2)
        /// Timed out reading response data.
        /// @note Available from ESP-IDF 5.5.2.
        read_timeout          = 0x700B,
        /// Response data ended before the expected length or final chunk.
        /// @note Available from ESP-IDF 5.5.2.
        incomplete_data       = 0x700C,
#endif
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
        /// Redirect from HTTPS to a non-HTTPS URL was blocked.
        /// @note Available from ESP-IDF 6.0.
        redirect_downgrade    = 0x700D,
        /// A request header does not fit in the transmit buffer.
        /// @note Available from ESP-IDF 6.0.
        header_too_long       = 0x700E,
#endif
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
        std::string url = {};  ///< Full URL
        std::string host = {}; ///< Host name
        /// Port number, or std::nullopt to let the driver choose based on the transport. An explicit value of 0
        /// or less is rejected with `errc::invalid_arg` when the client is created.
        std::optional<int> port = std::nullopt;
        /// URL path, or std::nullopt to let the driver choose. An explicit empty path is rejected with
        /// `errc::invalid_arg` when the client is created.
        std::optional<std::string> path = std::nullopt;
        std::string query = {};                             ///< URL query string
        enum method method = method::get;                   ///< HTTP method
        enum transport transport_type = transport::unknown; ///< Transport type
        /// Network timeout, or std::nullopt to let the driver choose. An explicit timeout of 0 or less is
        /// rejected with `errc::invalid_arg` when the client is created.
        std::optional<std::chrono::milliseconds> timeout = std::nullopt;

        // Authentication
        std::string username = {};                  ///< Username for authentication
        std::string password = {};                  ///< Password for authentication
        enum auth_type auth_type = auth_type::none; ///< Authentication type

        // TLS (client stores owned copies to keep data alive for the handle)
        std::string cert_pem = {};        ///< Server CA certificate (PEM)
        std::string client_cert_pem = {}; ///< Client certificate (PEM)
        std::string client_key_pem = {};  ///< Client private key (PEM)

        // Headers
        /// User-Agent header value, or std::nullopt to let the driver choose. An explicit string, including an
        /// empty one, is sent as given.
        std::optional<std::string> user_agent = std::nullopt;

        // Redirect
        bool disable_auto_redirect = false; ///< Disable automatic redirect following
        /// Maximum number of redirects to follow, or std::nullopt to let the driver choose. An explicit value of 0
        /// or less is rejected with `errc::invalid_arg` when the client is created; to stop following redirects,
        /// set `disable_auto_redirect`, which returns the 3xx response to the caller instead.
        std::optional<int> max_redirection_count = std::nullopt;
        /// Maximum number of authorization retries, or std::nullopt to let the driver choose. An explicit value of 0
        /// disables retries. A negative value is rejected with `errc::invalid_arg` when the client is created.
        std::optional<int> max_authorization_retries = std::nullopt;

        // Buffers
        /// Receive buffer size, in bytes, or std::nullopt to let the driver choose. An explicit value of 0 or
        /// less is rejected with `errc::invalid_arg` when the client is created.
        std::optional<int> buffer_size = std::nullopt;
        /// Transmit buffer size, in bytes, or std::nullopt to let the driver choose. An explicit value of 0 or
        /// less is rejected with `errc::invalid_arg` when the client is created.
        std::optional<int> buffer_size_tx = std::nullopt;

        // Keep-alive
        bool keep_alive_enable = false; ///< Enable TCP keep-alive
        /// Keep-alive idle time before the first probe, or std::nullopt to let the driver choose. An explicit
        /// value of 0 or less is rejected with `errc::invalid_arg` when the client is created.
        std::optional<std::chrono::seconds> keep_alive_idle = std::nullopt;
        /// Interval between keep-alive probes, or std::nullopt to let the driver choose. An explicit value of 0 or
        /// less is rejected with `errc::invalid_arg` when the client is created.
        std::optional<std::chrono::seconds> keep_alive_interval = std::nullopt;
        /// Number of unanswered keep-alive probes before the connection is dropped, or std::nullopt to let the
        /// driver choose. An explicit value of 0 or less is rejected with `errc::invalid_arg` when the client is
        /// created.
        std::optional<int> keep_alive_count = std::nullopt;

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
     * If the response body ends early — the timeout expires or the server closes
     * the connection before the full Content-Length or the final chunk has been
     * received — the request fails with `client::errc::incomplete_data`. The two
     * causes are not distinguished. Before ESP-IDF 5.5.2 the request instead
     * succeeds with the truncated body.
     *
     * If a chunked request was abandoned by discarding its chunk_writer before
     * finish(), the connection is closed first and a warning is logged.
     *
     * @code
     * idfxx::http::client c({.url = "https://example.com"});
     * c.perform();
     * auto status = c.status_code();
     * @endcode
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure, including `client::errc::incomplete_data`
     *         when the response body ends early (ESP-IDF 5.5.2+).
     */
    void perform() { unwrap(try_perform()); }
#endif

    /**
     * @brief Performs a blocking HTTP request.
     *
     * Sends the request and reads the complete response. Use event callbacks
     * or the streaming API to process response data.
     *
     * If the response body ends early — the timeout expires or the server closes
     * the connection before the full Content-Length or the final chunk has been
     * received — the request fails with `client::errc::incomplete_data`. The two
     * causes are not distinguished. Before ESP-IDF 5.5.2 the request instead
     * succeeds with the truncated body.
     *
     * If a chunked request was abandoned by discarding its chunk_writer before
     * try_finish(), the connection is closed first and a warning is logged.
     *
     * @return Success, or an error.
     * @retval client::errc::connect Connecting to the server failed.
     * @retval client::errc::eagain The timeout expired before the response
     *         headers were received.
     * @retval client::errc::fetch_header Reading the response headers failed,
     *         including the server closing the connection before sending them.
     * @retval client::errc::connection_closed The connection was found closed
     *         while reading the response headers.
     * @retval client::errc::incomplete_data The response body ended early (timeout
     *         or connection closed). ESP-IDF 5.5.2+.
     */
    [[nodiscard]] result<void> try_perform();

    // =========================================================================
    // Streaming API
    // =========================================================================

    /**
     * @headerfile <idfxx/http/client>
     * @brief Writes a request body using chunked transfer encoding.
     *
     * Obtained from client::open_chunked(), which opens the connection with a
     * `Transfer-Encoding: chunked` request header. Each write() sends its data
     * as exactly one chunk, so the body can be streamed without knowing its
     * total length in advance. finish() sends the terminating zero-length
     * chunk, completing the request body; call it before
     * client::fetch_headers().
     *
     * The writer refers to the client that created it. The client must outlive
     * the writer and must not be moved while the writer is in use.
     *
     * If sending fails, the request body cannot be completed: the writer sends
     * nothing further, and later write() and finish() calls fail with
     * idfxx::errc::invalid_state.
     *
     * Destroying a writer before finish() has completed, including after a
     * failure, abandons the request. Nothing is sent, so the request body is
     * left unterminated on the connection. A warning is logged and the client
     * refuses to continue that request: client::fetch_headers(), client::read(),
     * client::flush_response() and client::write() fail with
     * idfxx::errc::invalid_state. The next request started with client::open(),
     * client::open_chunked() or client::perform() closes the connection first
     * (logging a warning) and runs on a fresh connection. client::close() or
     * client::cancel_request() also ends the abandoned request.
     *
     * This type is non-copyable and move-only. A moved-from object must not be
     * used: any operation other than destruction or assignment is undefined
     * behavior.
     *
     * @code
     * auto writer = c.open_chunked();
     * writer.write("first part");
     * writer.write("second part");
     * writer.finish();
     * c.fetch_headers();
     * @endcode
     */
    class chunk_writer {
    public:
        /**
         * @brief Destroys the writer.
         *
         * Sends nothing. If finish() has not completed, the request is
         * abandoned: its body is left unterminated, a warning is logged, and
         * the client refuses to continue the request (see the class
         * description).
         */
        ~chunk_writer();

        chunk_writer(const chunk_writer&) = delete;
        chunk_writer& operator=(const chunk_writer&) = delete;

        /** @brief Move constructor. Transfers the writer's state. */
        chunk_writer(chunk_writer&& other) noexcept;

        /**
         * @brief Move assignment. Transfers the writer's state.
         *
         * If this writer had not completed finish(), its request is abandoned
         * as for destruction.
         */
        chunk_writer& operator=(chunk_writer&& other) noexcept;

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
        /**
         * @brief Sends binary data as one chunk of the request body.
         *
         * Sends all of the data or fails. Blocks until the chunk has been sent or
         * the timeout expires. Empty data sends nothing, since a zero-length
         * chunk would end the body.
         *
         * @param data Binary data to send.
         * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
         * @throws std::system_error with client::errc::eagain if the timeout expired, or
         *         client::errc::write_data if sending failed (in either case part of the chunk
         *         may have been sent, and no further data is sent after this),
         *         idfxx::errc::invalid_state if finish() has already completed or an earlier
         *         write or finish failed, or
         *         idfxx::errc::invalid_arg if the data exceeds INT_MAX bytes.
         */
        void write(std::span<const uint8_t> data) { unwrap(try_write(data)); }

        /**
         * @brief Sends string data as one chunk of the request body.
         *
         * Sends all of the data or fails. Blocks until the chunk has been sent or
         * the timeout expires. Empty data sends nothing, since a zero-length
         * chunk would end the body.
         *
         * @param data String data to send.
         * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
         * @throws std::system_error with client::errc::eagain if the timeout expired, or
         *         client::errc::write_data if sending failed (in either case part of the chunk
         *         may have been sent, and no further data is sent after this),
         *         idfxx::errc::invalid_state if finish() has already completed or an earlier
         *         write or finish failed, or
         *         idfxx::errc::invalid_arg if the data exceeds INT_MAX bytes.
         */
        void write(std::string_view data) { unwrap(try_write(data)); }
#endif

        /**
         * @brief Sends binary data as one chunk of the request body.
         *
         * Sends all of the data or fails. Blocks until the chunk has been sent or
         * the timeout expires. Empty data sends nothing, since a zero-length
         * chunk would end the body.
         *
         * @param data Binary data to send.
         * @return Success, or an error.
         * @retval client::errc::eagain if the timeout expired before the chunk was sent. Part
         *         of the chunk may have been sent; no further data is sent after this.
         * @retval client::errc::write_data if sending failed. Part of the chunk may have been
         *         sent; no further data is sent after this.
         * @retval idfxx::errc::invalid_state if finish() has already completed or an earlier
         *         write or finish failed.
         * @retval idfxx::errc::invalid_arg if the data exceeds INT_MAX bytes.
         */
        [[nodiscard]] result<void> try_write(std::span<const uint8_t> data);

        /**
         * @brief Sends string data as one chunk of the request body.
         *
         * Sends all of the data or fails. Blocks until the chunk has been sent or
         * the timeout expires. Empty data sends nothing, since a zero-length
         * chunk would end the body.
         *
         * @param data String data to send.
         * @return Success, or an error.
         * @retval client::errc::eagain if the timeout expired before the chunk was sent. Part
         *         of the chunk may have been sent; no further data is sent after this.
         * @retval client::errc::write_data if sending failed. Part of the chunk may have been
         *         sent; no further data is sent after this.
         * @retval idfxx::errc::invalid_state if finish() has already completed or an earlier
         *         write or finish failed.
         * @retval idfxx::errc::invalid_arg if the data exceeds INT_MAX bytes.
         */
        [[nodiscard]] result<void> try_write(std::string_view data);

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
        /**
         * @brief Completes the request body.
         *
         * Sends the terminating zero-length chunk, blocking until it has been
         * sent or the timeout expires. Call this before client::fetch_headers().
         * No further data can be written afterwards.
         *
         * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
         * @throws std::system_error with client::errc::eagain if the timeout expired, or
         *         client::errc::write_data if sending failed (in either case no further data is
         *         sent after this), or
         *         idfxx::errc::invalid_state if finish() has already completed or an earlier
         *         write or finish failed.
         */
        void finish() { unwrap(try_finish()); }
#endif

        /**
         * @brief Completes the request body.
         *
         * Sends the terminating zero-length chunk, blocking until it has been
         * sent or the timeout expires. Call this before client::fetch_headers().
         * No further data can be written afterwards.
         *
         * @return Success, or an error.
         * @retval client::errc::eagain if the timeout expired before the terminating chunk was
         *         sent. No further data is sent after this.
         * @retval client::errc::write_data if sending the terminating chunk failed. No
         *         further data is sent after this.
         * @retval idfxx::errc::invalid_state if finish() has already completed or an earlier
         *         write or finish failed.
         */
        [[nodiscard]] result<void> try_finish();

    private:
        friend class client;

        enum class stage : uint8_t {
            open,     ///< Accepting chunks
            failed,   ///< A send failed; the body cannot be completed
            finished, ///< The terminating chunk has been sent
        };

        explicit chunk_writer(client& c) noexcept
            : _client(&c) {}

        [[nodiscard]] result<void> _send(const char* data, size_t len);
        void _abandon_if_unfinished() noexcept;

        client* _client;
        stage _stage = stage::open;
    };

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Opens a connection for streaming.
     *
     * Opens the HTTP connection and sends request headers. If write_len is
     * greater than zero, the connection is opened for writing that many bytes
     * before reading the response.
     *
     * If a chunked request was abandoned by discarding its chunk_writer before
     * finish(), the connection is closed first and a warning is logged.
     *
     * @param write_len Number of bytes to write (0 for read-only requests).
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error if write_len exceeds INT_MAX or the connection fails.
     */
    void open(size_t write_len = 0) { unwrap(try_open(write_len)); }

    /// @overload
    void open(int write_len) { open(static_cast<size_t>(write_len)); }

    /**
     * @brief Opens a connection for streaming a chunked request body.
     *
     * Opens the HTTP connection and sends request headers with
     * `Transfer-Encoding: chunked` in place of a Content-Length, so that a
     * body of unknown length can be sent. Send the body through the returned
     * chunk_writer and call chunk_writer::finish() before fetch_headers();
     * fetching headers while the writer is still in use and unfinished is a
     * usage error. Discarding the writer unfinished abandons the request (see
     * chunk_writer).
     *
     * If an earlier chunked request was abandoned, the connection is closed
     * first and a warning is logged.
     *
     * @code
     * idfxx::http::client c({.url = "http://example.com/upload", .method = idfxx::http::method::post});
     * auto writer = c.open_chunked();
     * writer.write("first part");
     * writer.write("second part");
     * writer.finish();
     * c.fetch_headers();
     * auto status = c.status_code();
     * c.close();
     * @endcode
     *
     * @return A writer for the request body. The client must outlive it.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error if the connection fails.
     */
    [[nodiscard]] chunk_writer open_chunked() { return unwrap(try_open_chunked()); }

    /**
     * @brief Writes data to the open connection.
     *
     * Blocks until all of @p data has been written or the timeout expires.
     * When the write fails, including on a timeout, an unspecified prefix of
     * @p data may already have been sent.
     *
     * @param data Binary data to write.
     * @return Number of bytes written.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure, including `client::errc::eagain` when
     *         the timeout expires, `client::errc::write_data` when the write fails, and
     *         `idfxx::errc::invalid_state` when a chunked request was abandoned.
     */
    [[nodiscard]] size_t write(std::span<const uint8_t> data) { return unwrap(try_write(data)); }

    /**
     * @brief Writes string data to the open connection.
     *
     * Blocks until all of @p data has been written or the timeout expires.
     * When the write fails, including on a timeout, an unspecified prefix of
     * @p data may already have been sent.
     *
     * @param data String data to write.
     * @return Number of bytes written.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure, including `client::errc::eagain` when
     *         the timeout expires, `client::errc::write_data` when the write fails, and
     *         `idfxx::errc::invalid_state` when a chunked request was abandoned.
     */
    [[nodiscard]] size_t write(std::string_view data) { return unwrap(try_write(data)); }

    /**
     * @brief Reads and processes response headers.
     *
     * Must be called after open() (and any write() calls) before reading
     * response body data. Blocks until the response headers have been received
     * or the timeout expires. After open_chunked(), finish the request body
     * with chunk_writer::finish() first.
     *
     * @return The response's Content-Length when it is greater than zero, or 0
     *         when the length is zero or not known in advance (a chunked response
     *         or one without a Content-Length header); content_length() returns
     *         -1 for the latter.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure, including `client::errc::eagain` when
     *         the timeout expires before the headers are received and
     *         `client::errc::fetch_header` when reading the headers fails, and
     *         `idfxx::errc::invalid_state` when a chunked request was abandoned.
     */
    int64_t fetch_headers() { return unwrap(try_fetch_headers()); }

    /**
     * @brief Reads response body data.
     *
     * Blocks until @p buf is full, the response body is complete, the
     * connection closes, or the timeout expires; data received before a close
     * or timeout is returned first.
     *
     * @param buf Buffer to read into.
     * @return Number of bytes read, or 0 at end of response.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure, including `client::errc::eagain` when
     *         the timeout expires before any data is read and
     *         `client::errc::connection_closed` when the connection closes or fails
     *         before the response body is complete and no data was read, and
     *         `idfxx::errc::invalid_state` when a chunked request was abandoned.
     */
    [[nodiscard]] size_t read(std::span<uint8_t> buf) { return unwrap(try_read(buf)); }

    /**
     * @brief Reads response body data into a character buffer.
     *
     * Blocks until @p buf is full, the response body is complete, the
     * connection closes, or the timeout expires; data received before a close
     * or timeout is returned first.
     *
     * @param buf Buffer to read into.
     * @return Number of bytes read, or 0 at end of response.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure, including `client::errc::eagain` when
     *         the timeout expires before any data is read and
     *         `client::errc::connection_closed` when the connection closes or fails
     *         before the response body is complete and no data was read, and
     *         `idfxx::errc::invalid_state` when a chunked request was abandoned.
     */
    [[nodiscard]] size_t read(std::span<char> buf) { return unwrap(try_read(buf)); }

    /**
     * @brief Discards any remaining response data.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure, including `idfxx::errc::invalid_state`
     *         when a chunked request was abandoned.
     */
    void flush_response() { unwrap(try_flush_response()); }

    /**
     * @brief Closes the connection.
     *
     * This also ends a chunked request that was abandoned by discarding its
     * chunk_writer before finish().
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
     * If a chunked request was abandoned by discarding its chunk_writer before
     * try_finish(), the connection is closed first and a warning is logged.
     *
     * @param write_len Number of bytes to write (0 for read-only requests).
     * @return Success, or an error.
     * @retval idfxx::errc::invalid_arg if write_len exceeds INT_MAX.
     */
    [[nodiscard]] result<void> try_open(size_t write_len = 0);

    /// @overload
    [[nodiscard]] result<void> try_open(int write_len) { return try_open(static_cast<size_t>(write_len)); }

    /**
     * @brief Opens a connection for streaming a chunked request body.
     *
     * Opens the HTTP connection and sends request headers with
     * `Transfer-Encoding: chunked` in place of a Content-Length, so that a
     * body of unknown length can be sent. Send the body through the returned
     * chunk_writer and call chunk_writer::try_finish() before
     * try_fetch_headers(); fetching headers while the writer is still in use
     * and unfinished is a usage error. Discarding the writer unfinished
     * abandons the request (see chunk_writer).
     *
     * If an earlier chunked request was abandoned, the connection is closed
     * first and a warning is logged.
     *
     * @return A writer for the request body, or an error. The client must
     *         outlive the writer.
     */
    [[nodiscard]] result<chunk_writer> try_open_chunked();

    /**
     * @brief Writes data to the open connection.
     *
     * Blocks until all of @p data has been written or the timeout expires.
     * When the write fails, including on a timeout, an unspecified prefix of
     * @p data may already have been sent.
     *
     * @param data Binary data to write.
     * @return Number of bytes written, or an error.
     * @retval client::errc::eagain The timeout expired while waiting to write.
     * @retval client::errc::write_data The write failed.
     * @retval idfxx::errc::invalid_state A chunked request was abandoned by
     *         discarding its chunk_writer before try_finish().
     */
    [[nodiscard]] result<size_t> try_write(std::span<const uint8_t> data);

    /**
     * @brief Writes string data to the open connection.
     *
     * Blocks until all of @p data has been written or the timeout expires.
     * When the write fails, including on a timeout, an unspecified prefix of
     * @p data may already have been sent.
     *
     * @param data String data to write.
     * @return Number of bytes written, or an error.
     * @retval client::errc::eagain The timeout expired while waiting to write.
     * @retval client::errc::write_data The write failed.
     * @retval idfxx::errc::invalid_state A chunked request was abandoned by
     *         discarding its chunk_writer before try_finish().
     */
    [[nodiscard]] result<size_t> try_write(std::string_view data);

    /**
     * @brief Reads and processes response headers.
     *
     * Must be called after open() (and any write() calls) before reading
     * response body data. Blocks until the response headers have been received
     * or the timeout expires. After try_open_chunked(), finish the request
     * body with chunk_writer::try_finish() first.
     *
     * @return The response's Content-Length when it is greater than zero, 0 when
     *         the length is zero or not known in advance (a chunked response or
     *         one without a Content-Length header; content_length() returns -1
     *         for the latter), or an error.
     * @retval client::errc::eagain The timeout expired before the headers were
     *         received.
     * @retval client::errc::fetch_header Reading the headers failed, or the
     *         connection closed before they were received.
     * @retval idfxx::errc::invalid_state A chunked request was abandoned by
     *         discarding its chunk_writer before try_finish().
     */
    // [[nodiscard]] intentionally omitted: callers may fetch headers for their
    // side effects without needing the returned content-length value.
    result<int64_t> try_fetch_headers();

    /**
     * @brief Reads response body data.
     *
     * Blocks until @p buf is full, the response body is complete, the
     * connection closes, or the timeout expires; data received before a close
     * or timeout is returned first.
     *
     * @param buf Buffer to read into.
     * @return Number of bytes read (0 at end of response), or an error.
     * @retval client::errc::eagain The timeout expired before any data was read.
     *         Nothing was consumed; the read may be retried.
     * @retval client::errc::connection_closed The connection closed or failed
     *         before the response body was complete, and no data was read.
     * @retval idfxx::errc::invalid_state A chunked request was abandoned by
     *         discarding its chunk_writer before try_finish().
     */
    [[nodiscard]] result<size_t> try_read(std::span<uint8_t> buf);

    /**
     * @brief Reads response body data into a character buffer.
     *
     * Blocks until @p buf is full, the response body is complete, the
     * connection closes, or the timeout expires; data received before a close
     * or timeout is returned first.
     *
     * @param buf Buffer to read into.
     * @return Number of bytes read (0 at end of response), or an error.
     * @retval client::errc::eagain The timeout expired before any data was read.
     *         Nothing was consumed; the read may be retried.
     * @retval client::errc::connection_closed The connection closed or failed
     *         before the response body was complete, and no data was read.
     * @retval idfxx::errc::invalid_state A chunked request was abandoned by
     *         discarding its chunk_writer before try_finish().
     */
    [[nodiscard]] result<size_t> try_read(std::span<char> buf);

    /**
     * @brief Discards any remaining response data.
     *
     * @return Success, or an error.
     * @retval idfxx::errc::invalid_state A chunked request was abandoned by
     *         discarding its chunk_writer before try_finish().
     */
    [[nodiscard]] result<void> try_flush_response();

    /**
     * @brief Closes the connection.
     *
     * This also ends a chunked request that was abandoned by discarding its
     * chunk_writer before try_finish().
     *
     * @return Success, or an error.
     */
    result<void> try_close();

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

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Returns the length of the current chunk in a chunked response.
     *
     * Only valid when the response uses chunked transfer encoding.
     *
     * @return The chunk length in bytes.
     * @throws std::system_error if the response is not chunked or the client is not initialized.
     * @note Only available when `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled.
     *
     * @code
     * if (client.is_chunked_response()) {
     *     int len = client.chunk_length();
     * }
     * @endcode
     */
    [[nodiscard]] int chunk_length() const { return unwrap(try_chunk_length()); }
#endif

    /**
     * @brief Returns the length of the current chunk in a chunked response.
     *
     * Only valid when the response uses chunked transfer encoding.
     *
     * @return The chunk length in bytes.
     * @retval errc::invalid_state if the client is not initialized.
     * @retval errc::not_supported if the response is not chunked.
     */
    [[nodiscard]] result<int> try_chunk_length() const;

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

    // =========================================================================
    // Request Control
    // =========================================================================

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Cancels an ongoing HTTP request.
     *
     * Closes the current connection and opens a new socket.
     * Useful for aborting long-running or streaming requests.
     *
     * This also ends a chunked request that was abandoned by discarding its
     * chunk_writer before finish().
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    void cancel_request() { unwrap(try_cancel_request()); }
#endif

    /**
     * @brief Cancels an ongoing HTTP request.
     *
     * Closes the current connection and opens a new socket.
     * Useful for aborting long-running or streaming requests.
     *
     * This also ends a chunked request that was abandoned by discarding its
     * chunk_writer before try_finish().
     *
     * @return Success, or an error.
     */
    [[nodiscard]] result<void> try_cancel_request();

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
    /**
     * @headerfile <idfxx/http/client>
     * @brief HTTP client lifecycle state.
     *
     * Covers the full lifecycle: initialization, connection establishment,
     * request transmission, and response reception.
     */
    enum class state : int {
        // clang-format off
        uninit              = 0, ///< Client not yet initialized.
        init                = 1, ///< Client initialized.
        connecting          = 2, ///< Connecting to server.
        connected           = 3, ///< Connected to server.
        req_complete_header = 4, ///< Request headers sent.
        req_complete_data   = 5, ///< Request data sent.
        res_complete_header = 6, ///< Response headers received.
        res_on_data_start   = 7, ///< Response data started.
        res_complete_data   = 8, ///< Response data completed.
        close               = 9, ///< Connection closed.
        // clang-format on
    };

    /**
     * @brief Returns the current lifecycle state of the client.
     *
     * Reports where the client is in the connect/request/response lifecycle.
     *
     * @return The client state.
     */
    [[nodiscard]] enum state get_state() const;
#endif

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
    void _discard_incomplete_request();

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
    /// A chunk_writer was discarded before finishing, leaving the request body unterminated
    bool _request_incomplete = false;
};

/** @cond INTERNAL */
static_assert(idfxx::byte_source<idfxx::http::client>);
/** @endcond */

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

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
/**
 * @headerfile <idfxx/http/client>
 * @brief Returns a string representation of an HTTP client lifecycle state.
 *
 * @param s The client state to convert.
 * @return "UNINIT", "INIT", "CONNECTING", etc., or "unknown(N)" for unrecognized values.
 */
[[nodiscard]] std::string to_string(http::client::state s);
#endif

} // namespace idfxx

/** @cond INTERNAL */
namespace std {
template<>
struct is_error_code_enum<idfxx::http::client::errc> : true_type {};
} // namespace std
/** @endcond */

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

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
template<>
struct formatter<idfxx::http::client::state> {
    constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }

    template<typename FormatContext>
    auto format(idfxx::http::client::state s, FormatContext& ctx) const {
        auto str = idfxx::to_string(s);
        return std::copy(str.begin(), str.end(), ctx.out());
    }
};
#endif

} // namespace std
/** @endcond */
#endif // CONFIG_IDFXX_STD_FORMAT

/** @} */ // end of idfxx_http_client
