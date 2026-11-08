// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#include <idfxx/epaper/image_writer>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <epaper_image.h>
#include <memory>
#include <optional>
#include <span>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

// Rows of rgb888 go to the converter as they are.
static_assert(sizeof(idfxx::image::rgb888) == sizeof(epi_rgb_t));
static_assert(std::is_standard_layout_v<idfxx::image::rgb888>);
static_assert(offsetof(idfxx::image::rgb888, r) == offsetof(epi_rgb_t, r));
static_assert(offsetof(idfxx::image::rgb888, g) == offsetof(epi_rgb_t, g));
static_assert(offsetof(idfxx::image::rgb888, b) == offsetof(epi_rgb_t, b));

namespace idfxx::epaper::detail {

struct calibration_access {
    [[nodiscard]] static const epi_panel_t& panel(const calibration& cal) noexcept { return *cal._panel; }
};

namespace {

using band_type = std::variant<mono_framebuffer, gray4_framebuffer, color6_framebuffer>;

errc to_errc(epi_err_t err) {
    switch (err) {
    case EPI_ERR_INVALID_ARG:
        return errc::invalid_arg;
    case EPI_ERR_WORKSPACE_TOO_SMALL:
    case EPI_ERR_SOURCE_TOO_LARGE:
        return errc::invalid_size;
    case EPI_ERR_NOT_READY:
    case EPI_ERR_NOT_DRAINED:
    case EPI_ERR_FINISHED:
        return errc::invalid_state;
    case EPI_ERR_CANCELLED:
    case EPI_OK:
        break;
    }
    return errc::fail;
}

size_t x_alignment_of(enum color_mode mode) {
    switch (mode) {
    case color_mode::mono:
        return mono_framebuffer::x_alignment;
    case color_mode::gray4:
        return gray4_framebuffer::x_alignment;
    case color_mode::color6:
        return color6_framebuffer::x_alignment;
    }
    return 1;
}

// The bytes of a band of width x rows.
size_t band_bytes(enum color_mode mode, size_t width, size_t rows) {
    switch (mode) {
    case color_mode::mono:
        return (width + 7) / 8 * rows;
    case color_mode::gray4:
        return 2 * ((width + 7) / 8) * rows;
    case color_mode::color6:
        return (width + 1) / 2 * rows;
    }
    return 0;
}

epi_config_t converter_config(const calibration& cal, const epi_intent_t* intent, size_t width, size_t height) {
    return {
        .panel = &calibration_access::panel(cal),
        .intent = intent,
        .width = static_cast<uint16_t>(width),
        .height = static_cast<uint16_t>(height),
    };
}

template<typename Framebuffer>
result<band_type> make_band(size_t width, size_t rows) {
    auto fb = Framebuffer::make(width, rows);
    if (!fb) {
        return error(fb.error());
    }
    return band_type(std::move(*fb));
}

result<band_type> make_band(enum color_mode mode, size_t width, size_t rows) {
    switch (mode) {
    case color_mode::mono:
        return make_band<mono_framebuffer>(width, rows);
    case color_mode::gray4:
        return make_band<gray4_framebuffer>(width, rows);
    case color_mode::color6:
        return make_band<color6_framebuffer>(width, rows);
    }
    return error(errc::invalid_arg);
}

// The bytes of a band whose rows are packed ink codes. A gray4 band keeps its bits in two
// planes instead.
uint8_t* packed_rows(band_type& band) {
    if (auto* mono = std::get_if<mono_framebuffer>(&band)) {
        return mono->data().data();
    }
    if (auto* color = std::get_if<color6_framebuffer>(&band)) {
        return color->data().data();
    }
    return nullptr;
}

// Splits a row of ink codes into a gray4 band's two planes. Padding bits stay white.
void set_gray_row(gray4_framebuffer& band, size_t y, std::span<const uint8_t> codes) {
    const size_t stride = band.stride_bytes();
    const auto plane0 = band.plane(0).subspan(y * stride, stride);
    const auto plane1 = band.plane(1).subspan(y * stride, stride);
    std::ranges::fill(plane0, uint8_t{0});
    std::ranges::fill(plane1, uint8_t{0});
    for (size_t x = 0; x < codes.size(); ++x) {
        const auto level = std::to_underlying(pixel_traits<gray4>::from_code(codes[x]));
        const auto mask = static_cast<uint8_t>(0x80u >> (x % 8));
        if ((level & 0x01) != 0) {
            plane0[x / 8] |= mask;
        }
        if ((level & 0x02) != 0) {
            plane1[x / 8] |= mask;
        }
    }
}

} // namespace

struct image_stream::state {
    destination to;
    size_t x;          // the picture's left edge on the destination
    size_t y;          // the next row's position on the destination
    size_t band_rows;  // rows between flushes
    size_t filled = 0; // rows since the last flush
    size_t band_y = 0; // the position on the destination of the band's first row
    std::vector<uint8_t> workspace{};
    epi_converter_t* converter = nullptr;
    const epi_panel_t* panel = nullptr;
    std::vector<uint8_t> codes{};             // the row popped
    std::optional<band_type> band{};          // for a panel
    uint8_t* packed = nullptr;                // the band's bytes, when rows are packed into them
    epi_pack_format_t format = EPI_PACK_4BPP; // of a row in those bytes
    size_t stride = 0;                        // of a row in those bytes

