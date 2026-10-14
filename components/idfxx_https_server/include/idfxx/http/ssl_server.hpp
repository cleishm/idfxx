// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

/**
 * @headerfile <idfxx/http/ssl_server>
 * @file ssl_server.hpp
 * @brief HTTPS server class with TLS support.
 *
 * @defgroup idfxx_https_server HTTPS Server Component
 * @brief Type-safe HTTPS server for ESP32 with TLS support.
 *
 * Provides an @ref idfxx::http::ssl_server class that extends
 * @ref idfxx::http::server with TLS encryption. All handler registration,
 * request handling, and WebSocket APIs are inherited from the base class.
 *
 * Functions accepting @c server& work with both HTTP and HTTPS servers
 * (polymorphism), while functions requiring @c ssl_server& enforce HTTPS
 * at the type level.
 *
 * Depends on @ref idfxx_core for error handling and @ref idfxx_http_server for
 * the server type.
 * @{
 */

#include "sdkconfig.h"

#include <idfxx/cpu>
#include <idfxx/error>
#include <idfxx/http/server>

#include <chrono>
#include <cstdint>
#include <esp_idf_version.h>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace idfxx::http {

/**
 * @headerfile <idfxx/http/ssl_server>
 * @brief HTTPS server with TLS support.
 *
 * Extends @ref server with TLS encryption. Inherits all handler registration,
 * request/response, WebSocket, and session management APIs.
 *
 * Functions taking @c server& accept both HTTP and HTTPS servers, while
 * functions requiring @c ssl_server& enforce that only HTTPS servers are used.
 *
 * Certificate data (PEM strings) is consumed during server creation — ESP-IDF
 * copies all certificate buffers internally, so the strings do not need to
 * outlive the call.
 */
class ssl_server : public server {
public:
    /**
     * @brief HTTPS server configuration.
     *
     * Contains all HTTP server settings (with HTTPS-appropriate defaults) plus
     * TLS certificate and key fields.
     *
     * @note The HTTP server fields are intentionally duplicated from server::config
     *       rather than inherited, to preserve C++20 designated initializer support.
     *       If adding or modifying common fields, mirror the changes in server::config.
     */
    struct config {
        // Task
        task_priority priority = 5;                          ///< Priority of the server task
        size_t stack_size = 10240;                           ///< Stack size for the server task (10KB)
        std::optional<core_id> core_affinity = std::nullopt; ///< Core pin (nullopt = any core)

        // Network
        uint16_t server_port = 443; ///< HTTPS listening port
        uint16_t ctrl_port = 32769; ///< Control port for server commands
        uint16_t backlog_conn = 5;  ///< Maximum backlog connections
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 1, 0)
        /**
         * @brief Network interface to accept connections on (nullopt = all interfaces).
         *
         * Identified by its lwIP interface name (e.g. `"st1"`, as returned by
         * `esp_netif_get_netif_impl_name()`). Equivalent to the POSIX
         * `SO_BINDTODEVICE` option on the listening socket: only connections
         * arriving through that interface are accepted. The interface must exist
         * when the server starts, otherwise starting fails. An empty name has the
         * same effect as std::nullopt. Names of `IFNAMSIZ` (6) or more characters
         * are rejected with idfxx::errc::invalid_arg.
         *
         * @note Only available from ESP-IDF 6.1.
         */
        std::optional<std::string_view> bind_to_device = std::nullopt;
#endif

        // Limits
        uint16_t max_open_sockets = 4; ///< Maximum concurrent client connections
        uint16_t max_uri_handlers = 8; ///< Maximum registered URI handlers
        uint16_t max_resp_headers = 8; ///< Maximum additional response headers
        /**
         * @brief Maximum request header length, in bytes.
         *
         * Defaults to the project's CONFIG_HTTPD_MAX_REQ_HDR_LEN setting. Zero is
         * rejected with idfxx::errc::invalid_arg.
         */
        size_t max_req_hdr_len = CONFIG_HTTPD_MAX_REQ_HDR_LEN;
        /**
         * @brief Maximum URI length, in bytes.
         *
         * Defaults to the project's CONFIG_HTTPD_MAX_URI_LEN setting. Zero is
         * rejected with idfxx::errc::invalid_arg.
         */
        size_t max_uri_len = CONFIG_HTTPD_MAX_URI_LEN;

