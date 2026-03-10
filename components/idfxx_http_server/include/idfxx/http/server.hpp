// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

/**
 * @headerfile <idfxx/http/server>
 * @file server.hpp
 * @brief HTTP server class.
 *
 * @defgroup idfxx_http_server HTTP Server Component
 * @brief Type-safe HTTP server for ESP32.
 *
 * Provides URI handler registration with C++ callable handlers, RAII lifecycle
 * management, request/response handling, and optional WebSocket support.
 *
 * Depends on @ref idfxx_core for error handling.
 * @{
 */

#include <idfxx/cpu>
#include <idfxx/error>
#include <idfxx/http/types>

#include <chrono>
#include <cstdint>
#include <functional>
#include <list>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

typedef void* httpd_handle_t;
struct httpd_config;
struct httpd_req;
typedef struct httpd_req httpd_req_t;
typedef int esp_err_t;

namespace idfxx::http {

class server;

/**
 * @headerfile <idfxx/http/server>
 * @brief Non-owning view of an HTTP request.
 *
 * Constructed on the stack within the C-to-C++ trampoline and passed by
 * reference to handlers. Provides methods for reading request data
 * (headers, query parameters, body) and sending responses.
 *
 * This class must not outlive the handler invocation. It is non-copyable
 * and non-movable to prevent accidental use-after-callback.
 */
class request {
public:
    request(const request&) = delete;
    request& operator=(const request&) = delete;
    request(request&&) = delete;
    request& operator=(request&&) = delete;

    // =========================================================================
    // Request Properties
    // =========================================================================

    /**
     * @brief Returns the HTTP method of this request.
     * @return The request method.
     */
    [[nodiscard]] enum method method() const;

    /**
     * @brief Returns the URI of this request.
     * @return The request URI string.
     */
    [[nodiscard]] std::string_view uri() const;

    /**
     * @brief Returns the content length of the request body.
     * @return The content length in bytes.
     */
    [[nodiscard]] size_t content_length() const;

    /**
     * @brief Returns the socket file descriptor for this request's connection.
     * @return The socket file descriptor.
     */
    [[nodiscard]] int socket_fd() const;

    // =========================================================================
    // Headers
    // =========================================================================

    /**
     * @brief Retrieves a request header value by name.
     *
     * @param field Header name to look up.
     * @return The header value, or std::nullopt if the header is not present.
     */
    [[nodiscard]] std::optional<std::string> header(std::string_view field) const;

    // =========================================================================
    // Query String
    // =========================================================================

    /**
     * @brief Returns the full query string from the request URI.
     * @return The query string, or std::nullopt if no query string is present.
     */
    [[nodiscard]] std::optional<std::string> query_string() const;

    /**
     * @brief Retrieves a single query parameter value by key.
     *
     * @param key The query parameter name.
     * @return The parameter value, or std::nullopt if the key is not present.
     */
    [[nodiscard]] std::optional<std::string> query_param(std::string_view key) const;

    // =========================================================================
    // Request Body
    // =========================================================================

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Receives raw request body data into a buffer.
     *
     * @param buf Buffer to receive data into.
     * @return Number of bytes received.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    [[nodiscard]] size_t recv(std::span<uint8_t> buf) { return unwrap(try_recv(buf)); }

    /**
     * @brief Receives raw request body data into a character buffer.
     *
     * @param buf Buffer to receive data into.
     * @return Number of bytes received.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    [[nodiscard]] size_t recv(std::span<char> buf) { return unwrap(try_recv(buf)); }

    /**
     * @brief Receives the entire request body as a string.
     *
     * Reads all remaining body data up to content_length() bytes.
     *
     * @return The request body string.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    [[nodiscard]] std::string recv_body() { return unwrap(try_recv_body()); }
#endif

    /**
     * @brief Receives raw request body data into a buffer.
     *
     * @param buf Buffer to receive data into.
     * @return Number of bytes received, or an error.
     */
    [[nodiscard]] result<size_t> try_recv(std::span<uint8_t> buf);

