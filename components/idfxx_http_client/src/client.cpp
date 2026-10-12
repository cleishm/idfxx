// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#include <idfxx/error>
#include <idfxx/http/client>

#include <algorithm>
#include <array>
#include <charconv>
#include <climits>
#include <esp_http_client.h>
#include <esp_idf_version.h>
#include <esp_log.h>
#include <optional>
#include <utility>

// Verify event_id enum values match ESP-IDF constants
static_assert(std::to_underlying(idfxx::http::event_id::error) == HTTP_EVENT_ERROR);
static_assert(std::to_underlying(idfxx::http::event_id::on_connected) == HTTP_EVENT_ON_CONNECTED);
static_assert(std::to_underlying(idfxx::http::event_id::headers_sent) == HTTP_EVENT_HEADERS_SENT);
static_assert(std::to_underlying(idfxx::http::event_id::on_header) == HTTP_EVENT_ON_HEADER);
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
static_assert(std::to_underlying(idfxx::http::event_id::on_headers_complete) == HTTP_EVENT_ON_HEADERS_COMPLETE);
static_assert(std::to_underlying(idfxx::http::event_id::on_status_code) == HTTP_EVENT_ON_STATUS_CODE);
#endif
static_assert(std::to_underlying(idfxx::http::event_id::on_data) == HTTP_EVENT_ON_DATA);
static_assert(std::to_underlying(idfxx::http::event_id::on_finish) == HTTP_EVENT_ON_FINISH);
static_assert(std::to_underlying(idfxx::http::event_id::disconnected) == HTTP_EVENT_DISCONNECTED);
static_assert(std::to_underlying(idfxx::http::event_id::redirect) == HTTP_EVENT_REDIRECT);

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
// Verify client::state enum values match ESP-IDF constants
static_assert(std::to_underlying(idfxx::http::client::state::uninit) == HTTP_STATE_UNINIT);
static_assert(std::to_underlying(idfxx::http::client::state::init) == HTTP_STATE_INIT);
static_assert(std::to_underlying(idfxx::http::client::state::connecting) == HTTP_STATE_CONNECTING);
static_assert(std::to_underlying(idfxx::http::client::state::connected) == HTTP_STATE_CONNECTED);
static_assert(std::to_underlying(idfxx::http::client::state::req_complete_header) == HTTP_STATE_REQ_COMPLETE_HEADER);
static_assert(std::to_underlying(idfxx::http::client::state::req_complete_data) == HTTP_STATE_REQ_COMPLETE_DATA);
static_assert(std::to_underlying(idfxx::http::client::state::res_complete_header) == HTTP_STATE_RES_COMPLETE_HEADER);
static_assert(std::to_underlying(idfxx::http::client::state::res_on_data_start) == HTTP_STATE_RES_ON_DATA_START);
static_assert(std::to_underlying(idfxx::http::client::state::res_complete_data) == HTTP_STATE_RES_COMPLETE_DATA);
static_assert(std::to_underlying(idfxx::http::client::state::close) == HTTP_STATE_CLOSE);
#endif

// Verify errc enum values match ESP-IDF constants
static_assert(std::to_underlying(idfxx::http::client::errc::max_redirect) == ESP_ERR_HTTP_MAX_REDIRECT);
static_assert(std::to_underlying(idfxx::http::client::errc::connect) == ESP_ERR_HTTP_CONNECT);
static_assert(std::to_underlying(idfxx::http::client::errc::write_data) == ESP_ERR_HTTP_WRITE_DATA);
static_assert(std::to_underlying(idfxx::http::client::errc::fetch_header) == ESP_ERR_HTTP_FETCH_HEADER);
static_assert(std::to_underlying(idfxx::http::client::errc::invalid_transport) == ESP_ERR_HTTP_INVALID_TRANSPORT);
static_assert(std::to_underlying(idfxx::http::client::errc::connecting) == ESP_ERR_HTTP_CONNECTING);
static_assert(std::to_underlying(idfxx::http::client::errc::eagain) == ESP_ERR_HTTP_EAGAIN);
static_assert(std::to_underlying(idfxx::http::client::errc::connection_closed) == ESP_ERR_HTTP_CONNECTION_CLOSED);
static_assert(std::to_underlying(idfxx::http::client::errc::not_modified) == ESP_ERR_HTTP_NOT_MODIFIED);
static_assert(
    std::to_underlying(idfxx::http::client::errc::range_not_satisfiable) == ESP_ERR_HTTP_RANGE_NOT_SATISFIABLE
);
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 5, 2)
static_assert(std::to_underlying(idfxx::http::client::errc::read_timeout) == ESP_ERR_HTTP_READ_TIMEOUT);
static_assert(std::to_underlying(idfxx::http::client::errc::incomplete_data) == ESP_ERR_HTTP_INCOMPLETE_DATA);
#endif
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
static_assert(std::to_underlying(idfxx::http::client::errc::redirect_downgrade) == ESP_ERR_HTTP_REDIRECT_DOWNGRADE);
static_assert(std::to_underlying(idfxx::http::client::errc::header_too_long) == ESP_ERR_HTTP_HEADER_TOO_LONG);
#endif

