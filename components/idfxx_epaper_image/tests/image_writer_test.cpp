// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Unit tests for idfxx_epaper_image
// Uses ESP-IDF Unity test framework with compile-time static_asserts
//
// The expected output is computed independently: the source goes through the
// library's own resampler and converter directly, and a recording panel keeps the
// bytes that reach it. Targets are small, as QEMU has no PSRAM, but the panels
// have the calibrations' full sizes: they hold what they are given, not a frame.

#include "../../idfxx_jpeg/tests/jpeg_test_images.hpp"

#include "sdkconfig.h"

#include <epaper_image.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <idfxx/byte_source>
#include <idfxx/epaper/color>
#include <idfxx/epaper/color6_framebuffer>
#include <idfxx/epaper/gray4_framebuffer>
#include <idfxx/epaper/image_writer>
#include <idfxx/epaper/mono_framebuffer>
#include <idfxx/epaper/panel>
#include <idfxx/error>
#include <idfxx/gfx>
#include <idfxx/image>
#include <idfxx/jpeg>
#include <unity.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>
#include <vector>

namespace image = idfxx::image;
namespace epaper = idfxx::epaper;
namespace gfx = idfxx::gfx;
using idfxx::errc;
using idfxx::result;

namespace img = jpeg_test_images;

namespace {

// A decoder that delivers a picture a row at a time, optionally failing part way.
class fake_decoder {
public:
    struct info_type {
        size_t width;
        size_t height;
        image::orientation orientation;
    };

    explicit fake_decoder(image::buffer<> picture, image::orientation orientation = image::orientation::identity)
        : _picture(std::move(picture))
        , _info{_picture.width(), _picture.height(), orientation} {}

    const info_type& info() const noexcept { return _info; }

    template<typename S>
    result<void> try_decode(S&& sink) {
        ++decodes;
        for (size_t y = 0; y < _picture.height(); ++y) {
            if (y == fail_at_row) {
                return idfxx::error(errc::timeout);
            }
            const image::view<const image::rgb888> row(&std::as_const(_picture)[0, y], _picture.width(), 1);
            if (auto r = sink(image::point{0, y}, row); !r) {
                return r;
            }
        }
        return {};
    }

    size_t fail_at_row = SIZE_MAX;
    size_t decodes = 0;

private:
    image::buffer<> _picture;
    info_type _info;
};

// A panel that keeps the bytes written to it, write by write. Its gray4 writes are
// kept with all of plane 0 first, as in a framebuffer.
class capture_panel : public epaper::panel {
public:
    capture_panel(size_t width, size_t height, enum epaper::color_mode mode)
        : epaper::panel(width, height) {
        TEST_ASSERT_TRUE(try_set_color_mode(mode).has_value());
    }

    struct record {
        size_t x;
        size_t y;
        size_t width;
        size_t rows;
        std::vector<uint8_t> first;  // the packed rows, or plane 0
        std::vector<uint8_t> second; // plane 1
    };

    std::vector<record> writes;
    // The write that fails, counting from 0.
    size_t fail_at_write = SIZE_MAX;

    // Everything written, in order, plane by plane.
    [[nodiscard]] std::vector<uint8_t> bytes() const {
        std::vector<uint8_t> out;
        for (const auto& w : writes) {
            out.insert(out.end(), w.first.begin(), w.first.end());
        }
        for (const auto& w : writes) {
            out.insert(out.end(), w.second.begin(), w.second.end());
        }
        return out;
    }

private:
    template<typename Rows>
    [[nodiscard]] result<void> _keep(size_t x, size_t y, size_t width, size_t rows, Rows&& fill) {
        if (writes.size() == fail_at_write) {
            writes.push_back({});
            return idfxx::error(errc::not_allowed);
        }
        record r{.x = x, .y = y, .width = width, .rows = rows, .first = {}, .second = {}};
        fill(r);
        writes.push_back(std::move(r));
        return {};
    }

    [[nodiscard]] result<void>
    do_write(const epaper::mono_framebuffer& fb, size_t row_start, size_t row_end, size_t x, size_t y) override {
        return _keep(x, y + row_start, fb.width(), row_end - row_start, [&](record& r) {
            for (size_t row = row_start; row < row_end; ++row) {
                r.first.insert(r.first.end(), fb.row(row).begin(), fb.row(row).end());
            }
        });
    }

    [[nodiscard]] result<void>
    do_write(const epaper::gray4_framebuffer& fb, size_t row_start, size_t row_end, size_t x, size_t y) override {
        return _keep(x, y + row_start, fb.width(), row_end - row_start, [&](record& r) {
            for (size_t row = row_start; row < row_end; ++row) {
                r.first.insert(r.first.end(), fb.plane_row(0, row).begin(), fb.plane_row(0, row).end());
                r.second.insert(r.second.end(), fb.plane_row(1, row).begin(), fb.plane_row(1, row).end());
            }
        });
    }

    [[nodiscard]] result<void>
    do_write(const epaper::color6_framebuffer& fb, size_t row_start, size_t row_end, size_t x, size_t y) override {
        return _keep(x, y + row_start, fb.width(), row_end - row_start, [&](record& r) {
            for (size_t row = row_start; row < row_end; ++row) {
                r.first.insert(r.first.end(), fb.row(row).begin(), fb.row(row).end());
            }
        });
    }

    [[nodiscard]] result<void> do_clear() override { return {}; }

    [[nodiscard]] result<void> do_refresh(epaper::refresh_mode) override { return {}; }

    [[nodiscard]] bool do_supports(enum epaper::color_mode) const noexcept override { return true; }

    [[nodiscard]] bool do_supports(epaper::refresh_mode) const noexcept override { return true; }

    [[nodiscard]] bool do_supports(enum epaper::border) const noexcept override { return false; }

    [[nodiscard]] result<void> do_set_color_mode(enum epaper::color_mode) override { return {}; }

    [[nodiscard]] result<void> do_sleep() override { return {}; }

    [[nodiscard]] result<void> do_wake() override { return {}; }
};

// An image with detail at every frequency, reproducible from its size.
image::buffer<> noise(size_t w, size_t h) {
    image::buffer<> out(w, h);
    uint32_t state = 0x9E3779B9u ^ static_cast<uint32_t>(w * 65599u + h);
    for (size_t y = 0; y < h; ++y) {
        for (size_t x = 0; x < w; ++x) {
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;
            out[x, y] = {
                static_cast<uint8_t>(state),
                static_cast<uint8_t>(state >> 8),
                static_cast<uint8_t>(state >> 16),
            };
        }
    }
    return out;
}

// Black on the left to white on the right, so that a picture has every gray level.
image::buffer<> ramp(size_t w, size_t h) {
    image::buffer<> out(w, h);
    for (size_t y = 0; y < h; ++y) {
        for (size_t x = 0; x < w; ++x) {
            const auto level = static_cast<uint8_t>(x * 255 / (w - 1));
            out[x, y] = {level, level, level};
        }
    }
    return out;
}

image::buffer<> flat(size_t w, size_t h, image::rgb888 color) {
    image::buffer<> out(w, h);
    for (size_t y = 0; y < h; ++y) {
        for (size_t x = 0; x < w; ++x) {
            out[x, y] = color;
        }
    }
    return out;
}

constexpr size_t source_w = 64;
constexpr size_t source_h = 48;

image::buffer<> source() {
    return noise(source_w, source_h);
}

// A built-in calibration with its counterpart in the library.
struct panel_kind {
    const epi_panel_t* library;
    const epaper::calibration* cal;
    enum epaper::color_mode mode;
    size_t alignment;
};

std::array<panel_kind, 3> all_panels() {
    return {{
        {&epi_panel_el133uf1, &epaper::calibration::el133uf1(), epaper::color_mode::color6, 2},
        {&epi_panel_gdey075t7_mono, &epaper::calibration::gdey075t7_mono(), epaper::color_mode::mono, 8},
        {&epi_panel_gdey075t7_grey4, &epaper::calibration::gdey075t7_gray4(), epaper::color_mode::gray4, 8},
    }};
}

panel_kind color6_panel() {
    return all_panels()[0];
}

panel_kind mono_panel() {
    return all_panels()[1];
}

panel_kind gray4_panel() {
    return all_panels()[2];
}

// The panel a built-in calibration is for.
capture_panel full_size_panel(const panel_kind& kind) {
    return capture_panel(kind.cal->width(), kind.cal->height(), kind.mode);
}

// Where the library is asked to put a picture: on an output of width x height, by its
// own layout helpers, or stretched to a rectangle that may overhang the output.
struct library_place {
    enum { cover, fit, stretch } how;
    uint16_t width;
    uint16_t height;
    int32_t dst_x = 0;
    int32_t dst_y = 0;
    uint16_t dst_w = 0;
    uint16_t dst_h = 0;
};

// The ink codes of the library's own resampler and converter, run directly.
std::vector<uint8_t> library_convert(
    const epi_panel_t& panel,
    const image::buffer<>& picture,
    const library_place& place,
    const epi_intent_t* intent
) {
    epi_resampler_config_t rc = {};
    rc.src_width = static_cast<uint32_t>(picture.width());
    rc.src_height = static_cast<uint32_t>(picture.height());
    rc.dst_width = place.width;
    rc.dst_height = place.height;
    rc.background = {255, 255, 255};
    rc.filter = EPI_FILTER_LANCZOS3;
    switch (place.how) {
    case library_place::cover:
        epi_layout_cover(&rc);
        break;
    case library_place::fit:
        epi_layout_fit(&rc);
        break;
    case library_place::stretch:
        rc.src_w = static_cast<float>(picture.width());
        rc.src_h = static_cast<float>(picture.height());
        rc.dst_x = place.dst_x;
        rc.dst_y = place.dst_y;
        rc.dst_w = place.dst_w;
        rc.dst_h = place.dst_h;
        break;
    }
    std::vector<uint8_t> resampler_memory(epi_resampler_workspace_size(&rc));
    epi_resampler_t* resampler = nullptr;
    TEST_ASSERT_EQUAL_INT(EPI_OK, epi_resampler_init(&rc, resampler_memory.data(), resampler_memory.size(), &resampler));

    const epi_config_t cc{
        .panel = &panel,
        .intent = intent,
        .width = place.width,
        .height = place.height,
    };
    std::vector<uint8_t> converter_memory(epi_converter_workspace_size(&cc));
    epi_converter_t* converter = nullptr;
    TEST_ASSERT_EQUAL_INT(EPI_OK, epi_converter_init(&cc, converter_memory.data(), converter_memory.size(), &converter));

    std::vector<epi_rgb_t> resampled(place.width);
    std::vector<uint8_t> row(place.width);
    std::vector<uint8_t> out;
    for (size_t y = 0; y < picture.height(); ++y) {
        const auto* src = reinterpret_cast<const epi_rgb_t*>(&picture[0, y]);
        TEST_ASSERT_EQUAL_INT(EPI_OK, epi_resampler_push(resampler, src));
        while (epi_resampler_pop(resampler, resampled.data()) == EPI_OK) {
            TEST_ASSERT_EQUAL_INT(EPI_OK, epi_converter_push(converter, resampled.data()));
            while (epi_converter_pop(converter, row.data()) == EPI_OK) {
                out.insert(out.end(), row.begin(), row.end());
            }
        }
    }
    TEST_ASSERT_EQUAL_UINT32(row.size() * place.height, out.size());
    return out;
}

// The library's packed rows of these ink codes.
std::vector<uint8_t> library_pack(
    const epi_panel_t& panel,
    epi_pack_format_t format,
    const std::vector<uint8_t>& codes,
    uint16_t w,
    uint16_t h
) {
    const size_t stride = epi_pack_row_size(format, w);
    std::vector<uint8_t> out(stride * h);
    for (size_t y = 0; y < h; ++y) {
        TEST_ASSERT_EQUAL_INT(EPI_OK, epi_pack_row(&panel, format, &codes[y * w], w, &out[y * stride]));
    }
    return out;
}

// The two planes of a gray4 framebuffer, from the library's ink codes, one after the other.
std::vector<uint8_t> gray_planes(const std::vector<uint8_t>& codes, size_t w, size_t h) {
    const size_t stride = (w + 7) / 8;
    std::vector<uint8_t> out(2 * stride * h, 0);
    for (size_t y = 0; y < h; ++y) {
        for (size_t x = 0; x < w; ++x) {
            const unsigned level = 3u - codes[y * w + x];
            const auto bit = static_cast<uint8_t>(0x80u >> (x % 8));
            if ((level & 1u) != 0) {
                out[y * stride + x / 8] |= bit;
            }
            if ((level & 2u) != 0) {
                out[stride * h + y * stride + x / 8] |= bit;
            }
        }
    }
    return out;
}

// What the panel of this mode should receive, by way of the library alone.
std::vector<uint8_t> library_panel_bytes(
    const epi_panel_t& panel,
    enum epaper::color_mode mode,
    const image::buffer<>& picture,
    const library_place& place,
    const epi_intent_t* intent = nullptr
) {
    const auto codes = library_convert(panel, picture, place, intent);
    switch (mode) {
    case epaper::color_mode::color6:
        return library_pack(panel, EPI_PACK_4BPP, codes, place.width, place.height);
    case epaper::color_mode::mono:
        return library_pack(panel, EPI_PACK_1BPP, codes, place.width, place.height);
    case epaper::color_mode::gray4:
        return gray_planes(codes, place.width, place.height);
    }
    return {};
}

void assert_bytes_equal(const std::vector<uint8_t>& expected, const std::vector<uint8_t>& actual) {
    TEST_ASSERT_EQUAL_UINT32(expected.size(), actual.size());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected.data(), actual.data(), expected.size());
}

