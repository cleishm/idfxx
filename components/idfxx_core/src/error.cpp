// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#include <idfxx/error>

#include <esp_err.h>

namespace idfxx {

const error_category& default_category() noexcept {
    static const error_category instance{};
    return instance;
}

const char* error_category::name() const noexcept {
    return "idfxx::Error";
}

std::string error_category::message(int ec) const {
    switch (errc(ec)) {
    case errc::fail:
        return "Generic failure";
    case errc::no_mem:
        return "Out of memory";
    case errc::invalid_arg:
        return "Invalid argument";
    case errc::invalid_state:
        return "Invalid state";
    case errc::invalid_size:
        return "Invalid size";
    case errc::not_found:
        return "Requested resource not found";
    case errc::not_supported:
        return "Operation or feature not supported";
    case errc::timeout:
        return "Operation timed out";
    case errc::invalid_response:
        return "Received response was invalid";
    case errc::invalid_crc:
        return "CRC or checksum was invalid";
    case errc::invalid_version:
        return "Version was invalid";
    case errc::invalid_mac:
        return "MAC address was invalid";
    case errc::not_finished:
        return "Operation has not fully completed";
    case errc::not_allowed:
        return "Operation is not allowed";
    default:
        return esp_err_to_name(static_cast<esp_err_t>(ec));
    }
}

static errc make_errc(esp_err_t e) noexcept {
    switch (e) {
    case ESP_ERR_NO_MEM:
        return errc::no_mem;
    case ESP_ERR_INVALID_ARG:
        return errc::invalid_arg;
    case ESP_ERR_INVALID_STATE:
        return errc::invalid_state;
    case ESP_ERR_INVALID_SIZE:
        return errc::invalid_size;
    case ESP_ERR_NOT_FOUND:
        return errc::not_found;
    case ESP_ERR_NOT_SUPPORTED:
        return errc::not_supported;
    case ESP_ERR_TIMEOUT:
        return errc::timeout;
    case ESP_ERR_INVALID_RESPONSE:
        return errc::invalid_response;
    case ESP_ERR_INVALID_CRC:
        return errc::invalid_crc;
    case ESP_ERR_INVALID_VERSION:
        return errc::invalid_version;
    case ESP_ERR_INVALID_MAC:
        return errc::invalid_mac;
    case ESP_ERR_NOT_FINISHED:
        return errc::not_finished;
    case ESP_ERR_NOT_ALLOWED:
        return errc::not_allowed;
    default:
        return errc::fail; // Map unknown errors to generic fail
    }
}

std::error_code make_error_code(esp_err_t e) noexcept {
    return make_error_code(make_errc(e));
}

} // namespace idfxx