namespace {
const char* TAG = "idfxx::http";
}

namespace idfxx::http {

const client::error_category& http_client_category() noexcept {
    static const client::error_category instance{};
    return instance;
}

static std::optional<client::errc> make_errc(esp_err_t e) noexcept {
    switch (e) {
    case ESP_ERR_HTTP_MAX_REDIRECT:
        return client::errc::max_redirect;
    case ESP_ERR_HTTP_CONNECT:
        return client::errc::connect;
    case ESP_ERR_HTTP_WRITE_DATA:
        return client::errc::write_data;
    case ESP_ERR_HTTP_FETCH_HEADER:
        return client::errc::fetch_header;
    case ESP_ERR_HTTP_INVALID_TRANSPORT:
        return client::errc::invalid_transport;
    case ESP_ERR_HTTP_CONNECTING:
        return client::errc::connecting;
    case ESP_ERR_HTTP_EAGAIN:
        return client::errc::eagain;
    case ESP_ERR_HTTP_CONNECTION_CLOSED:
        return client::errc::connection_closed;
    case ESP_ERR_HTTP_NOT_MODIFIED:
        return client::errc::not_modified;
    case ESP_ERR_HTTP_RANGE_NOT_SATISFIABLE:
        return client::errc::range_not_satisfiable;
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 5, 2)
    case ESP_ERR_HTTP_READ_TIMEOUT:
        return client::errc::read_timeout;
    case ESP_ERR_HTTP_INCOMPLETE_DATA:
        return client::errc::incomplete_data;
#endif
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
    case ESP_ERR_HTTP_REDIRECT_DOWNGRADE:
        return client::errc::redirect_downgrade;
    case ESP_ERR_HTTP_HEADER_TOO_LONG:
        return client::errc::header_too_long;
#endif
    default:
        return std::nullopt;
    }
}

std::unexpected<std::error_code> http_client_error(esp_err_t e) {
    if (e == ESP_ERR_NO_MEM) {
        idfxx::raise_no_mem();
    }
    return std::unexpected(make_errc(e)
                               .transform([](client::errc ec) {
                                   return std::error_code{std::to_underlying(ec), http_client_category()};
                               })
                               .value_or(idfxx::make_error_code(e)));
}

} // namespace idfxx::http

namespace idfxx {

std::string to_string(http::event_id id) {
    switch (id) {
    case http::event_id::error:
        return "ERROR";
    case http::event_id::on_connected:
        return "ON_CONNECTED";
    case http::event_id::headers_sent:
        return "HEADERS_SENT";
    case http::event_id::on_header:
        return "ON_HEADER";
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
    case http::event_id::on_headers_complete:
        return "ON_HEADERS_COMPLETE";
    case http::event_id::on_status_code:
        return "ON_STATUS_CODE";
#endif
    case http::event_id::on_data:
        return "ON_DATA";
    case http::event_id::on_finish:
        return "ON_FINISH";
    case http::event_id::disconnected:
        return "DISCONNECTED";
    case http::event_id::redirect:
        return "REDIRECT";
    default:
        return "unknown(" + std::to_string(static_cast<int>(id)) + ")";
    }
}

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
std::string to_string(http::client::state s) {
    switch (s) {
    case http::client::state::uninit:
        return "UNINIT";
    case http::client::state::init:
        return "INIT";
    case http::client::state::connecting:
        return "CONNECTING";
    case http::client::state::connected:
        return "CONNECTED";
    case http::client::state::req_complete_header:
        return "REQ_COMPLETE_HEADER";
    case http::client::state::req_complete_data:
        return "REQ_COMPLETE_DATA";
    case http::client::state::res_complete_header:
        return "RES_COMPLETE_HEADER";
    case http::client::state::res_on_data_start:
        return "RES_ON_DATA_START";
    case http::client::state::res_complete_data:
        return "RES_COMPLETE_DATA";
    case http::client::state::close:
        return "CLOSE";
    default:
        return "unknown(" + std::to_string(static_cast<int>(s)) + ")";
    }
}
#endif

} // namespace idfxx

