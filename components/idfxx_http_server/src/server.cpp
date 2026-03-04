// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#include <idfxx/error>
#include <idfxx/http/detail/server_config.ipp>
#include <idfxx/http/server>

#include <cassert>
#include <cstring>
#include <esp_http_server.h>
#include <esp_log.h>
#include <optional>
#include <utility>

// Verify errc enum values match ESP-IDF constants
static_assert(std::to_underlying(idfxx::http::server::errc::handlers_full) == ESP_ERR_HTTPD_HANDLERS_FULL);
static_assert(std::to_underlying(idfxx::http::server::errc::handler_exists) == ESP_ERR_HTTPD_HANDLER_EXISTS);
static_assert(std::to_underlying(idfxx::http::server::errc::invalid_request) == ESP_ERR_HTTPD_INVALID_REQ);
static_assert(std::to_underlying(idfxx::http::server::errc::result_truncated) == ESP_ERR_HTTPD_RESULT_TRUNC);
static_assert(std::to_underlying(idfxx::http::server::errc::resp_header) == ESP_ERR_HTTPD_RESP_HDR);
static_assert(std::to_underlying(idfxx::http::server::errc::resp_send) == ESP_ERR_HTTPD_RESP_SEND);
static_assert(std::to_underlying(idfxx::http::server::errc::alloc_mem) == ESP_ERR_HTTPD_ALLOC_MEM);
static_assert(std::to_underlying(idfxx::http::server::errc::task) == ESP_ERR_HTTPD_TASK);

// The method enum uses esp_http_client_method_t values, which differ from the
// http_parser enum (httpd_method_t) used by the HTTP server. Verify and provide
// conversion functions.

// clang-format off
static constexpr httpd_method_t httpd_method_map[] = {
    static_cast<httpd_method_t>(HTTP_GET),         // method::get
    static_cast<httpd_method_t>(HTTP_POST),        // method::post
    static_cast<httpd_method_t>(HTTP_PUT),         // method::put
    static_cast<httpd_method_t>(HTTP_PATCH),       // method::patch
    static_cast<httpd_method_t>(HTTP_DELETE),      // method::delete_
    static_cast<httpd_method_t>(HTTP_HEAD),        // method::head
    static_cast<httpd_method_t>(HTTP_NOTIFY),      // method::notify
    static_cast<httpd_method_t>(HTTP_SUBSCRIBE),   // method::subscribe
    static_cast<httpd_method_t>(HTTP_UNSUBSCRIBE), // method::unsubscribe
    static_cast<httpd_method_t>(HTTP_OPTIONS),     // method::options
    static_cast<httpd_method_t>(HTTP_COPY),        // method::copy
    static_cast<httpd_method_t>(HTTP_MOVE),        // method::move
    static_cast<httpd_method_t>(HTTP_LOCK),        // method::lock
    static_cast<httpd_method_t>(HTTP_UNLOCK),      // method::unlock
    static_cast<httpd_method_t>(HTTP_PROPFIND),    // method::propfind
    static_cast<httpd_method_t>(HTTP_PROPPATCH),   // method::proppatch
    static_cast<httpd_method_t>(HTTP_MKCOL),       // method::mkcol
    static_cast<httpd_method_t>(HTTP_REPORT),      // method::report
};
// clang-format on

static constexpr size_t httpd_method_count = sizeof(httpd_method_map) / sizeof(httpd_method_map[0]);

#ifdef CONFIG_HTTPD_WS_SUPPORT
// Verify ws_frame_type enum values match ESP-IDF constants
static_assert(std::to_underlying(idfxx::http::request::ws_frame_type::continuation) == HTTPD_WS_TYPE_CONTINUE);
static_assert(std::to_underlying(idfxx::http::request::ws_frame_type::text) == HTTPD_WS_TYPE_TEXT);
static_assert(std::to_underlying(idfxx::http::request::ws_frame_type::binary) == HTTPD_WS_TYPE_BINARY);
static_assert(std::to_underlying(idfxx::http::request::ws_frame_type::close) == HTTPD_WS_TYPE_CLOSE);
static_assert(std::to_underlying(idfxx::http::request::ws_frame_type::ping) == HTTPD_WS_TYPE_PING);
static_assert(std::to_underlying(idfxx::http::request::ws_frame_type::pong) == HTTPD_WS_TYPE_PONG);
#endif