    /**
     * @brief Receives raw request body data into a character buffer.
     *
     * @param buf Buffer to receive data into.
     * @return Number of bytes received, or an error.
     */
    [[nodiscard]] result<size_t> try_recv(std::span<char> buf);

    /**
     * @brief Receives the entire request body as a string.
     *
     * Reads all remaining body data up to content_length() bytes.
     *
     * @return The request body string, or an error.
     */
    [[nodiscard]] result<std::string> try_recv_body();

    // =========================================================================
    // Response Setup
    // =========================================================================

    /**
     * @brief Sets the HTTP response status line.
     *
     * The string must remain in the format "NNN Reason Phrase" (e.g. "200 OK").
     * The string is copied internally and kept alive until the response is sent.
     *
     * @param status The status string (e.g. "200 OK").
     */
    void set_status(std::string_view status);

    /**
     * @brief Sets the HTTP response status code.
     *
     * Maps common status codes to their standard reason phrases. Falls back
     * to just the numeric code for unrecognized values.
     *
     * @param code The HTTP status code (e.g. 200, 404).
     */
    void set_status(int code);

    /**
     * @brief Sets the Content-Type response header.
     *
     * The string is copied internally and kept alive until the response is sent.
     *
     * @param content_type The MIME type string (e.g. "application/json").
     */
    void set_content_type(std::string_view content_type);

    /**
     * @brief Adds a response header.
     *
     * Both field and value are copied internally and kept alive until the
     * response is sent.
     *
     * @param field Header name.
     * @param value Header value.
     */
    void set_header(std::string_view field, std::string_view value);

    // =========================================================================
    // Response Sending
    // =========================================================================

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Sends a complete response with string body.
     *
     * @code
     * server.on_get("/hello", [](idfxx::http::request& req) -> idfxx::result<void> {
     *     req.set_content_type("text/plain");
     *     req.send("Hello, World!");
     *     return {};
     * });
     * @endcode
     *
     * @param body The response body string.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    void send(std::string_view body) { unwrap(try_send(body)); }

    /**
     * @brief Sends a complete response with binary body.
     *
     * @param body The response body data.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    void send(std::span<const uint8_t> body) { unwrap(try_send(body)); }

    /**
     * @brief Sends a chunk of a chunked response.
     *
     * Call repeatedly to stream data to the client. Call end_chunked()
     * to finalize the chunked response.
     *
     * @param chunk The chunk data to send.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    void send_chunk(std::string_view chunk) { unwrap(try_send_chunk(chunk)); }

    /**
     * @brief Finalizes a chunked response.
     *
     * Sends the terminal empty chunk, signalling end of response.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    void end_chunked() { unwrap(try_end_chunked()); }

    /**
     * @brief Sends an HTTP error response.
     *
     * @param code The HTTP error status code (e.g. 404, 500).
     * @param message Optional error message body.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    void send_error(int code, std::string_view message = {}) { unwrap(try_send_error(code, message)); }
#endif

    /**
     * @brief Sends a complete response with string body.
     *
     * @param body The response body string.
     * @return Success, or an error.
     */
    [[nodiscard]] result<void> try_send(std::string_view body);

    /**
     * @brief Sends a complete response with binary body.
     *
     * @param body The response body data.
     * @return Success, or an error.
     */
    [[nodiscard]] result<void> try_send(std::span<const uint8_t> body);

    /**
     * @brief Sends a chunk of a chunked response.
     *
     * Call repeatedly to stream data to the client. Call try_end_chunked()
     * to finalize the chunked response.
     *
     * @param chunk The chunk data to send.
     * @return Success, or an error.
     */
    [[nodiscard]] result<void> try_send_chunk(std::string_view chunk);

    /**
     * @brief Finalizes a chunked response.
     *
     * Sends the terminal empty chunk, signalling end of response.
     *
     * @return Success, or an error.
     */
    [[nodiscard]] result<void> try_end_chunked();

    /**
     * @brief Sends an HTTP error response.
     *
     * @param code The HTTP error status code (e.g. 404, 500).
     * @param message Optional error message body.
     * @return Success, or an error.
     */
    [[nodiscard]] result<void> try_send_error(int code, std::string_view message = {});

