// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#include <idfxx/image>

namespace idfxx::image {

const error_category& image_category() noexcept {
    static const error_category instance{};
    return instance;
}

const char* error_category::name() const noexcept {
    return "image::Error";
}

std::string error_category::message(int ec) const {
    switch (errc(ec)) {
    case errc::too_large:
        return "Rendering or decoding needs more working memory than the configured limit";
    default:
        return "Unknown image error";
    }
}

} // namespace idfxx::image