namespace {
const char* TAG = "idfxx::httpd";

httpd_method_t to_httpd_method(idfxx::http::method m) {
    auto idx = static_cast<size_t>(std::to_underlying(m));
    assert(idx < httpd_method_count);
    return httpd_method_map[idx];
}

idfxx::http::method from_httpd_method(unsigned m) {
    // clang-format off
    switch (m) {
    case HTTP_GET:         return idfxx::http::method::get;
    case HTTP_POST:        return idfxx::http::method::post;
    case HTTP_PUT:         return idfxx::http::method::put;
    case HTTP_PATCH:       return idfxx::http::method::patch;
    case HTTP_DELETE:      return idfxx::http::method::delete_;
    case HTTP_HEAD:        return idfxx::http::method::head;
    case HTTP_NOTIFY:      return idfxx::http::method::notify;
    case HTTP_SUBSCRIBE:   return idfxx::http::method::subscribe;
    case HTTP_UNSUBSCRIBE: return idfxx::http::method::unsubscribe;
    case HTTP_OPTIONS:     return idfxx::http::method::options;
    case HTTP_COPY:        return idfxx::http::method::copy;
    case HTTP_MOVE:        return idfxx::http::method::move;
    case HTTP_LOCK:        return idfxx::http::method::lock;
    case HTTP_UNLOCK:      return idfxx::http::method::unlock;
    case HTTP_PROPFIND:    return idfxx::http::method::propfind;
    case HTTP_PROPPATCH:   return idfxx::http::method::proppatch;
    case HTTP_MKCOL:       return idfxx::http::method::mkcol;
    case HTTP_REPORT:      return idfxx::http::method::report;
    default:               return static_cast<idfxx::http::method>(m);
    }
    // clang-format on
}
} // namespace