    // =========================================================================
    // WebSocket
    // =========================================================================

#ifdef CONFIG_HTTPD_WS_SUPPORT
    /**
     * @headerfile <idfxx/http/server>
     * @brief WebSocket frame types.
     */
    enum class ws_frame_type : int {
        // clang-format off
        continuation = 0x0, ///< Continuation frame
        text         = 0x1, ///< Text frame
        binary       = 0x2, ///< Binary frame
        close        = 0x8, ///< Close frame
        ping         = 0x9, ///< Ping frame
        pong         = 0xA, ///< Pong frame
        // clang-format on
    };

    /**
     * @headerfile <idfxx/http/server>
     * @brief WebSocket frame data.
     */
    struct ws_frame {
        ws_frame_type type;            ///< Frame type
        bool final = true;             ///< Whether this is the final fragment
        std::span<const uint8_t> data; ///< Frame payload data
    };

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Receives a WebSocket frame.
     *
     * Reads the next WebSocket frame from the connection into the provided buffer.
     *
     * @param buf Buffer to receive the frame payload.
     * @return The received frame with type and payload span referencing buf.
     * @note Only available when CONFIG_HTTPD_WS_SUPPORT and CONFIG_COMPILER_CXX_EXCEPTIONS are enabled.
     * @throws std::system_error on failure.
     */
    [[nodiscard]] ws_frame ws_recv(std::span<uint8_t> buf) { return unwrap(try_ws_recv(buf)); }

    /**
     * @brief Sends a WebSocket text frame.
     *
     * @param text The text data to send.
     * @note Only available when CONFIG_HTTPD_WS_SUPPORT and CONFIG_COMPILER_CXX_EXCEPTIONS are enabled.
     * @throws std::system_error on failure.
     */
    void ws_send_text(std::string_view text) { unwrap(try_ws_send_text(text)); }

    /**
     * @brief Sends a WebSocket binary frame.
     *
     * @param data The binary data to send.
     * @note Only available when CONFIG_HTTPD_WS_SUPPORT and CONFIG_COMPILER_CXX_EXCEPTIONS are enabled.
     * @throws std::system_error on failure.
     */
    void ws_send_binary(std::span<const uint8_t> data) { unwrap(try_ws_send_binary(data)); }

    /**
     * @brief Sends a WebSocket frame with the given type.
     *
     * @param frame The frame to send.
     * @note Only available when CONFIG_HTTPD_WS_SUPPORT and CONFIG_COMPILER_CXX_EXCEPTIONS are enabled.
     * @throws std::system_error on failure.
     */
    void ws_send(const ws_frame& frame) { unwrap(try_ws_send(frame)); }
#endif

    /**
     * @brief Receives a WebSocket frame.
     *
     * Reads the next WebSocket frame from the connection into the provided buffer.
     *
     * @param buf Buffer to receive the frame payload.
     * @return The received frame with type and payload span referencing buf, or an error.
     */
    [[nodiscard]] result<ws_frame> try_ws_recv(std::span<uint8_t> buf);

    /**
     * @brief Sends a WebSocket text frame.
     *
     * @param text The text data to send.
     * @return Success, or an error.
     */
    [[nodiscard]] result<void> try_ws_send_text(std::string_view text);

    /**
     * @brief Sends a WebSocket binary frame.
     *
     * @param data The binary data to send.
     * @return Success, or an error.
     */
    [[nodiscard]] result<void> try_ws_send_binary(std::span<const uint8_t> data);

    /**
     * @brief Sends a WebSocket frame with the given type.
     *
     * @param frame The frame to send.
     * @return Success, or an error.
     */
    [[nodiscard]] result<void> try_ws_send(const ws_frame& frame);
#endif // CONFIG_HTTPD_WS_SUPPORT

    // =========================================================================
    // Escape Hatch
    // =========================================================================

    /**
     * @brief Returns the underlying ESP-IDF request handle.
     * @return The httpd_req_t pointer.
     */
    [[nodiscard]] httpd_req_t* idf_handle() const noexcept { return _req; }

private:
    friend class server;
    explicit request(httpd_req_t* req)
        : _req(req) {}

