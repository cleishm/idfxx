// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#include <idfxx/error>
#include <idfxx/http/detail/server_config.ipp>
#include <idfxx/http/ssl_server>

#include <cassert>
#include <esp_https_server.h>
#include <esp_log.h>
#include <utility>

namespace {
const char* TAG = "idfxx::httpd_ssl";
} // namespace

namespace idfxx::http {

result<ssl_server> ssl_server::make(config cfg) {
    httpd_ssl_config_t ssl_cfg = HTTPD_SSL_CONFIG_DEFAULT();

    auto ctx = std::make_unique<server_ctx>();
    bool has_session_open = static_cast<bool>(cfg.on_session_open);
    bool has_session_close = static_cast<bool>(cfg.on_session_close);
    auto on_session_open = std::move(cfg.on_session_open);
    auto on_session_close = std::move(cfg.on_session_close);

    _populate_httpd_config(ssl_cfg.httpd, cfg, ctx.get(), has_session_open, has_session_close);

    // Move cert strings out of config — they only need to live until httpd_ssl_start() returns,
    // since ESP-IDF copies all certificate data internally.
    auto server_cert = std::move(cfg.server_cert);
    auto private_key = std::move(cfg.private_key);
    auto client_ca_cert = std::move(cfg.client_ca_cert);

    // TLS configuration
    ssl_cfg.servercert = reinterpret_cast<const uint8_t*>(server_cert.c_str());
    ssl_cfg.servercert_len = server_cert.size() + 1; // include null terminator for PEM
    ssl_cfg.prvtkey_pem = reinterpret_cast<const uint8_t*>(private_key.c_str());
    ssl_cfg.prvtkey_len = private_key.size() + 1;

    if (!client_ca_cert.empty()) {
        ssl_cfg.cacert_pem = reinterpret_cast<const uint8_t*>(client_ca_cert.c_str());
        ssl_cfg.cacert_len = client_ca_cert.size() + 1;
    }

    ssl_cfg.use_ecdsa_peripheral = cfg.use_ecdsa_peripheral;
    ssl_cfg.ecdsa_key_efuse_blk = cfg.ecdsa_key_efuse_blk;
    ssl_cfg.session_tickets = cfg.session_tickets;
    ssl_cfg.use_secure_element = cfg.use_secure_element;

    if (cfg.handshake_timeout.count() > 0) {
        ssl_cfg.tls_handshake_timeout_ms = static_cast<unsigned int>(cfg.handshake_timeout.count());
    }

    ssl_cfg.port_secure = cfg.server_port;
    ssl_cfg.transport_mode = HTTPD_SSL_TRANSPORT_SECURE;

    httpd_handle_t handle = nullptr;
    esp_err_t err = httpd_ssl_start(&handle, &ssl_cfg);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "httpd_ssl_start failed: %s", esp_err_to_name(err));
        return http_server_error(err);
    }

    return ssl_server{handle, std::move(ctx), std::move(on_session_open), std::move(on_session_close)};
}

ssl_server::ssl_server(
    httpd_handle_t handle,
    std::unique_ptr<server_ctx> ctx,
    std::move_only_function<result<void>(int) const> on_session_open,
    std::move_only_function<void(int) const> on_session_close
)
    : server(handle, std::move(ctx), std::move(on_session_open), std::move(on_session_close), httpd_ssl_stop) {}

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
ssl_server::ssl_server(config cfg)
    : ssl_server(unwrap(make(std::move(cfg)))) {}
#endif

} // namespace idfxx::http