size_t error_value(const result<void>& r) {
    TEST_ASSERT_FALSE(r.has_value());
    return static_cast<size_t>(r.error().value());
}

std::vector<uint8_t> framebuffer_bytes(const epaper::mono_framebuffer& fb) {
    return {fb.data().begin(), fb.data().end()};
}

std::vector<uint8_t> framebuffer_bytes(const epaper::color6_framebuffer& fb) {
    return {fb.data().begin(), fb.data().end()};
}

std::vector<uint8_t> framebuffer_bytes(const epaper::gray4_framebuffer& fb) {
    std::vector<uint8_t> out(fb.plane(0).begin(), fb.plane(0).end());
    out.insert(out.end(), fb.plane(1).begin(), fb.plane(1).end());
    return out;
}

// The framebuffer of a mode, for a test written once for each.
template<typename Framebuffer>
panel_kind panel_of() {
    if constexpr (std::is_same_v<Framebuffer, epaper::color6_framebuffer>) {
        return color6_panel();
    } else if constexpr (std::is_same_v<Framebuffer, epaper::mono_framebuffer>) {
        return mono_panel();
    } else {
        return gray4_panel();
    }
}

// A pixel value that stands out.
template<typename Framebuffer>
typename Framebuffer::pixel_type mark() {
    if constexpr (std::is_same_v<Framebuffer, epaper::color6_framebuffer>) {
        return epaper::color6::red;
    } else if constexpr (std::is_same_v<Framebuffer, epaper::mono_framebuffer>) {
        return true;
    } else {
        return epaper::gray4::black;
    }
}

template<typename Framebuffer>
Framebuffer make_framebuffer(size_t w, size_t h) {
    auto fb = Framebuffer::make(w, h);
    TEST_ASSERT_TRUE(fb.has_value());
    return std::move(*fb);
}

// A gray panel measured with n levels, as the library builds one.
struct gray_panel {
    std::vector<uint8_t> storage;
    const epi_panel_t* panel = nullptr;
    epi_err_t status = EPI_ERR_INVALID_ARG;
};

gray_panel make_gray_panel(std::span<const epi_ink_t> levels, uint16_t w, uint16_t h) {
    const epi_grey_panel_config_t config{
        .name = "test",
        .width = w,
        .height = h,
        .levels = levels.data(),
        .level_count = levels.size(),
        .detail_sigma = 2.0f,
    };
    gray_panel out;
    out.storage.resize(epi_grey_panel_size(&config));
    out.status = epi_grey_panel_init(&config, out.storage.data(), out.storage.size(), &out.panel, nullptr);
    return out;
}

constexpr epi_ink_t two_levels[] = {
    {0, {0, 0, 0}, {40, 40, 40}},
    {1, {255, 255, 255}, {200, 200, 200}},
};
constexpr epi_ink_t three_levels[] = {
    {0, {0, 0, 0}, {40, 40, 40}},
    {1, {128, 128, 128}, {120, 120, 120}},
    {2, {255, 255, 255}, {200, 200, 200}},
};
constexpr epi_ink_t four_levels[] = {
    {0, {0, 0, 0}, {40, 40, 40}},
    {1, {85, 85, 85}, {90, 90, 90}},
    {2, {170, 170, 170}, {150, 150, 150}},
    {3, {255, 255, 255}, {200, 200, 200}},
};

constexpr epi_intent_t library_intent{0.25f, 0.5f, 0.75f, 1.2f, 1.0f, 12.0f};
constexpr epaper::rendering_intent our_intent{
    .tone = 0.25f,
    .detail = 0.5f,
    .adapt = 0.75f,
    .vibrance = 1.2f,
    .veil = 1.0f,
    .lift = 12.0f,
};

} // namespace

// =============================================================================
// Compile-time tests (static_assert)
// =============================================================================

static_assert(epaper::write_options{}.band_rows == 32);
static_assert(!epaper::write_options{}.intent.has_value());
static_assert(!epaper::write_options{}.memory_limit.has_value());
static_assert(!std::is_default_constructible_v<epaper::rendering_intent>);
static_assert(std::is_copy_constructible_v<epaper::calibration>);
static_assert(std::is_copy_assignable_v<epaper::calibration>);

// Framebuffers and views of them are surfaces; panels are not.
static_assert(epaper::detail::image_surface<epaper::mono_framebuffer>);
static_assert(epaper::detail::image_surface<epaper::gray4_framebuffer>);
static_assert(epaper::detail::image_surface<epaper::color6_framebuffer>);
static_assert(epaper::detail::image_surface<gfx::rotated<epaper::mono_framebuffer>>);
static_assert(epaper::detail::image_surface<gfx::rotated<epaper::gray4_framebuffer>>);
static_assert(epaper::detail::image_surface<gfx::rotated<epaper::color6_framebuffer>>);
static_assert(epaper::detail::image_surface<gfx::canvas<epaper::mono_framebuffer>>);
static_assert(epaper::detail::image_surface<gfx::canvas<gfx::rotated<epaper::color6_framebuffer>>>);
static_assert(!epaper::detail::image_surface<epaper::panel>);
static_assert(!epaper::detail::image_surface<capture_panel>);

// Whether try_write_image accepts a call with these argument types.
template<typename... Args>
concept can_write = requires(Args&&... args) { epaper::try_write_image(std::forward<Args>(args)...); };

// A panel and a surface can be written; a decoder alone cannot.
static_assert(can_write<capture_panel&, const epaper::calibration&, fake_decoder&>);
static_assert(can_write<epaper::color6_framebuffer&, const epaper::calibration&, fake_decoder&>);
static_assert(can_write<gfx::rotated<epaper::mono_framebuffer>&, const epaper::calibration&, fake_decoder&>);
static_assert(!can_write<fake_decoder&, const epaper::calibration&, fake_decoder&>);

using overlay_fn = decltype([](epaper::gray4_framebuffer&, size_t, size_t) {});
using bad_overlay_fn = decltype([](epaper::mono_framebuffer&, size_t, size_t) {});

// Whether a band's type and an overlay go together.
template<typename Framebuffer, typename Overlay>
concept can_overlay = requires(
    capture_panel& display,
    const epaper::calibration& cal,
    fake_decoder& decoder,
    const image::placement& where,
    const epaper::write_options& options,
    Overlay draw
) { epaper::try_write_image<Framebuffer>(display, cal, decoder, where, options, draw); };

static_assert(can_overlay<epaper::gray4_framebuffer, overlay_fn>);
static_assert(!can_overlay<epaper::gray4_framebuffer, bad_overlay_fn>);

// An overlay may follow the decoder or a placement, on a panel.
template<typename Framebuffer, typename... Args>
concept can_write_as = requires(Args&&... args) { epaper::try_write_image<Framebuffer>(std::forward<Args>(args)...); };

static_assert(can_write_as<epaper::gray4_framebuffer, capture_panel&, const epaper::calibration&, fake_decoder&, overlay_fn>);
static_assert(can_write_as<epaper::gray4_framebuffer, capture_panel&, const epaper::calibration&, fake_decoder&, overlay_fn&>);
static_assert(can_write_as<epaper::gray4_framebuffer, capture_panel&, const epaper::calibration&, fake_decoder&, const image::placement&, overlay_fn>);
static_assert(!can_write_as<epaper::gray4_framebuffer, capture_panel&, const epaper::calibration&, fake_decoder&, bad_overlay_fn>);
static_assert(!can_write_as<epaper::gray4_framebuffer, capture_panel&, const epaper::calibration&, fake_decoder&, const image::placement&, bad_overlay_fn>);
static_assert(!can_write_as<epaper::gray4_framebuffer, epaper::gray4_framebuffer&, const epaper::calibration&, fake_decoder&, overlay_fn>);

// The band's type can't be deduced, so an overlay without it isn't taken.
static_assert(!can_write<capture_panel&, const epaper::calibration&, fake_decoder&, overlay_fn>);
static_assert(!can_write<capture_panel&, const epaper::calibration&, fake_decoder&, const image::placement&, overlay_fn>);

// A placement and options aren't taken for an overlay.
static_assert(can_write<capture_panel&, const epaper::calibration&, fake_decoder&, const image::placement&>);
static_assert(can_write<capture_panel&, const epaper::calibration&, fake_decoder&, const image::placement&, const epaper::write_options&>);
static_assert(!can_write_as<epaper::gray4_framebuffer, capture_panel&, const epaper::calibration&, fake_decoder&, const image::placement&>);
static_assert(!can_write_as<epaper::gray4_framebuffer, capture_panel&, const epaper::calibration&, fake_decoder&, const image::placement&, const epaper::write_options&>);

// =============================================================================
// Calibration
// =============================================================================