    httpd_req_t* _req;
    std::string _status_str;
    std::string _content_type;
    std::vector<std::pair<std::string, std::string>> _resp_headers;
    mutable bool _query_fetched = false;
    mutable std::optional<std::string> _query_cache;
};

/**
 * @headerfile <idfxx/http/server>
 * @brief HTTP server with URI handler registration and RAII lifecycle.
 *
 * Manages an HTTP server instance. Register URI handlers as C++ callables,
 * which are invoked when matching requests arrive. Supports standard HTTP
 * methods and optional WebSocket endpoints.
 *
 * Not thread-safe for handler registration — register all handlers before
 * serving concurrent requests.
 */
class server {
public:
    /** @brief Handler function type for URI handlers. */
    using handler_type = std::move_only_function<result<void>(request&) const>;

    /**
     * @brief Error codes for HTTP server operations.
     */
    enum class errc : esp_err_t {
        // clang-format off
        handlers_full    = 0xb001, ///< All URI handler slots are full
        handler_exists   = 0xb002, ///< URI handler already registered for this method and URI
        invalid_request  = 0xb003, ///< Invalid request pointer
        result_truncated = 0xb004, ///< Result string was truncated
        resp_header      = 0xb005, ///< Response header field too large
        resp_send        = 0xb006, ///< Error sending response
        alloc_mem        = 0xb007, ///< Memory allocation failed
        task             = 0xb008, ///< Failed to create server task
        // clang-format on
    };

    /**
     * @brief Error category for HTTP server errors.
     */
    class error_category : public std::error_category {
    public:
        /** @brief Returns the name of the error category. */
        [[nodiscard]] const char* name() const noexcept override final;

        /** @brief Returns a human-readable message for the given error code. */
        [[nodiscard]] std::string message(int ec) const override final;
    };

    /**
     * @brief HTTP server configuration.
     *
     * @note If adding or modifying fields, mirror the changes in ssl_server::config.
     *       The fields are intentionally duplicated (not inherited) to preserve
     *       C++20 designated initializer support.
     */
    struct config {
        // Task
        task_priority priority = 5;                          ///< Priority of the server task
        size_t stack_size = 4096;                            ///< Stack size for the server task
        std::optional<core_id> core_affinity = std::nullopt; ///< Core pin (nullopt = any core)

        // Network
        uint16_t server_port = 80;  ///< HTTP listening port
        uint16_t ctrl_port = 32768; ///< Control port for server commands
        uint16_t backlog_conn = 5;  ///< Maximum backlog connections

        // Limits
        uint16_t max_open_sockets = 7; ///< Maximum concurrent client connections
        uint16_t max_uri_handlers = 8; ///< Maximum registered URI handlers
        uint16_t max_resp_headers = 8; ///< Maximum additional response headers
        size_t max_req_hdr_len = 0;    ///< Maximum request header length (0 = Kconfig default)
        size_t max_uri_len = 0;        ///< Maximum URI length (0 = Kconfig default)

        // Timeouts
        std::chrono::seconds recv_wait_timeout{5}; ///< Receive timeout
        std::chrono::seconds send_wait_timeout{5}; ///< Send timeout

        // LRU
        bool lru_purge_enable = false; ///< Purge least-recently-used connections when full

        // SO_LINGER
        bool enable_so_linger = false;          ///< Enable SO_LINGER on sockets
        std::chrono::seconds linger_timeout{0}; ///< Linger timeout

        // Keep-alive
        bool keep_alive_enable = false;              ///< Enable TCP keep-alive
        std::chrono::seconds keep_alive_idle{0};     ///< Keep-alive idle time
        std::chrono::seconds keep_alive_interval{0}; ///< Keep-alive probe interval
        int keep_alive_count = 0;                    ///< Keep-alive probe count

        // URI matching
        bool wildcard_uri_match = false; ///< Enable wildcard URI matching

