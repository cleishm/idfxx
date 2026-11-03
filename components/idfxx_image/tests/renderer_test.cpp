// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Unit tests for the idfxx_image renderer
// Uses ESP-IDF Unity test framework with compile-time static_asserts
//
// The resampler is a port of the one in cleishm/epaper-image. A test-only
// dependency on that library lets these tests compare the two byte for byte.

#include "../../idfxx_jpeg/tests/jpeg_test_images.hpp"
#include "yield_test_utils.hpp"

#include "sdkconfig.h"

#include <epaper_image.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <idfxx/byte_source>
#include <idfxx/error>
#include <idfxx/image>
#include <idfxx/jpeg>
#include <unity.h>

#include <algorithm>
#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <optional>
#include <span>
#include <system_error>
#include <type_traits>
#include <utility>
#include <vector>

using namespace idfxx::image;
using idfxx::result;

namespace img = jpeg_test_images;

// =============================================================================
// Compile-time tests (static_assert)
// These verify correctness at compile time - if this file compiles, they pass.
// =============================================================================

static_assert(sizeof(epi_rgb_t) == sizeof(rgb888));

static_assert(!std::is_copy_constructible_v<renderer>);
static_assert(!std::is_copy_assignable_v<renderer>);
static_assert(std::is_nothrow_move_constructible_v<renderer>);
static_assert(std::is_nothrow_move_assignable_v<renderer>);

// placement's defaults.
static_assert(placement{}.layout == layout::cover);
static_assert(placement{}.orientation == orientation::identity);
static_assert(placement{}.filter == filter::lanczos3);
static_assert(placement{}.background == rgb888{255, 255, 255});
static_assert(!placement{}.fill_to_alignment);
static_assert(render_config{.screen_width = 1, .screen_height = 1}.x_alignment == 1);
static_assert(!std::is_default_constructible_v<render_config>);

// jpeg::decoder is what render() is duck-typed on: pin the names it relies on.
static_assert(std::same_as<decltype(idfxx::jpeg::info::width), size_t>);
static_assert(std::same_as<decltype(idfxx::jpeg::info::height), size_t>);
static_assert(std::same_as<decltype(idfxx::jpeg::info::orientation), orientation>);
static_assert(decoder<idfxx::jpeg::decoder>);
static_assert(scaling_decoder<idfxx::jpeg::decoder>);

// A type with info() but no try_decode() isn't a decoder.
struct info_only {
    struct info_type {
        size_t width = 1;
        size_t height = 1;
        idfxx::image::orientation orientation = orientation::identity;
    };
    const info_type& info() const { return _info; }

private:
    info_type _info;
};

static_assert(!decoder<info_only>);
static_assert(!scaling_decoder<info_only>);

// =============================================================================
// Helpers
// =============================================================================