TEST_CASE("calibration built-ins describe the library's panels", "[epaper_image]") {
    TEST_ASSERT_EQUAL_STRING("EL133UF1", epaper::calibration::el133uf1().name());
    TEST_ASSERT_EQUAL_STRING("GDEY075T7", epaper::calibration::gdey075t7_mono().name());
    TEST_ASSERT_EQUAL_STRING("GDEY075T7", epaper::calibration::gdey075t7_gray4().name());

    for (const auto& kind : all_panels()) {
        TEST_ASSERT_TRUE(epaper::calibration::make(*kind.library).has_value());
        TEST_ASSERT_TRUE(kind.mode == kind.cal->color_mode());
        TEST_ASSERT_EQUAL_UINT32(kind.library->width, kind.cal->width());
        TEST_ASSERT_EQUAL_UINT32(kind.library->height, kind.cal->height());
        TEST_ASSERT_EQUAL_UINT32(kind.library->ink_count, kind.cal->inks().size());
        for (size_t i = 0; i < kind.library->ink_count; ++i) {
            const epi_ink_t& from = kind.library->inks[i];
            const epaper::ink& to = kind.cal->inks()[i];
            TEST_ASSERT_TRUE((image::rgb888{from.nominal.r, from.nominal.g, from.nominal.b}) == to.nominal);
            TEST_ASSERT_TRUE((image::rgb888{from.measured.r, from.measured.g, from.measured.b}) == to.measured);
        }
    }

    // The sizes the panels are written at.
    TEST_ASSERT_EQUAL_UINT32(1200, epaper::calibration::el133uf1().width());
    TEST_ASSERT_EQUAL_UINT32(1600, epaper::calibration::el133uf1().height());
    TEST_ASSERT_EQUAL_UINT32(800, epaper::calibration::gdey075t7_gray4().width());
    TEST_ASSERT_EQUAL_UINT32(480, epaper::calibration::gdey075t7_gray4().height());
}

TEST_CASE("calibration ink values are the framebuffers' pixel values", "[epaper_image]") {
    // Six inks: each the color6 value of its code, and all six of them.
    {
        const auto inks = epaper::calibration::el133uf1().inks();
        TEST_ASSERT_EQUAL_UINT32(6, inks.size());
        std::vector<uint8_t> values;
        for (const auto& i : inks) {
            values.push_back(i.value);
        }
        std::ranges::sort(values);
        const std::vector<uint8_t> all{
            std::to_underlying(epaper::color6::black),
            std::to_underlying(epaper::color6::white),
            std::to_underlying(epaper::color6::yellow),
            std::to_underlying(epaper::color6::red),
            std::to_underlying(epaper::color6::blue),
            std::to_underlying(epaper::color6::green),
        };
        TEST_ASSERT_EQUAL_UINT8_ARRAY(all.data(), values.data(), all.size());
    }

    // Four levels: darkest first, with gray4 values counting from the lightest.
    {
        const auto inks = epaper::calibration::gdey075t7_gray4().inks();
        TEST_ASSERT_EQUAL_UINT32(4, inks.size());
        TEST_ASSERT_EQUAL_UINT8(std::to_underlying(epaper::gray4::black), inks[0].value);
        TEST_ASSERT_EQUAL_UINT8(std::to_underlying(epaper::gray4::dark), inks[1].value);
        TEST_ASSERT_EQUAL_UINT8(std::to_underlying(epaper::gray4::light), inks[2].value);
        TEST_ASSERT_EQUAL_UINT8(std::to_underlying(epaper::gray4::white), inks[3].value);
        TEST_ASSERT_LESS_THAN_UINT8(inks[3].measured.r, inks[0].measured.r);
    }

    // Two levels: the bit in a mono_framebuffer, where set is white.
    {
        const auto inks = epaper::calibration::gdey075t7_mono().inks();
        TEST_ASSERT_EQUAL_UINT32(2, inks.size());
        TEST_ASSERT_EQUAL_UINT8(0, inks[0].value);
        TEST_ASSERT_EQUAL_UINT8(1, inks[1].value);
        TEST_ASSERT_LESS_THAN_UINT8(inks[1].measured.r, inks[0].measured.r);
    }
}

TEST_CASE("calibration presets match the library's", "[epaper_image]") {
    for (const auto& kind : all_panels()) {
        const auto presets = kind.cal->presets();
        TEST_ASSERT_EQUAL_UINT32(kind.library->preset_count, presets.size());
        TEST_ASSERT_GREATER_THAN_UINT32(0, presets.size());
        for (size_t i = 0; i < presets.size(); ++i) {
            const epi_preset_t& from = kind.library->presets[i];
            TEST_ASSERT_EQUAL_STRING(from.name, presets[i].name);
            TEST_ASSERT_TRUE(from.intent->tone == presets[i].intent.tone);
            TEST_ASSERT_TRUE(from.intent->detail == presets[i].intent.detail);
            TEST_ASSERT_TRUE(from.intent->adapt == presets[i].intent.adapt);
            TEST_ASSERT_TRUE(from.intent->vibrance == presets[i].intent.vibrance);
            TEST_ASSERT_TRUE(from.intent->veil == presets[i].intent.veil);
            TEST_ASSERT_TRUE(from.intent->lift == presets[i].intent.lift);
        }
    }
}

TEST_CASE("calibration::try_preset finds the panel's presets by name", "[epaper_image]") {
    for (const auto& kind : all_panels()) {
        for (const auto& p : kind.cal->presets()) {
            const auto found = kind.cal->try_preset(p.name);
            TEST_ASSERT_TRUE(found.has_value());
            TEST_ASSERT_TRUE(found->tone == p.intent.tone);
            TEST_ASSERT_TRUE(found->detail == p.intent.detail);
            TEST_ASSERT_TRUE(found->adapt == p.intent.adapt);
            TEST_ASSERT_TRUE(found->vibrance == p.intent.vibrance);
            TEST_ASSERT_TRUE(found->veil == p.intent.veil);
            TEST_ASSERT_TRUE(found->lift == p.intent.lift);
        }
        TEST_ASSERT_TRUE(kind.cal->try_preset("Vivid").error() == errc::not_found);
        TEST_ASSERT_TRUE(kind.cal->try_preset("").error() == errc::not_found);
    }

    // Presets are the panel's own: Bright is a color panel's. Names ignore ASCII case, but not a trailing NUL.
    const auto& color = epaper::calibration::el133uf1();
    const auto bright = color.try_preset("Bright");
    TEST_ASSERT_TRUE(bright.has_value());
    TEST_ASSERT_TRUE(bright->lift == 16.0f);
    const auto lower = color.try_preset("bRIGHT");
    TEST_ASSERT_TRUE(lower.has_value());
    TEST_ASSERT_TRUE(lower->lift == 16.0f);
    TEST_ASSERT_TRUE(color.try_preset(std::string_view("Bright\0", 7)).error() == errc::not_found);
    TEST_ASSERT_TRUE(color.try_preset("Bright ").error() == errc::not_found);
    TEST_ASSERT_TRUE(epaper::calibration::gdey075t7_gray4().try_preset("Bright").error() == errc::not_found);
}

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
TEST_CASE("calibration::preset throws for a name the panel has no preset for", "[epaper_image]") {
    const auto& color = epaper::calibration::el133uf1();
    TEST_ASSERT_TRUE(color.preset("Bright").lift == 16.0f);
    bool thrown = false;
    try {
        (void)color.preset("Vivid");
    } catch (const std::system_error& e) {
        thrown = e.code() == errc::not_found;
    }
    TEST_ASSERT_TRUE(thrown);
}
#endif

TEST_CASE("calibration::make takes gray panels of 2 and 4 levels only", "[epaper_image]") {
    const auto two = make_gray_panel(two_levels, 122, 250);
    TEST_ASSERT_EQUAL_INT(EPI_OK, two.status);
    auto mono = epaper::calibration::make(*two.panel);
    TEST_ASSERT_TRUE(mono.has_value());
    TEST_ASSERT_TRUE(mono->color_mode() == epaper::color_mode::mono);
    TEST_ASSERT_EQUAL_STRING("test", mono->name());
    TEST_ASSERT_EQUAL_UINT32(122, mono->width());
    TEST_ASSERT_EQUAL_UINT32(250, mono->height());
    TEST_ASSERT_EQUAL_UINT32(3, mono->presets().size());

    const auto four = make_gray_panel(four_levels, 122, 250);
    TEST_ASSERT_EQUAL_INT(EPI_OK, four.status);
    auto gray = epaper::calibration::make(*four.panel);
    TEST_ASSERT_TRUE(gray.has_value());
    TEST_ASSERT_TRUE(gray->color_mode() == epaper::color_mode::gray4);
    TEST_ASSERT_EQUAL_UINT8(3, gray->inks()[0].value);
    TEST_ASSERT_EQUAL_UINT8(0, gray->inks()[3].value);

    const auto three = make_gray_panel(three_levels, 122, 250);
    TEST_ASSERT_EQUAL_INT(EPI_OK, three.status);
    TEST_ASSERT_TRUE(epaper::calibration::make(*three.panel).error() == errc::invalid_arg);
}

TEST_CASE("calibration::make rejects ink codes it cannot map", "[epaper_image]") {
    // Gray levels out of order, or not from 0.
    constexpr epi_ink_t swapped_two[] = {
        {1, {0, 0, 0}, {40, 40, 40}},
        {0, {255, 255, 255}, {200, 200, 200}},
    };
    constexpr epi_ink_t lifted_two[] = {
        {1, {0, 0, 0}, {40, 40, 40}},
        {2, {255, 255, 255}, {200, 200, 200}},
    };
    constexpr epi_ink_t swapped_four[] = {
        {3, {0, 0, 0}, {40, 40, 40}},
        {2, {85, 85, 85}, {90, 90, 90}},
        {1, {170, 170, 170}, {150, 150, 150}},
        {0, {255, 255, 255}, {200, 200, 200}},
    };
    TEST_ASSERT_TRUE(epaper::calibration::make(*make_gray_panel(swapped_two, 8, 8).panel).error() == errc::invalid_arg);
    TEST_ASSERT_TRUE(epaper::calibration::make(*make_gray_panel(lifted_two, 8, 8).panel).error() == errc::invalid_arg);
    TEST_ASSERT_TRUE(epaper::calibration::make(*make_gray_panel(swapped_four, 8, 8).panel).error() == errc::invalid_arg);

    // A color panel with an ink that isn't a color6 value.
    std::array<epi_ink_t, 6> inks{};
    std::copy_n(epi_panel_el133uf1.inks, 6, inks.begin());
    epi_panel_t panel = epi_panel_el133uf1;
    panel.inks = inks.data();
    TEST_ASSERT_TRUE(epaper::calibration::make(panel).has_value());
    inks[2].code = 4;
    TEST_ASSERT_TRUE(epaper::calibration::make(panel).error() == errc::invalid_arg);
    inks[2].code = 7;
    TEST_ASSERT_TRUE(epaper::calibration::make(panel).error() == errc::invalid_arg);

    // Another number of inks.
    inks[2] = epi_panel_el133uf1.inks[2];
    panel.ink_count = 5;
    TEST_ASSERT_TRUE(epaper::calibration::make(panel).error() == errc::invalid_arg);
}