        // Session callbacks
        /** @brief Called when a new session is opened. Return an error to reject. */
        std::move_only_function<result<void>(int) const> on_session_open = {};
        /** @brief Called when a session is closed. */
        std::move_only_function<void(int) const> on_session_close = {};
    };

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Creates and starts an HTTP server.
     *
     * @param cfg Server configuration.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    [[nodiscard]] explicit server(config cfg);
#endif

    /**
     * @brief Creates and starts an HTTP server.
     *
     * @param cfg Server configuration.
     * @return The new server, or an error.
     */
    [[nodiscard]] static result<server> make(config cfg);

    /**
     * @brief Stops the server and releases all resources.
     */
    virtual ~server();

    server(const server&) = delete;
    server& operator=(const server&) = delete;

    /** @brief Move constructor. Transfers server ownership. */
    server(server&&) noexcept;

    /** @brief Move assignment. Transfers server ownership. */
    server& operator=(server&&) noexcept;

    // =========================================================================
    // Handler Registration
    // =========================================================================

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Registers a URI handler for a specific HTTP method.
     *
     * @code
     * idfxx::http::server srv({});
     * srv.on(idfxx::http::method::get, "/api/status", [](idfxx::http::request& req) -> idfxx::result<void> {
     *     req.set_content_type("application/json");
     *     req.send(R"({"status": "ok"})");
     *     return {};
     * });
     * @endcode
     *
     * @param m The HTTP method to handle.
     * @param uri The URI pattern to match.
     * @param handler The handler function.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    void on(enum method m, std::string uri, handler_type handler) {
        unwrap(try_on(m, std::move(uri), std::move(handler)));
    }

    /**
     * @brief Registers a URI handler that matches any HTTP method.
     *
     * @param uri The URI pattern to match.
     * @param handler The handler function.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    void on_any(std::string uri, handler_type handler) { unwrap(try_on_any(std::move(uri), std::move(handler))); }

    /**
     * @brief Registers a GET handler.
     *
     * @param uri The URI pattern to match.
     * @param handler The handler function.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    void on_get(std::string uri, handler_type handler) { unwrap(try_on_get(std::move(uri), std::move(handler))); }

    /**
     * @brief Registers a POST handler.
     *
     * @param uri The URI pattern to match.
     * @param handler The handler function.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    void on_post(std::string uri, handler_type handler) { unwrap(try_on_post(std::move(uri), std::move(handler))); }

    /**
     * @brief Registers a PUT handler.
     *
     * @param uri The URI pattern to match.
     * @param handler The handler function.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    void on_put(std::string uri, handler_type handler) { unwrap(try_on_put(std::move(uri), std::move(handler))); }

    /**
     * @brief Registers a PATCH handler.
     *
     * @param uri The URI pattern to match.
     * @param handler The handler function.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    void on_patch(std::string uri, handler_type handler) { unwrap(try_on_patch(std::move(uri), std::move(handler))); }

    /**
     * @brief Registers a DELETE handler.
     *
     * @param uri The URI pattern to match.
     * @param handler The handler function.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    void on_delete(std::string uri, handler_type handler) { unwrap(try_on_delete(std::move(uri), std::move(handler))); }

    /**
     * @brief Registers a HEAD handler.
     *
     * @param uri The URI pattern to match.
     * @param handler The handler function.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    void on_head(std::string uri, handler_type handler) { unwrap(try_on_head(std::move(uri), std::move(handler))); }
#endif

    /**
     * @brief Registers a URI handler for a specific HTTP method.
     *
     * @param m The HTTP method to handle.
     * @param uri The URI pattern to match.
     * @param handler The handler function.
     * @return Success, or an error.
     */
    [[nodiscard]] result<void> try_on(enum method m, std::string uri, handler_type handler);

    /**
     * @brief Registers a URI handler that matches any HTTP method.
     *
     * @param uri The URI pattern to match.
     * @param handler The handler function.
     * @return Success, or an error.
     */
    [[nodiscard]] result<void> try_on_any(std::string uri, handler_type handler);