namespace idfxx::http {

// =============================================================================
// Error Category
// =============================================================================

const server::error_category& http_server_category() noexcept {
    static const server::error_category instance{};
    return instance;
}

static std::optional<server::errc> make_errc(esp_err_t e) noexcept {
    switch (e) {
    case ESP_ERR_HTTPD_HANDLERS_FULL:
        return server::errc::handlers_full;
    case ESP_ERR_HTTPD_HANDLER_EXISTS:
        return server::errc::handler_exists;
    case ESP_ERR_HTTPD_INVALID_REQ:
        return server::errc::invalid_request;
    case ESP_ERR_HTTPD_RESULT_TRUNC:
        return server::errc::result_truncated;
    case ESP_ERR_HTTPD_RESP_HDR:
        return server::errc::resp_header;
    case ESP_ERR_HTTPD_RESP_SEND:
        return server::errc::resp_send;
    case ESP_ERR_HTTPD_ALLOC_MEM:
        return server::errc::alloc_mem;
    case ESP_ERR_HTTPD_TASK:
        return server::errc::task;
    default:
        return std::nullopt;
    }
}

std::unexpected<std::error_code> http_server_error(esp_err_t e) {
    if (e == ESP_ERR_NO_MEM) {
        idfxx::raise_no_mem();
    }
    return std::unexpected(make_errc(e)
                               .transform([](server::errc ec) {
                                   return std::error_code{std::to_underlying(ec), http_server_category()};
                               })
                               .value_or(idfxx::make_error_code(e)));
}

const char* server::error_category::name() const noexcept {
    return "http::server::Error";
}

std::string server::error_category::message(int ec) const {
    switch (server::errc(ec)) {
    case server::errc::handlers_full:
        return "All URI handler slots are full";
    case server::errc::handler_exists:
        return "URI handler already registered";
    case server::errc::invalid_request:
        return "Invalid request";
    case server::errc::result_truncated:
        return "Result string truncated";
    case server::errc::resp_header:
        return "Response header field too large";
    case server::errc::resp_send:
        return "Error sending response";
    case server::errc::alloc_mem:
        return "Memory allocation failed";
    case server::errc::task:
        return "Failed to create server task";
    default:
        return esp_err_to_name(static_cast<esp_err_t>(ec));
    }
}

// =============================================================================
// Trampolines
// =============================================================================

esp_err_t server::_uri_handler_trampoline(httpd_req_t* req) {
    auto* record = static_cast<handler_record*>(req->user_ctx);
    request r{req};
    auto res = record->handler(r);
    if (!res) {
        ESP_LOGD(TAG, "URI handler for %s returned error: %s", req->uri, res.error().message().c_str());
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t server::_session_open_trampoline(httpd_handle_t hd, int sockfd) {
    auto* ctx = static_cast<server_ctx*>(httpd_get_global_user_ctx(hd));
    if (ctx && ctx->self && ctx->self->_on_session_open) {
        auto res = ctx->self->_on_session_open(sockfd);
        if (!res) {
            return ESP_FAIL;
        }
    }
    return ESP_OK;
}

void server::_session_close_trampoline(httpd_handle_t hd, int sockfd) {
    auto* ctx = static_cast<server_ctx*>(httpd_get_global_user_ctx(hd));
    if (ctx && ctx->self && ctx->self->_on_session_close) {
        ctx->self->_on_session_close(sockfd);
    }
}

// =============================================================================
// Server Lifecycle
// =============================================================================

result<httpd_handle_t>
server::_start_server(const config& cfg, server_ctx* ctx, bool has_session_open, bool has_session_close) {
    httpd_config_t httpd_cfg = HTTPD_DEFAULT_CONFIG();
    _populate_httpd_config(httpd_cfg, cfg, ctx, has_session_open, has_session_close);

    httpd_handle_t handle = nullptr;
    esp_err_t err = httpd_start(&handle, &httpd_cfg);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "httpd_start failed: %s", esp_err_to_name(err));
        return http_server_error(err);
    }
    return handle;
}

result<server> server::make(config cfg) {
    auto ctx = std::make_unique<server_ctx>();
    bool has_session_open = static_cast<bool>(cfg.on_session_open);
    bool has_session_close = static_cast<bool>(cfg.on_session_close);
    auto on_session_open = std::move(cfg.on_session_open);
    auto on_session_close = std::move(cfg.on_session_close);

    return _start_server(cfg, ctx.get(), has_session_open, has_session_close).transform([&](auto handle) {
        return server{handle, std::move(ctx), std::move(on_session_open), std::move(on_session_close), httpd_stop};
    });
}

server::server(
    httpd_handle_t handle,
    std::unique_ptr<server_ctx> ctx,
    std::move_only_function<result<void>(int) const> on_session_open,
    std::move_only_function<void(int) const> on_session_close,
    stop_fn_type stop_fn
)
    : _handle(handle)
    , _stop_fn(stop_fn)
    , _ctx(std::move(ctx))
    , _on_session_open(std::move(on_session_open))
    , _on_session_close(std::move(on_session_close)) {
    _ctx->self = this;
}

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
server::server(config cfg)
    : _handle(nullptr)
    , _stop_fn(httpd_stop)
    , _on_session_open(std::move(cfg.on_session_open))
    , _on_session_close(std::move(cfg.on_session_close)) {
    _ctx = std::make_unique<server_ctx>();
    _ctx->self = this;
    bool has_session_open = static_cast<bool>(_on_session_open);
    bool has_session_close = static_cast<bool>(_on_session_close);
    _handle = unwrap(_start_server(cfg, _ctx.get(), has_session_open, has_session_close));
}
#endif

server::~server() {
    if (_handle) {
        _stop_fn(_handle);
    }
}

server::server(server&& other) noexcept
    : _handle(other._handle)
    , _stop_fn(other._stop_fn)
    , _ctx(std::move(other._ctx))
    , _handlers(std::move(other._handlers))
    , _on_session_open(std::move(other._on_session_open))
    , _on_session_close(std::move(other._on_session_close)) {
    other._handle = nullptr;
    other._stop_fn = nullptr;
    if (_ctx) {
        _ctx->self = this;
    }
}

server& server::operator=(server&& other) noexcept {
    if (this != &other) {
        if (_handle) {
            _stop_fn(_handle);
        }
        _handle = other._handle;
        _stop_fn = other._stop_fn;
        _ctx = std::move(other._ctx);
        _handlers = std::move(other._handlers);
        _on_session_open = std::move(other._on_session_open);
        _on_session_close = std::move(other._on_session_close);
        other._handle = nullptr;
        other._stop_fn = nullptr;
        if (_ctx) {
            _ctx->self = this;
        }
    }
    return *this;
}

// =============================================================================
// Handler Registration
// =============================================================================

result<void> server::_register_handler(handler_record& record) {
    httpd_uri_t uri_def{};
    uri_def.uri = record.uri.c_str();
    uri_def.method = static_cast<httpd_method_t>(record.httpd_method);
    uri_def.handler = _uri_handler_trampoline;
    uri_def.user_ctx = &record;
#ifdef CONFIG_HTTPD_WS_SUPPORT
    uri_def.is_websocket = record.is_websocket;
    uri_def.handle_ws_control_frames = record.handle_ws_control_frames;
    if (!record.supported_subprotocol.empty()) {
        uri_def.supported_subprotocol = record.supported_subprotocol.c_str();
    }
#endif

    esp_err_t err = httpd_register_uri_handler(_handle, &uri_def);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "httpd_register_uri_handler failed for %s: %s", record.uri.c_str(), esp_err_to_name(err));
        return http_server_error(err);
    }
    return {};
}