namespace idfxx::http {

const char* client::error_category::name() const noexcept {
    return "http::client::Error";
}

std::string client::error_category::message(int ec) const {
    switch (client::errc(ec)) {
    case client::errc::max_redirect:
        return "Maximum number of redirects exceeded";
    case client::errc::connect:
        return "Connection to server failed";
    case client::errc::write_data:
        return "Error writing request data";
    case client::errc::fetch_header:
        return "Error reading response headers";
    case client::errc::invalid_transport:
        return "Invalid transport type specified";
    case client::errc::connecting:
        return "Connection in progress";
    case client::errc::eagain:
        return "Resource temporarily unavailable, try again";
    case client::errc::connection_closed:
        return "Connection closed by remote";
    case client::errc::not_modified:
        return "Not modified (HTTP 304)";
    case client::errc::range_not_satisfiable:
        return "Range not satisfiable (HTTP 416)";
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 5, 2)
    case client::errc::read_timeout:
        return "Timed out reading response data";
    case client::errc::incomplete_data:
        return "Response data incomplete";
#endif
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
    case client::errc::redirect_downgrade:
        return "Redirect from HTTPS to a non-HTTPS URL blocked";
    case client::errc::header_too_long:
        return "Request header too long for the transmit buffer";
#endif
    default:
        return esp_err_to_name(static_cast<esp_err_t>(ec));
    }
}

esp_err_t client::_http_event_handler(esp_http_client_event* evt) {
    auto* self = static_cast<client*>(evt->user_data);
    if (self && self->_on_event) {
        event_data data{
            .id = static_cast<event_id>(evt->event_id),
            .data = (evt->data && evt->data_len > 0)
                ? std::span<const uint8_t>(static_cast<const uint8_t*>(evt->data), evt->data_len)
                : std::span<const uint8_t>{},
            .header_key = evt->header_key ? std::string_view(evt->header_key) : std::string_view{},
            .header_value = evt->header_value ? std::string_view(evt->header_value) : std::string_view{},
        };
        self->_on_event(data);
    }
    return ESP_OK;
}

// Thread-local used to pass the active client to the crt_bundle trampoline.
// This is safe because perform()/open() are blocking and the client is not thread-safe.
static thread_local client* tls_active_client = nullptr;

// True when an optional config value is set to zero or a negative value.
template<typename T>
static bool set_and_not_positive(const std::optional<T>& value) {
    return value && *value <= T{};
}

esp_err_t client::_crt_bundle_trampoline(void* conf) {
    return tls_active_client->_crt_bundle_attach(conf);
}