        // Timeouts
        std::chrono::seconds recv_wait_timeout{5}; ///< Receive timeout
        std::chrono::seconds send_wait_timeout{5}; ///< Send timeout

        // LRU
        bool lru_purge_enable = true; ///< Purge least-recently-used connections when full

        // SO_LINGER
        bool enable_so_linger = false;          ///< Enable SO_LINGER on sockets
        std::chrono::seconds linger_timeout{0}; ///< Linger timeout

        // Keep-alive
        bool keep_alive_enable = false; ///< Enable TCP keep-alive
        /**
         * @brief Keep-alive idle time before the first probe, or std::nullopt to let the driver choose.
         *
         * A value of zero or less is rejected with idfxx::errc::invalid_arg.
         */
        std::optional<std::chrono::seconds> keep_alive_idle = std::nullopt;
        /**
         * @brief Keep-alive probe interval, or std::nullopt to let the driver choose.
         *
         * A value of zero or less is rejected with idfxx::errc::invalid_arg.
         */
        std::optional<std::chrono::seconds> keep_alive_interval = std::nullopt;
        /**
         * @brief Keep-alive probe count, or std::nullopt to let the driver choose.
         *
         * A value of zero or less is rejected with idfxx::errc::invalid_arg.
         */
        std::optional<int> keep_alive_count = std::nullopt;

        // URI matching
        bool wildcard_uri_match = false; ///< Enable wildcard URI matching

        // Session callbacks
        /** @brief Called when a new session is opened. Return an error to reject. */
        std::move_only_function<result<void>(int) const> on_session_open = {};
        /**
         * @brief Called when a session is closed.
         *
         * The server closes the session's socket after this callback returns; the callback must not close it.
         */
        std::move_only_function<void(int) const> on_session_close = {};

        // TLS configuration
        std::string server_cert = {};      ///< Server certificate (PEM format)
        std::string private_key = {};      ///< Server private key (PEM format)
        std::string client_ca_cert = {};   ///< CA cert for client verification (optional, PEM format)
        bool use_ecdsa_peripheral = false; ///< Use ECDSA peripheral for key operations
        uint8_t ecdsa_key_efuse_blk = 0;   ///< eFuse block number for ECDSA key
        bool session_tickets = false;      ///< Enable TLS session tickets
        /**
         * @brief TLS handshake timeout, or std::nullopt to let the driver choose.
         *
         * A value of zero or less is rejected with idfxx::errc::invalid_arg.
         */
        std::optional<std::chrono::milliseconds> handshake_timeout = std::nullopt;
    };

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Creates and starts an HTTPS server.
     *
     * @code
     * idfxx::http::ssl_server srv({
     *     .server_port = 443,
     *     .server_cert = server_cert_pem,
     *     .private_key = server_key_pem,
     * });
     *
     * srv.on_get("/api/status", [](idfxx::http::request& req) -> idfxx::result<void> {
     *     req.set_content_type("application/json");
     *     req.send(R"({"status": "ok"})");
     *     return {};
     * });
     * @endcode
     *
     * @param cfg Server configuration including TLS certificates.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    [[nodiscard]] explicit ssl_server(config cfg);
#endif

    /**
     * @brief Creates and starts an HTTPS server.
     *
     * @param cfg Server configuration including TLS certificates.
     * @return The new HTTPS server, or an error.
     * @retval idfxx::errc::invalid_arg A configuration field holds an out-of-range value.
     */
    [[nodiscard]] static result<ssl_server> make(config cfg);

    ~ssl_server() override = default;

    ssl_server(const ssl_server&) = delete;
    ssl_server& operator=(const ssl_server&) = delete;

    /** @brief Move constructor. Transfers server ownership. */
    ssl_server(ssl_server&&) noexcept = default;

    /** @brief Move assignment. Transfers server ownership. */
    ssl_server& operator=(ssl_server&&) noexcept = default;

private:
    /** @cond INTERNAL */
    ssl_server(
        httpd_handle_t handle,
        std::unique_ptr<server_ctx> ctx,
        std::move_only_function<result<void>(int) const> on_session_open,
        std::move_only_function<void(int) const> on_session_close
    );
    /** @endcond */
};

} // namespace idfxx::http

/** @} */ // end of idfxx_https_server