TEST_CASE("calibration::make takes a color panel built from measured inks", "[epaper_image]") {
    // The built-in panel's inks and neutral-cast knots, so its pictures are the expected ones. The knots are those
    // of the library's tools/panelgen/measurements/el133uf1-neutral-2026-11-05.json.
    constexpr epi_neutral_knot_t neutral[] = {
        {36.0f, 0.0f, 0.0f},
        {39.4f, 0.0f, -1.2f},
        {42.7f, 0.6f, -5.1f},
        {46.8f, 1.0f, -6.5f},
        {51.9f, 1.6f, -9.7f},
        {57.7f, 4.7f, -13.5f},
        {61.0f, 6.5f, -11.7f},
        {64.4f, 4.3f, -9.8f},
        {71.3f, 3.8f, -8.6f},
        {78.2f, 2.7f, -8.5f},
        {81.0f, 2.0f, -6.2f},
        {86.0f, 2.0f, -4.8f},
        {91.0f, 0.8f, -2.3f},
        {93.4f, 0.0f, 0.0f},
    };
    const epi_colour_panel_config_t config{
        .name = "measured",
        .width = epi_panel_el133uf1.width,
        .height = epi_panel_el133uf1.height,
        .inks = epi_panel_el133uf1.inks,
        .ink_count = epi_panel_el133uf1.ink_count,
        .detail_sigma = epi_panel_el133uf1.detail_sigma,
        .progress = [](void*, unsigned, unsigned) {
            vTaskDelay(1);
            return true;
        },
        .progress_ctx = nullptr,
        .neutral = neutral,
        .neutral_count = std::size(neutral),
    };
    std::vector<uint8_t> storage(epi_colour_panel_size(&config));
    const epi_panel_t* panel = nullptr;
    TEST_ASSERT_EQUAL_INT(EPI_OK, epi_colour_panel_init(&config, storage.data(), storage.size(), &panel, nullptr));

    auto measured = epaper::calibration::make(*panel);
    TEST_ASSERT_TRUE(measured.has_value());
    TEST_ASSERT_TRUE(measured->color_mode() == epaper::color_mode::color6);
    TEST_ASSERT_EQUAL_STRING("measured", measured->name());
    TEST_ASSERT_EQUAL_UINT32(4, measured->presets().size());

    const auto kind = panel_of<epaper::color6_framebuffer>();
    const auto built_in_inks = kind.cal->inks();
    const auto measured_inks = measured->inks();
    TEST_ASSERT_EQUAL_UINT32(built_in_inks.size(), measured_inks.size());
    for (size_t i = 0; i < measured_inks.size(); ++i) {
        TEST_ASSERT_EQUAL_UINT8(built_in_inks[i].value, measured_inks[i].value);
    }

    const image::rect target{.x = 16, .y = 8, .width = 48, .height = 32};
    auto expected = full_size_panel(kind);
    fake_decoder expected_decoder(source());
    TEST_ASSERT_TRUE(epaper::try_write_image(expected, *kind.cal, expected_decoder, {.target = target}).has_value());
    auto display = full_size_panel(kind);
    fake_decoder decoder(source());
    TEST_ASSERT_TRUE(epaper::try_write_image(display, *measured, decoder, {.target = target}).has_value());
    assert_bytes_equal(expected.bytes(), display.bytes());
}

TEST_CASE("calibration copies hold their own inks", "[epaper_image]") {
    const auto four = make_gray_panel(four_levels, 16, 16);
    auto original = epaper::calibration::make(*four.panel);
    TEST_ASSERT_TRUE(original.has_value());
    const epaper::calibration copy = *original;
    const auto inks = copy.inks();
    TEST_ASSERT_TRUE(inks.data() != original->inks().data());
    *original = epaper::calibration::gdey075t7_mono();
    TEST_ASSERT_EQUAL_UINT32(4, inks.size());
    TEST_ASSERT_EQUAL_UINT8(40, inks[0].measured.r);
    TEST_ASSERT_EQUAL_STRING("test", copy.name());
    TEST_ASSERT_EQUAL_UINT32(2, original->inks().size());
}

// =============================================================================
// Byte equality with the library
// =============================================================================

TEST_CASE("epaper write_image reaches the panel byte for byte as the library converts", "[epaper_image]") {
    constexpr size_t w = 48, h = 32;
    const auto picture = source();
    const library_place place{.how = library_place::cover, .width = w, .height = h};
    for (const auto& kind : all_panels()) {
        const image::rect target{.x = 16, .y = 8, .width = w, .height = h};
        const std::optional<epaper::rendering_intent> intents[] = {std::nullopt, our_intent};
        for (const auto& intent : intents) {
            const auto expected =
                library_panel_bytes(*kind.library, kind.mode, picture, place, intent ? &library_intent : nullptr);
            // 1, divisors of the height, non-divisors, the height, and more than the height.
            for (const size_t band_rows : {1, 8, 13, 32, 100}) {
                auto display = full_size_panel(kind);
                fake_decoder decoder(source());
                auto r = epaper::try_write_image(
                    display, *kind.cal, decoder, {.target = target}, {.intent = intent, .band_rows = band_rows}
                );
                TEST_ASSERT_TRUE(r.has_value());
                assert_bytes_equal(expected, display.bytes());

                // Each write is a full band at its position, but the last.
                const size_t rows = std::min(band_rows, h);
                TEST_ASSERT_EQUAL_UINT32((h + rows - 1) / rows, display.writes.size());
                for (size_t i = 0; i < display.writes.size(); ++i) {
                    TEST_ASSERT_EQUAL_UINT32(target.x, display.writes[i].x);
                    TEST_ASSERT_EQUAL_UINT32(target.y + i * rows, display.writes[i].y);
                    TEST_ASSERT_EQUAL_UINT32(w, display.writes[i].width);
                    TEST_ASSERT_EQUAL_UINT32(std::min(rows, h - i * rows), display.writes[i].rows);
                }
            }
        }
    }
}

TEST_CASE("epaper write_image with each preset's intent matches the library", "[epaper_image]") {
    constexpr size_t w = 32, h = 16;
    const auto picture = source();
    const library_place place{.how = library_place::cover, .width = w, .height = h};
    for (const auto& kind : all_panels()) {
        // No intent is the first preset.
        const auto preset_default = library_panel_bytes(*kind.library, kind.mode, picture, place, kind.library->presets[0].intent);
        assert_bytes_equal(preset_default, library_panel_bytes(*kind.library, kind.mode, picture, place));

        const auto presets = kind.cal->presets();
        for (size_t i = 0; i < presets.size(); ++i) {
            const auto expected =
                library_panel_bytes(*kind.library, kind.mode, picture, place, kind.library->presets[i].intent);
            auto display = full_size_panel(kind);
            fake_decoder decoder(source());
            auto r = epaper::try_write_image(
                display,
                *kind.cal,
                decoder,
                {.target = image::rect{.x = 0, .y = 0, .width = w, .height = h}},
                {.intent = presets[i].intent}
            );
            TEST_ASSERT_TRUE(r.has_value());
            assert_bytes_equal(expected, display.bytes());
        }
    }
}

TEST_CASE("epaper write_image layouts match the library's", "[epaper_image]") {
    constexpr size_t w = 48, h = 32;
    // A 3:2 target on a 4:3 picture, so that both layouts have something to do.
    const auto picture = source();
    for (const auto& kind : all_panels()) {
        const struct {
            image::layout layout;
            library_place place;
        } cases[] = {
            {image::layout::cover, {.how = library_place::cover, .width = w, .height = h}},
            {image::layout::fit, {.how = library_place::fit, .width = w, .height = h}},
            {image::layout::stretch,
             {.how = library_place::stretch, .width = w, .height = h, .dst_w = w, .dst_h = h}},
        };
        for (const auto& c : cases) {
            auto display = full_size_panel(kind);
            fake_decoder decoder(source());
            auto r = epaper::try_write_image(
                display,
                *kind.cal,
                decoder,
                {.target = image::rect{.x = 24, .y = 3, .width = w, .height = h}, .layout = c.layout},
                {.band_rows = 10}
            );
            TEST_ASSERT_TRUE(r.has_value());
            assert_bytes_equal(library_panel_bytes(*kind.library, kind.mode, picture, c.place), display.bytes());
        }
    }
}

TEST_CASE("epaper write_image converts a turned picture as the library converts its rows", "[epaper_image]") {
    constexpr size_t w = 32, h = 48;
    const auto stored = source();
    const image::placement where{.target = image::rect{.x = 0, .y = 0, .width = w, .height = h}};

    // Turning changes the order of the resampling's passes, so the library gets the rows
    // of the placed picture: at the output's own size, its resampler copies them.
    image::buffer<> upright(w, h);
    auto rendered = image::renderer::make(
        stored.width(), stored.height(), image::orientation::rotate_90, where, {.screen_width = w, .screen_height = h}
    );
    TEST_ASSERT_TRUE(rendered.has_value());
    auto pushed = rendered->try_push(
        {0, 0},
        image::view<const image::rgb888>(stored),
        [&](image::point at, image::view<const image::rgb888> rows) -> result<void> {
            std::ranges::copy(rows.row(0), &upright[at.x, at.y]);
            return {};
        }
    );
    TEST_ASSERT_TRUE(pushed.has_value());

    const library_place place{.how = library_place::cover, .width = w, .height = h};
    for (const auto& kind : all_panels()) {
        auto display = full_size_panel(kind);
        fake_decoder decoder(source(), image::orientation::rotate_90);
        TEST_ASSERT_TRUE(epaper::try_write_image(display, *kind.cal, decoder, where, {.band_rows = 7}).has_value());
        assert_bytes_equal(library_panel_bytes(*kind.library, kind.mode, upright, place), display.bytes());
    }
}

// =============================================================================
// Pixel values
// =============================================================================

TEST_CASE("epaper write_image maps gray4 ink codes to levels and planes", "[epaper_image]") {
    constexpr size_t w = 40, h = 16;
    const auto picture = ramp(w, h);
    const auto kind = gray4_panel();
    const auto codes =
        library_convert(*kind.library, picture, {.how = library_place::cover, .width = w, .height = h}, nullptr);

    // The picture has every level, so nothing here passes by luck.
    for (const uint8_t code : {0, 1, 2, 3}) {
        TEST_ASSERT_TRUE(std::ranges::find(codes, code) != codes.end());
    }

    // By level, through a surface: code 0 is the darkest level, gray4::black.
    auto fb = make_framebuffer<epaper::gray4_framebuffer>(w, h);
    fake_decoder into_fb(ramp(w, h));
    TEST_ASSERT_TRUE(epaper::try_write_image(fb, *kind.cal, into_fb).has_value());
    for (size_t y = 0; y < h; ++y) {
        for (size_t x = 0; x < w; ++x) {
            TEST_ASSERT_EQUAL_UINT8(3 - codes[y * w + x], std::to_underlying(fb.get_pixel(x, y)));
        }
    }

    // By bit, on the panel: the level's bit 0 in plane 0, and its bit 1 in plane 1.
    auto display = full_size_panel(kind);
    fake_decoder streamed(ramp(w, h));
    TEST_ASSERT_TRUE(epaper::try_write_image(
                         display, *kind.cal, streamed, {.target = image::rect{.x = 8, .y = 0, .width = w, .height = h}}
    ).has_value());
    for (size_t y = 0; y < h; ++y) {
        for (size_t x = 0; x < w; ++x) {
            const auto level = std::to_underlying(fb.get_pixel(x, y));
            const auto& band = display.writes[0];
            const uint8_t mask = 0x80u >> (x % 8);
            TEST_ASSERT_EQUAL((level & 1) != 0, (band.first[y * (w / 8) + x / 8] & mask) != 0);
            TEST_ASSERT_EQUAL((level & 2) != 0, (band.second[y * (w / 8) + x / 8] & mask) != 0);
        }
    }

    // A black picture and a white one come out with the extreme levels.
    auto levels = [&](image::rgb888 color, epaper::gray4 level) {
        auto out = make_framebuffer<epaper::gray4_framebuffer>(w, h);
        fake_decoder decoder(flat(w, h, color));
        TEST_ASSERT_TRUE(epaper::try_write_image(out, *kind.cal, decoder).has_value());
        size_t count = 0;
        for (size_t y = 0; y < h; ++y) {
            for (size_t x = 0; x < w; ++x) {
                count += out.get_pixel(x, y) == level ? 1 : 0;
            }
        }
        TEST_ASSERT_GREATER_THAN_UINT32(w * h * 9 / 10, count);
    };
    levels({0, 0, 0}, epaper::gray4::black);
    levels({255, 255, 255}, epaper::gray4::white);
}

