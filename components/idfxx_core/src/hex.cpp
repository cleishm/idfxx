// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#include <idfxx/hex>

#include <iterator>
#include <string>

namespace idfxx {

std::string to_string(hex h) {
    std::string result;
    result.reserve(h.bytes().size() * (2 + h.options().separator.size()));
    detail::write_hex(h, std::back_inserter(result));
    return result;
}

} // namespace idfxx