    [[nodiscard]] result<void> row_done();
    [[nodiscard]] result<void> flush();
};

result<void> image_stream::state::row_done() {
    if (to.display == nullptr) {
        to.set_surface_row(to.surface, x, y, codes);
    } else {
        if (filled == 0) {
            band_y = y;
        }
        if (packed != nullptr) {
            const auto width = static_cast<uint16_t>(codes.size());
            if (const auto err = epi_pack_row(panel, format, codes.data(), width, packed + filled * stride);
                err != EPI_OK) {
                return error(to_errc(err));
            }
        } else {
            set_gray_row(std::get<gray4_framebuffer>(*band), filled, codes);
        }
    }
    ++y;
    if (++filled < band_rows) {
        return {};
    }
    return flush();
}

result<void> image_stream::state::flush() {
    const size_t rows = std::exchange(filled, 0);
    if (rows == 0 || to.display == nullptr) {
        return {};
    }
    return std::visit(
        [&](auto& fb) -> result<void> {
            if (to.draw_overlay != nullptr) {
                to.draw_overlay(to.overlay, &fb, x, band_y);
            }
            return to.display->try_write_rows(fb, 0, rows, x, band_y);
        },
        *band
    );
}

result<image::render_config>
image_stream::plan(const destination& to, const calibration& cal, const write_options& options) {
    const enum color_mode mode = cal.color_mode();
    if (options.band_rows == 0 || to.pixels.value_or(mode) != mode) {
        return error(errc::invalid_arg);
    }
    image::render_config render{
        .screen_width = to.surface_width,
        .screen_height = to.surface_height,
        .memory_limit = options.memory_limit,
    };
    if (to.display != nullptr) {
        if (to.display->width() != cal.width() || to.display->height() != cal.height()) {
            return error(errc::invalid_arg);
        }
        if (to.display->color_mode() != mode || to.display->asleep()) {
            return error(errc::invalid_state);
        }
        render.screen_width = to.display->display_width();
        render.screen_height = to.display->display_height();
        render.x_alignment = x_alignment_of(mode);
    }
    // The converter counts in 16 bits.
    if (render.screen_width > UINT16_MAX || render.screen_height > UINT16_MAX) {
        return error(errc::invalid_size);
    }

    // The rest of the stream is at most the screen wide: take its bytes from the limit.
    if (options.memory_limit) {
        const size_t width = render.screen_width;
        const size_t rows = std::min<size_t>(options.band_rows, render.screen_height);
        // Only the presence of an intent matters here.
        const epi_intent_t any_intent{};
        const epi_config_t config = converter_config(cal, &any_intent, width, 1);
        const size_t bytes = sizeof(state) + epi_converter_workspace_size(&config) + width +
            (to.display != nullptr ? band_bytes(mode, width, rows) : 0);
        if (bytes > *options.memory_limit) {
            return error(image::errc::too_large);
        }
        render.memory_limit = *options.memory_limit - bytes;
    }
    return render;
}

result<image_stream> image_stream::make(
    const destination& to,
    const calibration& cal,
    const image::rect& output,
    const write_options& options
) {
    epi_intent_t intent{};
    if (options.intent) {
        intent = {
            .tone = options.intent->tone,
            .detail = options.intent->detail,
            .adapt = options.intent->adapt,
            .vibrance = options.intent->vibrance,
            .veil = options.intent->veil,
            .lift = options.intent->lift,
        };
    }
    const epi_config_t config = converter_config(cal, options.intent ? &intent : nullptr, output.width, output.height);
    const size_t workspace_bytes = epi_converter_workspace_size(&config);
    if (workspace_bytes == 0) {
        return error(errc::invalid_arg);
    }

    auto s = std::make_unique<state>(state{
        .to = to,
        .x = static_cast<size_t>(output.x),
        .y = static_cast<size_t>(output.y),
        .band_rows = std::min(options.band_rows, output.height),
        .panel = config.panel,
    });
    s->workspace.resize(workspace_bytes);
    if (const auto err = epi_converter_init(&config, s->workspace.data(), workspace_bytes, &s->converter);
        err != EPI_OK) {
        return error(to_errc(err));
    }
    s->codes.resize(output.width);
    if (to.display != nullptr) {
        auto band = make_band(cal.color_mode(), output.width, s->band_rows);
        if (!band) {
            return error(band.error());
        }
        s->band.emplace(std::move(*band));
        s->packed = packed_rows(*s->band);
        s->format = cal.color_mode() == color_mode::mono ? EPI_PACK_1BPP : EPI_PACK_4BPP;
        s->stride = epi_pack_row_size(s->format, config.width);
    }
    return image_stream(std::move(s));
}

image_stream::image_stream(std::unique_ptr<state> s) noexcept
    : _state(std::move(s)) {}

image_stream::image_stream(image_stream&& other) noexcept = default;
image_stream& image_stream::operator=(image_stream&& other) noexcept = default;
image_stream::~image_stream() = default;

result<void> image_stream::operator()(image::point, image::view<const image::rgb888> rows) {
    state& s = *_state;
    for (size_t row = 0; row < rows.height(); ++row) {
        const auto* pixels = reinterpret_cast<const epi_rgb_t*>(rows.row(row).data());
        if (const auto err = epi_converter_push(s.converter, pixels); err != EPI_OK) {
            return error(to_errc(err));
        }
        while (true) {
            const auto err = epi_converter_pop(s.converter, s.codes.data());
            if (err == EPI_ERR_NOT_READY || err == EPI_ERR_FINISHED) {
                break;
            }
            if (err != EPI_OK) {
                return error(to_errc(err));
            }
            if (auto r = s.row_done(); !r) {
                return r;
            }
        }
    }
    return {};
}

result<void> image_stream::try_finish() {
    return _state->flush();
}

} // namespace idfxx::epaper::detail
