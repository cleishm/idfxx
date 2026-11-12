// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

// Internal helpers shared by the idfxx_crypto implementation files.

#include <idfxx/crypto/error>
#include <idfxx/error>

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

// Initializes the PSA Crypto library on first use. Must be called before any other PSA function.
[[nodiscard]] result<void> psa_init();

} // namespace idfxx::crypto::detail