result<void> server::_try_register(unsigned httpd_method, std::string uri, handler_type handler) {
    if (!_handle) {
        return error(idfxx::errc::invalid_state);
    }

    auto& record = _handlers.emplace_back(handler_record{
        .uri = std::move(uri),
        .httpd_method = httpd_method,
        .handler = std::move(handler),
    });

    auto res = _register_handler(record);
    if (!res) {
        _handlers.pop_back();
        return res;
    }
    return {};
}

result<void> server::try_on(enum method m, std::string uri, handler_type handler) {
    return _try_register(to_httpd_method(m), std::move(uri), std::move(handler));
}

result<void> server::try_on_any(std::string uri, handler_type handler) {
    return _try_register(static_cast<unsigned>(HTTP_ANY), std::move(uri), std::move(handler));
}

result<void> server::try_unregister_handler(enum method m, std::string_view uri) {
    if (!_handle) {
        return error(idfxx::errc::invalid_state);
    }

    std::string uri_str{uri};
    auto hm = to_httpd_method(m);
    esp_err_t err = httpd_unregister_uri_handler(_handle, uri_str.c_str(), hm);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "httpd_unregister_uri_handler failed: %s", esp_err_to_name(err));
        return http_server_error(err);
    }

    _handlers.remove_if([&](const handler_record& r) { return r.uri == uri && r.httpd_method == hm; });
    return {};
}

#ifdef CONFIG_HTTPD_WS_SUPPORT
result<void> server::try_on_ws(std::string uri, handler_type handler, ws_config ws_cfg) {
    if (!_handle) {
        return error(idfxx::errc::invalid_state);
    }

    auto& record = _handlers.emplace_back(handler_record{
        .uri = std::move(uri),
        .httpd_method = to_httpd_method(method::get),
        .handler = std::move(handler),
        .is_websocket = true,
        .handle_ws_control_frames = ws_cfg.handle_control_frames,
        .supported_subprotocol = std::move(ws_cfg.supported_subprotocol),
    });

    auto res = _register_handler(record);
    if (!res) {
        _handlers.pop_back();
        return res;
    }
    return {};
}
#endif

