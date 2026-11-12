// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#include "psa.hpp"

#include <idfxx/crypto/mac>

#include <array>
#include <cstddef>
#include <cstdint>
#include <mbedtls/platform_util.h>
#include <memory>
#include <psa/crypto.h>
#include <utility>

// mbedtls/constant_time.h has no C++ linkage guard
extern "C" {
#include <mbedtls/constant_time.h>
}

namespace idfxx::crypto::mac {

using detail::as_psa;

namespace {

// The tag length of a PSA MAC algorithm, in bytes: its truncated length, or the full length of an
// untruncated algorithm. 0 if the algorithm is not a MAC algorithm, or one of unknown length.
constexpr std::size_t mac_tag_size(psa_algorithm_t alg) noexcept {
    if (!PSA_ALG_IS_MAC(alg)) {
        return 0;
    }
    std::size_t truncated = PSA_MAC_TRUNCATED_LENGTH(alg);
    return truncated != 0 ? truncated : detail::mac_full_tag_size(alg);
}

// Whether a key permits a call: the call's tag length if it does, 0 (errc::not_permitted) if not.
// `alg` and `usage` are the key's; the call needs any one of the usages in `needed`. Decided here
// rather than relying on how the PSA implementation reports it, so that a key permitted no MAC
// algorithm, or without the usage, is refused before the tag or buffer passed is looked at.
std::size_t
permitted_tag_size(psa_algorithm_t alg, flags<key_usage> usage, flags<key_usage> needed) noexcept {
    return usage.contains_any(needed) ? mac_tag_size(alg) : 0;
}

} // namespace

struct operation::state {
    // PSA fixes an operation's direction at setup: it computes tags (`sign`) or checks them.
    state(const key& k, bool sign) noexcept
        : alg(k.algorithm().idf_algorithm())
        , key_id(k.idf_handle())
        , usage(k.usage())
        , signing(sign) {}

    ~state() { discard(); }

    state(const state&) = delete;
    state& operator=(const state&) = delete;

    // Sets up the PSA operation on first use, so that an idle operation holds no PSA resources (on
    // the ESP32, an HMAC operation that is set up holds the SHA engine).
    [[nodiscard]] result<void> start() {
        if (active) {
            return {};
        }
        psa_status_t status =
            signing ? psa_mac_sign_setup(&op, key_id, alg) : psa_mac_verify_setup(&op, key_id, alg);
        if (auto r = detail::psa_wrap(status); !r) {
            psa_mac_abort(&op);
            return r;
        }
        active = true;
        return {};
    }

    // Adds data to the message, starting the operation if needed. On failure, discards the message.
    [[nodiscard]] result<void> update(byte_view data) {
        if (auto r = start(); !r) {
            return r;
        }
        psa_status_t status = detail::feed_in_chunks(data, [this](const uint8_t* p, std::size_t n) {
            return psa_mac_update(&op, p, n);
        });
        if (status != PSA_SUCCESS) {
            discard();
        }
        return detail::psa_wrap(status);
    }

    // Completes the message of an operation that computes tags, writing its tag to `out` (exactly
    // the tag size), and returns the operation to its initial state.
    [[nodiscard]] result<void> finish(byte_span out) {
        if (auto r = start(); !r) {
            return r;
        }
        size_t length = 0;
        psa_status_t status = psa_mac_sign_finish(&op, as_psa(out.data()), out.size(), &length);
        discard();
        return detail::psa_wrap(status);
    }

    // Completes the message, checks its tag against `expected` (of the tag size), and returns the
    // operation to its initial state.
    [[nodiscard]] result<bool> verify(byte_view expected) {
        if (auto r = start(); !r) {
            return error(r.error());
        }
        if (!signing) {
            psa_status_t status =
                psa_mac_verify_finish(&op, as_psa(expected.data()), expected.size());
            discard();
            return detail::verify_result(status);
        }
        // An operation set up to compute a tag can't check one: compute it and compare.
        std::array<uint8_t, PSA_MAC_MAX_SIZE> actual;
        size_t length = 0;
        psa_status_t status = psa_mac_sign_finish(&op, actual.data(), actual.size(), &length);
        bool match = status == PSA_SUCCESS && length == expected.size() &&
            mbedtls_ct_memcmp(actual.data(), expected.data(), length) == 0;
        mbedtls_platform_zeroize(actual.data(), actual.size());
        discard();
        return detail::psa_wrap(status).transform([match] { return match; });
    }

    // Returns the operation to its initial state, ready for a new message.
    void discard() noexcept {
        if (active) {
            psa_mac_abort(&op);
            active = false;
        }
    }