TEST_CASE("epaper write_image keeps a mono surface's ink and a mono panel's bits apart", "[epaper_image]") {
    constexpr size_t w = 40, h = 16;
    const auto kind = mono_panel();
    const auto picture = source();
    const auto bytes = library_pack(
        *kind.library,
        EPI_PACK_1BPP,
        library_convert(*kind.library, picture, {.how = library_place::cover, .width = w, .height = h}, nullptr),
        w,
        h
    );

    // A set bit in the library's output is its lightest level: a surface's false.
    auto fb = make_framebuffer<epaper::mono_framebuffer>(w, h);
    fake_decoder decoder(source());
    TEST_ASSERT_TRUE(epaper::try_write_image(fb, *kind.cal, decoder).has_value());
    size_t ink = 0;
    for (size_t y = 0; y < h; ++y) {
        for (size_t x = 0; x < w; ++x) {
            const bool white = (bytes[y * (w / 8) + x / 8] & (0x80u >> (x % 8))) != 0;
            TEST_ASSERT_EQUAL(!white, fb.get_pixel(x, y));
            ink += white ? 0 : 1;
        }
    }
    TEST_ASSERT_GREATER_THAN_UINT32(0, ink);
    TEST_ASSERT_LESS_THAN_UINT32(w * h, ink);

    // Black is ink, whose bits are clear, and white is not, whose bits are set.
    auto extremes = [&](image::rgb888 color, bool expect_ink) {
        auto out = make_framebuffer<epaper::mono_framebuffer>(w, h);
        fake_decoder into_fb(flat(w, h, color));
        TEST_ASSERT_TRUE(epaper::try_write_image(out, *kind.cal, into_fb).has_value());
        auto display = full_size_panel(kind);
        fake_decoder streamed(flat(w, h, color));
        TEST_ASSERT_TRUE(epaper::try_write_image(
                             display, *kind.cal, streamed, {.target = image::rect{.x = 0, .y = 0, .width = w, .height = h}}
        ).has_value());
        const auto panel_bytes = display.bytes();
        size_t matching = 0;
        for (size_t y = 0; y < h; ++y) {
            for (size_t x = 0; x < w; ++x) {
                const bool clear = (panel_bytes[y * (w / 8) + x / 8] & (0x80u >> (x % 8))) == 0;
                matching += (out.get_pixel(x, y) == expect_ink && clear == expect_ink) ? 1 : 0;
            }
        }
        TEST_ASSERT_GREATER_THAN_UINT32(w * h * 9 / 10, matching);
    };
    extremes({0, 0, 0}, true);
    extremes({255, 255, 255}, false);
}

namespace {

template<typename Framebuffer>
void check_framebuffer_matches_stream() {
    constexpr size_t w = 48, h = 32;
    const auto kind = panel_of<Framebuffer>();
    const image::placement placements[] = {
        {},
        {.layout = image::layout::fit, .background = {0, 0, 0}},
        {.orientation = image::orientation::rotate_270},
    };
    for (const auto& base : placements) {
        image::placement streaming = base;
        streaming.target = image::rect{.x = 0, .y = 0, .width = w, .height = h};
        auto display = full_size_panel(kind);
        fake_decoder streamed(source());
        TEST_ASSERT_TRUE(epaper::try_write_image(display, *kind.cal, streamed, streaming, {.band_rows = 9}).has_value());

        // The default placement of a surface of the same size is the same picture.
        auto fb = make_framebuffer<Framebuffer>(w, h);
        fake_decoder decoder(source());
        TEST_ASSERT_TRUE(epaper::try_write_image(fb, *kind.cal, decoder, base).has_value());
        assert_bytes_equal(display.bytes(), framebuffer_bytes(fb));

        // And it reaches a panel as the panel's own write sends it.
        capture_panel again(w, h, kind.mode);
        TEST_ASSERT_TRUE(again.try_write(fb).has_value());
        assert_bytes_equal(display.bytes(), again.bytes());
        TEST_ASSERT_EQUAL_UINT32(1, again.writes.size());
    }
}

template<typename Framebuffer>
void check_rotated_surface() {
    constexpr size_t w = 48, h = 32;
    const auto kind = panel_of<Framebuffer>();
    auto expected = make_framebuffer<Framebuffer>(w, h);
    fake_decoder plain(source());
    TEST_ASSERT_TRUE(epaper::try_write_image(expected, *kind.cal, plain).has_value());

    // A 32x48 framebuffer viewed turned a quarter turn is a 48x32 surface.
    auto fb = make_framebuffer<Framebuffer>(h, w);
    gfx::rotated view(fb, gfx::rotation::ccw90);
    TEST_ASSERT_EQUAL_UINT32(w, view.width());
    TEST_ASSERT_EQUAL_UINT32(h, view.height());
    fake_decoder decoder(source());
    TEST_ASSERT_TRUE(epaper::try_write_image(view, *kind.cal, decoder).has_value());
    for (size_t y = 0; y < h; ++y) {
        for (size_t x = 0; x < w; ++x) {
            // ccw90: view (x, y) is framebuffer (width - 1 - y, x).
            TEST_ASSERT_TRUE(expected.get_pixel(x, y) == fb.get_pixel(h - 1 - y, x));
        }
    }
}

template<typename Framebuffer>
void check_canvas_window() {
    constexpr size_t w = 48, h = 32;
    const auto kind = panel_of<Framebuffer>();
    auto expected = make_framebuffer<Framebuffer>(w, h);
    fake_decoder plain(source());
    TEST_ASSERT_TRUE(epaper::try_write_image(expected, *kind.cal, plain).has_value());

    // A window into a larger framebuffer, which is left alone around it.
    auto fb = make_framebuffer<Framebuffer>(w + 20, h + 12);
    fb.fill(mark<Framebuffer>());
    gfx::canvas whole(fb);
    auto window = whole.window(8, 6, w, h);
    fake_decoder decoder(source());
    TEST_ASSERT_TRUE(epaper::try_write_image(window, *kind.cal, decoder).has_value());
    for (size_t y = 0; y < fb.height(); ++y) {
        for (size_t x = 0; x < fb.width(); ++x) {
            const bool inside = x >= 8 && x < 8 + w && y >= 6 && y < 6 + h;
            const auto want = inside ? expected.get_pixel(x - 8, y - 6) : mark<Framebuffer>();
            TEST_ASSERT_TRUE(want == fb.get_pixel(x, y));
        }
    }
}

} // namespace

TEST_CASE("epaper write_image into a framebuffer matches the stream to a panel", "[epaper_image]") {
    check_framebuffer_matches_stream<epaper::color6_framebuffer>();
    check_framebuffer_matches_stream<epaper::mono_framebuffer>();
    check_framebuffer_matches_stream<epaper::gray4_framebuffer>();
}

TEST_CASE("epaper write_image into a rotated view", "[epaper_image]") {
    check_rotated_surface<epaper::color6_framebuffer>();
    check_rotated_surface<epaper::mono_framebuffer>();
    check_rotated_surface<epaper::gray4_framebuffer>();
}

TEST_CASE("epaper write_image into a canvas window", "[epaper_image]") {
    check_canvas_window<epaper::color6_framebuffer>();
    check_canvas_window<epaper::mono_framebuffer>();
    check_canvas_window<epaper::gray4_framebuffer>();
}

// =============================================================================
// Alignment and placement
// =============================================================================

TEST_CASE("epaper write_image rejects an unaligned target on a panel", "[epaper_image]") {
    const auto expect_rejected = [](const panel_kind& kind, const image::rect& target) {
        auto display = full_size_panel(kind);
        fake_decoder decoder(source());
        auto r = epaper::try_write_image(display, *kind.cal, decoder, {.target = target});
        TEST_ASSERT_EQUAL_UINT32(std::to_underlying(errc::invalid_arg), error_value(r));
        TEST_ASSERT_EQUAL_UINT32(0, display.writes.size());
        TEST_ASSERT_EQUAL_UINT32(0, decoder.decodes);
    };
    const auto expect_accepted = [](const panel_kind& kind, const image::rect& target) {
        auto display = full_size_panel(kind);
        fake_decoder decoder(source());
        TEST_ASSERT_TRUE(epaper::try_write_image(display, *kind.cal, decoder, {.target = target}).has_value());
        TEST_ASSERT_EQUAL_UINT32(target.x, display.writes.at(0).x);
    };

    // Six-color panels write two pixels to the byte.
    const auto color = color6_panel();
    expect_rejected(color, {.x = 3, .y = 0, .width = 16, .height = 4});
    expect_rejected(color, {.x = 4, .y = 0, .width = 15, .height = 4});
    expect_accepted(color, {.x = 2, .y = 0, .width = 16, .height = 4});

    // Monochrome and gray ones, eight.
    for (const auto& kind : {mono_panel(), gray4_panel()}) {
        expect_rejected(kind, {.x = 2, .y = 0, .width = 16, .height = 4});
        expect_rejected(kind, {.x = 4, .y = 0, .width = 16, .height = 4});
        expect_rejected(kind, {.x = 8, .y = 0, .width = 15, .height = 4});
        expect_accepted(kind, {.x = 8, .y = 0, .width = 16, .height = 4});
    }

    // Rows have no alignment.
    expect_accepted(color, {.x = 2, .y = 3, .width = 16, .height = 5});
}

TEST_CASE("epaper write_image with fill_to_alignment lands the picture exactly on background", "[epaper_image]") {
    constexpr size_t w = 41, h = 16;
    const auto picture = source();
    for (const auto& kind : all_panels()) {
        // The picture starts one past the alignment, and ends before the next one.
        const int32_t x = static_cast<int32_t>(kind.alignment) + 1;
        const size_t left = kind.alignment;
        const size_t right = (x + w + kind.alignment - 1) / kind.alignment * kind.alignment;
        const library_place place{
            .how = library_place::stretch,
            .width = static_cast<uint16_t>(right - left),
            .height = h,
            .dst_x = x - static_cast<int32_t>(left),
            .dst_y = 0,
            .dst_w = w,
            .dst_h = h,
        };
        auto display = full_size_panel(kind);
        fake_decoder decoder(source());
        auto r = epaper::try_write_image(
            display,
            *kind.cal,
            decoder,
            {.target = image::rect{.x = x, .y = 5, .width = w, .height = h},
             .layout = image::layout::stretch,
             .fill_to_alignment = true}
        );
        TEST_ASSERT_TRUE(r.has_value());
        TEST_ASSERT_EQUAL_UINT32(left, display.writes.at(0).x);
        TEST_ASSERT_EQUAL_UINT32(5, display.writes.at(0).y);
        TEST_ASSERT_EQUAL_UINT32(right - left, display.writes.at(0).width);
        assert_bytes_equal(library_panel_bytes(*kind.library, kind.mode, picture, place), display.bytes());
    }
}