// =============================================================================
// Session Control
// =============================================================================

result<void> server::try_close_session(int sockfd) {
    if (!_handle) {
        return error(idfxx::errc::invalid_state);
    }
    esp_err_t err = httpd_sess_trigger_close(_handle, sockfd);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "httpd_sess_trigger_close failed: %s", esp_err_to_name(err));
        return http_server_error(err);
    }
    return {};
}

// =============================================================================
// Request Properties
// =============================================================================

enum method request::method() const {
    return from_httpd_method(static_cast<unsigned>(_req->method));
}

std::string_view request::uri() const {
    return _req->uri;
}

size_t request::content_length() const {
    return _req->content_len;
}

int request::socket_fd() const {
    return httpd_req_to_sockfd(_req);
}

// =============================================================================
// Headers
// =============================================================================

std::optional<std::string> request::header(std::string_view field) const {
    std::string field_str{field};
    size_t len = httpd_req_get_hdr_value_len(_req, field_str.c_str());
    if (len == 0) {
        return std::nullopt;
    }
    std::string value(len, '\0');
    esp_err_t err = httpd_req_get_hdr_value_str(_req, field_str.c_str(), value.data(), len + 1);
    if (err != ESP_OK) {
        return std::nullopt;
    }
    return value;
}

// =============================================================================
// Query String
// =============================================================================

std::optional<std::string> request::query_string() const {
    size_t len = httpd_req_get_url_query_len(_req);
    if (len == 0) {
        return std::nullopt;
    }
    std::string query(len, '\0');
    esp_err_t err = httpd_req_get_url_query_str(_req, query.data(), len + 1);
    if (err != ESP_OK) {
        return std::nullopt;
    }
    return query;
}

std::optional<std::string> request::query_param(std::string_view key) const {
    if (!_query_fetched) {
        _query_cache = query_string();
        _query_fetched = true;
    }
    if (!_query_cache) {
        return std::nullopt;
    }
    std::string key_str{key};
    char value[256];
    esp_err_t err = httpd_query_key_value(_query_cache->c_str(), key_str.c_str(), value, sizeof(value));
    if (err != ESP_OK) {
        return std::nullopt;
    }
    return std::string(value);
}

// =============================================================================
// Request Body
// =============================================================================

result<size_t> request::try_recv(std::span<uint8_t> buf) {
    int ret = httpd_req_recv(_req, reinterpret_cast<char*>(buf.data()), buf.size());
    if (ret < 0) {
        if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
            return error(idfxx::errc::timeout);
        }
        return http_server_error(ESP_ERR_HTTPD_INVALID_REQ);
    }
    return static_cast<size_t>(ret);
}

result<size_t> request::try_recv(std::span<char> buf) {
    return try_recv(std::span<uint8_t>{reinterpret_cast<uint8_t*>(buf.data()), buf.size()});
}

result<std::string> request::try_recv_body() {
    size_t len = _req->content_len;
    std::string body(len, '\0');
    size_t received = 0;
    while (received < len) {
        int ret = httpd_req_recv(_req, body.data() + received, len - received);
        if (ret < 0) {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
                return error(idfxx::errc::timeout);
            }
            return http_server_error(ESP_ERR_HTTPD_INVALID_REQ);
        }
        if (ret == 0) {
            break;
        }
        received += static_cast<size_t>(ret);
    }
    body.resize(received);
    return body;
}

// =============================================================================
// Response Setup
// =============================================================================

void request::set_status(std::string_view status) {
    _status_str = std::string(status);
    httpd_resp_set_status(_req, _status_str.c_str());
}