result<esp_http_client_handle_t> client::_init_client(
    config& cfg,
    const std::string& cert_pem,
    const std::string& client_cert_pem,
    const std::string& client_key_pem
) {
    if (set_and_not_positive(cfg.port) || (cfg.path && cfg.path->empty()) || set_and_not_positive(cfg.timeout) ||
        set_and_not_positive(cfg.max_redirection_count) ||
        (cfg.max_authorization_retries && *cfg.max_authorization_retries < 0) ||
        set_and_not_positive(cfg.buffer_size) || set_and_not_positive(cfg.buffer_size_tx) ||
        set_and_not_positive(cfg.keep_alive_idle) || set_and_not_positive(cfg.keep_alive_interval) ||
        set_and_not_positive(cfg.keep_alive_count)) {
        return error(idfxx::errc::invalid_arg);
    }

    esp_http_client_config_t esp_cfg{};

    if (!cfg.url.empty()) {
        esp_cfg.url = cfg.url.c_str();
    }
    if (!cfg.host.empty()) {
        esp_cfg.host = cfg.host.c_str();
    }
    esp_cfg.port = cfg.port.value_or(0); // 0 selects the driver default
    if (cfg.path) {
        esp_cfg.path = cfg.path->c_str();
    }
    if (!cfg.query.empty()) {
        esp_cfg.query = cfg.query.c_str();
    }
    esp_cfg.method = static_cast<esp_http_client_method_t>(cfg.method);
    esp_cfg.transport_type = static_cast<esp_http_client_transport_t>(cfg.transport_type);
    esp_cfg.timeout_ms = static_cast<int>(cfg.timeout.value_or(std::chrono::milliseconds{0}).count());

    if (!cfg.username.empty()) {
        esp_cfg.username = cfg.username.c_str();
    }
    if (!cfg.password.empty()) {
        esp_cfg.password = cfg.password.c_str();
    }
    esp_cfg.auth_type = static_cast<esp_http_client_auth_type_t>(cfg.auth_type);

    if (!cert_pem.empty()) {
        esp_cfg.cert_pem = cert_pem.c_str();
        esp_cfg.cert_len = cert_pem.size() + 1;
    }
    if (!client_cert_pem.empty()) {
        esp_cfg.client_cert_pem = client_cert_pem.c_str();
        esp_cfg.client_cert_len = client_cert_pem.size() + 1;
    }
    if (!client_key_pem.empty()) {
        esp_cfg.client_key_pem = client_key_pem.c_str();
        esp_cfg.client_key_len = client_key_pem.size() + 1;
    }

    if (cfg.user_agent) {
        esp_cfg.user_agent = cfg.user_agent->c_str();
    }

    esp_cfg.disable_auto_redirect = cfg.disable_auto_redirect;
    esp_cfg.max_redirection_count = cfg.max_redirection_count.value_or(0);
    // The driver reads 0 as "use the default" and -1 as "no retries".
    if (!cfg.max_authorization_retries) {
        esp_cfg.max_authorization_retries = 0;
    } else if (*cfg.max_authorization_retries == 0) {
        esp_cfg.max_authorization_retries = -1;
    } else {
        esp_cfg.max_authorization_retries = *cfg.max_authorization_retries;
    }

    esp_cfg.buffer_size = cfg.buffer_size.value_or(0);
    esp_cfg.buffer_size_tx = cfg.buffer_size_tx.value_or(0);

    esp_cfg.keep_alive_enable = cfg.keep_alive_enable;
    esp_cfg.keep_alive_idle = static_cast<int>(cfg.keep_alive_idle.value_or(std::chrono::seconds{0}).count());
    esp_cfg.keep_alive_interval = static_cast<int>(cfg.keep_alive_interval.value_or(std::chrono::seconds{0}).count());
    esp_cfg.keep_alive_count = cfg.keep_alive_count.value_or(0);

    esp_cfg.use_global_ca_store = cfg.use_global_ca_store;
    esp_cfg.skip_cert_common_name_check = cfg.skip_cert_common_name_check;
    if (!cfg.common_name.empty()) {
        esp_cfg.common_name = cfg.common_name.c_str();
    }
    if (cfg.crt_bundle_attach) {
        esp_cfg.crt_bundle_attach = client::_crt_bundle_trampoline;
    }

    esp_cfg.event_handler = client::_http_event_handler;
    // user_data set to nullptr here; will be updated after client construction
    esp_cfg.user_data = nullptr;

    auto handle = esp_http_client_init(&esp_cfg);
    if (!handle) {
        ESP_LOGD(TAG, "esp_http_client_init failed");
        raise_no_mem();
    }

    return handle;
}

