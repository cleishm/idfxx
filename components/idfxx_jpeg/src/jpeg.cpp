// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#include "jpeg_shim.h"

#include <idfxx/error>
#include <idfxx/jpeg>

#include <algorithm>
#include <cinttypes>
#include <cstddef>
#include <esp_log.h>
#include <utility>

// The decoder writes libjpeg's packed RGB output (JCS_EXT_RGB) straight into
// rgb888 pixels.
static_assert(offsetof(idfxx::image::rgb888, r) == 0);
static_assert(offsetof(idfxx::image::rgb888, g) == 1);
static_assert(offsetof(idfxx::image::rgb888, b) == 2);

namespace {
const char* TAG = "idfxx::jpeg";
}

namespace idfxx::jpeg {

const error_category& jpeg_category() noexcept {
    static const error_category instance{};
    return instance;
}

const char* error_category::name() const noexcept {
    return "jpeg::Error";
}

std::string error_category::message(int ec) const {
    switch (errc(ec)) {
    case errc::invalid_data:
        return "Not a JPEG, or corrupt";
    case errc::truncated:
        return "JPEG data ended before the end of the image";
    case errc::not_supported:
        return "JPEG uses a feature the decoder does not support";
    case errc::too_large:
        return "JPEG needs more working memory than the configured limit";
    default:
        return "Unknown JPEG error";
    }
}

struct decoder::impl {
    impl(void* src, read_fn fn) noexcept
        : source(src)
        , read(fn) {}

    impl(const impl&) = delete;
    impl& operator=(const impl&) = delete;

    ~impl() { idfxx_jpeg_destroy(ctx); }

    // Called by the shim whenever libjpeg needs more input.
    static bool fill(void* user, uint8_t* buf, size_t capacity, size_t* count) noexcept {
        auto* self = static_cast<impl*>(user);
        size_t n = 0;
        if (!self->read(self->source, std::span<uint8_t>(buf, capacity), n, self->failure)) {
            return false;
        }
        *count = std::min(n, capacity);
        return true;
    }

    // Converts a failed shim status to the error reported to the caller.
    [[nodiscard]] std::unexpected<std::error_code> error_for(idfxx_jpeg_status status) {
        switch (status) {
        case IDFXX_JPEG_SOURCE_FAILED:
#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
            if (failure.exception) {
                std::rethrow_exception(std::exchange(failure.exception, nullptr));
            }
#endif
            return error(failure.error);
        case IDFXX_JPEG_NO_MEM:
            raise_no_mem();
        case IDFXX_JPEG_TRUNCATED:
            return error(errc::truncated);
        case IDFXX_JPEG_NOT_SUPPORTED:
            return error(errc::not_supported);
        case IDFXX_JPEG_TOO_LARGE:
            return error(errc::too_large);
        case IDFXX_JPEG_OK:
        case IDFXX_JPEG_INVALID_DATA:
            break;
        }
        return error(errc::invalid_data);
    }

    [[nodiscard]] result<void> check(idfxx_jpeg_status status) {
        if (status != IDFXX_JPEG_OK) {
            return error_for(status);
        }
        return {};
    }

    void* source;
    read_fn read;
    detail::read_failure failure;
    idfxx_jpeg_ctx* ctx = nullptr;
    // The output scale, in eighths, that output_width and output_height are for.
    unsigned eighths = 8;
};

decoder::decoder(std::unique_ptr<impl> state) noexcept
    : _impl(std::move(state)) {}

decoder::decoder(decoder&& other) noexcept = default;

decoder& decoder::operator=(decoder&& other) noexcept = default;

decoder::~decoder() = default;

result<decoder> decoder::_make(void* source, read_fn read, config cfg) {
    decoder dec(std::make_unique<impl>(source, read));
    impl& state = *dec._impl;

    const idfxx_jpeg_status created =
        idfxx_jpeg_create(&impl::fill, &state, cfg.memory_limit.has_value(), cfg.memory_limit.value_or(0), &state.ctx);
    if (state.ctx == nullptr) {
        raise_no_mem();
    }
    if (auto r = state.check(created); !r) {
        return error(r.error());
    }

    idfxx_jpeg_header header{};
    if (auto r = state.check(idfxx_jpeg_read_header(state.ctx, &header)); !r) {
        return error(r.error());
    }
    // The shim reports 0 when there is no valid Orientation tag.
    const image::orientation orientation =
        header.orientation != 0 ? static_cast<image::orientation>(header.orientation) : image::orientation::identity;
    dec._info = jpeg::info{
        .width = header.width,
        .height = header.height,
        .orientation = orientation,
        .progressive = header.progressive,
    };
    dec._output_width = header.output_width;
    dec._output_height = header.output_height;
    return dec;
}

void decoder::scale_to_cover(size_t width, size_t height) {
    if (!_impl || _decoded) {
        return;
    }
    unsigned eighths = 8;
    for (unsigned m = 1; m < 8; ++m) {
        uint32_t w = 0;
        uint32_t h = 0;
        if (idfxx_jpeg_calc_output(_impl->ctx, m, &w, &h) != IDFXX_JPEG_OK) {
            break;
        }
        if (w >= width && h >= height) {
            eighths = m;
            break;
        }
    }
    uint32_t w = 0;
    uint32_t h = 0;
    if (idfxx_jpeg_calc_output(_impl->ctx, eighths, &w, &h) != IDFXX_JPEG_OK) {
        ESP_LOGE(TAG, "failed to select output scale %u/8", eighths);
        return;
    }
    _impl->eighths = eighths;
    _output_width = w;
    _output_height = h;
}

result<void> decoder::try_decode(image::view<image::rgb888> dst) {
    if (!_impl || _decoded) {
        return error(idfxx::errc::invalid_state);
    }
    if (dst.width() != _output_width || dst.height() != _output_height) {
        return error(idfxx::errc::invalid_size);
    }
    if (auto r = _begin(); !r) {
        return r;
    }
    for (size_t y = 0; y < _output_height; ++y) {
        if (auto r = _read_row(dst.row(y).data()); !r) {
            return r;
        }
    }
    return _finish();
}

result<void> decoder::_begin() {
    if (!_impl || _decoded) {
        return error(idfxx::errc::invalid_state);
    }
    _decoded = true;
    uint32_t w = 0;
    uint32_t h = 0;
    if (auto r = _impl->check(idfxx_jpeg_start(_impl->ctx, _impl->eighths, &w, &h)); !r) {
        return r;
    }
    // Rows are written for the output size the caller sized its destination
    // by, so libjpeg must produce exactly that.
    if (w != _output_width || h != _output_height) {
        ESP_LOGE(TAG, "output is %" PRIu32 "x%" PRIu32 ", expected %zux%zu", w, h, _output_width, _output_height);
        _abort();
        return error(idfxx::errc::invalid_state);
    }
    return {};
}

result<image::rgb888*> decoder::_allocate_row() {
    uint8_t* row = nullptr;
    if (auto r = _impl->check(idfxx_jpeg_alloc_row(_impl->ctx, &row)); !r) {
        return error(r.error());
    }
    return reinterpret_cast<image::rgb888*>(row);
}

result<void> decoder::_read_row(image::rgb888* row) {
    return _impl->check(idfxx_jpeg_read_row(_impl->ctx, reinterpret_cast<uint8_t*>(row)));
}

result<void> decoder::_finish() {
    return _impl->check(idfxx_jpeg_finish(_impl->ctx));
}

void decoder::_abort() noexcept {
    idfxx_jpeg_abort(_impl->ctx);
}

} // namespace idfxx::jpeg
