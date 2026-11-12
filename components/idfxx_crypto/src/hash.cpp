// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#include "psa.hpp"

#include <idfxx/crypto/hash>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <psa/crypto.h>

namespace idfxx::crypto::hash {

namespace {

const uint8_t* as_psa(const std::byte* p) noexcept {
    return reinterpret_cast<const uint8_t*>(p);
}

uint8_t* as_psa(std::byte* p) noexcept {
    return reinterpret_cast<uint8_t*>(p);
}

#ifndef MBEDTLS_PSA_ASSUME_EXCLUSIVE_BUFFERS
// Without MBEDTLS_PSA_ASSUME_EXCLUSIVE_BUFFERS (mbedTLS 3.6, ESP-IDF 5.5), each PSA call copies its
// whole input to the heap first. Input is fed in chunks of this size to bound that copy.
constexpr std::size_t input_chunk_size = 4096;
#endif

// Interprets the status of a PSA digest check: a mismatch is false, not an error.
result<bool> verify_result(psa_status_t status) {
    if (status == PSA_ERROR_INVALID_SIGNATURE) {
        return false;
    }
    return detail::psa_wrap(status).transform([] { return true; });
}

} // namespace

struct operation::state {
    explicit state(hash::algorithm a) noexcept
        : alg(static_cast<psa_algorithm_t>(a)) {}

    ~state() { discard(); }

    state(const state&) = delete;
    state& operator=(const state&) = delete;

    // Checks that an algorithm can be used, by setting up an operation for it and aborting it.
    [[nodiscard]] static result<void> probe(hash::algorithm a) {
        state s{a};
        return s.start();
    }