result<client> client::make(config cfg) {
    auto cert_pem = std::move(cfg.cert_pem);
    auto client_cert_pem = std::move(cfg.client_cert_pem);
    auto client_key_pem = std::move(cfg.client_key_pem);
    auto on_event = std::move(cfg.on_event);

    return _init_client(cfg, cert_pem, client_cert_pem, client_key_pem).transform([&](auto handle) {
        client c{
            handle,
            std::move(on_event),
            std::move(cfg.crt_bundle_attach),
            std::string{},
            std::move(cert_pem),
            std::move(client_cert_pem),
            std::move(client_key_pem)
        };
        esp_http_client_set_user_data(handle, &c);
        return c;
    });
}

client::client(
    esp_http_client_handle_t handle,
    std::move_only_function<void(const event_data&) const> on_event,
    std::move_only_function<int(void*) const> crt_bundle_attach,
    std::string post_data,
    std::string cert_pem,
    std::string client_cert_pem,
    std::string client_key_pem
)
    : _handle(handle)
    , _on_event(std::move(on_event))
    , _crt_bundle_attach(std::move(crt_bundle_attach))
    , _post_data(std::move(post_data))
    , _cert_pem(std::move(cert_pem))
    , _client_cert_pem(std::move(client_cert_pem))
    , _client_key_pem(std::move(client_key_pem)) {}

client::client(client&& other) noexcept
    : _handle(std::exchange(other._handle, nullptr))
    , _on_event(std::move(other._on_event))
    , _crt_bundle_attach(std::move(other._crt_bundle_attach))
    , _post_data(std::move(other._post_data))
    , _cert_pem(std::move(other._cert_pem))
    , _client_cert_pem(std::move(other._client_cert_pem))
    , _client_key_pem(std::move(other._client_key_pem))
    , _request_incomplete(std::exchange(other._request_incomplete, false)) {
    if (_handle) {
        esp_http_client_set_user_data(_handle, this);
    }
}

client& client::operator=(client&& other) noexcept {
    if (this != &other) {
        if (_handle) {
            esp_http_client_cleanup(_handle);
        }
        _handle = std::exchange(other._handle, nullptr);
        _on_event = std::move(other._on_event);
        _crt_bundle_attach = std::move(other._crt_bundle_attach);
        _post_data = std::move(other._post_data);
        _cert_pem = std::move(other._cert_pem);
        _client_cert_pem = std::move(other._client_cert_pem);
        _client_key_pem = std::move(other._client_key_pem);
        _request_incomplete = std::exchange(other._request_incomplete, false);
        if (_handle) {
            esp_http_client_set_user_data(_handle, this);
        }
    }
    return *this;
}

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
client::client(config cfg)
    : _handle(nullptr)
    , _on_event(std::move(cfg.on_event))
    , _post_data()
    , _cert_pem(std::move(cfg.cert_pem))
    , _client_cert_pem(std::move(cfg.client_cert_pem))
    , _client_key_pem(std::move(cfg.client_key_pem)) {
    _handle = unwrap(_init_client(cfg, _cert_pem, _client_cert_pem, _client_key_pem));
    _crt_bundle_attach = std::move(cfg.crt_bundle_attach);
    esp_http_client_set_user_data(_handle, this);
}
#endif

client::~client() {
    if (_handle) {
        esp_http_client_cleanup(_handle);
    }
}

// =========================================================================
// Request Configuration
// =========================================================================

result<void> client::try_set_url(std::string_view url) {
    if (!_handle) {
        return error(idfxx::errc::invalid_state);
    }
    std::string url_str{url};
    esp_err_t err = esp_http_client_set_url(_handle, url_str.c_str());
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "Failed to set URL: %s", esp_err_to_name(err));
        return http_client_error(err);
    }
    return {};
}

void client::set_method(enum method m) {
    if (!_handle) {
        return;
    }
    esp_http_client_set_method(_handle, static_cast<esp_http_client_method_t>(m));
}

void client::set_header(std::string_view key, std::string_view value) {
    if (!_handle) {
        return;
    }
    std::string key_str{key};
    std::string value_str{value};
    esp_err_t err = esp_http_client_set_header(_handle, key_str.c_str(), value_str.c_str());
    if (err != ESP_OK) {
        idfxx::raise_no_mem();
    }
}

void client::delete_header(std::string_view key) {
    if (!_handle) {
        return;
    }
    std::string key_str{key};
    esp_http_client_delete_header(_handle, key_str.c_str());
}

