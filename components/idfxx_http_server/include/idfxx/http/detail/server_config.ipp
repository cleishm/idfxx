// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Template definition for server::_populate_httpd_config.
// This file must be included from .cpp files that have <esp_http_server.h> available.

#pragma once

#include <idfxx/http/server>

#include <esp_http_server.h>
#include <utility>

namespace {

void _noop_free(void*) {}

} // namespace

template<typename Config>
void idfxx::http::server::_populate_httpd_config(
    struct httpd_config& httpd_cfg,
    const Config& cfg,
    server_ctx* ctx,
    bool has_session_open,
    bool has_session_close
) {
    httpd_cfg.task_priority = cfg.priority.value();
    httpd_cfg.stack_size = cfg.stack_size;
    httpd_cfg.core_id =
        cfg.core_affinity ? static_cast<BaseType_t>(std::to_underlying(*cfg.core_affinity)) : tskNO_AFFINITY;
    httpd_cfg.server_port = cfg.server_port;
    httpd_cfg.ctrl_port = cfg.ctrl_port;
    httpd_cfg.backlog_conn = cfg.backlog_conn;
    httpd_cfg.max_open_sockets = cfg.max_open_sockets;
    httpd_cfg.max_uri_handlers = cfg.max_uri_handlers;
    httpd_cfg.max_resp_headers = cfg.max_resp_headers;
    if (cfg.max_req_hdr_len > 0) {
        httpd_cfg.max_req_hdr_len = cfg.max_req_hdr_len;
    }
    if (cfg.max_uri_len > 0) {
        httpd_cfg.max_uri_len = cfg.max_uri_len;
    }
    httpd_cfg.recv_wait_timeout = static_cast<uint16_t>(cfg.recv_wait_timeout.count());
    httpd_cfg.send_wait_timeout = static_cast<uint16_t>(cfg.send_wait_timeout.count());
    httpd_cfg.lru_purge_enable = cfg.lru_purge_enable;
    httpd_cfg.enable_so_linger = cfg.enable_so_linger;
    httpd_cfg.linger_timeout = static_cast<int>(cfg.linger_timeout.count());
    httpd_cfg.keep_alive_enable = cfg.keep_alive_enable;
    httpd_cfg.keep_alive_idle = static_cast<int>(cfg.keep_alive_idle.count());
    httpd_cfg.keep_alive_interval = static_cast<int>(cfg.keep_alive_interval.count());
    httpd_cfg.keep_alive_count = cfg.keep_alive_count;

    if (cfg.wildcard_uri_match) {
        httpd_cfg.uri_match_fn = httpd_uri_match_wildcard;
    }

    // ESP-IDF calls free() on global_user_ctx if no free function is set.
    // Provide a no-op since the server destructor manages this via unique_ptr.
    httpd_cfg.global_user_ctx = ctx;
    httpd_cfg.global_user_ctx_free_fn = _noop_free;

    if (has_session_open) {
        httpd_cfg.open_fn = server::_session_open_trampoline;
    }
    if (has_session_close) {
        httpd_cfg.close_fn = server::_session_close_trampoline;
    }
}
