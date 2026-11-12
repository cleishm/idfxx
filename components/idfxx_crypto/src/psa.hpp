// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

// Internal helpers shared by the idfxx_crypto implementation files.

#include <idfxx/byte_view>
#include <idfxx/crypto/error>
#include <idfxx/error>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <psa/crypto.h>
#include <system_error>

namespace idfxx::crypto::detail {

// Translates a PSA status to an idfxx::crypto error code. PSA_SUCCESS maps to an empty
// error_code, statuses without a matching errc to errc::generic_error.
// PSA_ERROR_INSUFFICIENT_MEMORY is never returned: it raises (bad_alloc or abort), per the idfxx
// out-of-memory policy.
[[nodiscard]] std::error_code psa_error_code(psa_status_t status);

// Converts a PSA status to a result: success for PSA_SUCCESS, otherwise its error code.
[[nodiscard]] inline result<void> psa_wrap(psa_status_t status) {
    if (status != PSA_SUCCESS) {
        return error(psa_error_code(status));
    }
    return {};
}

// Interprets the status of a PSA check of a tag or digest: a mismatch is false, not an error.
[[nodiscard]] inline result<bool> verify_result(psa_status_t status) {
    if (status == PSA_ERROR_INVALID_SIGNATURE) {
        return false;
    }
    return psa_wrap(status).transform([] { return true; });
}

// Initializes the PSA Crypto library on first use. Must be called before any other PSA function.
[[nodiscard]] result<void> psa_init();

// Views a byte buffer as the uint8_t buffer that PSA functions take.
[[nodiscard]] inline const uint8_t* as_psa(const std::byte* p) noexcept {
    return reinterpret_cast<const uint8_t*>(p);
}

// Views a byte buffer as the uint8_t buffer that PSA functions take.
[[nodiscard]] inline uint8_t* as_psa(std::byte* p) noexcept {
    return reinterpret_cast<uint8_t*>(p);
}

// Passes data to `update`, a callable taking (const uint8_t*, size_t) and returning a psa_status_t,
// as PSA multi-part operations take input: in one call where PSA uses the caller's buffer directly,
// and otherwise in chunks of 4096 bytes (none for empty data). Stops at the first failure and
// returns its status.
template<typename Update>
[[nodiscard]] psa_status_t feed_in_chunks(byte_view data, Update&& update) {
#ifdef MBEDTLS_PSA_ASSUME_EXCLUSIVE_BUFFERS
    return update(as_psa(data.data()), data.size());
#else
    // Without MBEDTLS_PSA_ASSUME_EXCLUSIVE_BUFFERS (mbedTLS 3.6, ESP-IDF 5.5), each PSA call copies
    // its whole input to the heap first. Input is fed in chunks of this size to bound that copy.
    constexpr std::size_t chunk_size = 4096;
    for (std::size_t pos = 0; pos < data.size(); pos += chunk_size) {
        const std::size_t n = std::min(chunk_size, data.size() - pos);
        psa_status_t status = update(as_psa(data.data() + pos), n);
        if (status != PSA_SUCCESS) {
            return status;
        }
    }
    return PSA_SUCCESS;
#endif
}

} // namespace idfxx::crypto::detail
