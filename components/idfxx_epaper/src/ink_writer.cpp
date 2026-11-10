// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#include <idfxx/epaper/ink_writer>

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <utility>
#include <vector>

namespace idfxx::epaper::detail {

namespace {

// A bit for each nibble that is a color6 value.
constexpr uint16_t valid_color6_nibbles = (1u << std::to_underlying(color6::black)) |
    (1u << std::to_underlying(color6::white)) | (1u << std::to_underlying(color6::yellow)) |
    (1u << std::to_underlying(color6::red)) | (1u << std::to_underlying(color6::blue)) |
    (1u << std::to_underlying(color6::green));

[[nodiscard]] constexpr bool valid_color6_nibble(uint8_t nibble) noexcept {
    return ((valid_color6_nibbles >> nibble) & 1u) != 0;
}

struct rectangle {
    size_t x;
    size_t y;
    size_t width;
    size_t height;
};

// The rectangle the options describe on a destination, once it's known to fit and to be aligned.
[[nodiscard]] result<rectangle>
place(size_t dest_width, size_t dest_height, size_t x_alignment, const ink_options& options) {
    if (options.band_rows == 0 || options.x >= dest_width || options.y >= dest_height) {
        return error(errc::invalid_arg);
    }
    const size_t width = options.width.value_or(dest_width - options.x);
    const size_t height = options.height.value_or(dest_height - options.y);
    if (width == 0 || height == 0 || width > dest_width - options.x ||
        height > dest_height - options.y) {
        return error(errc::invalid_arg);
    }
    const size_t right = options.x + width;
    if (options.x % x_alignment != 0 || (right != dest_width && right % x_alignment != 0)) {
        return error(errc::invalid_arg);
    }
    return rectangle{.x = options.x, .y = options.y, .width = width, .height = height};
}

// Fills the buffer from the source. Reads nothing past it, and doesn't look for the end of the
// stream.
[[nodiscard]] result<void> read_exactly(const ink_source& source, std::span<uint8_t> into) {
    while (!into.empty()) {
        auto n = source.read(source.source, into);
        if (!n) {
            return error(n.error());
        }
        if (*n == 0) {
            return error(errc::invalid_size);
        }
        into = into.subspan(std::min(*n, into.size()));
    }
    return {};
}

// Reads the first rows of a band. The bytes of a mono band are the input's, with the padding bits
// white: a framebuffer's padding is uploaded with its row, and the next column may be on the glass.
[[nodiscard]] result<void> read_band(
    mono_framebuffer& band,
    size_t rows,
    const ink_source& source,
    std::span<uint8_t> /* packed_row */
) {
    const size_t stride = band.stride_bytes();
    const auto bytes = band.data().first(rows * stride);
    if (auto r = read_exactly(source, bytes); !r) {
        return r;
    }
    if (const size_t used = band.width() % 8; used != 0) {
        for (size_t y = 0; y < rows; ++y) {
            bytes[y * stride + stride - 1] |= static_cast<uint8_t>(0xFF >> used);
        }
    }
    return {};
}

// The bytes of a color6 band are the input's, once they are known to be valid, with the padding
// nibble white, as in a mono band.
[[nodiscard]] result<void> read_band(
    color6_framebuffer& band,
    size_t rows,
    const ink_source& source,
    std::span<uint8_t> /* packed_row */
) {
    const auto bytes = band.data().first(rows * band.stride_bytes());
    if (auto r = read_exactly(source, bytes); !r) {
        return r;
    }
    const bool valid = std::ranges::all_of(bytes, [](uint8_t byte) {
        return valid_color6_nibble(byte >> 4) && valid_color6_nibble(byte & 0x0F);
    });
    if (!valid) {
        return error(errc::invalid_arg);
    }
    if (band.width() % 2 != 0) {
        const size_t stride = band.stride_bytes();
        for (size_t y = 0; y < rows; ++y) {
            uint8_t& last = bytes[y * stride + stride - 1];
            last = static_cast<uint8_t>((last & 0xF0) | std::to_underlying(color6::white));
        }
    }
    return {};
}

// A gray4 band keeps its levels in two planes. The input's code 0 is the darkest and a level's 0 is
// white, so a level is 3 - code. Padding bits stay white.
[[nodiscard]] result<void> read_band(
    gray4_framebuffer& band,
    size_t rows,
    const ink_source& source,
    std::span<uint8_t> packed_row
) {
    const size_t stride = band.stride_bytes();
    const size_t width = band.width();
    for (size_t y = 0; y < rows; ++y) {
        if (auto r = read_exactly(source, packed_row); !r) {
            return r;
        }
        const auto plane0 = band.plane(0).subspan(y * stride, stride);
        const auto plane1 = band.plane(1).subspan(y * stride, stride);
        std::ranges::fill(plane0, uint8_t{0});
        std::ranges::fill(plane1, uint8_t{0});
        for (size_t x = 0; x < width; ++x) {
            const auto code = static_cast<uint8_t>((packed_row[x / 4] >> (6 - 2 * (x % 4))) & 0x03);
            const auto level = static_cast<uint8_t>(3 - code);
            const auto mask = static_cast<uint8_t>(0x80u >> (x % 8));
            if ((level & 0x01) != 0) {
                plane0[x / 8] |= mask;
            }
            if ((level & 0x02) != 0) {
                plane1[x / 8] |= mask;
            }
        }
    }
    return {};
}

// Reads the rectangle's rows a band at a time, passing each band to write_band with the
// number of rows it holds and the destination row of the first.
template<typename Framebuffer, typename WriteBand>
[[nodiscard]] result<void> write_bands(
    const ink_source& source,
    const rectangle& at,
    size_t band_rows,
    WriteBand&& write_band
) {
    const size_t band_height = std::min(band_rows, at.height);
    auto band = Framebuffer::make(at.width, band_height);
    if (!band) {
        return error(band.error());
    }
    std::vector<uint8_t> packed_row;
    if constexpr (std::same_as<Framebuffer, gray4_framebuffer>) {
        packed_row.resize((at.width + 3) / 4);
    }
    for (size_t done = 0; done < at.height;) {
        const size_t rows = std::min(band_height, at.height - done);
        if (auto r = read_band(*band, rows, source, packed_row); !r) {
            return r;
        }
        if (auto r = write_band(std::as_const(*band), rows, at.y + done); !r) {
            return r;
        }
        done += rows;
    }
    return {};
}

template<typename Framebuffer>
[[nodiscard]] result<void>
write_to_panel(panel& display, const ink_source& source, const ink_options& options) {
    const auto at =
        place(display.display_width(), display.display_height(), Framebuffer::x_alignment, options);
    if (!at) {
        return error(at.error());
    }
    return write_bands<Framebuffer>(
        source, *at, options.band_rows, [&](const Framebuffer& band, size_t rows, size_t y) {
            return display.try_write_rows(band, 0, rows, at->x, y);
        }
    );
}

// Copies the rows of a band's bytes to a destination's. The left edge is on a byte boundary. So is
// the right edge, or else it's the destination's own, where both have the same padding: the band's
// stride never reaches past what the rectangle owns.
void copy_rows(
    std::span<const uint8_t> band,
    size_t band_stride,
    size_t rows,
    std::span<uint8_t> dest,
    size_t dest_stride,
    size_t dest_x_byte,
    size_t dest_y
) {
    for (size_t row = 0; row < rows; ++row) {
        std::memcpy(
            dest.data() + (dest_y + row) * dest_stride + dest_x_byte,
            band.data() + row * band_stride,
            band_stride
        );
    }
}

template<typename Framebuffer>
[[nodiscard]] result<void>
write_to_framebuffer(Framebuffer& dest, const ink_source& source, const ink_options& options) {
    const auto at = place(dest.width(), dest.height(), Framebuffer::x_alignment, options);
    if (!at) {
        return error(at.error());
    }
    // A framebuffer's alignment is the number of pixels in a byte of its rows.
    const size_t x_byte = at->x / Framebuffer::x_alignment;
    return write_bands<Framebuffer>(
        source,
        *at,
        options.band_rows,
        [&](const Framebuffer& band, size_t rows, size_t y) -> result<void> {
            const size_t band_stride = band.stride_bytes();
            const size_t dest_stride = dest.stride_bytes();
            if constexpr (std::same_as<Framebuffer, gray4_framebuffer>) {
                copy_rows(band.plane(0), band_stride, rows, dest.plane(0), dest_stride, x_byte, y);
                copy_rows(band.plane(1), band_stride, rows, dest.plane(1), dest_stride, x_byte, y);
            } else {
                copy_rows(band.data(), band_stride, rows, dest.data(), dest_stride, x_byte, y);
            }
            return {};
        }
    );
}

} // namespace

result<void> try_write_inks(panel& display, const ink_source& source, const ink_options& options) {
    if (display.asleep()) {
        return error(errc::invalid_state);
    }
    switch (display.color_mode()) {
    case color_mode::mono:
        return write_to_panel<mono_framebuffer>(display, source, options);
    case color_mode::gray4:
        return write_to_panel<gray4_framebuffer>(display, source, options);
    case color_mode::color6:
        return write_to_panel<color6_framebuffer>(display, source, options);
    }
    return error(errc::invalid_state);
}

result<void> try_write_inks(
    mono_framebuffer& framebuffer,
    const ink_source& source,
    const ink_options& options
) {
    return write_to_framebuffer(framebuffer, source, options);
}

result<void> try_write_inks(
    gray4_framebuffer& framebuffer,
    const ink_source& source,
    const ink_options& options
) {
    return write_to_framebuffer(framebuffer, source, options);
}

result<void> try_write_inks(
    color6_framebuffer& framebuffer,
    const ink_source& source,
    const ink_options& options
) {
    return write_to_framebuffer(framebuffer, source, options);
}

} // namespace idfxx::epaper::detail