namespace {

constexpr std::array<orientation, 8> all_orientations{
    orientation::identity,
    orientation::flip_horizontal,
    orientation::rotate_180,
    orientation::flip_vertical,
    orientation::transpose,
    orientation::rotate_90,
    orientation::transverse,
    orientation::rotate_270,
};

constexpr std::array<layout, 3> all_layouts{layout::cover, layout::fit, layout::stretch};

// An image with detail at every frequency, reproducible from its size.
buffer<> noise(size_t w, size_t h) {
    buffer<> out(w, h);
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

bool same_pixels(view<const rgb888> a, view<const rgb888> b) {
    if (a.width() != b.width() || a.height() != b.height()) {
        return false;
    }
    for (size_t y = 0; y < a.height(); ++y) {
        if (a.width() != 0 && std::memcmp(a.row(y).data(), b.row(y).data(), a.width() * sizeof(rgb888)) != 0) {
            return false;
        }
    }
    return true;
}

// Collects a renderer's output, checking the promises the sink relies on.
struct collector {
    explicit collector(rect expected)
        : output(expected)
        , image(expected.width, expected.height) {}

    result<void> operator()(point at, view<const rgb888> rows) {
        TEST_ASSERT_EQUAL(output.x, at.x);
        TEST_ASSERT_EQUAL(output.y + next_row, at.y);
        TEST_ASSERT_EQUAL(output.width, rows.width());
        TEST_ASSERT_LESS_OR_EQUAL(output.height, next_row + rows.height());
        for (size_t y = 0; y < rows.height(); ++y) {
            std::ranges::copy(rows.row(y), image.row(next_row + y).begin());
        }
        next_row += rows.height();
        return {};
    }

    rect output;
    buffer<> image;
    size_t next_row = 0;
};

static_assert(sink<collector>);
static_assert(sink<collector&>);

// Renders an image through a renderer, pushing blocks of the given height.
buffer<> rendered(
    view<const rgb888> src,
    orientation own,
    const placement& where,
    const render_config& config,
    size_t block = 1
) {
    auto r = renderer::make(src.width(), src.height(), own, where, config);
    TEST_ASSERT_TRUE(r.has_value());
    collector out(r->output());
    for (size_t y = 0; y < src.height(); y += block) {
        const size_t n = std::min(block, src.height() - y);
        TEST_ASSERT_TRUE(r->try_push({0, y}, src.subview(0, y, src.width(), n), out).has_value());
    }
    TEST_ASSERT_EQUAL(out.output.height, out.next_row);
    return std::move(out.image);
}

// The sizes of a case for the byte-equality matrix.
struct library_case {
    size_t src_w;
    size_t src_h;
    std::optional<rect> crop;
    layout how;
    filter kernel;
    rect target;
    size_t screen_w;
    size_t screen_h;
};

epi_filter_t library_filter(filter f) {
    switch (f) {
    case filter::box:
        return EPI_FILTER_BOX;
    case filter::bilinear:
        return EPI_FILTER_BILINEAR;
    case filter::bicubic:
        return EPI_FILTER_BICUBIC;
    case filter::lanczos3:
        break;
    }
    return EPI_FILTER_LANCZOS3;
}

// The library lays out a whole source on a whole canvas. Laying out the crop in
// an area of the target's size, then translating both, is what makes that a
// reference for a crop and a target; the library's resampler then runs on an
// identical configuration, on a canvas of the whole screen.
epi_resampler_config_t library_config(const library_case& c) {
    const rect crop = c.crop.value_or(rect{0, 0, c.src_w, c.src_h});
    epi_resampler_config_t cfg{};
    cfg.src_width = static_cast<uint32_t>(crop.width);
    cfg.src_height = static_cast<uint32_t>(crop.height);
    cfg.dst_width = static_cast<uint16_t>(c.target.width);
    cfg.dst_height = static_cast<uint16_t>(c.target.height);
    switch (c.how) {
    case layout::cover:
        epi_layout_cover(&cfg);
        break;
    case layout::fit:
        epi_layout_fit(&cfg);
        break;
    case layout::stretch:
        cfg.src_w = static_cast<float>(crop.width);
        cfg.src_h = static_cast<float>(crop.height);
        cfg.dst_w = cfg.dst_width;
        cfg.dst_h = cfg.dst_height;
        break;
    }
    cfg.src_x += static_cast<float>(crop.x);
    cfg.src_y += static_cast<float>(crop.y);
    cfg.src_width = static_cast<uint32_t>(c.src_w);
    cfg.src_height = static_cast<uint32_t>(c.src_h);
    cfg.dst_x += c.target.x;
    cfg.dst_y += c.target.y;
    cfg.dst_width = static_cast<uint16_t>(c.screen_w);
    cfg.dst_height = static_cast<uint16_t>(c.screen_h);
    cfg.background = {255, 255, 255};
    cfg.filter = library_filter(c.kernel);
    return cfg;
}

buffer<> library_render(view<const rgb888> src, const library_case& c) {
    const epi_resampler_config_t cfg = library_config(c);
    const size_t workspace_size = epi_resampler_workspace_size(&cfg);
    TEST_ASSERT_NOT_EQUAL(0, workspace_size);
    std::vector<uint8_t> workspace(workspace_size);
    epi_resampler_t* rs = nullptr;
    TEST_ASSERT_EQUAL(EPI_OK, epi_resampler_init(&cfg, workspace.data(), workspace.size(), &rs));
    buffer<> screen(c.screen_w, c.screen_h);
    size_t y = 0;
    for (size_t row = 0; row < src.height(); ++row) {
        TEST_ASSERT_EQUAL(EPI_OK, epi_resampler_push(rs, reinterpret_cast<const epi_rgb_t*>(src.row(row).data())));
        while (epi_resampler_pop(rs, reinterpret_cast<epi_rgb_t*>(screen.row(y).data())) == EPI_OK) {
            ++y;
        }
    }
    TEST_ASSERT_EQUAL(c.screen_h, y);
    return screen;
}

// Renders a case both ways and compares what the renderer delivers with the same
// rectangle of the library's canvas.
void check_against_library(view<const rgb888> src, const library_case& c) {
    const buffer<> want = library_render(src, c);
    const placement where{.target = c.target, .layout = c.how, .crop = c.crop, .filter = c.kernel};
    const render_config config{.screen_width = c.screen_w, .screen_height = c.screen_h};
    auto r = renderer::make(c.src_w, c.src_h, orientation::identity, where, config);
    TEST_ASSERT_TRUE(r.has_value());
    const rect out = r->output();
    collector got(out);
    TEST_ASSERT_TRUE(r->try_push({0, 0}, src, got).has_value());
    TEST_ASSERT_EQUAL(out.height, got.next_row);
    if (!same_pixels(got.image, want.subview(out.x, out.y, out.width, out.height))) {
        printf(
            "mismatch: src %ux%u crop %d,%d %ux%u layout %d filter %d target %d,%d %ux%u screen %ux%u\n",
            static_cast<unsigned>(c.src_w),
            static_cast<unsigned>(c.src_h),
            c.crop ? static_cast<int>(c.crop->x) : -1,
            c.crop ? static_cast<int>(c.crop->y) : -1,
            c.crop ? static_cast<unsigned>(c.crop->width) : 0u,
            c.crop ? static_cast<unsigned>(c.crop->height) : 0u,
            static_cast<int>(c.how),
            static_cast<int>(c.kernel),
            static_cast<int>(c.target.x),
            static_cast<int>(c.target.y),
            static_cast<unsigned>(c.target.width),
            static_cast<unsigned>(c.target.height),
            static_cast<unsigned>(c.screen_w),
            static_cast<unsigned>(c.screen_h)
        );
        TEST_FAIL_MESSAGE("renderer output differs from the library's");
    }
}

// Source sizes, with a smaller and a larger target for each. Includes odd sizes,
// one-pixel sides, and sources tall enough for the vertical pass to run first.
struct library_grid {
    size_t src_w;
    size_t src_h;
    size down;
    size up;
};

constexpr std::array<library_grid, 10> library_grids{{
    {37, 23, {16, 11}, {53, 41}},
    {64, 48, {33, 31}, {90, 70}},
    {47, 33, {29, 19}, {61, 59}},
    {16, 16, {7, 5}, {24, 31}},
    {5, 61, {3, 17}, {9, 73}},
    {61, 5, {17, 3}, {73, 9}},
    {1, 21, {1, 7}, {2, 29}},
    {21, 1, {7, 1}, {29, 2}},
    {2, 250, {2, 40}, {3, 47}},
    {3, 304, {2, 31}, {5, 47}},
}};

// A target at each of: inside the screen; hanging over the left and top; hanging
// over the right and bottom; and hanging over all four sides.
struct library_placement {
    int32_t x;
    int32_t y;
    int32_t extra_w; // screen size minus target size
    int32_t extra_h;
};

constexpr std::array<library_placement, 4> library_placements{{
    {2, 1, 5, 3},
    {-3, -2, 0, 0},
    {4, 3, 0, 0},
    {-1, -1, -2, -2},
}};

void check_filter_against_library(filter kernel) {
    for (const library_grid& grid : library_grids) {
        const buffer<> src = noise(grid.src_w, grid.src_h);
        const std::array<size, 3> targets{grid.down, grid.up, size{grid.src_w, grid.src_h}};
        const rect half{
            static_cast<int32_t>(grid.src_w / 4),
            static_cast<int32_t>(grid.src_h / 4),
            std::max<size_t>(grid.src_w / 2, 1),
            std::max<size_t>(grid.src_h / 2, 1),
        };
        // An off-center crop, narrower than the middle of the image where it can be.
        const rect off_center{
            static_cast<int32_t>(std::min<size_t>(grid.src_w - 1, 1)),
            static_cast<int32_t>(grid.src_h / 3),
            std::max<size_t>(grid.src_w * 2 / 3, 1),
            std::max<size_t>(grid.src_h / 3, 1),
        };
        const std::array<std::optional<rect>, 3> crops{std::nullopt, half, off_center};
        for (const size& t : targets) {
            for (layout how : all_layouts) {
                for (const std::optional<rect>& crop : crops) {
                    for (const library_placement& p : library_placements) {
                        const int64_t screen_w = static_cast<int64_t>(t.width) + p.extra_w;
                        const int64_t screen_h = static_cast<int64_t>(t.height) + p.extra_h;
                        if (screen_w <= 0 || screen_h <= 0 || p.x + static_cast<int64_t>(t.width) <= 0 ||
                            p.y + static_cast<int64_t>(t.height) <= 0 || p.x >= screen_w || p.y >= screen_h) {
                            continue; // nothing visible
                        }
                        check_against_library(
                            src,
                            {
                                .src_w = grid.src_w,
                                .src_h = grid.src_h,
                                .crop = crop,
                                .how = how,
                                .kernel = kernel,
                                .target = {p.x, p.y, t.width, t.height},
                                .screen_w = static_cast<size_t>(screen_w),
                                .screen_h = static_cast<size_t>(screen_h),
                            }
                        );
                    }
                }
            }
            vTaskDelay(1);
        }
    }
}

// A decoder with the least render() relies on.
class plain_decoder {
public:
    struct info_type {
        size_t width;
        size_t height;
        idfxx::image::orientation orientation;
    };

    plain_decoder(view<const rgb888> image, orientation own)
        : _image(image)
        , _info{image.width(), image.height(), own} {}

    [[nodiscard]] info_type info() const { return _info; }

    template<sink S>
    [[nodiscard]] result<void> try_decode(S&& out) {
        ++decode_calls;
        for (size_t y = 0; y < _image.height(); ++y) {
            if (auto r = out(point{0, y}, _image.subview(0, y, _image.width(), 1)); !r) {
                return r;
            }
        }
        return {};
    }

    int decode_calls = 0;

private:
    view<const rgb888> _image;
    info_type _info;
};

static_assert(decoder<plain_decoder>);
static_assert(!scaling_decoder<plain_decoder>);

// A decoder that fails without delivering anything.
struct failing_decoder {
    struct info_type {
        size_t width = 8;
        size_t height = 8;
        idfxx::image::orientation orientation = orientation::identity;
    };

    [[nodiscard]] info_type info() const { return {}; }

    template<sink S>
    [[nodiscard]] result<void> try_decode(S&&) {
        return idfxx::error(idfxx::errc::invalid_response);
    }
};

// A decoder that can scale, as jpeg::decoder does: the smallest M/8 that covers.
class scaling_fake_decoder : public plain_decoder {
public:
    scaling_fake_decoder(view<const rgb888> full, orientation own, const buffer<>& half)
        : plain_decoder(half, own)
        , _full_width(full.width())
        , _full_height(full.height())
        , _half_width(half.width())
        , _half_height(half.height()) {}

    [[nodiscard]] info_type info() const {
        info_type i = plain_decoder::info();
        i.width = _full_width;
        i.height = _full_height;
        return i;
    }

    void scale_to_cover(size_t width, size_t height) {
        asked_width = width;
        asked_height = height;
    }

    [[nodiscard]] size_t output_width() const { return _half_width; }
    [[nodiscard]] size_t output_height() const { return _half_height; }

    size_t asked_width = 0;
    size_t asked_height = 0;

private:
    size_t _full_width;
    size_t _full_height;
    size_t _half_width;
    size_t _half_height;
};

static_assert(decoder<scaling_fake_decoder>);
static_assert(scaling_decoder<scaling_fake_decoder>);

// What a scale_to_cover call asks for, without decoding anything.
struct asked_size {
    size_t width;
    size_t height;
};

asked_size asked_for(size_t w, size_t h, orientation own, const placement& where, const render_config& config) {
    // Only the asking matters: the decode is a single pixel.
    const buffer<> half(1, 1);
    scaling_fake_decoder dec(view<const rgb888>(nullptr, w, h), own, half);
    (void)try_render(
        dec,
        [](point, view<const rgb888>) -> result<void> { return {}; },
        where,
        config
    );
    return {dec.asked_width, dec.asked_height};
}

} // namespace

// =============================================================================
// Byte equality with the library
// =============================================================================

TEST_CASE("renderer matches the library byte for byte: box", "[idfxx][image]") {
    check_filter_against_library(filter::box);
}

TEST_CASE("renderer matches the library byte for byte: bilinear", "[idfxx][image]") {
    check_filter_against_library(filter::bilinear);
}

TEST_CASE("renderer matches the library byte for byte: bicubic", "[idfxx][image]") {
    check_filter_against_library(filter::bicubic);
}

TEST_CASE("renderer matches the library byte for byte: lanczos3", "[idfxx][image]") {
    check_filter_against_library(filter::lanczos3);
}

TEST_CASE("renderer matches the library on a crop the size of the target", "[idfxx][image]") {
    const buffer<> src = noise(37, 23);
    for (filter kernel : {filter::box, filter::lanczos3}) {
        // A crop at integer offsets is copied, not convolved, in both axes or neither.
        check_against_library(
            src, {37, 23, rect{5, 3, 11, 7}, layout::stretch, kernel, {2, 1, 11, 7}, 20, 12}
        );
        check_against_library(
            src, {37, 23, rect{5, 3, 11, 7}, layout::cover, kernel, {-2, -1, 11, 7}, 8, 5}
        );
        check_against_library(
            src, {37, 23, rect{5, 3, 11, 8}, layout::stretch, kernel, {0, 0, 11, 7}, 11, 7}
        );
        // Copying on the tall path needs only the one axis to crop.
        const buffer<> tall = noise(3, 304);
        check_against_library(
            tall, {3, 304, rect{0, 10, 3, 200}, layout::stretch, kernel, {0, 0, 3, 40}, 3, 40}
        );
    }
}

// =============================================================================
// Orientation
// =============================================================================

TEST_CASE("renderer orientations equal an oriented identity render", "[idfxx][image]") {
    const buffer<> src = noise(40, 20);
    // Sizes where the layouts' arithmetic gives the same result in either frame:
    // stretch always; cover with an exact source rectangle; fit with an even gap.
    struct geometry {
        layout how;
        size target;
    };
    const std::array<geometry, 4> geometries{{
        {layout::stretch, {13, 29}},
        {layout::cover, {30, 30}},
        {layout::fit, {20, 20}},
        {layout::stretch, {40, 20}},
    }};
    for (filter kernel : {filter::box, filter::bilinear, filter::bicubic, filter::lanczos3}) {
        for (const geometry& g : geometries) {
            const size stored_target{g.target.width, g.target.height};
            for (orientation o : all_orientations) {
                const size t = oriented_size(o, stored_target.width, stored_target.height);
                const buffer<> identity = rendered(
                    src,
                    orientation::identity,
                    {.layout = g.how, .filter = kernel},
                    {.screen_width = stored_target.width, .screen_height = stored_target.height}
                );
                const buffer<> want = oriented(identity, o);
                const render_config config{.screen_width = t.width, .screen_height = t.height};
                // Whether the turn comes from the source or from the placement.
                TEST_ASSERT_TRUE(same_pixels(want, rendered(src, o, {.layout = g.how, .filter = kernel}, config)));
                TEST_ASSERT_TRUE(same_pixels(
                    want,
                    rendered(
                        src, orientation::identity, {.layout = g.how, .orientation = o, .filter = kernel}, config
                    )
                ));
            }
        }
        vTaskDelay(1);
    }
}

TEST_CASE("renderer composes the source's orientation with the placement's", "[idfxx][image]") {
    const buffer<> src = noise(9, 5);
    const buffer<> identity = rendered(
        src, orientation::identity, {.layout = layout::stretch}, {.screen_width = 7, .screen_height = 6}
    );
    for (orientation own : all_orientations) {
        for (orientation extra : all_orientations) {
            const orientation combined = compose(own, extra);
            const size t = oriented_size(combined, 7, 6);
            const buffer<> got = rendered(
                src,
                own,
                {.layout = layout::stretch, .orientation = extra},
                {.screen_width = t.width, .screen_height = t.height}
            );
            TEST_ASSERT_TRUE(same_pixels(oriented(identity, combined), got));
        }
    }
}

TEST_CASE("renderer streams identity and flip, and buffers the other orientations", "[idfxx][image]") {
    const buffer<> src = noise(16, 16);
    for (orientation o : all_orientations) {
        const bool streams = o == orientation::identity || o == orientation::flip_horizontal;
        auto r = renderer::make(
            16,
            16,
            o,
            {.layout = layout::stretch, .filter = filter::bilinear},
            {.screen_width = 8, .screen_height = 8}
        );
        TEST_ASSERT_TRUE(r.has_value());
        collector out(r->output());
        TEST_ASSERT_TRUE(r->try_push({0, 0}, src.subview(0, 0, 16, 15), out).has_value());
        if (streams) {
            TEST_ASSERT_GREATER_THAN(0, out.next_row);
        } else {
            TEST_ASSERT_EQUAL(0, out.next_row);
        }
        TEST_ASSERT_TRUE(r->try_push({0, 15}, src.subview(0, 15, 16, 1), out).has_value());
        TEST_ASSERT_EQUAL(8, out.next_row);
    }
}

TEST_CASE("renderer's streaming and buffered paths agree across frames", "[idfxx][image]") {
    // Where resampling is symmetric, the same picture comes out whichever frame the
    // image is stored in. A copy always is. Box and bilinear at an exact 2:1 are when
    // the axes stay put: the passes round in turn, and swapping the axes swaps them.
    const buffer<> upright = noise(24, 16);
    struct geometry {
        filter kernel;
        size target;
        bool any_frame;
    };
    const std::array<geometry, 3> geometries{{
        {filter::lanczos3, {24, 16}, true},
        {filter::box, {12, 8}, false},
        {filter::bilinear, {12, 8}, false},
    }};
    for (const geometry& g : geometries) {
        const placement where{.layout = layout::stretch, .filter = g.kernel};
        const render_config config{.screen_width = g.target.width, .screen_height = g.target.height};
        const buffer<> want = rendered(upright, orientation::identity, where, config);
        for (orientation o : all_orientations) {
            if (g.any_frame || !swaps_axes(o)) {
                const buffer<> stored = oriented(upright, inverse(o));
                TEST_ASSERT_TRUE(same_pixels(want, rendered(stored, o, where, config)));
            }
        }
    }
}

TEST_CASE("renderer crop is in the displayed orientation", "[idfxx][image]") {
    const buffer<> src = noise(13, 9);
    for (orientation o : all_orientations) {
        const buffer<> displayed = oriented(src, o);
        const rect crop{2, 1, 4, 5};
        // Scale 1 from an integer crop: a copy of exactly those displayed pixels.
        const buffer<> got = rendered(
            src, o, {.layout = layout::stretch, .crop = crop}, {.screen_width = 4, .screen_height = 5}
        );
        TEST_ASSERT_TRUE(same_pixels(displayed.subview(2, 1, 4, 5), got));
    }
    // A crop is checked against the displayed size, not the stored one.
    const render_config config{.screen_width = 4, .screen_height = 5};
    const placement tall{.crop = rect{0, 0, 9, 13}};
    TEST_ASSERT_TRUE(renderer::make(13, 9, orientation::rotate_90, tall, config).has_value());
    TEST_ASSERT_TRUE(renderer::make(13, 9, orientation::identity, tall, config).error() == idfxx::errc::invalid_arg);
}

// =============================================================================
// Clipping
// =============================================================================

TEST_CASE("renderer clipping on every side matches the unclipped picture", "[idfxx][image]") {
    const buffer<> src = noise(23, 17);
    for (layout how : all_layouts) {
        for (orientation o : all_orientations) {
            const placement base{.layout = how, .orientation = o};
            placement whole = base;
            whole.target = rect{0, 0, 19, 15};
            const buffer<> unclipped = rendered(
                src, orientation::identity, whole, {.screen_width = 19, .screen_height = 15}
            );
            // Moves the target so that the screen shows x..x+w, y..y+h of it.
            struct window {
                size_t x;
                size_t y;
                size_t w;
                size_t h;
            };
            for (const window& win : {
                     window{5, 0, 14, 15},  // left
                     window{0, 0, 11, 15},  // right
                     window{0, 4, 19, 11},  // top
                     window{0, 0, 19, 9},   // bottom
                     window{3, 2, 13, 10},  // all four
                     window{18, 14, 1, 1},  // one pixel
                 }) {
                placement clipped = base;
                clipped.target = rect{-static_cast<int32_t>(win.x), -static_cast<int32_t>(win.y), 19, 15};
                const render_config config{.screen_width = win.w, .screen_height = win.h};
                auto r = renderer::make(23, 17, orientation::identity, clipped, config);
                TEST_ASSERT_TRUE(r.has_value());
                TEST_ASSERT_TRUE((r->output() == rect{0, 0, win.w, win.h}));
                const buffer<> got = rendered(src, orientation::identity, clipped, config);
                TEST_ASSERT_TRUE(same_pixels(unclipped.subview(win.x, win.y, win.w, win.h), got));
            }
        }
        vTaskDelay(1);
    }
}

TEST_CASE("renderer output is in screen coordinates", "[idfxx][image]") {
    const placement inside{.target = rect{3, 2, 10, 6}};
    const render_config config{.screen_width = 30, .screen_height = 20};
    auto r = renderer::make(23, 17, orientation::identity, inside, config);
    TEST_ASSERT_TRUE(r.has_value());
    TEST_ASSERT_TRUE((r->output() == rect{3, 2, 10, 6}));
    TEST_ASSERT_FALSE(r->empty());
    // The default target is the whole screen.
    auto whole = renderer::make(23, 17, orientation::identity, {}, config);
    TEST_ASSERT_TRUE(whole.has_value());
    TEST_ASSERT_TRUE((whole->output() == rect{0, 0, 30, 20}));
}

TEST_CASE("renderer fit fills the rest of the target with the background", "[idfxx][image]") {
    const buffer<> src = noise(20, 10);
    const rgb888 bg{1, 2, 3};
    const placement where{.target = rect{1, 1, 20, 20}, .layout = layout::fit, .background = bg};
    const render_config config{.screen_width = 22, .screen_height = 22};
    for (orientation o : {orientation::identity, orientation::rotate_180}) {
        placement oriented_where = where;
        oriented_where.orientation = o;
        const buffer<> got = rendered(src, orientation::identity, oriented_where, config);
        TEST_ASSERT_EQUAL(20, got.width());
        TEST_ASSERT_EQUAL(20, got.height());
        for (size_t y = 0; y < 20; ++y) {
            const bool in_picture = y >= 5 && y < 15;
            for (size_t x = 0; x < 20; ++x) {
                if (!in_picture) {
                    TEST_ASSERT_TRUE((got[x, y] == bg));
                }
            }
        }
        // The picture is an unscaled copy.
        const buffer<> displayed = oriented(src, o);
        TEST_ASSERT_TRUE(same_pixels(displayed, got.subview(0, 5, 20, 10)));
    }
}

TEST_CASE("renderer fit whose picture is off-screen outputs only background", "[idfxx][image]") {
    const buffer<> src = noise(20, 10);
    const rgb888 bg{9, 8, 7};
    // The picture's 10 rows are centerd in the 40-high target: the screen shows
    // only the 5 rows above them.
    for (orientation o : {orientation::identity, orientation::rotate_180}) {
        const buffer<> got = rendered(
            src,
            o,
            {.target = rect{0, 10, 20, 40}, .layout = layout::fit, .background = bg},
            {.screen_width = 20, .screen_height = 15}
        );
        TEST_ASSERT_EQUAL(5, got.height());
        for (size_t y = 0; y < got.height(); ++y) {
            for (const rgb888& px : got.row(y)) {
                TEST_ASSERT_TRUE((px == bg));
            }
        }
    }
}

TEST_CASE("renderer with a fully off-screen target outputs nothing", "[idfxx][image]") {
    const buffer<> src = noise(8, 6);
    for (const rect& target : {
             rect{-4, 0, 4, 4},
             rect{10, 0, 4, 4},
             rect{0, -4, 4, 4},
             rect{0, 10, 4, 4},
             rect{-100, -100, 4, 4},
         }) {
        for (orientation o : {orientation::identity, orientation::rotate_90}) {
            // No limit to be over, however small: nothing is delivered.
            const render_config limited{.screen_width = 10, .screen_height = 10, .memory_limit = 0};
            auto r = renderer::make(8, 6, o, {.target = target}, limited);
            TEST_ASSERT_TRUE(r.has_value());
            TEST_ASSERT_TRUE(r->empty());
            TEST_ASSERT_EQUAL(0, r->output().width);
            TEST_ASSERT_EQUAL(0, r->output().height);
            int calls = 0;
            auto never = [&](point, view<const rgb888>) -> result<void> {
                ++calls;
                return {};
            };
            // Rows are accepted and ignored, but still have to be the right ones.
            TEST_ASSERT_TRUE(r->try_push({0, 0}, src.subview(0, 0, 8, 2), never).has_value());
            TEST_ASSERT_TRUE(r->try_push({0, 3}, src.subview(0, 3, 8, 1), never).error() == idfxx::errc::invalid_state);
            TEST_ASSERT_TRUE(r->try_push({0, 2}, src.subview(0, 2, 4, 1), never).error() == idfxx::errc::invalid_size);
            TEST_ASSERT_TRUE(r->try_push({0, 2}, src.subview(0, 2, 8, 4), never).has_value());
            TEST_ASSERT_EQUAL(0, calls);
        }
    }
}

// =============================================================================
// Alignment
// =============================================================================

TEST_CASE("renderer rejects an unaligned output without fill_to_alignment", "[idfxx][image]") {
    const render_config config{.screen_width = 100, .screen_height = 20, .x_alignment = 8};
    const auto make = [&](rect target) {
        return renderer::make(16, 16, orientation::identity, {.target = target}, config);
    };
    // Aligned both ways.
    TEST_ASSERT_TRUE(make({8, 0, 16, 10}).has_value());
    // Left edge, then right edge.
    TEST_ASSERT_TRUE(make({3, 0, 13, 10}).error() == idfxx::errc::invalid_arg);
    TEST_ASSERT_TRUE(make({8, 0, 13, 10}).error() == idfxx::errc::invalid_arg);
    // The right edge needs no alignment at the edge of the screen, even past it.
    TEST_ASSERT_TRUE(make({96, 0, 4, 10}).has_value());
    TEST_ASSERT_TRUE(make({96, 0, 50, 10}).has_value());
    // It's the visible rectangle that's checked, not the target.
    TEST_ASSERT_TRUE(make({-3, 0, 11, 10}).has_value());
    TEST_ASSERT_TRUE(make({-3, 0, 12, 10}).error() == idfxx::errc::invalid_arg);
    // An off-screen target has no visible rectangle to align.
    TEST_ASSERT_TRUE(make({3, 30, 13, 10}).has_value());
    // Zero isn't an alignment.
    const render_config zero{.screen_width = 100, .screen_height = 20, .x_alignment = 0};
    TEST_ASSERT_TRUE(renderer::make(16, 16, orientation::identity, {}, zero).error() == idfxx::errc::invalid_arg);
}

TEST_CASE("renderer fill_to_alignment keeps the picture in place and fills the gap", "[idfxx][image]") {
    const buffer<> src = noise(23, 17);
    const rgb888 bg{10, 20, 30};
    for (orientation o : {orientation::identity, orientation::flip_horizontal, orientation::rotate_90}) {
        for (layout how : all_layouts) {
            placement where{.layout = how, .orientation = o, .background = bg, .fill_to_alignment = true};
            where.target = rect{3, 2, 12, 9};

            // The picture without any alignment.
            const buffer<> picture =
                rendered(src, orientation::identity, where, {.screen_width = 40, .screen_height = 20});
            const render_config config{.screen_width = 40, .screen_height = 20, .x_alignment = 8};
            auto r = renderer::make(23, 17, orientation::identity, where, config);
            TEST_ASSERT_TRUE(r.has_value());
            TEST_ASSERT_TRUE((r->output() == rect{0, 2, 16, 9}));
            const buffer<> got = rendered(src, orientation::identity, where, config);
            TEST_ASSERT_TRUE(same_pixels(picture, got.subview(3, 0, 12, 9)));
            for (size_t y = 0; y < got.height(); ++y) {
                for (size_t x : {size_t{0}, size_t{1}, size_t{2}, size_t{15}}) {
                    TEST_ASSERT_TRUE((got[x, y] == bg));
                }
            }
        }
    }
}

TEST_CASE("renderer fill_to_alignment stops at the screen", "[idfxx][image]") {
    placement where{.target = rect{33, 0, 5, 5}, .fill_to_alignment = true};
    const render_config config{.screen_width = 36, .screen_height = 5, .x_alignment = 8};
    auto r = renderer::make(8, 8, orientation::identity, where, config);
    TEST_ASSERT_TRUE(r.has_value());
    TEST_ASSERT_TRUE((r->output() == rect{32, 0, 4, 5}));
    const render_config wider{.screen_width = 37, .screen_height = 5, .x_alignment = 8};
    auto rounded = renderer::make(8, 8, orientation::identity, where, wider);
    TEST_ASSERT_TRUE(rounded.has_value());
    TEST_ASSERT_TRUE((rounded->output() == rect{32, 0, 5, 5}));
}

// =============================================================================
// Rows in and out
// =============================================================================

TEST_CASE("renderer rejects out-of-sequence and wrong-width rows", "[idfxx][image]") {
    const buffer<> src = noise(12, 8);
    auto r = renderer::make(12, 8, orientation::identity, {}, {.screen_width = 6, .screen_height = 4});
    TEST_ASSERT_TRUE(r.has_value());
    collector out(r->output());
    const view<const rgb888> all = src;

    TEST_ASSERT_TRUE(r->try_push({0, 1}, all.subview(0, 1, 12, 1), out).error() == idfxx::errc::invalid_state);
    TEST_ASSERT_TRUE(r->try_push({1, 0}, all.subview(0, 0, 12, 1), out).error() == idfxx::errc::invalid_state);
    TEST_ASSERT_TRUE(r->try_push({0, 0}, all.subview(0, 0, 11, 1), out).error() == idfxx::errc::invalid_size);
    TEST_ASSERT_TRUE(r->try_push({0, 0}, all.subview(0, 0, 12, 3), out).has_value());
    // An earlier row, the same one again, one too far ahead.
    TEST_ASSERT_TRUE(r->try_push({0, 1}, all.subview(0, 1, 12, 1), out).error() == idfxx::errc::invalid_state);
    TEST_ASSERT_TRUE(r->try_push({0, 4}, all.subview(0, 4, 12, 1), out).error() == idfxx::errc::invalid_state);
    // An empty block is nothing, and a failed push consumed nothing.
    TEST_ASSERT_TRUE(r->try_push({0, 3}, view<const rgb888>(src.data(), 12, 0), out).has_value());
    TEST_ASSERT_TRUE(r->try_push({0, 3}, all.subview(0, 3, 12, 5), out).has_value());
    // There is nothing after the last row.
    TEST_ASSERT_TRUE(r->try_push({0, 8}, all.subview(0, 0, 12, 1), out).error() == idfxx::errc::invalid_state);
    TEST_ASSERT_EQUAL(4, out.next_row);
}

TEST_CASE("renderer rejects a block running past the end of the image", "[idfxx][image]") {
    const buffer<> big = noise(12, 12);
    auto r = renderer::make(12, 8, orientation::identity, {}, {.screen_width = 6, .screen_height = 4});
    TEST_ASSERT_TRUE(r.has_value());
    collector out(r->output());
    TEST_ASSERT_TRUE(r->try_push({0, 0}, big.subview(0, 0, 12, 9), out).error() == idfxx::errc::invalid_state);
    TEST_ASSERT_EQUAL(0, out.next_row);
}

TEST_CASE("renderer accepts blocks of any height and stride", "[idfxx][image]") {
    const buffer<> src = noise(23, 17);
    for (orientation o : {orientation::identity, orientation::flip_horizontal, orientation::rotate_270}) {
        const placement where{.orientation = o};
        const render_config config{.screen_width = 11, .screen_height = 13};
        const buffer<> want = rendered(src, orientation::identity, where, config);
        for (size_t block : {size_t{2}, size_t{3}, size_t{7}, size_t{17}, size_t{100}}) {
            TEST_ASSERT_TRUE(same_pixels(want, rendered(src, orientation::identity, where, config, block)));
        }
        // Rows inside a larger image.
        buffer<> wide(30, 17);
        for (size_t y = 0; y < 17; ++y) {
            std::ranges::copy(src.row(y), wide.row(y).begin() + 4);
        }
        const view<const rgb888> inner = std::as_const(wide).subview(4, 0, 23, 17);
        TEST_ASSERT_TRUE(same_pixels(want, rendered(inner, orientation::identity, where, config, 5)));
    }
}

TEST_CASE("renderer stops and returns a sink's error", "[idfxx][image]") {
    const buffer<> src = noise(8, 8);
    for (orientation o : {orientation::identity, orientation::rotate_90}) {
        auto r = renderer::make(8, 8, o, {}, {.screen_width = 8, .screen_height = 8});
        TEST_ASSERT_TRUE(r.has_value());
        int calls = 0;
        auto failing = [&](point, view<const rgb888>) -> result<void> {
            ++calls;
            return idfxx::error(idfxx::errc::timeout);
        };
        TEST_ASSERT_TRUE(r->try_push({0, 0}, src, failing).error() == idfxx::errc::timeout);
        TEST_ASSERT_EQUAL(1, calls);
        // The renderer can't continue after that.
        TEST_ASSERT_TRUE(r->try_push({0, 8}, src.subview(0, 0, 8, 1), failing).error() == idfxx::errc::invalid_state);
    }
}

TEST_CASE("renderer moves", "[idfxx][image]") {
    const buffer<> src = noise(8, 8);
    const render_config config{.screen_width = 4, .screen_height = 4};
    const buffer<> want = rendered(src, orientation::identity, {}, config);
    auto a = renderer::make(8, 8, orientation::identity, {}, config);
    auto other = renderer::make(2, 2, orientation::identity, {}, config);
    TEST_ASSERT_TRUE(a.has_value());
    TEST_ASSERT_TRUE(other.has_value());
    renderer b = std::move(*a);
    *other = std::move(b);
    collector out(other->output());
    TEST_ASSERT_TRUE(other->try_push({0, 0}, src, out).has_value());
    TEST_ASSERT_TRUE(same_pixels(want, out.image));
}

// =============================================================================
// Arguments and limits
// =============================================================================

TEST_CASE("renderer rejects invalid arguments", "[idfxx][image]") {
    const render_config config{.screen_width = 10, .screen_height = 10};
    const auto make = [&](const placement& where) {
        return renderer::make(13, 9, orientation::identity, where, config);
    };
    // Empty targets.
    TEST_ASSERT_TRUE(make({.target = rect{0, 0, 0, 5}}).error() == idfxx::errc::invalid_arg);
    TEST_ASSERT_TRUE(make({.target = rect{0, 0, 5, 0}}).error() == idfxx::errc::invalid_arg);
    // Empty crops, and crops outside the image.
    TEST_ASSERT_TRUE(make({.crop = rect{0, 0, 0, 5}}).error() == idfxx::errc::invalid_arg);
    TEST_ASSERT_TRUE(make({.crop = rect{0, 0, 5, 0}}).error() == idfxx::errc::invalid_arg);
    TEST_ASSERT_TRUE(make({.crop = rect{-1, 0, 5, 5}}).error() == idfxx::errc::invalid_arg);
    TEST_ASSERT_TRUE(make({.crop = rect{0, -1, 5, 5}}).error() == idfxx::errc::invalid_arg);
    TEST_ASSERT_TRUE(make({.crop = rect{9, 0, 5, 5}}).error() == idfxx::errc::invalid_arg);
    TEST_ASSERT_TRUE(make({.crop = rect{0, 5, 5, 5}}).error() == idfxx::errc::invalid_arg);
    TEST_ASSERT_TRUE(make({.crop = rect{8, 4, 5, 5}}).has_value());
    // A bad crop is just as wrong on an off-screen target.
    TEST_ASSERT_TRUE(make({.target = rect{50, 0, 5, 5}, .crop = rect{9, 0, 5, 5}}).error() == idfxx::errc::invalid_arg);
    // An empty screen or image.
    TEST_ASSERT_TRUE(
        renderer::make(13, 9, orientation::identity, {}, {.screen_width = 0, .screen_height = 10}).error() ==
        idfxx::errc::invalid_arg
    );
    TEST_ASSERT_TRUE(
        renderer::make(13, 9, orientation::identity, {}, {.screen_width = 10, .screen_height = 0}).error() ==
        idfxx::errc::invalid_arg
    );
    TEST_ASSERT_TRUE(renderer::make(0, 9, orientation::identity, {}, config).error() == idfxx::errc::invalid_arg);
    TEST_ASSERT_TRUE(renderer::make(13, 0, orientation::identity, {}, config).error() == idfxx::errc::invalid_arg);
}

TEST_CASE("renderer limits the source size", "[idfxx][image]") {
    // Off-screen, so that these sizes allocate nothing.
    const placement away{.target = rect{100, 0, 5, 5}};
    const render_config config{.screen_width = 10, .screen_height = 10};
    TEST_ASSERT_TRUE(renderer::make(65535, 1, orientation::identity, away, config).has_value());
    TEST_ASSERT_TRUE(renderer::make(1, 65535, orientation::identity, away, config).has_value());
    TEST_ASSERT_TRUE(renderer::make(10000, 10000, orientation::identity, away, config).has_value());
    TEST_ASSERT_TRUE(renderer::make(65536, 1, orientation::identity, away, config).error() == idfxx::errc::invalid_size);
    TEST_ASSERT_TRUE(renderer::make(1, 65536, orientation::identity, away, config).error() == idfxx::errc::invalid_size);
    TEST_ASSERT_TRUE(renderer::make(10001, 10000, orientation::identity, away, config).error() == idfxx::errc::invalid_size);
    TEST_ASSERT_TRUE(renderer::make(65535, 65535, orientation::identity, away, config).error() == idfxx::errc::invalid_size);
}

TEST_CASE("renderer memory_limit is checked before allocating", "[idfxx][image]") {
    // These would not fit in this memory if the limit weren't checked first.
    const placement rotated{.orientation = orientation::rotate_90};
    render_config config{.screen_width = 1200, .screen_height = 1600, .memory_limit = 1024 * 1024};
    TEST_ASSERT_TRUE(renderer::make(4000, 3000, orientation::identity, rotated, config).error() == idfxx::image::errc::too_large);
    config.memory_limit = 1024;
    TEST_ASSERT_TRUE(renderer::make(4000, 3000, orientation::identity, {}, config).error() == idfxx::image::errc::too_large);
    // Over what a size_t can even hold.
    config.screen_width = 60000;
    config.screen_height = 60000;
    config.memory_limit = std::nullopt;
    TEST_ASSERT_TRUE(renderer::make(4000, 3000, orientation::identity, rotated, config).error() == idfxx::errc::invalid_size);
}

TEST_CASE("renderer memory_limit is the exact working memory", "[idfxx][image]") {
    const buffer<> src = noise(23, 17);
    for (orientation o : {orientation::identity, orientation::rotate_90}) {
        const placement where{.orientation = o};
        // The smallest limit that's accepted.
        size_t lo = 0;
        size_t hi = 1024 * 1024;
        while (lo + 1 < hi) {
            const size_t mid = lo + (hi - lo) / 2;
            render_config config{.screen_width = 11, .screen_height = 13, .memory_limit = mid};
            if (renderer::make(23, 17, orientation::identity, where, config).has_value()) {
                hi = mid;
            } else {
                lo = mid;
            }
        }
        render_config config{.screen_width = 11, .screen_height = 13, .memory_limit = hi};
        auto r = renderer::make(23, 17, orientation::identity, where, config);
        TEST_ASSERT_TRUE(r.has_value());
        // A frame on the buffered path; at least the rows and weights on either.
        TEST_ASSERT_GREATER_THAN(11 * 3, hi);
        if (o != orientation::identity) {
            TEST_ASSERT_GREATER_THAN(11 * 13 * 3, hi);
        }
        config.memory_limit = hi - 1;
        TEST_ASSERT_TRUE(renderer::make(23, 17, orientation::identity, where, config).error() == idfxx::image::errc::too_large);
        collector out(r->output());
        TEST_ASSERT_TRUE(r->try_push({0, 0}, src, out).has_value());
        TEST_ASSERT_EQUAL(13, out.next_row);
    }
}

// =============================================================================
// render()
// =============================================================================

TEST_CASE("try_render matches a renderer fed by hand", "[idfxx][image]") {
    const buffer<> src = noise(23, 17);
    const render_config config{.screen_width = 11, .screen_height = 13};
    for (orientation own : all_orientations) {
        const placement where{
            .target = rect{2, 1, 8, 9},
            .layout = layout::fit,
            .orientation = orientation::rotate_180,
        };
        const buffer<> want = rendered(src, own, where, config);
        plain_decoder dec(src, own);
        auto r = renderer::make(23, 17, own, where, config);
        TEST_ASSERT_TRUE(r.has_value());
        collector out(r->output());
        TEST_ASSERT_TRUE(try_render(dec, out, where, config).has_value());
        TEST_ASSERT_EQUAL(1, dec.decode_calls);
        TEST_ASSERT_EQUAL(want.height(), out.next_row);
        TEST_ASSERT_TRUE(same_pixels(want, out.image));
    }
}

TEST_CASE("try_render does not decode for an off-screen target or a bad placement", "[idfxx][image]") {
    const buffer<> src = noise(8, 8);
    plain_decoder dec(src, orientation::identity);
    auto never = [](point, view<const rgb888>) -> result<void> {
        TEST_FAIL_MESSAGE("nothing should be delivered");
        return {};
    };
    const render_config config{.screen_width = 10, .screen_height = 10};
    TEST_ASSERT_TRUE(try_render(dec, never, {.target = rect{10, 0, 5, 5}}, config).has_value());
    TEST_ASSERT_TRUE(try_render(dec, never, {.target = rect{0, 0, 0, 5}}, config).error() == idfxx::errc::invalid_arg);
    TEST_ASSERT_TRUE(try_render(dec, never, {.crop = rect{0, 0, 9, 1}}, config).error() == idfxx::errc::invalid_arg);
    render_config small = config;
    small.memory_limit = 1;
    TEST_ASSERT_TRUE(try_render(dec, never, {}, small).error() == idfxx::image::errc::too_large);
    TEST_ASSERT_EQUAL(0, dec.decode_calls);
}

TEST_CASE("renderer::make with a decoder reports the output before anything is decoded", "[idfxx][image]") {
    const buffer<> src = noise(8, 8);
    plain_decoder dec(src, orientation::identity);
    const render_config config{.screen_width = 20, .screen_height = 10};

    // The visible part of the target, clipped to the screen.
    auto r = renderer::make(dec, {.target = rect{4, 2, 30, 4}, .layout = layout::stretch}, config);
    TEST_ASSERT_TRUE(r.has_value());
    TEST_ASSERT_FALSE(r->empty());
    TEST_ASSERT_TRUE(r->output() == (rect{4, 2, 16, 4}));

    // Off-screen is an empty renderer that accepts and ignores rows, not an error.
    auto away = renderer::make(dec, {.target = rect{20, 0, 5, 5}}, config);
    TEST_ASSERT_TRUE(away.has_value());
    TEST_ASSERT_TRUE(away->empty());
    auto never = [](point, view<const rgb888>) -> result<void> {
        TEST_FAIL_MESSAGE("nothing should be delivered");
        return {};
    };
    TEST_ASSERT_TRUE(away->try_push({0, 0}, view<const rgb888>(src), never).has_value());

    // An image too large to render is no problem off-screen, as for try_render.
    plain_decoder huge(view<const rgb888>(nullptr, 70000, 70000), orientation::identity);
    auto huge_away = renderer::make(huge, {.target = rect{20, 0, 5, 5}}, config);
    TEST_ASSERT_TRUE(huge_away.has_value());
    TEST_ASSERT_TRUE(huge_away->empty());
    TEST_ASSERT_TRUE(renderer::make(huge, {}, config).error() == idfxx::errc::invalid_size);

    // Bad placements and limits fail as for renderer::make.
    TEST_ASSERT_TRUE(renderer::make(dec, {.target = rect{0, 0, 0, 5}}, config).error() == idfxx::errc::invalid_arg);
    TEST_ASSERT_TRUE(renderer::make(dec, {.crop = rect{0, 0, 9, 1}}, config).error() == idfxx::errc::invalid_arg);
    render_config small = config;
    small.memory_limit = 1;
    TEST_ASSERT_TRUE(renderer::make(dec, {}, small).error() == idfxx::image::errc::too_large);

    TEST_ASSERT_EQUAL(0, dec.decode_calls);
    TEST_ASSERT_EQUAL(0, huge.decode_calls);
}

TEST_CASE("renderer::make with a decoder selects the decode scale, and renders as try_render does", "[idfxx][image]") {
    const buffer<> half = noise(100, 75);
    const render_config config{.screen_width = 100, .screen_height = 75};

    // Decoding through renderer::make's renderer...
    scaling_fake_decoder dec(view<const rgb888>(nullptr, 800, 600), orientation::identity, half);
    auto r = renderer::make(dec, {}, config);
    TEST_ASSERT_TRUE(r.has_value());
    TEST_ASSERT_EQUAL(100, dec.asked_width);
    TEST_ASSERT_EQUAL(75, dec.asked_height);
    std::vector<rgb888> actual;
    auto keep_actual = [&](point, view<const rgb888> rows) -> result<void> {
        actual.insert(actual.end(), rows.row(0).begin(), rows.row(0).end());
        return {};
    };
    TEST_ASSERT_TRUE(dec.try_decode([&](point at, view<const rgb888> rows) -> result<void> {
                            return r->try_push(at, rows, keep_actual);
                        }).has_value());

    // ...gives what try_render gives.
    scaling_fake_decoder again(view<const rgb888>(nullptr, 800, 600), orientation::identity, half);
    std::vector<rgb888> expected;
    auto keep_expected = [&](point, view<const rgb888> rows) -> result<void> {
        expected.insert(expected.end(), rows.row(0).begin(), rows.row(0).end());
        return {};
    };
    TEST_ASSERT_TRUE(try_render(again, keep_expected, {}, config).has_value());
    TEST_ASSERT_EQUAL(100 * 75, expected.size());
    TEST_ASSERT_TRUE(actual == expected);
}

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
TEST_CASE("renderer constructed from a decoder throws on error", "[idfxx][image]") {
    const buffer<> src = noise(8, 8);
    plain_decoder dec(src, orientation::identity);
    const render_config config{.screen_width = 20, .screen_height = 10};
    renderer r(dec, {.target = rect{4, 2, 8, 4}, .layout = layout::stretch}, config);
    TEST_ASSERT_TRUE(r.output() == (rect{4, 2, 8, 4}));
    TEST_ASSERT_EQUAL(0, dec.decode_calls);
    try {
        renderer bad(dec, {.target = rect{0, 0, 0, 5}}, config);
        TEST_FAIL_MESSAGE("expected an exception");
    } catch (const std::system_error& e) {
        TEST_ASSERT_TRUE(e.code() == idfxx::errc::invalid_arg);
    }
}
#endif

TEST_CASE("try_render returns the errors of the decoder and the sink", "[idfxx][image]") {
    const buffer<> src = noise(8, 8);
    const render_config config{.screen_width = 8, .screen_height = 8};
    plain_decoder dec(src, orientation::identity);
    auto failing = [](point, view<const rgb888>) -> result<void> { return idfxx::error(idfxx::errc::timeout); };
    TEST_ASSERT_TRUE(try_render(dec, failing, {}, config).error() == idfxx::errc::timeout);

    failing_decoder bad;
    auto fine = [](point, view<const rgb888>) -> result<void> { return {}; };
    TEST_ASSERT_TRUE(try_render(bad, fine, {}, config).error() == idfxx::errc::invalid_response);
}

TEST_CASE("try_render picks the decode scale from the crop, the target and the orientation", "[idfxx][image]") {
    const render_config config{.screen_width = 200, .screen_height = 200};
    const rect target{0, 0, 100, 75};

    // The whole image, in the same proportions as the target.
    asked_size a = asked_for(800, 600, orientation::identity, {.target = target}, config);
    TEST_ASSERT_EQUAL(100, a.width);
    TEST_ASSERT_EQUAL(75, a.height);

    // A quarter-size crop has to be decoded at twice the scale.
    a = asked_for(800, 600, orientation::identity, {.target = target, .crop = rect{100, 100, 400, 300}}, config);
    TEST_ASSERT_EQUAL(200, a.width);
    TEST_ASSERT_EQUAL(150, a.height);

    // Cover scales by the larger ratio, in both axes.
    a = asked_for(800, 800, orientation::identity, {.target = target}, config);
    TEST_ASSERT_EQUAL(100, a.width);
    TEST_ASSERT_EQUAL(100, a.height);

    // Fit by the smaller.
    a = asked_for(800, 800, orientation::identity, {.target = target, .layout = layout::fit}, config);
    TEST_ASSERT_EQUAL(75, a.width);
    TEST_ASSERT_EQUAL(75, a.height);

    // Stretch by each axis's own.
    a = asked_for(800, 800, orientation::identity, {.target = target, .layout = layout::stretch}, config);
    TEST_ASSERT_EQUAL(100, a.width);
    TEST_ASSERT_EQUAL(75, a.height);

    // In the stored orientation, when the transform swaps the axes, and whichever
    // orientation swaps them.
    a = asked_for(600, 800, orientation::rotate_90, {.target = target}, config);
    TEST_ASSERT_EQUAL(75, a.width);
    TEST_ASSERT_EQUAL(100, a.height);
    a = asked_for(600, 800, orientation::identity, {.target = target, .orientation = orientation::rotate_270}, config);
    TEST_ASSERT_EQUAL(75, a.width);
    TEST_ASSERT_EQUAL(100, a.height);

    // A clipped target still needs its full resolution.
    a = asked_for(800, 600, orientation::identity, {.target = rect{-50, -30, 100, 75}}, config);
    TEST_ASSERT_EQUAL(100, a.width);
    TEST_ASSERT_EQUAL(75, a.height);

    // Enlarging can't be asked of a decoder that never enlarges.
    a = asked_for(80, 60, orientation::identity, {.target = target}, config);
    TEST_ASSERT_EQUAL(80, a.width);
    TEST_ASSERT_EQUAL(60, a.height);
}

TEST_CASE("try_render applies the crop to a reduced-scale decode", "[idfxx][image]") {
    // A decoder that gives half-size pixels, and a crop in full-size coordinates.
    const buffer<> half = noise(30, 20);
    const view<const rgb888> full(nullptr, 60, 40);
    const render_config config{.screen_width = 16, .screen_height = 12};
    for (orientation own : {orientation::identity, orientation::rotate_90}) {
        const size displayed_half = oriented_size(own, 30, 20);
        const rect full_crop{4, 8, displayed_half.width, displayed_half.height};
        const rect half_crop{2, 4, displayed_half.width / 2, displayed_half.height / 2};
        const placement via_full{.layout = layout::stretch, .crop = full_crop};
        const placement via_half{.layout = layout::stretch, .crop = half_crop};
        const buffer<> want = rendered(half, own, via_half, config);
        scaling_fake_decoder dec(full, own, half);
        collector out(rect{0, 0, 16, 12});
        TEST_ASSERT_TRUE(try_render(dec, out, via_full, config).has_value());
        TEST_ASSERT_EQUAL(12, out.next_row);
        TEST_ASSERT_TRUE(same_pixels(want, out.image));
    }
}

TEST_CASE("try_render drives a jpeg::decoder", "[idfxx][image]") {
    // At full size.
    {
        idfxx::memory_source src{std::span<const uint8_t>(img::baseline_420)};
        auto dec = idfxx::jpeg::decoder::make(src);
        TEST_ASSERT_TRUE(dec.has_value());
        const render_config config{.screen_width = 37, .screen_height = 23};
        buffer<> direct(37, 23);
        TEST_ASSERT_TRUE(dec->try_decode(view<rgb888>(direct)).has_value());

        idfxx::memory_source again{std::span<const uint8_t>(img::baseline_420)};
        auto dec2 = idfxx::jpeg::decoder::make(again);
        TEST_ASSERT_TRUE(dec2.has_value());
        collector out(rect{0, 0, 37, 23});
        TEST_ASSERT_TRUE(try_render(*dec2, out, {}, config).has_value());
        TEST_ASSERT_TRUE(same_pixels(direct, out.image));
    }
    // Scaled to the target: the same as decoding at that scale and rendering it.
    {
        const size_t w = img::scaled_outputs[3].width;
        const size_t h = img::scaled_outputs[3].height;
        const render_config config{.screen_width = w, .screen_height = h};

        idfxx::memory_source src{std::span<const uint8_t>(img::scaled)};
        auto dec = idfxx::jpeg::decoder::make(src);
        TEST_ASSERT_TRUE(dec.has_value());
        dec->scale_to_cover(w, h);
        TEST_ASSERT_EQUAL(w, dec->output_width());
        TEST_ASSERT_EQUAL(h, dec->output_height());
        buffer<> direct(w, h);
        TEST_ASSERT_TRUE(dec->try_decode(view<rgb888>(direct)).has_value());

        idfxx::memory_source again{std::span<const uint8_t>(img::scaled)};
        auto dec2 = idfxx::jpeg::decoder::make(again);
        TEST_ASSERT_TRUE(dec2.has_value());
        collector out(rect{0, 0, w, h});
        TEST_ASSERT_TRUE(try_render(*dec2, out, {.layout = layout::stretch}, config).has_value());
        TEST_ASSERT_TRUE(same_pixels(direct, out.image));
    }
}

// =============================================================================
// Giving way to other tasks
// =============================================================================

TEST_CASE("renderer lets lower-priority tasks run while a frame fills", "[idfxx][image]") {
    const buffer<> src = noise(8, 8);
    const render_config config{.screen_width = 8, .screen_height = 8};

    uint32_t counted = 0;
    size_t delivered = 0;
    bool pushed = false;
    {
        yield_test::background_counter counter;
        auto r = renderer::make(8, 8, orientation::rotate_90, {}, config);
        if (r) {
            yield_test::busy_wait(yield_test::past_yield_interval);
            const uint32_t before = counter.count();
            pushed = r->try_push(
                          {0, 0},
                          src.subview(0, 0, 8, 1),
                          [&](point, view<const rgb888>) -> result<void> {
                              ++delivered;
                              return {};
                          }
            ).has_value();
            counted = counter.count() - before;
        }
    }
    TEST_ASSERT_TRUE(pushed);
    TEST_ASSERT_EQUAL_UINT32(0, delivered);
    TEST_ASSERT_GREATER_THAN_UINT32(0, counted);
}

TEST_CASE("renderer lets lower-priority tasks run while a slow sink takes rows", "[idfxx][image]") {
    const buffer<> src = noise(8, 8);
    const render_config config{.screen_width = 8, .screen_height = 8};
    const auto per_row = yield_test::past_yield_interval / 8;

    // Row by row as the source rows arrive, and in one burst after the last of them.
    for (const orientation o : {orientation::identity, orientation::rotate_90}) {
        uint32_t counted = 0;
        size_t delivered = 0;
        bool pushed = false;
        {
            yield_test::background_counter counter;
            auto r = renderer::make(8, 8, o, {}, config);
            if (r) {
                pushed = r->try_push({0, 0}, src, [&](point, view<const rgb888>) -> result<void> {
                    ++delivered;
                    yield_test::busy_wait(per_row);
                    return {};
                }).has_value();
                counted = counter.count();
            }
        }
        TEST_ASSERT_TRUE(pushed);
        TEST_ASSERT_EQUAL_UINT32(8, delivered);
        TEST_ASSERT_GREATER_THAN_UINT32(0, counted);
    }
}

// =============================================================================
// Exception-based API
// =============================================================================

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS

TEST_CASE("renderer's exception API matches try_*", "[idfxx][image]") {
    const buffer<> src = noise(8, 8);
    const render_config config{.screen_width = 4, .screen_height = 4};
    const buffer<> want = rendered(src, orientation::identity, {}, config);

    renderer r(8, 8, orientation::identity, {}, config);
    collector out(r.output());
    r.push({0, 0}, src.subview(0, 0, 8, 3), out);
    r.push({0, 3}, src.subview(0, 3, 8, 5), out);
    TEST_ASSERT_TRUE(same_pixels(want, out.image));

    bool threw = false;
    try {
        renderer bad(8, 8, orientation::identity, {.crop = rect{0, 0, 9, 1}}, config);
    } catch (const std::system_error& e) {
        threw = e.code() == idfxx::errc::invalid_arg;
    }
    TEST_ASSERT_TRUE(threw);

    threw = false;
    try {
        r.push({0, 0}, src.subview(0, 0, 8, 1), out);
    } catch (const std::system_error& e) {
        threw = e.code() == idfxx::errc::invalid_state;
    }
    TEST_ASSERT_TRUE(threw);
}

TEST_CASE("render's exception API matches try_render", "[idfxx][image]") {
    const buffer<> src = noise(8, 8);
    const render_config config{.screen_width = 4, .screen_height = 4};
    const buffer<> want = rendered(src, orientation::identity, {}, config);

    plain_decoder dec(src, orientation::identity);
    collector out(rect{0, 0, 4, 4});
    render(dec, out, {}, config);
    TEST_ASSERT_TRUE(same_pixels(want, out.image));

    bool threw = false;
    try {
        plain_decoder dec2(src, orientation::identity);
        render(dec2, out, {.crop = rect{0, 0, 9, 1}}, config);
    } catch (const std::system_error& e) {
        threw = e.code() == idfxx::errc::invalid_arg;
    }
    TEST_ASSERT_TRUE(threw);
}

#endif