TEST_CASE("epaper write_image takes a right edge at the panel's edge", "[epaper_image]") {
    // A panel whose width isn't a multiple of 8.
    const auto two = make_gray_panel(two_levels, 122, 250);
    const auto four = make_gray_panel(four_levels, 122, 250);
    const auto picture = source();
    const struct {
        const epi_panel_t* panel;
        enum epaper::color_mode mode;
    } panels[] = {{two.panel, epaper::color_mode::mono}, {four.panel, epaper::color_mode::gray4}};
    for (const auto& p : panels) {
        auto cal = epaper::calibration::make(*p.panel);
        TEST_ASSERT_TRUE(cal.has_value());

        // Ending exactly there.
        capture_panel display(122, 250, p.mode);
        fake_decoder decoder(source());
        auto r = epaper::try_write_image(
            display,
            *cal,
            decoder,
            {.target = image::rect{.x = 112, .y = 0, .width = 10, .height = 16}, .layout = image::layout::stretch}
        );
        TEST_ASSERT_TRUE(r.has_value());
        TEST_ASSERT_EQUAL_UINT32(112, display.writes.at(0).x);
        TEST_ASSERT_EQUAL_UINT32(10, display.writes.at(0).width);
        const library_place exact{.how = library_place::stretch, .width = 10, .height = 16, .dst_w = 10, .dst_h = 16};
        assert_bytes_equal(library_panel_bytes(*p.panel, p.mode, picture, exact), display.bytes());

        // Overhanging it: the visible part of the same picture.
        capture_panel overhang(122, 250, p.mode);
        fake_decoder again(source());
        r = epaper::try_write_image(
            overhang,
            *cal,
            again,
            {.target = image::rect{.x = 112, .y = 0, .width = 30, .height = 16}, .layout = image::layout::stretch}
        );
        TEST_ASSERT_TRUE(r.has_value());
        TEST_ASSERT_EQUAL_UINT32(10, overhang.writes.at(0).width);
        const library_place clipped{.how = library_place::stretch, .width = 10, .height = 16, .dst_w = 30, .dst_h = 16};
        assert_bytes_equal(library_panel_bytes(*p.panel, p.mode, picture, clipped), overhang.bytes());

        // The whole width, whose last byte is padded as a framebuffer's is.
        capture_panel whole(122, 250, p.mode);
        fake_decoder streamed(source());
        r = epaper::try_write_image(
            whole, *cal, streamed, {.target = image::rect{.x = 0, .y = 0, .width = 122, .height = 8}}
        );
        TEST_ASSERT_TRUE(r.has_value());
        fake_decoder into_fb(source());
        if (p.mode == epaper::color_mode::mono) {
            auto fb = make_framebuffer<epaper::mono_framebuffer>(122, 8);
            TEST_ASSERT_TRUE(epaper::try_write_image(fb, *cal, into_fb).has_value());
            assert_bytes_equal(framebuffer_bytes(fb), whole.bytes());
        } else {
            auto fb = make_framebuffer<epaper::gray4_framebuffer>(122, 8);
            TEST_ASSERT_TRUE(epaper::try_write_image(fb, *cal, into_fb).has_value());
            assert_bytes_equal(framebuffer_bytes(fb), whole.bytes());
        }
    }
}

TEST_CASE("epaper write_image clips a target that overhangs the top left", "[epaper_image]") {
    constexpr size_t w = 32, h = 16;
    const auto picture = source();
    for (const auto& kind : all_panels()) {
        // Off the left edge by an unaligned amount, with an aligned right edge.
        const library_place place{
            .how = library_place::stretch,
            .width = w,
            .height = h,
            .dst_x = -3,
            .dst_y = -2,
            .dst_w = w + 3,
            .dst_h = h + 2,
        };
        auto display = full_size_panel(kind);
        fake_decoder decoder(source());
        auto r = epaper::try_write_image(
            display,
            *kind.cal,
            decoder,
            {.target = image::rect{.x = -3, .y = -2, .width = w + 3, .height = h + 2}, .layout = image::layout::stretch}
        );
        TEST_ASSERT_TRUE(r.has_value());
        TEST_ASSERT_EQUAL_UINT32(0, display.writes.at(0).x);
        TEST_ASSERT_EQUAL_UINT32(0, display.writes.at(0).y);
        assert_bytes_equal(library_panel_bytes(*kind.library, kind.mode, picture, place), display.bytes());
    }
}

TEST_CASE("epaper write_image has no alignment rules on a surface", "[epaper_image]") {
    constexpr size_t w = 21, h = 9;
    const auto picture = source();
    const auto kind = gray4_panel();
    const auto codes =
        library_convert(*kind.library, picture, {.how = library_place::cover, .width = w, .height = h}, nullptr);
    auto fb = make_framebuffer<epaper::gray4_framebuffer>(40, 20);
    fb.fill(epaper::gray4::dark);
    fake_decoder decoder(source());
    auto r = epaper::try_write_image(fb, *kind.cal, decoder, {.target = image::rect{.x = 3, .y = 2, .width = w, .height = h}});
    TEST_ASSERT_TRUE(r.has_value());
    for (size_t y = 0; y < 20; ++y) {
        for (size_t x = 0; x < 40; ++x) {
            const bool inside = x >= 3 && x < 3 + w && y >= 2 && y < 2 + h;
            const uint8_t want = inside ? 3 - codes[(y - 2) * w + (x - 3)] : std::to_underlying(epaper::gray4::dark);
            TEST_ASSERT_EQUAL_UINT8(want, std::to_underlying(fb.get_pixel(x, y)));
        }
    }
}

TEST_CASE("epaper write_image with a picture off the screen writes nothing", "[epaper_image]") {
    for (const auto& kind : all_panels()) {
        const image::placement where{.target = image::rect{.x = 5000, .y = 0, .width = 16, .height = 16}};
        auto display = full_size_panel(kind);
        fake_decoder decoder(source());
        decoder.fail_at_row = 0; // decoding would be an error
        TEST_ASSERT_TRUE(epaper::try_write_image(display, *kind.cal, decoder, where).has_value());
        TEST_ASSERT_EQUAL_UINT32(0, display.writes.size());
        TEST_ASSERT_EQUAL_UINT32(0, decoder.decodes);
    }

    // With an overlay, and in a surface.
    const auto kind = color6_panel();
    const image::placement where{.target = image::rect{.x = 5000, .y = 0, .width = 16, .height = 16}};
    auto display = full_size_panel(kind);
    fake_decoder decoder(source());
    bool called = false;
    TEST_ASSERT_TRUE(epaper::try_write_image<epaper::color6_framebuffer>(
                         display,
                         *kind.cal,
                         decoder,
                         where,
                         {},
                         [&](epaper::color6_framebuffer&, size_t, size_t) { called = true; }
    ).has_value());
    TEST_ASSERT_FALSE(called);

    auto fb = make_framebuffer<epaper::color6_framebuffer>(16, 16);
    const auto before = framebuffer_bytes(fb);
    TEST_ASSERT_TRUE(epaper::try_write_image(fb, *kind.cal, decoder, where).has_value());
    assert_bytes_equal(before, framebuffer_bytes(fb));
    TEST_ASSERT_EQUAL_UINT32(0, decoder.decodes);
}

// =============================================================================
// Errors
// =============================================================================

TEST_CASE("epaper write_image rejects a panel that isn't the calibration's", "[epaper_image]") {
    const auto expect_rejected = [](capture_panel& display, const epaper::calibration& cal, errc code) {
        fake_decoder decoder(source());
        auto r = epaper::try_write_image(display, cal, decoder);
        TEST_ASSERT_EQUAL_UINT32(std::to_underlying(code), error_value(r));
        TEST_ASSERT_EQUAL_UINT32(0, display.writes.size());
        TEST_ASSERT_EQUAL_UINT32(0, decoder.decodes);
    };

    // The wrong color mode: the panel can be put in the right one.
    capture_panel mono_el133uf1(1200, 1600, epaper::color_mode::mono);
    expect_rejected(mono_el133uf1, epaper::calibration::el133uf1(), errc::invalid_state);
    capture_panel gray_on_mono(800, 480, epaper::color_mode::gray4);
    expect_rejected(gray_on_mono, epaper::calibration::gdey075t7_mono(), errc::invalid_state);
    capture_panel mono_on_gray(800, 480, epaper::color_mode::mono);
    expect_rejected(mono_on_gray, epaper::calibration::gdey075t7_gray4(), errc::invalid_state);

    // The wrong color mode with an overlay, whose band is the calibration's.
    fake_decoder overlaid(source());
    auto r = epaper::try_write_image<epaper::gray4_framebuffer>(
        mono_on_gray, epaper::calibration::gdey075t7_gray4(), overlaid, [](epaper::gray4_framebuffer&, size_t, size_t) {
            TEST_FAIL_MESSAGE("the overlay must not run");
        }
    );
    TEST_ASSERT_EQUAL_UINT32(std::to_underlying(errc::invalid_state), error_value(r));
    TEST_ASSERT_EQUAL_UINT32(0, overlaid.decodes);

    // The wrong size.
    capture_panel narrow(1199, 1600, epaper::color_mode::color6);
    expect_rejected(narrow, epaper::calibration::el133uf1(), errc::invalid_arg);
    capture_panel short_one(800, 479, epaper::color_mode::gray4);
    expect_rejected(short_one, epaper::calibration::gdey075t7_gray4(), errc::invalid_arg);

    // The wrong size and color mode: no color mode would make it the calibration's panel.
    capture_panel wrong_panel(800, 480, epaper::color_mode::mono);
    expect_rejected(wrong_panel, epaper::calibration::el133uf1(), errc::invalid_arg);
}

TEST_CASE("epaper write_image rejects framebuffer types that aren't the calibration's", "[epaper_image]") {
    const auto& color = epaper::calibration::el133uf1();
    const auto& gray = epaper::calibration::gdey075t7_gray4();
    const auto& mono = epaper::calibration::gdey075t7_mono();

    // A surface.
    auto mono_fb = make_framebuffer<epaper::mono_framebuffer>(16, 8);
    auto gray_fb = make_framebuffer<epaper::gray4_framebuffer>(16, 8);
    auto color_fb = make_framebuffer<epaper::color6_framebuffer>(16, 8);
    const auto invalid = std::to_underlying(errc::invalid_arg);
    fake_decoder decoder(source());
    TEST_ASSERT_EQUAL_UINT32(invalid, error_value(epaper::try_write_image(mono_fb, color, decoder)));
    TEST_ASSERT_EQUAL_UINT32(invalid, error_value(epaper::try_write_image(gray_fb, mono, decoder)));
    TEST_ASSERT_EQUAL_UINT32(invalid, error_value(epaper::try_write_image(color_fb, gray, decoder)));

    // The size of a surface doesn't have to be the calibration's.
    TEST_ASSERT_TRUE(epaper::try_write_image(gray_fb, gray, decoder).has_value());

    // An overlay's band.
    capture_panel display(800, 480, epaper::color_mode::gray4);
    fake_decoder overlaid(source());
    auto r = epaper::try_write_image<epaper::mono_framebuffer>(
        display, gray, overlaid, {}, {}, [](epaper::mono_framebuffer&, size_t, size_t) {
            TEST_FAIL_MESSAGE("the overlay must not run");
        }
    );
    TEST_ASSERT_EQUAL_UINT32(invalid, error_value(r));
    TEST_ASSERT_EQUAL_UINT32(0, overlaid.decodes);
    TEST_ASSERT_EQUAL_UINT32(0, display.writes.size());
}

TEST_CASE("epaper write_image propagates panel and decoder errors", "[epaper_image]") {
    constexpr size_t w = 32, h = 24;
    const auto kind = color6_panel();
    const image::placement where{.target = image::rect{.x = 0, .y = 0, .width = w, .height = h}};

    // A panel write fails: stops there, with its error.
    {
        auto display = full_size_panel(kind);
        display.fail_at_write = 1;
        fake_decoder decoder(source());
        auto r = epaper::try_write_image(display, *kind.cal, decoder, where, {.band_rows = 8});
        TEST_ASSERT_EQUAL_UINT32(std::to_underlying(errc::not_allowed), error_value(r));
        TEST_ASSERT_EQUAL_UINT32(2, display.writes.size());
    }

    // A panel write fails at the end, with a short last band.
    {
        auto display = full_size_panel(kind);
        display.fail_at_write = 2;
        fake_decoder decoder(source());
        auto r = epaper::try_write_image(display, *kind.cal, decoder, where, {.band_rows = 10});
        TEST_ASSERT_EQUAL_UINT32(std::to_underlying(errc::not_allowed), error_value(r));
    }

    // The decoder fails: no partial band is written after it.
    {
        auto display = full_size_panel(kind);
        fake_decoder decoder(source());
        decoder.fail_at_row = 30;
        auto r = epaper::try_write_image(display, *kind.cal, decoder, where, {.band_rows = 8});
        TEST_ASSERT_EQUAL_UINT32(std::to_underlying(errc::timeout), error_value(r));

        auto fb = make_framebuffer<epaper::color6_framebuffer>(w, h);
        fake_decoder again(source());
        again.fail_at_row = 30;
        TEST_ASSERT_EQUAL_UINT32(std::to_underlying(errc::timeout), error_value(epaper::try_write_image(fb, *kind.cal, again)));
    }
}

