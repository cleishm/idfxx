// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#include "psa.hpp"

#include <idfxx/crypto/error>

#include <atomic>
#include <mutex>
#include <psa/crypto.h>
#include <string>
#include <system_error>

namespace idfxx {

const crypto::error_category& crypto_category() noexcept {
    static const crypto::error_category instance{};
    return instance;
}

const char* crypto::error_category::name() const noexcept {
    return "crypto::Error";
}

std::string crypto::error_category::message(int ec) const {
    switch (crypto::errc(ec)) {
    case crypto::errc::generic_error:
        return "Generic cryptography error";
    case crypto::errc::not_supported:
        return "Operation or parameter not supported";
    case crypto::errc::not_permitted:
        return "Operation not permitted";
    case crypto::errc::buffer_too_small:
        return "Output buffer too small";
    case crypto::errc::already_exists:
        return "Item already exists";
    case crypto::errc::does_not_exist:
        return "Item does not exist";
    case crypto::errc::bad_state:
        return "Operation invalid in current state";
    case crypto::errc::invalid_argument:
        return "Invalid argument";
    case crypto::errc::insufficient_storage:
        return "Insufficient persistent storage";
    case crypto::errc::communication_failure:
        return "Communication with cryptoprocessor failed";
    case crypto::errc::storage_failure:
        return "Persistent storage failure";
    case crypto::errc::data_corrupt:
        return "Stored data corrupted";
    case crypto::errc::data_invalid:
        return "Stored data invalid";
    case crypto::errc::hardware_failure:
        return "Hardware failure";
    case crypto::errc::corruption_detected:
        return "Tampering detected";
    case crypto::errc::insufficient_entropy:
        return "Insufficient entropy";
    case crypto::errc::invalid_signature:
        return "Invalid signature";
    case crypto::errc::invalid_padding:
        return "Invalid padding";
    case crypto::errc::insufficient_data:
        return "Insufficient data";
    case crypto::errc::invalid_handle:
        return "Invalid handle";
    }
    return "Unknown crypto error (" + std::to_string(ec) + ")";
}

std::error_condition crypto::error_category::default_error_condition(int code) const noexcept {
    switch (crypto::errc(code)) {
    case crypto::errc::not_supported:
        return std::make_error_condition(std::errc::not_supported);
    case crypto::errc::not_permitted:
        return std::make_error_condition(std::errc::operation_not_permitted);
    case crypto::errc::buffer_too_small:
        return std::make_error_condition(std::errc::value_too_large);
    case crypto::errc::already_exists:
        return std::make_error_condition(std::errc::file_exists);
    case crypto::errc::does_not_exist:
        return std::make_error_condition(std::errc::no_such_file_or_directory);
    case crypto::errc::invalid_argument:
        return std::make_error_condition(std::errc::invalid_argument);
    case crypto::errc::insufficient_storage:
        return std::make_error_condition(std::errc::no_space_on_device);
    case crypto::errc::storage_failure:
    case crypto::errc::hardware_failure:
        return std::make_error_condition(std::errc::io_error);
    case crypto::errc::invalid_handle:
        return std::make_error_condition(std::errc::bad_file_descriptor);
    default:
        return {code, *this};
    }
}

namespace crypto::detail {

std::error_code psa_error_code(psa_status_t status) {
    switch (status) {
    case PSA_SUCCESS:
        return {};
    case PSA_ERROR_INSUFFICIENT_MEMORY:
        raise_no_mem();
    case PSA_ERROR_NOT_SUPPORTED:
        return make_error_code(errc::not_supported);
    case PSA_ERROR_NOT_PERMITTED:
        return make_error_code(errc::not_permitted);
    case PSA_ERROR_BUFFER_TOO_SMALL:
        return make_error_code(errc::buffer_too_small);
    case PSA_ERROR_ALREADY_EXISTS:
        return make_error_code(errc::already_exists);
    case PSA_ERROR_DOES_NOT_EXIST:
        return make_error_code(errc::does_not_exist);
    case PSA_ERROR_BAD_STATE:
        return make_error_code(errc::bad_state);
    case PSA_ERROR_INVALID_ARGUMENT:
        return make_error_code(errc::invalid_argument);
    case PSA_ERROR_INSUFFICIENT_STORAGE:
        return make_error_code(errc::insufficient_storage);
    case PSA_ERROR_COMMUNICATION_FAILURE:
        return make_error_code(errc::communication_failure);
    case PSA_ERROR_STORAGE_FAILURE:
        return make_error_code(errc::storage_failure);
    case PSA_ERROR_DATA_CORRUPT:
        return make_error_code(errc::data_corrupt);
    case PSA_ERROR_DATA_INVALID:
        return make_error_code(errc::data_invalid);
    case PSA_ERROR_HARDWARE_FAILURE:
        return make_error_code(errc::hardware_failure);
    case PSA_ERROR_CORRUPTION_DETECTED:
        return make_error_code(errc::corruption_detected);
    case PSA_ERROR_INSUFFICIENT_ENTROPY:
        return make_error_code(errc::insufficient_entropy);
    case PSA_ERROR_INVALID_SIGNATURE:
        return make_error_code(errc::invalid_signature);
    case PSA_ERROR_INVALID_PADDING:
        return make_error_code(errc::invalid_padding);
    case PSA_ERROR_INSUFFICIENT_DATA:
        return make_error_code(errc::insufficient_data);
    case PSA_ERROR_INVALID_HANDLE:
        return make_error_code(errc::invalid_handle);
    default:
        return make_error_code(errc::generic_error);
    }
}

result<void> psa_init() {
    // ESP-IDF 5.5 doesn't initialize PSA Crypto at startup (6.0 and later do, in
    // esp_psa_crypto_init.c), so it is initialized here on first use. psa_crypto_init() is
    // idempotent, but 5.5's is not thread-safe (MBEDTLS_THREADING_C is off by default), so calls
    // are serialized until one succeeds. A failure is not cached, so the next call tries again;
    // hence this is not std::call_once.
    static std::atomic<bool> initialized{false};
    if (initialized.load(std::memory_order_acquire)) {
        return {};
    }
    static std::mutex init_mutex;
    std::lock_guard lock{init_mutex};
    if (!initialized.load(std::memory_order_relaxed)) {
        psa_status_t status = psa_crypto_init();
        if (status != PSA_SUCCESS) {
            return error(psa_error_code(status));
        }
        initialized.store(true, std::memory_order_release);
    }
    return {};
}

} // namespace crypto::detail

} // namespace idfxx