    // Sets up the PSA operation on first use, so that an unused operation holds no PSA resources.
    [[nodiscard]] result<void> start() {
        if (active) {
            return {};
        }
        if (auto r = detail::psa_init(); !r) {
            return r;
        }
        if (auto r = detail::psa_wrap(psa_hash_setup(&op, alg)); !r) {
            psa_hash_abort(&op);
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
#ifdef MBEDTLS_PSA_ASSUME_EXCLUSIVE_BUFFERS
        psa_status_t status = psa_hash_update(&op, as_psa(data.data()), data.size());
#else
        psa_status_t status = PSA_SUCCESS;
        for (std::size_t pos = 0; status == PSA_SUCCESS && pos < data.size();
             pos += input_chunk_size) {
            const std::size_t n = std::min(input_chunk_size, data.size() - pos);
            status = psa_hash_update(&op, as_psa(data.data() + pos), n);
        }
#endif
        if (status != PSA_SUCCESS) {
            discard();
        }
        return detail::psa_wrap(status);
    }

    // Completes the message, writing its digest to `out` (exactly the digest size), and returns the
    // operation to its initial state.
    [[nodiscard]] result<void> finish(byte_span out) {
        if (auto r = start(); !r) {
            return r;
        }
        size_t length = 0;
        psa_status_t status = psa_hash_finish(&op, as_psa(out.data()), out.size(), &length);
        discard();
        return detail::psa_wrap(status);
    }

    // Completes the message, checks its digest against `expected` (of the digest size), and
    // returns the operation to its initial state.
    [[nodiscard]] result<bool> verify(byte_view expected) {
        if (auto r = start(); !r) {
            return error(r.error());
        }
        psa_status_t status = psa_hash_verify(&op, as_psa(expected.data()), expected.size());
        discard();
        return verify_result(status);
    }

    // Copies the partially digested message, if any, into `copy`, a state that is not started. On
    // failure, `copy` is left not started and this state is unchanged.
    [[nodiscard]] result<void> clone_to(state& copy) const {
        if (!active) {
            return {};
        }
        if (auto r = detail::psa_wrap(psa_hash_clone(&op, &copy.op)); !r) {
            psa_hash_abort(&copy.op);
            return r;
        }
        copy.active = true;
        return {};
    }

    // Returns the operation to its initial state, ready for a new message.
    void discard() noexcept {
        if (active) {
            psa_hash_abort(&op);
            active = false;
        }
    }

    psa_algorithm_t alg;
    psa_hash_operation_t op = psa_hash_operation_init();
    bool active = false;
};

operation::operation(unchecked_t, hash::algorithm alg)
    : _state(std::make_unique<state>(alg)) {}

operation::~operation() = default;
operation::operation(operation&&) noexcept = default;
operation& operation::operator=(operation&&) noexcept = default;

result<operation> operation::make(hash::algorithm alg) {
    if (auto r = state::probe(alg); !r) {
        return error(r.error());
    }
    return operation{unchecked_t{}, alg};
}

hash::algorithm operation::algorithm() const noexcept {
    return static_cast<hash::algorithm>(_state->alg);
}

result<void> operation::try_update(byte_view data) {
    return _state->update(data);
}

result<byte_span> operation::try_finish(byte_span digest) {
    const std::size_t size = digest_size();
    // Checked before PSA sees the buffer: PSA would end the operation, discarding the message.
    if (digest.size() < size) {
        return error(errc::buffer_too_small);
    }
    auto out = digest.first(size);
    if (auto r = _state->finish(out); !r) {
        return error(r.error());
    }
    return out;
}

result<bool> operation::try_verify(byte_view expected) {
    if (auto r = _state->start(); !r) {
        return error(r.error());
    }
    // A digest of the wrong length cannot match. Decide it here rather than relying on how the
    // PSA implementation reports it.
    if (expected.size() != digest_size()) {
        _state->discard();
        return false;
    }
    return _state->verify(expected);
}

result<operation> operation::try_clone() const {
    operation copy{unchecked_t{}, algorithm()};
    if (auto r = _state->clone_to(*copy._state); !r) {
        return error(r.error());
    }
    return copy;
}

void operation::reset() noexcept {
    _state->discard();
}

result<byte_span> operation::try_compute(hash::algorithm alg, byte_view data, byte_span digest) {
    const std::size_t size = hash::digest_size(alg);
    if (size == 0 || digest.size() < size) {
        // Report an unusable algorithm, as PSA does on setup, in preference to the buffer size.
        // PSA can't be relied on to rank them: ESP-IDF 6.1's MD5 driver (esp_md5_hash_compute)
        // rejects a null output buffer with PSA_ERROR_INVALID_ARGUMENT before checking the
        // algorithm, which ends the search for a driver that supports it.
        if (auto r = state::probe(alg); !r) {
            return error(r.error());
        }
        return error(size == 0 ? errc::not_supported : errc::buffer_too_small);
    }
    // Give PSA no more output buffer than the digest needs.
    auto out = digest.first(size);
#ifdef MBEDTLS_PSA_ASSUME_EXCLUSIVE_BUFFERS
    if (auto r = detail::psa_init(); !r) {
        return error(r.error());
    }
    size_t length = 0;
    auto r = detail::psa_wrap(psa_hash_compute(
        static_cast<psa_algorithm_t>(alg),
        as_psa(data.data()),
        data.size(),
        as_psa(out.data()),
        out.size(),
        &length
    ));
#else
    state s{alg};
    auto r = s.update(data).and_then([&] { return s.finish(out); });
#endif
    if (!r) {
        return error(r.error());
    }
    return out;
}

result<bool> operation::try_compare(hash::algorithm alg, byte_view data, byte_view expected) {
    const std::size_t size = hash::digest_size(alg);
    // A digest of the wrong length cannot match. Decide it here rather than relying on how the
    // PSA implementation reports it, but report an unusable algorithm (as PSA does on setup) as an
    // error, not a mismatch.
    if (size == 0 || expected.size() != size) {
        if (auto r = state::probe(alg); !r) {
            return error(r.error());
        }
        if (size == 0) {
            return error(errc::not_supported);
        }
        return false;
    }
#ifdef MBEDTLS_PSA_ASSUME_EXCLUSIVE_BUFFERS
    if (auto r = detail::psa_init(); !r) {
        return error(r.error());
    }
    return verify_result(psa_hash_compare(
        static_cast<psa_algorithm_t>(alg),
        as_psa(data.data()),
        data.size(),
        as_psa(expected.data()),
        expected.size()
    ));
#else
    state s{alg};
    if (auto r = s.update(data); !r) {
        return error(r.error());
    }
    return s.verify(expected);
#endif
}

} // namespace idfxx::crypto::hash