void client::set_post_field(std::string_view data) {
    if (!_handle) {
        return;
    }
    _post_data = std::string(data);
    esp_err_t err = esp_http_client_set_post_field(_handle, _post_data.c_str(), static_cast<int>(_post_data.size()));
    if (err != ESP_OK) {
        idfxx::raise_no_mem();
    }
}

void client::set_username(std::string_view username) {
    if (!_handle) {
        return;
    }
    std::string username_str{username};
    esp_http_client_set_username(_handle, username_str.c_str());
}

void client::set_password(std::string_view password) {
    if (!_handle) {
        return;
    }
    std::string password_str{password};
    esp_http_client_set_password(_handle, password_str.c_str());
}

void client::set_auth_type(enum auth_type type) {
    if (!_handle) {
        return;
    }
    esp_http_client_set_authtype(_handle, static_cast<esp_http_client_auth_type_t>(type));
}

void client::_set_timeout(std::chrono::milliseconds timeout) {
    if (!_handle) {
        return;
    }
    esp_http_client_set_timeout_ms(_handle, static_cast<int>(timeout.count()));
}

// =========================================================================
// Simple Request (blocking)
// =========================================================================

namespace {

/// RAII guard that sets tls_active_client for the duration of a scope.
struct tls_client_guard {
    explicit tls_client_guard(client* c) { tls_active_client = c; }
    ~tls_client_guard() { tls_active_client = nullptr; }
    tls_client_guard(const tls_client_guard&) = delete;
    tls_client_guard& operator=(const tls_client_guard&) = delete;
};

} // namespace

void client::_discard_incomplete_request() {
    if (_request_incomplete) {
        ESP_LOGW(TAG, "closing the connection of a chunked request whose body was not finished");
        esp_http_client_close(_handle);
        _request_incomplete = false;
    }
}

result<void> client::try_perform() {
    if (!_handle) {
        return error(idfxx::errc::invalid_state);
    }
    _discard_incomplete_request();
    tls_client_guard guard(this);
    esp_err_t err = esp_http_client_perform(_handle);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "perform failed: %s", esp_err_to_name(err));
        return http_client_error(err);
    }
    return {};
}

// =========================================================================
// Streaming API
// =========================================================================

result<void> client::try_open(size_t write_len) {
    if (!_handle) {
        return error(idfxx::errc::invalid_state);
    }
    if (write_len > static_cast<size_t>(INT_MAX)) {
        return error(idfxx::errc::invalid_arg);
    }
    _discard_incomplete_request();
    tls_client_guard guard(this);
    esp_err_t err = esp_http_client_open(_handle, static_cast<int>(write_len));
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "open failed: %s", esp_err_to_name(err));
        return http_client_error(err);
    }
    return {};
}

result<client::chunk_writer> client::try_open_chunked() {
    if (!_handle) {
        return error(idfxx::errc::invalid_state);
    }
    _discard_incomplete_request();
    // A chunked request must not carry a Content-Length, but some ESP-IDF
    // releases keep one left over from an earlier request on this handle.
    esp_http_client_delete_header(_handle, "Content-Length");
    tls_client_guard guard(this);
    esp_err_t err = esp_http_client_open(_handle, -1);
    // The request headers have been sent. Drop Transfer-Encoding so that it is
    // not carried into a later fixed-length request on this handle, which some
    // ESP-IDF releases would send alongside a Content-Length.
    esp_http_client_delete_header(_handle, "Transfer-Encoding");
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "open_chunked failed: %s", esp_err_to_name(err));
        return http_client_error(err);
    }
    return chunk_writer{*this};
}

result<size_t> client::try_write(std::span<const uint8_t> data) {
    if (!_handle || _request_incomplete) {
        return error(idfxx::errc::invalid_state);
    }
    int len = static_cast<int>(std::min(data.size(), static_cast<size_t>(INT_MAX)));
    int written = esp_http_client_write(_handle, reinterpret_cast<const char*>(data.data()), len);
    if (written < 0) {
        ESP_LOGD(TAG, "write failed");
        return http_client_error(ESP_ERR_HTTP_WRITE_DATA);
    }
    if (written == 0 && len > 0) {
        // The transport reports a write that timed out waiting for the socket
        // to become writable as 0 bytes written.
        ESP_LOGD(TAG, "write timed out");
        return http_client_error(ESP_ERR_HTTP_EAGAIN);
    }
    return static_cast<size_t>(written);
}