TEST_CASE("epaper write_image rejects a panel that is asleep, before decoding", "[epaper_image]") {
    constexpr size_t w = 32, h = 24;
    const auto kind = gray4_panel();
    const image::placement where{.target = image::rect{.x = 0, .y = 0, .width = w, .height = h}};
    const auto invalid_state = std::to_underlying(errc::invalid_state);

    auto display = full_size_panel(kind);
    TEST_ASSERT_TRUE(display.try_sleep().has_value());

    fake_decoder decoder(source());
    TEST_ASSERT_EQUAL_UINT32(invalid_state, error_value(epaper::try_write_image(display, *kind.cal, decoder, where)));
    TEST_ASSERT_EQUAL_UINT32(0, decoder.decodes);
    TEST_ASSERT_EQUAL_UINT32(0, display.writes.size());

    // With an overlay, which is then not called.
    size_t overlays = 0;
    auto r = epaper::try_write_image<epaper::gray4_framebuffer>(
        display, *kind.cal, decoder, where, {}, [&](epaper::gray4_framebuffer&, size_t, size_t) { ++overlays; }
    );
    TEST_ASSERT_EQUAL_UINT32(invalid_state, error_value(r));
    TEST_ASSERT_EQUAL_UINT32(0, overlays);
    TEST_ASSERT_EQUAL_UINT32(0, decoder.decodes);
    TEST_ASSERT_EQUAL_UINT32(0, display.writes.size());

    // Awake again, the same call writes.
    TEST_ASSERT_TRUE(display.try_wake().has_value());
    TEST_ASSERT_TRUE(epaper::try_write_image(display, *kind.cal, decoder, where).has_value());
    TEST_ASSERT_EQUAL_UINT32(1, decoder.decodes);
    TEST_ASSERT_GREATER_THAN_UINT32(0, display.writes.size());
}

TEST_CASE("epaper write_image rejects zero band rows", "[epaper_image]") {
    const auto kind = color6_panel();
    const auto invalid = std::to_underlying(errc::invalid_arg);
    fake_decoder decoder(source());

    auto display = full_size_panel(kind);
    TEST_ASSERT_EQUAL_UINT32(invalid, error_value(epaper::try_write_image(display, *kind.cal, decoder, {}, {.band_rows = 0})));
    TEST_ASSERT_EQUAL_UINT32(0, display.writes.size());

    auto fb = make_framebuffer<epaper::color6_framebuffer>(16, 8);
    TEST_ASSERT_EQUAL_UINT32(invalid, error_value(epaper::try_write_image(fb, *kind.cal, decoder, {}, {.band_rows = 0})));
    TEST_ASSERT_EQUAL_UINT32(0, decoder.decodes);
}

TEST_CASE("epaper write_image rejects a rendering intent out of range, before decoding", "[epaper_image]") {
    const epaper::rendering_intent bad[] = {
        {.tone = 2.0f, .detail = 0.5f, .adapt = 0.5f, .vibrance = 1.0f, .veil = 0.0f, .lift = 0.0f},
        {.tone = 0.5f, .detail = -1.0f, .adapt = 0.5f, .vibrance = 1.0f, .veil = 0.0f, .lift = 0.0f},
        {.tone = 0.5f, .detail = 0.5f, .adapt = 2.0f, .vibrance = 1.0f, .veil = 0.0f, .lift = 0.0f},
        {.tone = 0.5f, .detail = 0.5f, .adapt = 0.5f, .vibrance = 4.0f, .veil = 0.0f, .lift = 0.0f},
        {.tone = 0.5f, .detail = 0.5f, .adapt = 0.5f, .vibrance = 1.0f, .veil = 4.0f, .lift = 0.0f},
        {.tone = 0.5f, .detail = 0.5f, .adapt = 0.5f, .vibrance = 1.0f, .veil = 0.0f, .lift = 25.0f},
    };
    const auto invalid = std::to_underlying(errc::invalid_arg);
    for (const auto& intent : bad) {
        for (const auto& kind : all_panels()) {
            auto display = full_size_panel(kind);
            fake_decoder decoder(source());
            auto r = epaper::try_write_image(
                display, *kind.cal, decoder, {.target = image::rect{.x = 0, .y = 0, .width = 16, .height = 16}}, {.intent = intent}
            );
            TEST_ASSERT_EQUAL_UINT32(invalid, error_value(r));
            TEST_ASSERT_EQUAL_UINT32(0, decoder.decodes);
        }
        auto fb = make_framebuffer<epaper::color6_framebuffer>(16, 8);
        fake_decoder decoder(source());
        TEST_ASSERT_EQUAL_UINT32(invalid, error_value(epaper::try_write_image(fb, color6_panel().cal[0], decoder, {}, {.intent = intent})));
        TEST_ASSERT_EQUAL_UINT32(0, decoder.decodes);
    }
}

TEST_CASE("epaper write_image memory_limit bounds the renderer, the converter and the band", "[epaper_image]") {
    const image::placement where{.target = image::rect{.x = 0, .y = 0, .width = 32, .height = 16}};
    const auto kind = color6_panel();

    // The smallest limit, to the nearest 64 bytes, that lets a band of this height through.
    const auto smallest_limit = [&](size_t band_rows) {
        for (size_t limit = 0; limit < 1024 * 1024; limit += 64) {
            auto display = full_size_panel(kind);
            fake_decoder decoder(source());
            auto r = epaper::try_write_image(display, *kind.cal, decoder, where, {.band_rows = band_rows, .memory_limit = limit});
            if (r) {
                return limit;
            }
            TEST_ASSERT_TRUE(r.error() == image::errc::too_large);
            TEST_ASSERT_EQUAL_UINT32(0, display.writes.size());
            TEST_ASSERT_EQUAL_UINT32(0, decoder.decodes);
        }
        TEST_FAIL_MESSAGE("no memory_limit let the picture through");
        return size_t{0};
    };

    // The converter counts at the width of the screen, however small the picture.
    const epi_config_t full_width{
        .panel = kind.library,
        .intent = nullptr,
        .width = 1200,
        .height = 1,
    };
    const size_t one_row = smallest_limit(1);
    TEST_ASSERT_GREATER_THAN_UINT32(epi_converter_workspace_size(&full_width), one_row);

    // 15 more rows of the screen's width, at 2 pixels to the byte.
    const size_t sixteen_rows = smallest_limit(16);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(15 * 600 - 64, sixteen_rows - one_row);
    TEST_ASSERT_LESS_OR_EQUAL_UINT32(15 * 600 + 64, sixteen_rows - one_row);

    // Output doesn't depend on the limit.
    const auto picture = source();
    const auto expected = library_panel_bytes(
        *kind.library, kind.mode, picture, {.how = library_place::cover, .width = 32, .height = 16}
    );
    auto display = full_size_panel(kind);
    fake_decoder decoder(source());
    auto r = epaper::try_write_image(display, *kind.cal, decoder, where, {.band_rows = 16, .memory_limit = sixteen_rows});
    TEST_ASSERT_TRUE(r.has_value());
    assert_bytes_equal(expected, display.bytes());

    // Too small for the band and the converter alone.
    fake_decoder unused(source());
    TEST_ASSERT_TRUE(epaper::try_write_image(display, *kind.cal, unused, where, {.memory_limit = 0}).error() == image::errc::too_large);

    // A surface has no band to count: only the converter at its own width.
    auto fb = make_framebuffer<epaper::color6_framebuffer>(32, 16);
    const auto smallest_surface_limit = [&](size_t band_rows) {
        for (size_t limit = 0; limit < 1024 * 1024; limit += 64) {
            fake_decoder d(source());
            auto attempt = epaper::try_write_image(fb, *kind.cal, d, {}, {.band_rows = band_rows, .memory_limit = limit});
            if (attempt) {
                return limit;
            }
            TEST_ASSERT_TRUE(attempt.error() == image::errc::too_large);
        }
        TEST_FAIL_MESSAGE("no memory_limit let the picture through");
        return size_t{0};
    };
    TEST_ASSERT_EQUAL_UINT32(smallest_surface_limit(1), smallest_surface_limit(16));
    TEST_ASSERT_LESS_THAN_UINT32(one_row, smallest_surface_limit(16));

    // A turned picture holds a frame, counted by the renderer.
    fake_decoder turned(source(), image::orientation::rotate_90);
    TEST_ASSERT_TRUE(
        epaper::try_write_image(
            display, *kind.cal, turned, {.target = image::rect{.x = 0, .y = 0, .width = 1200, .height = 1600}}, {.memory_limit = 1024 * 1024}
        ).error() == image::errc::too_large
    );
    TEST_ASSERT_EQUAL_UINT32(0, turned.decodes);
}

// =============================================================================
// Overlay
// =============================================================================

namespace {

template<typename Framebuffer>
void check_overlay() {
    constexpr size_t w = 48, h = 32;
    constexpr int32_t tx = 24, ty = 8;
    // A rectangle crossing the band boundaries and past the picture's right and bottom edges.
    constexpr size_t rx = 40, ry = 12, rw = 80, rh = 80;
    const auto kind = panel_of<Framebuffer>();
    const image::placement where{.target = image::rect{.x = tx, .y = ty, .width = w, .height = h}};

    // The same picture in a framebuffer, drawn on afterward.
    auto expected = make_framebuffer<Framebuffer>(w, h);
    fake_decoder plain(source());
    TEST_ASSERT_TRUE(epaper::try_write_image(expected, *kind.cal, plain).has_value());
    gfx::canvas expected_canvas(expected, tx, ty);
    expected_canvas.fill_rect(rx, ry, rw, rh, mark<Framebuffer>());

    for (const size_t band_rows : {1, 7, 16, 32, 100}) {
        auto display = full_size_panel(kind);
        fake_decoder decoder(source());
        std::vector<image::point> calls;
        auto r = epaper::try_write_image<Framebuffer>(
            display,
            *kind.cal,
            decoder,
            where,
            {.band_rows = band_rows},
            [&](Framebuffer& band, size_t x, size_t y) {
                calls.push_back({x, y});
                TEST_ASSERT_EQUAL_UINT32(w, band.width());
                TEST_ASSERT_EQUAL_UINT32(std::min(band_rows, h), band.height());
                // This band is not yet on the panel.
                TEST_ASSERT_EQUAL_UINT32(display.writes.size() * std::min(band_rows, h), y - ty);
                gfx::canvas canvas(band, x, y);
                canvas.fill_rect(rx, ry, rw, rh, mark<Framebuffer>());
            }
        );
        TEST_ASSERT_TRUE(r.has_value());
        assert_bytes_equal(framebuffer_bytes(expected), display.bytes());

        // Once per band, at the band's top-left on the screen.
        const size_t rows = std::min(band_rows, h);
        TEST_ASSERT_EQUAL_UINT32((h + rows - 1) / rows, calls.size());
        TEST_ASSERT_EQUAL_UINT32(display.writes.size(), calls.size());
        for (size_t i = 0; i < calls.size(); ++i) {
            TEST_ASSERT_EQUAL_UINT32(tx, calls[i].x);
            TEST_ASSERT_EQUAL_UINT32(ty + i * rows, calls[i].y);
        }
    }
}

} // namespace