    /**
     * @brief Registers a GET handler.
     *
     * @param uri The URI pattern to match.
     * @param handler The handler function.
     * @return Success, or an error.
     */
    [[nodiscard]] result<void> try_on_get(std::string uri, handler_type handler) {
        return try_on(method::get, std::move(uri), std::move(handler));
    }

    /**
     * @brief Registers a POST handler.
     *
     * @param uri The URI pattern to match.
     * @param handler The handler function.
     * @return Success, or an error.
     */
    [[nodiscard]] result<void> try_on_post(std::string uri, handler_type handler) {
        return try_on(method::post, std::move(uri), std::move(handler));
    }

    /**
     * @brief Registers a PUT handler.
     *
     * @param uri The URI pattern to match.
     * @param handler The handler function.
     * @return Success, or an error.
     */
    [[nodiscard]] result<void> try_on_put(std::string uri, handler_type handler) {
        return try_on(method::put, std::move(uri), std::move(handler));
    }

    /**
     * @brief Registers a PATCH handler.
     *
     * @param uri The URI pattern to match.
     * @param handler The handler function.
     * @return Success, or an error.
     */
    [[nodiscard]] result<void> try_on_patch(std::string uri, handler_type handler) {
        return try_on(method::patch, std::move(uri), std::move(handler));
    }

    /**
     * @brief Registers a DELETE handler.
     *
     * @param uri The URI pattern to match.
     * @param handler The handler function.
     * @return Success, or an error.
     */
    [[nodiscard]] result<void> try_on_delete(std::string uri, handler_type handler) {
        return try_on(method::delete_, std::move(uri), std::move(handler));
    }

    /**
     * @brief Registers a HEAD handler.
     *
     * @param uri The URI pattern to match.
     * @param handler The handler function.
     * @return Success, or an error.
     */
    [[nodiscard]] result<void> try_on_head(std::string uri, handler_type handler) {
        return try_on(method::head, std::move(uri), std::move(handler));
    }

    // =========================================================================
    // Handler Unregistration
    // =========================================================================

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Unregisters a URI handler.
     *
     * @param m The HTTP method of the handler to remove.
     * @param uri The URI of the handler to remove.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    void unregister_handler(enum method m, std::string_view uri) { unwrap(try_unregister_handler(m, uri)); }
#endif

    /**
     * @brief Unregisters a URI handler.
     *
     * @param m The HTTP method of the handler to remove.
     * @param uri The URI of the handler to remove.
     * @return Success, or an error.
     */
    [[nodiscard]] result<void> try_unregister_handler(enum method m, std::string_view uri);

    // =========================================================================
    // WebSocket
    // =========================================================================

#ifdef CONFIG_HTTPD_WS_SUPPORT
    /**
     * @brief WebSocket endpoint configuration.
     */
    struct ws_config {
        bool handle_control_frames = false;     ///< Pass control frames (PING, PONG, CLOSE) to handler
        std::string supported_subprotocol = {}; ///< Supported WebSocket subprotocol (empty = none)
    };

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Registers a WebSocket endpoint.
     *
     * The handler is invoked for each incoming WebSocket frame after the
     * initial HTTP upgrade handshake.
     *
     * @param uri The URI for the WebSocket endpoint.
     * @param handler The handler function.
     * @param ws_cfg WebSocket configuration.
     * @note Only available when CONFIG_HTTPD_WS_SUPPORT and CONFIG_COMPILER_CXX_EXCEPTIONS are enabled.
     * @throws std::system_error on failure.
     */
    void on_ws(std::string uri, handler_type handler, ws_config ws_cfg = {}) {
        unwrap(try_on_ws(std::move(uri), std::move(handler), std::move(ws_cfg)));
    }
#endif

    /**
     * @brief Registers a WebSocket endpoint.
     *
     * The handler is invoked for each incoming WebSocket frame after the
     * initial HTTP upgrade handshake.
     *
     * @param uri The URI for the WebSocket endpoint.
     * @param handler The handler function.
     * @param ws_cfg WebSocket configuration.
     * @return Success, or an error.
     */
    [[nodiscard]] result<void> try_on_ws(std::string uri, handler_type handler, ws_config ws_cfg = {});
#endif // CONFIG_HTTPD_WS_SUPPORT

