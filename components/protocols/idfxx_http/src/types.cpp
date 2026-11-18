// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#include <idfxx/http/types>

#include <esp_http_client.h>
#include <utility>

// Verify method enum values match ESP-IDF constants
static_assert(std::to_underlying(idfxx::http::method::get) == HTTP_METHOD_GET);
static_assert(std::to_underlying(idfxx::http::method::post) == HTTP_METHOD_POST);
static_assert(std::to_underlying(idfxx::http::method::put) == HTTP_METHOD_PUT);
static_assert(std::to_underlying(idfxx::http::method::patch) == HTTP_METHOD_PATCH);
static_assert(std::to_underlying(idfxx::http::method::delete_) == HTTP_METHOD_DELETE);
static_assert(std::to_underlying(idfxx::http::method::head) == HTTP_METHOD_HEAD);
static_assert(std::to_underlying(idfxx::http::method::notify) == HTTP_METHOD_NOTIFY);
static_assert(std::to_underlying(idfxx::http::method::subscribe) == HTTP_METHOD_SUBSCRIBE);
static_assert(std::to_underlying(idfxx::http::method::unsubscribe) == HTTP_METHOD_UNSUBSCRIBE);
static_assert(std::to_underlying(idfxx::http::method::options) == HTTP_METHOD_OPTIONS);
static_assert(std::to_underlying(idfxx::http::method::copy) == HTTP_METHOD_COPY);
static_assert(std::to_underlying(idfxx::http::method::move) == HTTP_METHOD_MOVE);
static_assert(std::to_underlying(idfxx::http::method::lock) == HTTP_METHOD_LOCK);
static_assert(std::to_underlying(idfxx::http::method::unlock) == HTTP_METHOD_UNLOCK);
static_assert(std::to_underlying(idfxx::http::method::propfind) == HTTP_METHOD_PROPFIND);
static_assert(std::to_underlying(idfxx::http::method::proppatch) == HTTP_METHOD_PROPPATCH);
static_assert(std::to_underlying(idfxx::http::method::mkcol) == HTTP_METHOD_MKCOL);
static_assert(std::to_underlying(idfxx::http::method::report) == HTTP_METHOD_REPORT);

// Verify auth_type enum values match ESP-IDF constants
static_assert(std::to_underlying(idfxx::http::auth_type::none) == HTTP_AUTH_TYPE_NONE);
static_assert(std::to_underlying(idfxx::http::auth_type::basic) == HTTP_AUTH_TYPE_BASIC);
static_assert(std::to_underlying(idfxx::http::auth_type::digest) == HTTP_AUTH_TYPE_DIGEST);

// Verify transport enum values match ESP-IDF constants
static_assert(std::to_underlying(idfxx::http::transport::unknown) == HTTP_TRANSPORT_UNKNOWN);
static_assert(std::to_underlying(idfxx::http::transport::tcp) == HTTP_TRANSPORT_OVER_TCP);
static_assert(std::to_underlying(idfxx::http::transport::ssl) == HTTP_TRANSPORT_OVER_SSL);

namespace idfxx {

std::string to_string(http::method m) {
    switch (m) {
    case http::method::get:
        return "GET";
    case http::method::post:
        return "POST";
    case http::method::put:
        return "PUT";
    case http::method::patch:
        return "PATCH";
    case http::method::delete_:
        return "DELETE";
    case http::method::head:
        return "HEAD";
    case http::method::notify:
        return "NOTIFY";
    case http::method::subscribe:
        return "SUBSCRIBE";
    case http::method::unsubscribe:
        return "UNSUBSCRIBE";
    case http::method::options:
        return "OPTIONS";
    case http::method::copy:
        return "COPY";
    case http::method::move:
        return "MOVE";
    case http::method::lock:
        return "LOCK";
    case http::method::unlock:
        return "UNLOCK";
    case http::method::propfind:
        return "PROPFIND";
    case http::method::proppatch:
        return "PROPPATCH";
    case http::method::mkcol:
        return "MKCOL";
    case http::method::report:
        return "REPORT";
    default:
        return "unknown(" + std::to_string(static_cast<int>(m)) + ")";
    }
}

std::string to_string(http::auth_type t) {
    switch (t) {
    case http::auth_type::none:
        return "NONE";
    case http::auth_type::basic:
        return "BASIC";
    case http::auth_type::digest:
        return "DIGEST";
    default:
        return "unknown(" + std::to_string(static_cast<int>(t)) + ")";
    }
}

std::string to_string(http::transport t) {
    switch (t) {
    case http::transport::unknown:
        return "UNKNOWN";
    case http::transport::tcp:
        return "TCP";
    case http::transport::ssl:
        return "SSL";
    default:
        return "unknown(" + std::to_string(static_cast<int>(t)) + ")";
    }
}

} // namespace idfxx
