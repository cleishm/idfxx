// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Unit tests for a failed update of a hasher or MAC operation: the error is reported, and the
// object is ready for a new message.
//
// No input makes the crypto library's update fail, so the test app links with
// -Wl,--wrap=psa_hash_update and -Wl,--wrap=psa_mac_update (see link_options.cmake). The wrappers
// below pass every call through to the library, except the one a test arms them to fail.

#include "crypto_test_utils.hpp"
#include "idfxx/crypto/hash"
#include "test_cleanup.hpp"
#include "unity.h"

#include <cstddef>
#include <cstdint>
#include <psa/crypto.h>
#include <sdkconfig.h>
#include <string_view>

#ifdef CONFIG_MBEDTLS_THREADING_C
#include "idfxx/crypto/mac"
#endif

using namespace idfxx;
using namespace idfxx_crypto_test;

namespace {

// A failure to inject: the wrapped function fails its fail_call-th call since being armed (none
// when 0), as the library does on a fault, and counts the calls it has seen.
struct injected_failure {
    unsigned fail_call = 0;
    unsigned calls = 0;
    bool fired = false;
};

injected_failure hash_update_failure;
injected_failure mac_update_failure;

void disarm_failures() {
    hash_update_failure = {};
    mac_update_failure = {};
}

// Arms f to fail the n-th call from now, until the end of the test.
void fail_call(injected_failure& f, unsigned n) {
    idfxx::test::run_after_each_test(&disarm_failures);
    f = {.fail_call = n};
}

// Counts a call, and returns whether to fail it.
bool should_fail(injected_failure& f) {
    if (++f.calls != f.fail_call) {
        return false;
    }
    f.fail_call = 0;
    f.fired = true;
    return true;
}

// The update calls the crypto library sees for one update of n bytes: one, or one per 4096-byte
// chunk where the library would otherwise copy the whole input to the heap (ESP-IDF 5.5).
constexpr unsigned library_calls(std::size_t n) {
#ifdef MBEDTLS_PSA_ASSUME_EXCLUSIVE_BUFFERS
    return n > 0 ? 1 : 0;
#else
    return static_cast<unsigned>((n + 4095) / 4096);
#endif
}

constexpr std::string_view msg_abc = "abc";
constexpr std::size_t chunked_size = 3 * 4096 + 7;

// Digests from Python's hashlib, and tags under the key "Jefe" from Python's hmac module, of "abc"
// and patterned_message(chunked_size)
constexpr const char* sha256_abc =
    "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
constexpr const char* sha256_chunked =
    "a8663d3afd009279950f853aed4c16ada995594511282e538274159965db9967";

} // namespace

extern "C" {

psa_status_t
__real_psa_hash_update(psa_hash_operation_t* operation, const uint8_t* input, size_t length);

psa_status_t
__wrap_psa_hash_update(psa_hash_operation_t* operation, const uint8_t* input, size_t length) {
    if (should_fail(hash_update_failure)) {
        // As the library does when an update fails
        psa_hash_abort(operation);
        return PSA_ERROR_HARDWARE_FAILURE;
    }
    return __real_psa_hash_update(operation, input, length);
}

// Defined whether or not the MAC tests are built: the link option that redirects every call to it
// is not conditional
psa_status_t
__real_psa_mac_update(psa_mac_operation_t* operation, const uint8_t* input, size_t length);

psa_status_t
__wrap_psa_mac_update(psa_mac_operation_t* operation, const uint8_t* input, size_t length) {
    if (should_fail(mac_update_failure)) {
        psa_mac_abort(operation);
        return PSA_ERROR_HARDWARE_FAILURE;
    }
    return __real_psa_mac_update(operation, input, length);
}

} // extern "C"

TEST_CASE("crypto hasher is ready for a new message after a failed update", "[idfxx][crypto]") {
    crypto::hash::sha256 h;
    ok(h.try_update(msg_abc)); // discarded by the failure
    fail_call(hash_update_failure, 1);
    fails_with(crypto::errc::hardware_failure, h.try_update(msg_abc));
    TEST_ASSERT_TRUE(hash_update_failure.fired);

    ok(h.try_update(msg_abc));
    TEST_ASSERT_DIGEST(sha256_abc, ok(h.try_finish()));
}

TEST_CASE(
    "crypto hasher is ready for a new message after an update fails on a later chunk",
    "[idfxx][crypto]"
) {
    auto msg = patterned_message(chunked_size);
    constexpr unsigned calls = library_calls(chunked_size);
    crypto::hash::sha256 h;
    ok(h.try_update(msg_abc)); // discarded by the failure
    fail_call(hash_update_failure, calls); // the last chunk
    fails_with(crypto::errc::hardware_failure, h.try_update(msg));
    TEST_ASSERT_TRUE(hash_update_failure.fired);
    TEST_ASSERT_EQUAL_UINT32(calls, hash_update_failure.calls);

    ok(h.try_update(msg));
    TEST_ASSERT_DIGEST(sha256_chunked, ok(h.try_finish()));
}

#ifdef CONFIG_MBEDTLS_THREADING_C

namespace {
constexpr const char* hmac_sha256_abc =
    "7cf4ec4f741f51cb0d887013c46251d6f4175643c4f422906a1aaec688cc13e8";
constexpr const char* hmac_sha256_chunked =
    "89d35b67a34670fbc3c54709d55a56595eb68b3cd80222d16147368327194e5f";
} // namespace

TEST_CASE(
    "crypto authenticator is ready for a new message after a failed update", "[idfxx][crypto]"
) {
    auto k = ok(crypto::mac::hmac_sha256::try_import_key(bytes_of(std::string_view{"Jefe"})));
    auto m = ok(crypto::mac::hmac_sha256::make(k));
    ok(m.try_update(msg_abc)); // discarded by the failure
    fail_call(mac_update_failure, 1);
    fails_with(crypto::errc::hardware_failure, m.try_update(msg_abc));
    TEST_ASSERT_TRUE(mac_update_failure.fired);

    ok(m.try_update(msg_abc));
    TEST_ASSERT_TAG(hmac_sha256_abc, ok(m.try_finish()));
}

TEST_CASE(
    "crypto authenticator is ready for a new message after an update fails on a later chunk",
    "[idfxx][crypto]"
) {
    auto k = ok(crypto::mac::hmac_sha256::try_import_key(bytes_of(std::string_view{"Jefe"})));
    auto msg = patterned_message(chunked_size);
    constexpr unsigned calls = library_calls(chunked_size);
    auto m = ok(crypto::mac::hmac_sha256::make(k));
    ok(m.try_update(msg_abc)); // discarded by the failure
    fail_call(mac_update_failure, calls); // the last chunk
    fails_with(crypto::errc::hardware_failure, m.try_update(msg));
    TEST_ASSERT_TRUE(mac_update_failure.fired);
    TEST_ASSERT_EQUAL_UINT32(calls, mac_update_failure.calls);

    ok(m.try_update(msg));
    TEST_ASSERT_TAG(hmac_sha256_chunked, ok(m.try_finish()));
}

#endif // CONFIG_MBEDTLS_THREADING_C