result<size_t> client::try_write(std::string_view data) {
    return try_write(std::span<const uint8_t>{reinterpret_cast<const uint8_t*>(data.data()), data.size()});
}

result<int64_t> client::try_fetch_headers() {
    if (!_handle || _request_incomplete) {
        return error(idfxx::errc::invalid_state);
    }
    // Success is never negative: a chunked or unknown length is reported as 0.
    int64_t content_length = esp_http_client_fetch_headers(_handle);
    if (content_length == -ESP_ERR_HTTP_EAGAIN) {
        ESP_LOGD(TAG, "fetch_headers timed out");
        return http_client_error(ESP_ERR_HTTP_EAGAIN);
    }
    if (content_length < 0) {
        ESP_LOGD(TAG, "fetch_headers failed");
        return http_client_error(ESP_ERR_HTTP_FETCH_HEADER);
    }
    return content_length;
}

result<size_t> client::try_read(std::span<uint8_t> buf) {
    if (!_handle || _request_incomplete) {
        return error(idfxx::errc::invalid_state);
    }
    int len = static_cast<int>(std::min(buf.size(), static_cast<size_t>(INT_MAX)));
    int read_len = esp_http_client_read(_handle, reinterpret_cast<char*>(buf.data()), len);
    if (read_len == -ESP_ERR_HTTP_EAGAIN) {
        ESP_LOGD(TAG, "read timed out");
        return http_client_error(ESP_ERR_HTTP_EAGAIN);
    }
    if (read_len < 0) {
        ESP_LOGD(TAG, "read failed");
        return http_client_error(ESP_ERR_HTTP_CONNECTION_CLOSED);
    }
    return static_cast<size_t>(read_len);
}

result<size_t> client::try_read(std::span<char> buf) {
    return try_read(std::span<uint8_t>{reinterpret_cast<uint8_t*>(buf.data()), buf.size()});
}

result<void> client::try_flush_response() {
    if (!_handle || _request_incomplete) {
        return error(idfxx::errc::invalid_state);
    }
    esp_err_t err = esp_http_client_flush_response(_handle, nullptr);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "flush_response failed: %s", esp_err_to_name(err));
        return http_client_error(err);
    }
    return {};
}

result<void> client::try_close() {
    if (!_handle) {
        return error(idfxx::errc::invalid_state);
    }
    esp_err_t err = esp_http_client_close(_handle);
    _request_incomplete = false;
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "close failed: %s", esp_err_to_name(err));
        return http_client_error(err);
    }
    return {};
}

// =========================================================================
// Chunked request body
// =========================================================================

client::chunk_writer::chunk_writer(chunk_writer&& other) noexcept
    : _client(std::exchange(other._client, nullptr))
    , _stage(other._stage) {}

client::chunk_writer& client::chunk_writer::operator=(chunk_writer&& other) noexcept {
    if (this != &other) {
        _abandon_if_unfinished();
        _client = std::exchange(other._client, nullptr);
        _stage = other._stage;
    }
    return *this;
}

client::chunk_writer::~chunk_writer() {
    _abandon_if_unfinished();
}

void client::chunk_writer::_abandon_if_unfinished() noexcept {
    if (_client && _stage != stage::finished) {
        ESP_LOGW(TAG, "chunk_writer discarded before finish(); the request body is unterminated");
        _client->_request_incomplete = true;
    }
}

result<void> client::chunk_writer::_send(const char* data, size_t len) {
    int written = esp_http_client_write(_client->_handle, data, static_cast<int>(len));
    if (written >= 0 && static_cast<size_t>(written) == len) {
        return {};
    }
    // Part of a chunk may have been sent, so the body can no longer be framed correctly
    _stage = stage::failed;
    if (written == 0 && len > 0) {
        // The transport reports a write that timed out waiting for the socket
        // to become writable as 0 bytes written.
        ESP_LOGD(TAG, "chunk write timed out");
        return http_client_error(ESP_ERR_HTTP_EAGAIN);
    }
    ESP_LOGD(TAG, "chunk write failed");
    return http_client_error(ESP_ERR_HTTP_WRITE_DATA);
}