void request::set_status(int code) {
    // Map common status codes to reason phrases
    const char* phrase;
    switch (code) {
    case 200:
        phrase = "200 OK";
        break;
    case 201:
        phrase = "201 Created";
        break;
    case 204:
        phrase = "204 No Content";
        break;
    case 301:
        phrase = "301 Moved Permanently";
        break;
    case 302:
        phrase = "302 Found";
        break;
    case 304:
        phrase = "304 Not Modified";
        break;
    case 400:
        phrase = "400 Bad Request";
        break;
    case 401:
        phrase = "401 Unauthorized";
        break;
    case 403:
        phrase = "403 Forbidden";
        break;
    case 404:
        phrase = "404 Not Found";
        break;
    case 405:
        phrase = "405 Method Not Allowed";
        break;
    case 408:
        phrase = "408 Request Timeout";
        break;
    case 500:
        phrase = "500 Internal Server Error";
        break;
    case 501:
        phrase = "501 Not Implemented";
        break;
    case 503:
        phrase = "503 Service Unavailable";
        break;
    default:
        _status_str = std::to_string(code);
        httpd_resp_set_status(_req, _status_str.c_str());
        return;
    }
    httpd_resp_set_status(_req, phrase);
}

void request::set_content_type(std::string_view content_type) {
    _content_type = std::string(content_type);
    httpd_resp_set_type(_req, _content_type.c_str());
}

void request::set_header(std::string_view field, std::string_view value) {
    auto& [f, v] = _resp_headers.emplace_back(std::string(field), std::string(value));
    httpd_resp_set_hdr(_req, f.c_str(), v.c_str());
}

// =============================================================================
// Response Sending
// =============================================================================

result<void> request::try_send(std::string_view body) {
    esp_err_t err = httpd_resp_send(_req, body.data(), static_cast<ssize_t>(body.size()));
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "httpd_resp_send failed: %s", esp_err_to_name(err));
        return http_server_error(err);
    }
    return {};
}

result<void> request::try_send(std::span<const uint8_t> body) {
    esp_err_t err =
        httpd_resp_send(_req, reinterpret_cast<const char*>(body.data()), static_cast<ssize_t>(body.size()));
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "httpd_resp_send failed: %s", esp_err_to_name(err));
        return http_server_error(err);
    }
    return {};
}

result<void> request::try_send_chunk(std::string_view chunk) {
    esp_err_t err = httpd_resp_send_chunk(_req, chunk.data(), static_cast<ssize_t>(chunk.size()));
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "httpd_resp_send_chunk failed: %s", esp_err_to_name(err));
        return http_server_error(err);
    }
    return {};
}

result<void> request::try_end_chunked() {
    esp_err_t err = httpd_resp_send_chunk(_req, nullptr, 0);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "httpd_resp_send_chunk (end) failed: %s", esp_err_to_name(err));
        return http_server_error(err);
    }
    return {};
}