    // =========================================================================
    // Session Control
    // =========================================================================

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Closes a client session by socket file descriptor.
     *
     * @param sockfd The socket file descriptor to close.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    void close_session(int sockfd) { unwrap(try_close_session(sockfd)); }
#endif

    /**
     * @brief Closes a client session by socket file descriptor.
     *
     * @param sockfd The socket file descriptor to close.
     * @return Success, or an error.
     */
    [[nodiscard]] result<void> try_close_session(int sockfd);

    // =========================================================================
    // Escape Hatch
    // =========================================================================

    /**
     * @brief Returns the underlying ESP-IDF HTTP server handle.
     * @return The httpd_handle_t.
     */
    [[nodiscard]] httpd_handle_t idf_handle() const noexcept { return _handle; }

protected:
    /** @cond INTERNAL */
    using stop_fn_type = esp_err_t (*)(httpd_handle_t);

    struct server_ctx {
        server* self;
    };

    server(
        httpd_handle_t handle,
        std::unique_ptr<server_ctx> ctx,
        std::move_only_function<result<void>(int) const> on_session_open,
        std::move_only_function<void(int) const> on_session_close,
        stop_fn_type stop_fn
    );

    static esp_err_t _session_open_trampoline(httpd_handle_t hd, int sockfd);
    static void _session_close_trampoline(httpd_handle_t hd, int sockfd);

    template<typename Config>
    static void _populate_httpd_config(
        struct httpd_config& httpd_cfg,
        const Config& cfg,
        server_ctx* ctx,
        bool has_session_open,
        bool has_session_close
    );

private:
    struct handler_record {
        std::string uri;
        unsigned httpd_method; // httpd_method_t value (from http_parser enum)
        handler_type handler;
#ifdef CONFIG_HTTPD_WS_SUPPORT
        bool is_websocket = false;
        bool handle_ws_control_frames = false;
        std::string supported_subprotocol;
#endif
    };

    [[nodiscard]] result<void> _register_handler(handler_record& record);
    [[nodiscard]] result<void> _try_register(unsigned httpd_method, std::string uri, handler_type handler);
    [[nodiscard]] static result<httpd_handle_t>
    _start_server(const config& cfg, server_ctx* ctx, bool has_session_open, bool has_session_close);

    static esp_err_t _uri_handler_trampoline(httpd_req_t* req);

    httpd_handle_t _handle;
    stop_fn_type _stop_fn;
    std::unique_ptr<server_ctx> _ctx;
    std::list<handler_record> _handlers;
    std::move_only_function<result<void>(int) const> _on_session_open;
    std::move_only_function<void(int) const> _on_session_close;
    /** @endcond */
};

/**
 * @headerfile <idfxx/http/server>
 * @brief Returns a reference to the HTTP server error category singleton.
 *
 * @return Reference to the singleton server::error_category instance.
 */
[[nodiscard]] const server::error_category& http_server_category() noexcept;

/**
 * @headerfile <idfxx/http/server>
 * @brief Creates an error code from an idfxx::http::server::errc value.
 */
[[nodiscard]] inline std::error_code make_error_code(server::errc e) noexcept {
    return {std::to_underlying(e), http_server_category()};
}

/**
 * @headerfile <idfxx/http/server>
 * @brief Creates an unexpected error from an ESP-IDF error code, mapping to HTTP server
 *        error codes where possible.
 *
 * Converts the ESP-IDF error code to an HTTP server-specific error code if a mapping
 * exists, otherwise falls back to the default IDFXX error category.
 *
 * @param e The ESP-IDF error code.
 * @return An unexpected value suitable for returning from result-returning functions.
 */
[[nodiscard]] std::unexpected<std::error_code> http_server_error(esp_err_t e);

} // namespace idfxx::http

/** @cond INTERNAL */
namespace std {
template<>
struct is_error_code_enum<idfxx::http::server::errc> : true_type {};
} // namespace std
/** @endcond */

/** @} */ // end of idfxx_http_server