    psa_algorithm_t alg;
    // A key's ID and policy don't change, and stay with the key when it is moved
    psa_key_id_t key_id;
    flags<key_usage> usage;
    bool signing;
    psa_mac_operation_t op = psa_mac_operation_init();
    bool active = false;
};

operation::operation(std::unique_ptr<state> s) noexcept
    : _state(std::move(s)) {}

operation::~operation() = default;
operation::operation(operation&&) noexcept = default;
operation& operation::operator=(operation&&) noexcept = default;

result<operation> operation::make(const key& k) {
    if (permitted_tag_size(
            k.algorithm().idf_algorithm(),
            k.usage(),
            key_usage::sign_message | key_usage::verify_message
        ) == 0) {
        return error(errc::not_permitted);
    }
    // Key creation refused a key type the algorithm doesn't take. Anything else only the crypto
    // library can decide (such as a key size the hardware lacks) is reported on first use, so
    // that an idle operation holds no PSA resources. The operation computes a tag if the key
    // permits that, and otherwise checks one.
    return operation{std::make_unique<state>(k, k.usage().contains(key_usage::sign_message))};
}

// mac::operation uses whatever MAC algorithm a key permits: an authenticator accepts only keys
// permitted its own algorithm and tag length, as the crypto library's policy check would.
result<operation> operation::_make_as(key_algorithm alg, const key& k) {
    if (k.algorithm() != alg) {
        return error(errc::not_permitted);
    }
    return make(k);
}

result<byte_span>
operation::_try_compute_as(key_algorithm alg, const key& k, byte_view data, byte_span tag) {
    if (k.algorithm() != alg) {
        return error(errc::not_permitted);
    }
    return try_compute(k, data, tag);
}

result<bool>
operation::_try_compare_as(key_algorithm alg, const key& k, byte_view data, byte_view expected) {
    if (k.algorithm() != alg) {
        return error(errc::not_permitted);
    }
    return try_compare(k, data, expected);
}

mac::algorithm operation::algorithm() const noexcept {
    return static_cast<mac::algorithm>(PSA_ALG_FULL_LENGTH_MAC(_state->alg));
}

std::size_t operation::tag_size() const noexcept {
    return mac_tag_size(_state->alg);
}

result<void> operation::try_update(byte_view data) {
    return _state->update(data);
}

result<byte_span> operation::try_finish(byte_span tag) {
    const std::size_t size =
        permitted_tag_size(_state->alg, _state->usage, key_usage::sign_message);
    if (size == 0) {
        _state->discard();
        return error(errc::not_permitted);
    }
    // Checked before PSA sees the buffer: PSA would end the operation, discarding the message.
    if (tag.size() < size) {
        return error(errc::buffer_too_small);
    }
    auto out = tag.first(size);
    if (auto r = _state->finish(out); !r) {
        return error(r.error());
    }
    return out;
}

result<bool> operation::try_verify(byte_view expected) {
    const std::size_t size =
        permitted_tag_size(_state->alg, _state->usage, key_usage::verify_message);
    if (size == 0) {
        _state->discard();
        return error(errc::not_permitted);
    }
    // A tag of the wrong length cannot match. Decide it here, before any setup, rather than
    // relying on how the PSA implementation reports it.
    if (expected.size() != size) {
        _state->discard();
        return false;
    }
    return _state->verify(expected);
}

void operation::reset() noexcept {
    _state->discard();
}

result<byte_span> operation::try_compute(const key& k, byte_view data, byte_span tag) {
    const psa_algorithm_t alg = k.algorithm().idf_algorithm();
    const std::size_t size = permitted_tag_size(alg, k.usage(), key_usage::sign_message);
    if (size == 0) {
        return error(errc::not_permitted);
    }
    if (tag.size() < size) {
        return error(errc::buffer_too_small);
    }
    // Give PSA no more output buffer than the tag needs.
    auto out = tag.first(size);
#ifdef MBEDTLS_PSA_ASSUME_EXCLUSIVE_BUFFERS
    size_t length = 0;
    auto r = detail::psa_wrap(psa_mac_compute(
        k.idf_handle(),
        alg,
        as_psa(data.data()),
        data.size(),
        as_psa(out.data()),
        out.size(),
        &length
    ));
#else
    state s{k, true};
    auto r = s.update(data).and_then([&] { return s.finish(out); });
#endif
    if (!r) {
        return error(r.error());
    }
    return out;
}

result<bool> operation::try_compare(const key& k, byte_view data, byte_view expected) {
    const psa_algorithm_t alg = k.algorithm().idf_algorithm();
    const std::size_t size = permitted_tag_size(alg, k.usage(), key_usage::verify_message);
    if (size == 0) {
        return error(errc::not_permitted);
    }
    // A tag of the wrong length cannot match. Decide it here rather than relying on how the PSA
    // implementation reports it.
    if (expected.size() != size) {
        return false;
    }
#ifdef MBEDTLS_PSA_ASSUME_EXCLUSIVE_BUFFERS
    return detail::verify_result(psa_mac_verify(
        k.idf_handle(),
        alg,
        as_psa(data.data()),
        data.size(),
        as_psa(expected.data()),
        expected.size()
    ));
#else
    state s{k, false};
    if (auto r = s.update(data); !r) {
        return error(r.error());
    }
    return s.verify(expected);
#endif
}

} // namespace idfxx::crypto::mac