result<void> request::try_send_error(int code, std::string_view message) {
    httpd_err_code_t err_code;
    switch (code) {
    case 400:
        err_code = HTTPD_400_BAD_REQUEST;
        break;
    case 401:
        err_code = HTTPD_401_UNAUTHORIZED;
        break;
    case 403:
        err_code = HTTPD_403_FORBIDDEN;
        break;
    case 404:
        err_code = HTTPD_404_NOT_FOUND;
        break;
    case 405:
        err_code = HTTPD_405_METHOD_NOT_ALLOWED;
        break;
    case 408:
        err_code = HTTPD_408_REQ_TIMEOUT;
        break;
    case 411:
        err_code = HTTPD_411_LENGTH_REQUIRED;
        break;
    case 413:
        err_code = HTTPD_413_CONTENT_TOO_LARGE;
        break;
    case 414:
        err_code = HTTPD_414_URI_TOO_LONG;
        break;
    case 431:
        err_code = HTTPD_431_REQ_HDR_FIELDS_TOO_LARGE;
        break;
    case 500:
        err_code = HTTPD_500_INTERNAL_SERVER_ERROR;
        break;
    case 501:
        err_code = HTTPD_501_METHOD_NOT_IMPLEMENTED;
        break;
    case 505:
        err_code = HTTPD_505_VERSION_NOT_SUPPORTED;
        break;
    default:
        // For unmapped codes, use custom error response
        _status_str = std::to_string(code);
        esp_err_t err = httpd_resp_send_custom_err(
            _req, _status_str.c_str(), message.empty() ? nullptr : std::string(message).c_str()
        );
        if (err != ESP_OK) {
            return http_server_error(err);
        }
        return {};
    }

    std::string msg_str;
    const char* msg_ptr = nullptr;
    if (!message.empty()) {
        msg_str = std::string(message);
        msg_ptr = msg_str.c_str();
    }
    esp_err_t err = httpd_resp_send_err(_req, err_code, msg_ptr);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "httpd_resp_send_err failed: %s", esp_err_to_name(err));
        return http_server_error(err);
    }
    return {};
}

// =============================================================================
// WebSocket
// =============================================================================

#ifdef CONFIG_HTTPD_WS_SUPPORT
result<request::ws_frame> request::try_ws_recv(std::span<uint8_t> buf) {
    httpd_ws_frame_t ws_pkt{};
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;

    // First call with zero length to get the frame info
    esp_err_t err = httpd_ws_recv_frame(_req, &ws_pkt, 0);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "httpd_ws_recv_frame (header) failed: %s", esp_err_to_name(err));
        return http_server_error(err);
    }

    size_t recv_len = ws_pkt.len;
    if (recv_len > buf.size()) {
        recv_len = buf.size();
    }

    ws_pkt.payload = buf.data();
    err = httpd_ws_recv_frame(_req, &ws_pkt, recv_len);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "httpd_ws_recv_frame (payload) failed: %s", esp_err_to_name(err));
        return http_server_error(err);
    }

    return ws_frame{
        .type = static_cast<ws_frame_type>(ws_pkt.type),
        .final = ws_pkt.final,
        .data = std::span<const uint8_t>(buf.data(), ws_pkt.len),
    };
}

result<void> request::try_ws_send_text(std::string_view text) {
    httpd_ws_frame_t ws_pkt{};
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;
    ws_pkt.payload = const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(text.data()));
    ws_pkt.len = text.size();

    esp_err_t err = httpd_ws_send_frame(_req, &ws_pkt);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "httpd_ws_send_frame failed: %s", esp_err_to_name(err));
        return http_server_error(err);
    }
    return {};
}

result<void> request::try_ws_send_binary(std::span<const uint8_t> data) {
    httpd_ws_frame_t ws_pkt{};
    ws_pkt.type = HTTPD_WS_TYPE_BINARY;
    ws_pkt.payload = const_cast<uint8_t*>(data.data());
    ws_pkt.len = data.size();

    esp_err_t err = httpd_ws_send_frame(_req, &ws_pkt);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "httpd_ws_send_frame failed: %s", esp_err_to_name(err));
        return http_server_error(err);
    }
    return {};
}

result<void> request::try_ws_send(const ws_frame& frame) {
    httpd_ws_frame_t ws_pkt{};
    ws_pkt.type = static_cast<httpd_ws_type_t>(frame.type);
    ws_pkt.final = frame.final;
    ws_pkt.fragmented = !frame.final;
    ws_pkt.payload = const_cast<uint8_t*>(frame.data.data());
    ws_pkt.len = frame.data.size();

    esp_err_t err = httpd_ws_send_frame(_req, &ws_pkt);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "httpd_ws_send_frame failed: %s", esp_err_to_name(err));
        return http_server_error(err);
    }
    return {};
}
#endif // CONFIG_HTTPD_WS_SUPPORT

} // namespace idfxx::http
