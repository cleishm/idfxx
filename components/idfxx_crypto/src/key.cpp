// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#include "psa.hpp"

#include <idfxx/crypto/key>

#include <cstddef>
#include <psa/crypto.h>
#include <utility>

namespace idfxx::crypto {

namespace {

// Prepares to create a key: initializes the crypto library, refuses a MAC algorithm that doesn't
// take the key type, and sets the key's type and policy in `attributes`. PSA doesn't check a key's
// permitted algorithm against its type, and such a key would be one nothing can use.
result<void> prepare(const key::config& cfg, psa_key_attributes_t& attributes) {
    if (auto r = detail::psa_init(); !r) {
        return r;
    }
    const psa_key_type_t type = std::to_underlying(*cfg.type);
    const psa_key_type_t needed = detail::mac_key_type_of(cfg.algorithm.idf_algorithm());
    if (needed != 0 && needed != type) {
        return error(errc::invalid_argument);
    }
    psa_set_key_type(&attributes, type);
    psa_set_key_usage_flags(&attributes, to_underlying(cfg.usage));
    psa_set_key_algorithm(&attributes, cfg.algorithm.idf_algorithm());
    return {};
}

} // namespace

key::key(psa_key_id_t id, key_type type, key_algorithm algorithm) noexcept
    : _id(id)
    , _type(type)
    , _bits(0)
    , _usage(key_usage::none)
    , _algorithm(algorithm) {}

key::~key() {
    if (_id != PSA_KEY_ID_NULL) {
        psa_destroy_key(_id);
    }
}

key::key(key&& other) noexcept
    : _id(std::exchange(other._id, PSA_KEY_ID_NULL))
    , _type(other._type)
    , _bits(other._bits)
    , _usage(other._usage)
    , _algorithm(other._algorithm) {}

key& key::operator=(key&& other) noexcept {
    if (this != &other) {
        if (_id != PSA_KEY_ID_NULL) {
            psa_destroy_key(_id);
        }
        _id = std::exchange(other._id, PSA_KEY_ID_NULL);
        _type = other._type;
        _bits = other._bits;
        _usage = other._usage;
        _algorithm = other._algorithm;
    }
    return *this;
}

result<key>
key::_adopt(psa_status_t status, psa_key_id_t id, key_type type, key_algorithm algorithm) {
    if (auto r = detail::psa_wrap(status); !r) {
        return error(r.error());
    }
    // The key is owned from here on, so that it is destroyed if reading it back fails. Its size is
    // the one PSA settled on, and its usage includes the usages implied by those requested.
    key k{id, type, algorithm};
    psa_key_attributes_t attributes = psa_key_attributes_init();
    status = psa_get_key_attributes(id, &attributes);
    k._bits = psa_get_key_bits(&attributes);
    k._usage = flags<key_usage>{psa_get_key_usage_flags(&attributes)};
    psa_reset_key_attributes(&attributes);
    if (auto r = detail::psa_wrap(status); !r) {
        return error(r.error());
    }
    return k;
}

result<key> key::try_import(const config& cfg, byte_view material) {
    psa_key_attributes_t attributes = psa_key_attributes_init();
    if (auto r = prepare(cfg, attributes); !r) {
        return error(r.error());
    }
    // PSA rejects empty key material; decide it here rather than relying on how the PSA
    // implementation reports it.
    if (material.empty()) {
        psa_reset_key_attributes(&attributes);
        return error(errc::invalid_argument);
    }
    psa_key_id_t id = PSA_KEY_ID_NULL;
    psa_status_t status =
        psa_import_key(&attributes, detail::as_psa(material.data()), material.size(), &id);
    psa_reset_key_attributes(&attributes);
    return _adopt(status, id, *cfg.type, cfg.algorithm);
}

result<key> key::try_generate(const config& cfg, std::size_t bits) {
    psa_key_attributes_t attributes = psa_key_attributes_init();
    if (auto r = prepare(cfg, attributes); !r) {
        return error(r.error());
    }
    psa_set_key_bits(&attributes, bits);
    psa_key_id_t id = PSA_KEY_ID_NULL;
    psa_status_t status = psa_generate_key(&attributes, &id);
    psa_reset_key_attributes(&attributes);
    return _adopt(status, id, *cfg.type, cfg.algorithm);
}

result<key> key::try_copy(flags<key_usage> usage) const {
    // The copy takes this key's type and size; its policy is the intersection of this key's
    // policy with the one requested here
    psa_key_attributes_t attributes = psa_key_attributes_init();
    psa_set_key_usage_flags(&attributes, to_underlying(usage));
    psa_set_key_algorithm(&attributes, _algorithm.idf_algorithm());
    psa_key_id_t id = PSA_KEY_ID_NULL;
    psa_status_t status = psa_copy_key(_id, &attributes, &id);
    psa_reset_key_attributes(&attributes);
    return _adopt(status, id, _type, _algorithm);
}

result<byte_span> key::try_export_to(byte_span out) const {
    std::size_t length = 0;
    return detail::psa_wrap(psa_export_key(_id, detail::as_psa(out.data()), out.size(), &length))
        .transform([&] { return out.first(length); });
}

} // namespace idfxx::crypto