TEST_CASE("epaper write_image calls the overlay with each band before it is written", "[epaper_image]") {
    check_overlay<epaper::color6_framebuffer>();
    check_overlay<epaper::mono_framebuffer>();
    check_overlay<epaper::gray4_framebuffer>();
}

TEST_CASE("epaper write_image accepts an overlay as an lvalue, const or not", "[epaper_image]") {
    const auto kind = color6_panel();
    const image::placement where{.target = image::rect{.x = 0, .y = 0, .width = 8, .height = 8}};
    size_t calls = 0;

    auto draw = [&](epaper::color6_framebuffer&, size_t, size_t) { ++calls; };
    auto display = full_size_panel(kind);
    fake_decoder decoder(source());
    TEST_ASSERT_TRUE(epaper::try_write_image<epaper::color6_framebuffer>(display, *kind.cal, decoder, where, {}, draw).has_value());
    TEST_ASSERT_EQUAL_UINT32(1, calls);

    const auto const_draw = [&](epaper::color6_framebuffer&, size_t, size_t) { ++calls; };
    fake_decoder again(source());
    TEST_ASSERT_TRUE(epaper::try_write_image<epaper::color6_framebuffer>(display, *kind.cal, again, where, {}, const_draw).has_value());
    TEST_ASSERT_EQUAL_UINT32(2, calls);
}

TEST_CASE("epaper write_image takes an overlay after the decoder or a placement", "[epaper_image]") {
    using band_type = epaper::gray4_framebuffer;
    constexpr size_t w = 16, h = 16;
    // A calibration for a small panel, so the picture can fill the screen.
    const auto library = make_gray_panel(four_levels, w, h);
    TEST_ASSERT_EQUAL_INT(EPI_OK, library.status);
    const auto cal = epaper::calibration::make(*library.panel);
    TEST_ASSERT_TRUE(cal.has_value());

    size_t calls = 0;
    const auto draw = [&](band_type& band, size_t x, size_t y) {
        ++calls;
        gfx::canvas canvas(band, x, y);
        canvas.fill_rect(4, 4, 8, 8, epaper::gray4::black);
    };

    // Without a placement, the picture fills the screen.
    capture_panel full(w, h, epaper::color_mode::gray4);
    fake_decoder full_decoder(source());
    TEST_ASSERT_TRUE(epaper::try_write_image<band_type>(full, *cal, full_decoder, {}, {}, draw).has_value());
    const size_t full_calls = std::exchange(calls, 0);
    TEST_ASSERT_GREATER_THAN_UINT32(0, full_calls);

    capture_panel after_decoder(w, h, epaper::color_mode::gray4);
    fake_decoder decoder(source());
    TEST_ASSERT_TRUE(epaper::try_write_image<band_type>(after_decoder, *cal, decoder, draw).has_value());
    TEST_ASSERT_EQUAL_UINT32(full_calls, std::exchange(calls, 0));
    assert_bytes_equal(full.bytes(), after_decoder.bytes());

    // With a placement, the default options apply.
    const image::placement where{.target = image::rect{.x = 8, .y = 0, .width = 8, .height = 8}};
    capture_panel placed(w, h, epaper::color_mode::gray4);
    fake_decoder placed_decoder(source());
    TEST_ASSERT_TRUE(epaper::try_write_image<band_type>(placed, *cal, placed_decoder, where, {}, draw).has_value());
    const size_t placed_calls = std::exchange(calls, 0);
    TEST_ASSERT_GREATER_THAN_UINT32(0, placed_calls);

    capture_panel after_placement(w, h, epaper::color_mode::gray4);
    fake_decoder placement_decoder(source());
    TEST_ASSERT_TRUE(epaper::try_write_image<band_type>(after_placement, *cal, placement_decoder, where, draw).has_value());
    TEST_ASSERT_EQUAL_UINT32(placed_calls, std::exchange(calls, 0));
    assert_bytes_equal(placed.bytes(), after_placement.bytes());

    // A braced placement.
    capture_panel braced(w, h, epaper::color_mode::gray4);
    fake_decoder braced_decoder(source());
    TEST_ASSERT_TRUE(epaper::try_write_image<band_type>(braced, *cal, braced_decoder, {.target = where.target}, draw).has_value());
    assert_bytes_equal(placed.bytes(), braced.bytes());
}

TEST_CASE("epaper write_image without an overlay takes a braced placement and options", "[epaper_image]") {
    constexpr size_t w = 16, h = 16;
    const auto library = make_gray_panel(four_levels, w, h);
    TEST_ASSERT_EQUAL_INT(EPI_OK, library.status);
    const auto cal = epaper::calibration::make(*library.panel);
    TEST_ASSERT_TRUE(cal.has_value());

    capture_panel expected(w, h, epaper::color_mode::gray4);
    fake_decoder expected_decoder(source());
    TEST_ASSERT_TRUE(epaper::try_write_image(expected, *cal, expected_decoder).has_value());

    // Options after an empty placement.
    static constexpr epaper::write_options OPTIONS{.band_rows = 5};
    capture_panel with_options(w, h, epaper::color_mode::gray4);
    fake_decoder options_decoder(source());
    TEST_ASSERT_TRUE(epaper::try_write_image(with_options, *cal, options_decoder, {}, OPTIONS).has_value());
    TEST_ASSERT_EQUAL_UINT32(4, with_options.writes.size());

    // An empty placement alone.
    capture_panel empty_placement(w, h, epaper::color_mode::gray4);
    fake_decoder empty_decoder(source());
    TEST_ASSERT_TRUE(epaper::try_write_image(empty_placement, *cal, empty_decoder, {}).has_value());
    assert_bytes_equal(expected.bytes(), empty_placement.bytes());

    // A braced placement alone.
    capture_panel with_placement(w, h, epaper::color_mode::gray4);
    fake_decoder placement_decoder(source());
    TEST_ASSERT_TRUE(epaper::try_write_image(
        with_placement, *cal, placement_decoder, {.orientation = image::orientation::rotate_180}
    ).has_value());
    TEST_ASSERT_GREATER_THAN_UINT32(0, with_placement.writes.size());
}

// =============================================================================
// A real decoder
// =============================================================================

TEST_CASE("epaper write_image shows a real JPEG", "[epaper_image]") {
    // Larger than the image, so that it decodes at full size.
    constexpr size_t w = 48, h = 32;
    image::buffer<> decoded(img::baseline_420_width, img::baseline_420_height);
    {
        idfxx::memory_source src{std::span<const uint8_t>(img::baseline_420)};
        auto dec = idfxx::jpeg::decoder::make(src);
        TEST_ASSERT_TRUE(dec.has_value());
        TEST_ASSERT_TRUE(dec->try_decode(image::view<image::rgb888>(decoded)).has_value());
    }
    for (const auto& kind : all_panels()) {
        const auto expected = library_panel_bytes(
            *kind.library, kind.mode, decoded, {.how = library_place::cover, .width = w, .height = h}
        );
        idfxx::memory_source src{std::span<const uint8_t>(img::baseline_420)};
        auto dec = idfxx::jpeg::decoder::make(src);
        TEST_ASSERT_TRUE(dec.has_value());
        auto display = full_size_panel(kind);
        auto r = epaper::try_write_image(
            display, *kind.cal, *dec, {.target = image::rect{.x = 0, .y = 0, .width = w, .height = h}}, {.band_rows = 10}
        );
        TEST_ASSERT_TRUE(r.has_value());
        assert_bytes_equal(expected, display.bytes());
    }
}

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
TEST_CASE("epaper write_image throws on error", "[epaper_image]") {
    constexpr size_t w = 32, h = 16;
    const auto kind = color6_panel();
    const image::placement where{.target = image::rect{.x = 0, .y = 0, .width = w, .height = h}};
    const auto picture = source();
    const auto expected = library_panel_bytes(
        *kind.library, kind.mode, picture, {.how = library_place::cover, .width = w, .height = h}
    );

    // The panel form.
    auto display = full_size_panel(kind);
    fake_decoder decoder(source());
    epaper::write_image(display, *kind.cal, decoder, where, {.band_rows = 5});
    assert_bytes_equal(expected, display.bytes());

    // The overlay form.
    auto overlaid = full_size_panel(kind);
    fake_decoder with_overlay(source());
    size_t calls = 0;
    epaper::write_image<epaper::color6_framebuffer>(
        overlaid, *kind.cal, with_overlay, where, {.band_rows = 5}, [&](epaper::color6_framebuffer&, size_t, size_t) {
            ++calls;
        }
    );
    TEST_ASSERT_EQUAL_UINT32(4, calls);
    assert_bytes_equal(expected, overlaid.bytes());

    // The overlay after a placement.
    calls = 0;
    auto after_placement = full_size_panel(kind);
    fake_decoder for_placement_form(source());
    epaper::write_image<epaper::color6_framebuffer>(
        after_placement, *kind.cal, for_placement_form, where, [&](epaper::color6_framebuffer&, size_t, size_t) { ++calls; }
    );
    TEST_ASSERT_GREATER_THAN_UINT32(0, calls);
    assert_bytes_equal(expected, after_placement.bytes());

    // The surface form.
    auto fb = make_framebuffer<epaper::color6_framebuffer>(w, h);
    fake_decoder into_fb(source());
    epaper::write_image(fb, *kind.cal, into_fb);
    assert_bytes_equal(expected, framebuffer_bytes(fb));

    // Errors throw, in each form.
    const auto throws = [](auto&& write, errc code) {
        try {
            write();
        } catch (const std::system_error& e) {
            TEST_ASSERT_EQUAL_INT(std::to_underlying(code), e.code().value());
            return;
        }
        TEST_FAIL_MESSAGE("expected an exception");
    };
    throws(
        [&] {
            fake_decoder d(source());
            epaper::write_image(display, *kind.cal, d, {}, {.band_rows = 0});
        },
        errc::invalid_arg
    );
    throws(
        [&] {
            fake_decoder d(source());
            epaper::write_image<epaper::mono_framebuffer>(
                display, *kind.cal, d, {}, {}, [](epaper::mono_framebuffer&, size_t, size_t) {}
            );
        },
        errc::invalid_arg
    );
    throws(
        [&] {
            fake_decoder d(source());
            d.fail_at_row = 3;
            epaper::write_image(fb, *kind.cal, d);
        },
        errc::timeout
    );

    // The calibration from a library panel.
    const auto four = make_gray_panel(four_levels, 16, 16);
    const epaper::calibration cal(*four.panel);
    TEST_ASSERT_TRUE(cal.color_mode() == epaper::color_mode::gray4);

    // The overlay after the decoder.
    calls = 0;
    capture_panel small(16, 16, epaper::color_mode::gray4);
    fake_decoder for_decoder_form(source());
    epaper::write_image<epaper::gray4_framebuffer>(
        small, cal, for_decoder_form, [&](epaper::gray4_framebuffer&, size_t, size_t) { ++calls; }
    );
    TEST_ASSERT_GREATER_THAN_UINT32(0, calls);

    // A panel in another color mode.
    throws(
        [&] {
            capture_panel mono_panel(16, 16, epaper::color_mode::mono);
            fake_decoder d(source());
            epaper::write_image(mono_panel, cal, d);
        },
        errc::invalid_state
    );

    const auto three = make_gray_panel(three_levels, 16, 16);
    throws([&] { const epaper::calibration bad(*three.panel); }, errc::invalid_arg);
}
#endif