result<void> client::chunk_writer::try_write(std::span<const uint8_t> data) {
    if (!_client || !_client->_handle || _stage != stage::open) {
        return error(idfxx::errc::invalid_state);
    }
    if (data.empty()) {
        return {};
    }
    if (data.size() > static_cast<size_t>(INT_MAX)) {
        return error(idfxx::errc::invalid_arg);
    }
    // Chunk size in hex, followed by CRLF
    std::array<char, 2 * sizeof(size_t) + 2> header;
    auto end = std::to_chars(header.data(), header.data() + header.size() - 2, data.size(), 16).ptr;
    *end++ = '\r';
    *end++ = '\n';

    return _send(header.data(), static_cast<size_t>(end - header.data()))
        .and_then([&] { return _send(reinterpret_cast<const char*>(data.data()), data.size()); })
        .and_then([&] { return _send("\r\n", 2); });
}

result<void> client::chunk_writer::try_write(std::string_view data) {
    return try_write(std::span<const uint8_t>{reinterpret_cast<const uint8_t*>(data.data()), data.size()});
}

result<void> client::chunk_writer::try_finish() {
    if (!_client || !_client->_handle || _stage != stage::open) {
        return error(idfxx::errc::invalid_state);
    }
    constexpr std::string_view terminator = "0\r\n\r\n";
    return _send(terminator.data(), terminator.size()).transform([this] { _stage = stage::finished; });
}

// =========================================================================
// Response Accessors
// =========================================================================

int client::status_code() const {
    if (!_handle) {
        return 0;
    }
    return esp_http_client_get_status_code(_handle);
}

int64_t client::content_length() const {
    if (!_handle) {
        return -1;
    }
    return esp_http_client_get_content_length(_handle);
}

result<int> client::try_chunk_length() const {
    if (!_handle) {
        return error(idfxx::errc::invalid_state);
    }
    int len = 0;
    auto err = esp_http_client_get_chunk_length(_handle, &len);
    if (err == ESP_OK) {
        return len;
    }
    if (err == ESP_FAIL) {
        return error(idfxx::errc::not_supported);
    }
    ESP_LOGD(TAG, "chunk_length failed: %s", esp_err_to_name(err));
    return http_client_error(err);
}

bool client::is_chunked_response() const {
    if (!_handle) {
        return false;
    }
    return esp_http_client_is_chunked_response(_handle);
}

std::optional<std::string> client::get_header(std::string_view key) const {
    if (!_handle) {
        return std::nullopt;
    }
    std::string key_str{key};
    char* value = nullptr;
    esp_err_t err = esp_http_client_get_header(_handle, key_str.c_str(), &value);
    if (err != ESP_OK || !value) {
        return std::nullopt;
    }
    return std::string(value);
}

std::string client::get_url() const {
    if (!_handle) {
        return "";
    }
    char buf[512];
    esp_http_client_get_url(_handle, buf, sizeof(buf));
    return std::string(buf);
}

// =========================================================================
// Redirect Control
// =========================================================================

result<void> client::try_set_redirection() {
    if (!_handle) {
        return error(idfxx::errc::invalid_state);
    }
    esp_err_t err = esp_http_client_set_redirection(_handle);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "set_redirection failed: %s", esp_err_to_name(err));
        return http_client_error(err);
    }
    return {};
}

void client::reset_redirect_counter() {
    if (!_handle) {
        return;
    }
    esp_http_client_reset_redirect_counter(_handle);
}

// =========================================================================
// Request Control
// =========================================================================

result<void> client::try_cancel_request() {
    if (!_handle) {
        return error(idfxx::errc::invalid_state);
    }
    esp_err_t err = esp_http_client_cancel_request(_handle);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "cancel_request failed: %s", esp_err_to_name(err));
        return http_client_error(err);
    }
    _request_incomplete = false;
    return {};
}

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
client::state client::get_state() const {
    if (!_handle) {
        return state::uninit;
    }
    return static_cast<state>(esp_http_client_get_state(_handle));
}
#endif

} // namespace idfxx::http
