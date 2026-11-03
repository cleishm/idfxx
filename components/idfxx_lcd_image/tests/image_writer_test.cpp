// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Unit tests for idfxx_lcd_image
// Uses ESP-IDF Unity test framework with compile-time static_asserts
//
// The expected output is computed independently: the picture goes through an
// image::renderer directly, and its rows are converted to RGB565 into a screen
// of known contents. A recording panel keeps the pixels written to it.

#include "../../idfxx_jpeg/tests/jpeg_test_images.hpp"

#include "sdkconfig.h"

#include <idfxx/byte_source>
#include <idfxx/error>
#include <idfxx/gfx>
#include <idfxx/image>
#include <idfxx/jpeg>
#include <idfxx/lcd/image_writer>
#include <idfxx/lcd/mono_framebuffer>
#include <idfxx/lcd/panel>
#include <idfxx/lcd/rgb565_framebuffer>
#include <unity.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <system_error>
#include <type_traits>
#include <utility>
#include <vector>

namespace image = idfxx::image;
namespace lcd = idfxx::lcd;
namespace gfx = idfxx::gfx;
using idfxx::errc;
using idfxx::result;

namespace img = jpeg_test_images;

namespace {

// What a screen pixel holds until something writes it.
constexpr uint16_t untouched = 0xA5A5;

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

private:
    image::buffer<> _picture;
    info_type _info;
};

// A panel that keeps what is written to it, by display coordinates.
class screen_panel : public lcd::panel {
public:
    screen_panel(size_t width, size_t height)
        : lcd::panel(width, height)
        , pixels(width * height, untouched) {}

    std::vector<uint16_t> pixels;
    std::vector<image::rect> draws;
    // The draw call that fails, counting from 0.
    size_t fail_at_draw = SIZE_MAX;
    bool out_of_bounds = false;

private:
    [[nodiscard]] esp_lcd_panel_handle_t do_idf_handle() const override { return nullptr; }

    [[nodiscard]] result<void> do_swap_xy(bool) override { return {}; }

    [[nodiscard]] result<void>
    do_draw_bitmap(int x_start, int y_start, int x_end, int y_end, const void* color_data) override {
        if (draws.size() == fail_at_draw) {
            draws.push_back({});
            return idfxx::error(errc::not_allowed);
        }
        draws.push_back({x_start, y_start, static_cast<size_t>(x_end - x_start), static_cast<size_t>(y_end - y_start)});
        const auto* data = static_cast<const lcd::rgb565*>(color_data);
        const size_t stride = display_width();
        for (int y = y_start; y < y_end; ++y) {
            for (int x = x_start; x < x_end; ++x) {
                if (x < 0 || y < 0 || static_cast<size_t>(x) >= stride || static_cast<size_t>(y) >= display_height()) {
                    out_of_bounds = true;
                    continue;
                }
                pixels[y * stride + x] = data[(y - y_start) * (x_end - x_start) + (x - x_start)].value();
            }
        }
        return {};
    }
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

uint16_t to_565(image::rgb888 p) {
    return lcd::rgb565(p.r, p.g, p.b).value();
}

// The screen a picture should produce, by way of the renderer alone.
std::vector<uint16_t> reference(
    const image::buffer<>& picture,
    image::orientation source,
    const image::placement& where,
    size_t screen_width,
    size_t screen_height
) {
    std::vector<uint16_t> screen(screen_width * screen_height, untouched);
    auto r = image::renderer::make(
        picture.width(),
        picture.height(),
        source,
        where,
        {.screen_width = screen_width, .screen_height = screen_height}
    );
    TEST_ASSERT_TRUE(r.has_value());
    auto pushed = r->try_push(
        {0, 0},
        image::view<const image::rgb888>(picture),
        [&](image::point at, image::view<const image::rgb888> rows) -> result<void> {
            for (size_t y = 0; y < rows.height(); ++y) {
                for (size_t x = 0; x < rows.width(); ++x) {
                    screen[(at.y + y) * screen_width + at.x + x] = to_565(rows.row(y)[x]);
                }
            }
            return {};
        }
    );
    TEST_ASSERT_TRUE(pushed.has_value());
    return screen;
}

// Unity's array comparison reports where two screens first differ.
void assert_screens_equal(const std::vector<uint16_t>& expected, const std::vector<uint16_t>& actual) {
    TEST_ASSERT_EQUAL_UINT32(expected.size(), actual.size());
    TEST_ASSERT_EQUAL_UINT16_ARRAY(expected.data(), actual.data(), expected.size());
}

size_t error_value(const result<void>& r) {
    TEST_ASSERT_FALSE(r.has_value());
    return static_cast<size_t>(r.error().value());
}

constexpr size_t screen_w = 64;
constexpr size_t screen_h = 48;

} // namespace

// =============================================================================
// Compile-time tests (static_assert)
// =============================================================================

static_assert(lcd::write_options{}.band_rows == 16);
static_assert(!lcd::write_options{}.memory_limit.has_value());

// Color framebuffers and views of them are surfaces; monochrome ones and panels are not.
static_assert(lcd::detail::rgb565_surface<lcd::rgb565_framebuffer>);
static_assert(lcd::detail::rgb565_surface<gfx::rotated<lcd::rgb565_framebuffer>>);
static_assert(lcd::detail::rgb565_surface<gfx::canvas<lcd::rgb565_framebuffer>>);
static_assert(lcd::detail::rgb565_surface<gfx::canvas<gfx::rotated<lcd::rgb565_framebuffer>>>);
static_assert(!lcd::detail::rgb565_surface<lcd::mono_framebuffer>);
static_assert(!lcd::detail::rgb565_surface<gfx::canvas<lcd::mono_framebuffer>>);
static_assert(!lcd::detail::rgb565_surface<lcd::panel>);
static_assert(!lcd::detail::rgb565_surface<screen_panel>);

// Whether try_write_image accepts a call with these argument types.
template<typename... Args>
concept can_write = requires(Args&&... args) { lcd::try_write_image(std::forward<Args>(args)...); };

using overlay_fn = decltype([](lcd::rgb565_framebuffer&, size_t, size_t) {});
using bad_overlay_fn = decltype([](int) {});

// A panel and an RGB565 surface can be written; a monochrome framebuffer cannot.
static_assert(can_write<screen_panel&, fake_decoder&>);
static_assert(can_write<lcd::rgb565_framebuffer&, fake_decoder&>);
static_assert(!can_write<lcd::mono_framebuffer&, fake_decoder&>);
static_assert(!can_write<lcd::mono_framebuffer&, fake_decoder&, const image::placement&, const lcd::write_options&>);

// An overlay must take a band and a position.
static_assert(can_write<screen_panel&, fake_decoder&, const image::placement&, const lcd::write_options&, overlay_fn>);
static_assert(!can_write<screen_panel&, fake_decoder&, const image::placement&, const lcd::write_options&, bad_overlay_fn>);

// An overlay may follow the decoder or a placement, on a panel.
static_assert(can_write<screen_panel&, fake_decoder&, overlay_fn>);
static_assert(can_write<screen_panel&, fake_decoder&, overlay_fn&>);
static_assert(can_write<screen_panel&, fake_decoder&, const overlay_fn&>);
static_assert(can_write<screen_panel&, fake_decoder&, const image::placement&, overlay_fn>);
static_assert(!can_write<screen_panel&, fake_decoder&, bad_overlay_fn>);
static_assert(!can_write<screen_panel&, fake_decoder&, const image::placement&, bad_overlay_fn>);
static_assert(!can_write<lcd::rgb565_framebuffer&, fake_decoder&, overlay_fn>);
static_assert(!can_write<lcd::rgb565_framebuffer&, fake_decoder&, const image::placement&, overlay_fn>);

// A placement and options aren't taken for an overlay.
static_assert(can_write<screen_panel&, fake_decoder&, const image::placement&>);
static_assert(can_write<screen_panel&, fake_decoder&, image::placement&>);
static_assert(can_write<screen_panel&, fake_decoder&, const image::placement&, const lcd::write_options&>);
static_assert(can_write<screen_panel&, fake_decoder&, image::placement&, lcd::write_options&>);
static_assert(!can_write<screen_panel&, fake_decoder&, const lcd::write_options&>);

// =============================================================================
// Runtime tests (Unity TEST_CASE)
// =============================================================================

TEST_CASE("lcd write_image panel output matches the renderer across band sizes", "[lcd_image]") {
    const auto picture = noise(40, 30);
    for (const auto source : {image::orientation::identity, image::orientation::rotate_90}) {
        const auto expected = reference(picture, source, {}, screen_w, screen_h);
        // 1, divisors of the output height, non-divisors, and larger than the screen.
        for (const size_t band_rows : {1, 5, 7, 16, 24, 47, 48, 100}) {
            screen_panel display(screen_w, screen_h);
            fake_decoder decoder(noise(40, 30), source);
            auto r = lcd::try_write_image(display, decoder, {}, {.band_rows = band_rows});
            TEST_ASSERT_TRUE(r.has_value());
            assert_screens_equal(expected, display.pixels);
            TEST_ASSERT_FALSE(display.out_of_bounds);

            // Each draw is a full band, but the last.
            const size_t rows = std::min(band_rows, screen_h);
            TEST_ASSERT_EQUAL_UINT32((screen_h + rows - 1) / rows, display.draws.size());
            for (size_t i = 0; i < display.draws.size(); ++i) {
                TEST_ASSERT_EQUAL_INT32(0, display.draws[i].x);
                TEST_ASSERT_EQUAL_INT32(i * rows, display.draws[i].y);
                TEST_ASSERT_EQUAL_UINT32(screen_w, display.draws[i].width);
                TEST_ASSERT_EQUAL_UINT32(std::min(rows, screen_h - i * rows), display.draws[i].height);
            }
        }
    }
}

TEST_CASE("lcd write_image places the picture in a target and leaves the rest alone", "[lcd_image]") {
    const auto picture = noise(40, 30);
    const image::placement placements[] = {
        // A sub-rectangle, stretched.
        {.target = image::rect{.x = 10, .y = 8, .width = 30, .height = 20}, .layout = image::layout::stretch},
        // A fitted one, with a background around the picture.
        {.target = image::rect{.x = 5, .y = 4, .width = 50, .height = 20},
         .layout = image::layout::fit,
         .background = {10, 20, 30}},
        // Overhanging the top left and the bottom right.
        {.target = image::rect{.x = -10, .y = -5, .width = 40, .height = 30}, .layout = image::layout::stretch},
        {.target = image::rect{.x = 50, .y = 40, .width = 30, .height = 30}, .layout = image::layout::stretch},
        // Turned.
        {.target = image::rect{.x = 20, .y = 3, .width = 30, .height = 40}, .orientation = image::orientation::rotate_90},
    };
    for (const auto& where : placements) {
        const auto expected = reference(picture, image::orientation::identity, where, screen_w, screen_h);
        for (const size_t band_rows : {3, 16}) {
            screen_panel display(screen_w, screen_h);
            fake_decoder decoder(noise(40, 30));
            TEST_ASSERT_TRUE(lcd::try_write_image(display, decoder, where, {.band_rows = band_rows}).has_value());
            assert_screens_equal(expected, display.pixels);
            TEST_ASSERT_FALSE(display.out_of_bounds);
        }
    }

    // The first one really is partial: a corner outside it stays as it was.
    screen_panel display(screen_w, screen_h);
    fake_decoder decoder(noise(40, 30));
    TEST_ASSERT_TRUE(lcd::try_write_image(display, decoder, placements[0]).has_value());
    TEST_ASSERT_EQUAL_UINT16(untouched, display.pixels[0]);
    TEST_ASSERT_EQUAL_UINT16(untouched, display.pixels[screen_h * screen_w - 1]);
    TEST_ASSERT_NOT_EQUAL(untouched, display.pixels[8 * screen_w + 10]);
}

TEST_CASE("lcd write_image uses the swapped display size after swap_xy", "[lcd_image]") {
    const auto picture = noise(40, 30);
    const image::placement overhang{
        .target = image::rect{.x = 30, .y = 40, .width = 30, .height = 40},
        .layout = image::layout::stretch,
    };
    for (const auto& where : {image::placement{}, overhang}) {
        screen_panel display(screen_w, screen_h);
        TEST_ASSERT_TRUE(display.try_swap_xy(true).has_value());
        TEST_ASSERT_EQUAL_UINT32(screen_h, display.display_width());
        TEST_ASSERT_EQUAL_UINT32(screen_w, display.display_height());

        // The screen is 48 wide and 64 tall now.
        const auto expected = reference(picture, image::orientation::identity, where, screen_h, screen_w);
        fake_decoder decoder(noise(40, 30));
        TEST_ASSERT_TRUE(lcd::try_write_image(display, decoder, where, {.band_rows = 10}).has_value());
        assert_screens_equal(expected, display.pixels);
        TEST_ASSERT_FALSE(display.out_of_bounds);
    }

    // The default fills the swapped screen: its full width is written.
    screen_panel display(screen_w, screen_h);
    TEST_ASSERT_TRUE(display.try_swap_xy(true).has_value());
    fake_decoder decoder(noise(40, 30));
    TEST_ASSERT_TRUE(lcd::try_write_image(display, decoder).has_value());
    TEST_ASSERT_EQUAL_UINT32(screen_h, display.draws[0].width);
    TEST_ASSERT_EQUAL_UINT32(screen_w, display.draws.size() * 16); // 64 rows in bands of 16
}

TEST_CASE("lcd write_image calls the overlay with each band before it is written", "[lcd_image]") {
    const auto picture = noise(40, 30);
    const lcd::rgb565 red(255, 0, 0);
    // A rectangle crossing the band boundaries, partly outside the screen.
    constexpr size_t rx = 20, ry = 10, rw = 50, rh = 30;

    auto expected = reference(picture, image::orientation::identity, {}, screen_w, screen_h);
    for (size_t y = ry; y < std::min(ry + rh, screen_h); ++y) {
        for (size_t x = rx; x < std::min(rx + rw, screen_w); ++x) {
            expected[y * screen_w + x] = red.value();
        }
    }

    for (const size_t band_rows : {1, 7, 16, 48}) {
        screen_panel display(screen_w, screen_h);
        fake_decoder decoder(noise(40, 30));
        std::vector<image::point> calls;
        auto r = lcd::try_write_image(
            display,
            decoder,
            {},
            {.band_rows = band_rows},
            [&](lcd::rgb565_framebuffer& band, size_t x, size_t y) {
                calls.push_back({x, y});
                TEST_ASSERT_EQUAL_UINT32(screen_w, band.width());
                TEST_ASSERT_EQUAL_UINT32(band_rows, band.height());
                // The picture is already in the band, and this is not yet on the panel.
                TEST_ASSERT_EQUAL_UINT32(display.draws.size() * band_rows, y);
                gfx::canvas canvas(band, x, y);
                canvas.fill_rect(rx, ry, rw, rh, red);
            }
        );
        TEST_ASSERT_TRUE(r.has_value());
        assert_screens_equal(expected, display.pixels);

        // Once per band, at the band's top-left on the screen.
        TEST_ASSERT_EQUAL_UINT32(display.draws.size(), calls.size());
        TEST_ASSERT_EQUAL_UINT32((screen_h + band_rows - 1) / band_rows, calls.size());
        for (size_t i = 0; i < calls.size(); ++i) {
            TEST_ASSERT_EQUAL_UINT32(0, calls[i].x);
            TEST_ASSERT_EQUAL_UINT32(i * band_rows, calls[i].y);
        }
    }
}

TEST_CASE("lcd write_image gives the overlay screen positions for a picture in a target", "[lcd_image]") {
    const image::placement where{
        .target = image::rect{.x = 12, .y = 9, .width = 30, .height = 20},
        .layout = image::layout::stretch,
    };
    screen_panel display(screen_w, screen_h);
    fake_decoder decoder(noise(40, 30));
    std::vector<image::point> calls;
    auto r = lcd::try_write_image(
        display,
        decoder,
        where,
        {.band_rows = 8},
        [&](lcd::rgb565_framebuffer& band, size_t x, size_t y) {
            calls.push_back({x, y});
            TEST_ASSERT_EQUAL_UINT32(30, band.width());
            // Marks the picture's top-left pixel only, by its screen position.
            band.set_pixel(0, 0, lcd::rgb565(0, 255, 0));
        }
    );
    TEST_ASSERT_TRUE(r.has_value());
    TEST_ASSERT_EQUAL_UINT32(3, calls.size()); // 8 + 8 + 4 rows
    for (size_t i = 0; i < calls.size(); ++i) {
        TEST_ASSERT_EQUAL_UINT32(12, calls[i].x);
        TEST_ASSERT_EQUAL_UINT32(9 + i * 8, calls[i].y);
        TEST_ASSERT_EQUAL_UINT16(lcd::rgb565(0, 255, 0).value(), display.pixels[(9 + i * 8) * screen_w + 12]);
    }
}

TEST_CASE("lcd write_image takes an overlay after the decoder or a placement", "[lcd_image]") {
    const lcd::rgb565 red(255, 0, 0);
    const image::placement where{
        .target = image::rect{.x = 12, .y = 9, .width = 30, .height = 20},
        .layout = image::layout::stretch,
    };
    size_t calls = 0;
    const auto draw = [&](lcd::rgb565_framebuffer& band, size_t x, size_t y) {
        ++calls;
        gfx::canvas canvas(band, x, y);
        canvas.fill_rect(14, 10, 8, 30, red);
    };

    // Without a placement, the picture fills the screen.
    screen_panel full(screen_w, screen_h);
    fake_decoder full_decoder(noise(40, 30));
    TEST_ASSERT_TRUE(lcd::try_write_image(full, full_decoder, {}, {}, draw).has_value());
    const size_t full_calls = std::exchange(calls, 0);
    TEST_ASSERT_GREATER_THAN_UINT32(0, full_calls);

    screen_panel after_decoder(screen_w, screen_h);
    fake_decoder decoder(noise(40, 30));
    TEST_ASSERT_TRUE(lcd::try_write_image(after_decoder, decoder, draw).has_value());
    TEST_ASSERT_EQUAL_UINT32(full_calls, std::exchange(calls, 0));
    assert_screens_equal(full.pixels, after_decoder.pixels);
    TEST_ASSERT_EQUAL_UINT16(red.value(), after_decoder.pixels[10 * screen_w + 14]);

    // With a placement, the default options apply.
    screen_panel placed(screen_w, screen_h);
    fake_decoder placed_decoder(noise(40, 30));
    TEST_ASSERT_TRUE(lcd::try_write_image(placed, placed_decoder, where, {}, draw).has_value());
    const size_t placed_calls = std::exchange(calls, 0);
    TEST_ASSERT_GREATER_THAN_UINT32(0, placed_calls);

    screen_panel after_placement(screen_w, screen_h);
    fake_decoder placement_decoder(noise(40, 30));
    TEST_ASSERT_TRUE(lcd::try_write_image(after_placement, placement_decoder, where, draw).has_value());
    TEST_ASSERT_EQUAL_UINT32(placed_calls, std::exchange(calls, 0));
    assert_screens_equal(placed.pixels, after_placement.pixels);
    TEST_ASSERT_EQUAL_UINT16(untouched, after_placement.pixels[0]);

    // A braced placement.
    screen_panel braced(screen_w, screen_h);
    fake_decoder braced_decoder(noise(40, 30));
    TEST_ASSERT_TRUE(lcd::try_write_image(
        braced,
        braced_decoder,
        {.target = where.target, .layout = where.layout},
        draw
    ).has_value());
    assert_screens_equal(placed.pixels, braced.pixels);

    // The other kinds of callable.
    screen_panel member(screen_w, screen_h);
    fake_decoder member_decoder(noise(40, 30));
    TEST_ASSERT_TRUE(lcd::try_write_image(member, member_decoder, &lcd::rgb565_framebuffer::get_pixel).has_value());
    fake_decoder rvalue_decoder(noise(40, 30));
    TEST_ASSERT_TRUE(lcd::try_write_image(
        member, rvalue_decoder, where, [](lcd::rgb565_framebuffer&, size_t, size_t) {}
    ).has_value());
}

TEST_CASE("lcd write_image without an overlay takes a braced placement and options", "[lcd_image]") {
    static constexpr lcd::write_options OPTIONS{.band_rows = 7};
    const auto expected = reference(noise(40, 30), image::orientation::identity, {}, screen_w, screen_h);

    // Options after an empty placement.
    screen_panel with_options(screen_w, screen_h);
    fake_decoder options_decoder(noise(40, 30));
    TEST_ASSERT_TRUE(lcd::try_write_image(with_options, options_decoder, {}, OPTIONS).has_value());
    TEST_ASSERT_EQUAL_UINT32((screen_h + 6) / 7, with_options.draws.size());
    assert_screens_equal(expected, with_options.pixels);

    // An empty placement alone.
    screen_panel empty_placement(screen_w, screen_h);
    fake_decoder empty_decoder(noise(40, 30));
    TEST_ASSERT_TRUE(lcd::try_write_image(empty_placement, empty_decoder, {}).has_value());
    assert_screens_equal(expected, empty_placement.pixels);

    // A braced placement alone.
    const auto turned = reference(
        noise(40, 30), image::orientation::identity, {.orientation = image::orientation::rotate_180}, screen_w, screen_h
    );
    screen_panel with_placement(screen_w, screen_h);
    fake_decoder placement_decoder(noise(40, 30));
    TEST_ASSERT_TRUE(lcd::try_write_image(
        with_placement, placement_decoder, {.orientation = image::orientation::rotate_180}
    ).has_value());
    assert_screens_equal(turned, with_placement.pixels);
}

TEST_CASE("lcd write_image into a framebuffer matches the streamed panel output", "[lcd_image]") {
    const image::placement placements[] = {
        {},
        {.target = image::rect{.x = -6, .y = 5, .width = 40, .height = 60}, .layout = image::layout::fit},
        {.orientation = image::orientation::rotate_270},
    };
    for (const auto& where : placements) {
        screen_panel display(screen_w, screen_h);
        fake_decoder streamed(noise(40, 30), image::orientation::flip_horizontal);
        TEST_ASSERT_TRUE(lcd::try_write_image(display, streamed, where, {.band_rows = 9}).has_value());

        auto fb = lcd::rgb565_framebuffer::make(screen_w, screen_h);
        TEST_ASSERT_TRUE(fb.has_value());
        fb->fill(lcd::rgb565::from_value(untouched));
        fake_decoder decoder(noise(40, 30), image::orientation::flip_horizontal);
        TEST_ASSERT_TRUE(lcd::try_write_image(*fb, decoder, where).has_value());

        std::vector<uint16_t> actual;
        for (const auto p : fb->data()) {
            actual.push_back(p.value());
        }
        assert_screens_equal(display.pixels, actual);
    }
}

TEST_CASE("lcd write_image into a rotated view and a canvas window", "[lcd_image]") {
    const auto picture = noise(40, 30);
    const auto expected = reference(picture, image::orientation::identity, {}, screen_w, screen_h);

    // A 48x64 framebuffer viewed turned a quarter turn is a 64x48 surface.
    {
        auto fb = lcd::rgb565_framebuffer::make(screen_h, screen_w);
        TEST_ASSERT_TRUE(fb.has_value());
        gfx::rotated view(*fb, gfx::rotation::ccw90);
        TEST_ASSERT_EQUAL_UINT32(screen_w, view.width());
        fake_decoder decoder(noise(40, 30));
        TEST_ASSERT_TRUE(lcd::try_write_image(view, decoder).has_value());
        for (size_t y = 0; y < screen_h; ++y) {
            for (size_t x = 0; x < screen_w; ++x) {
                // ccw90: view (x, y) is framebuffer (width - 1 - y, x).
                TEST_ASSERT_EQUAL_UINT16(expected[y * screen_w + x], fb->get_pixel(screen_h - 1 - y, x).value());
            }
        }
    }

    // A window into a larger framebuffer, which is left alone around it.
    {
        auto fb = lcd::rgb565_framebuffer::make(screen_w + 20, screen_h + 12);
        TEST_ASSERT_TRUE(fb.has_value());
        fb->fill(lcd::rgb565::from_value(untouched));
        gfx::canvas whole(*fb);
        auto window = whole.window(8, 6, screen_w, screen_h);
        fake_decoder decoder(noise(40, 30));
        TEST_ASSERT_TRUE(lcd::try_write_image(window, decoder).has_value());
        for (size_t y = 0; y < fb->height(); ++y) {
            for (size_t x = 0; x < fb->width(); ++x) {
                const bool inside = x >= 8 && x < 8 + screen_w && y >= 6 && y < 6 + screen_h;
                const uint16_t want = inside ? expected[(y - 6) * screen_w + (x - 8)] : untouched;
                TEST_ASSERT_EQUAL_UINT16(want, fb->get_pixel(x, y).value());
            }
        }
    }
}

TEST_CASE("lcd write_image with a picture off the screen writes nothing", "[lcd_image]") {
    screen_panel display(screen_w, screen_h);
    fake_decoder decoder(noise(40, 30));
    decoder.fail_at_row = 0; // it must not be decoded
    const image::placement where{.target = image::rect{.x = 200, .y = 200, .width = 10, .height = 10}};
    TEST_ASSERT_TRUE(lcd::try_write_image(display, decoder, where).has_value());
    TEST_ASSERT_EQUAL_UINT32(0, display.draws.size());

    bool called = false;
    TEST_ASSERT_TRUE(lcd::try_write_image(
                         display,
                         decoder,
                         where,
                         {},
                         [&](lcd::rgb565_framebuffer&, size_t, size_t) { called = true; }
    ).has_value());
    TEST_ASSERT_FALSE(called);

    auto fb = lcd::rgb565_framebuffer::make(screen_w, screen_h);
    TEST_ASSERT_TRUE(fb.has_value());
    TEST_ASSERT_TRUE(lcd::try_write_image(*fb, decoder, where).has_value());
    for (const auto p : fb->data()) {
        TEST_ASSERT_EQUAL_UINT16(0, p.value());
    }
}

TEST_CASE("lcd write_image propagates panel and decoder errors", "[lcd_image]") {
    // A panel write fails: stops there, with its error.
    {
        screen_panel display(screen_w, screen_h);
        display.fail_at_draw = 2;
        fake_decoder decoder(noise(40, 30));
        auto r = lcd::try_write_image(display, decoder, {}, {.band_rows = 8});
        TEST_ASSERT_EQUAL_UINT32(std::to_underlying(errc::not_allowed), error_value(r));
        TEST_ASSERT_EQUAL_UINT32(3, display.draws.size());
    }

    // A panel write fails at the end.
    {
        screen_panel display(screen_w, screen_h);
        display.fail_at_draw = 5; // the last of six bands
        fake_decoder decoder(noise(40, 30));
        auto r = lcd::try_write_image(display, decoder, {}, {.band_rows = 8});
        TEST_ASSERT_EQUAL_UINT32(std::to_underlying(errc::not_allowed), error_value(r));
    }

    // The decoder fails: no partial band is written after it.
    {
        screen_panel display(screen_w, screen_h);
        fake_decoder decoder(noise(40, 30));
        decoder.fail_at_row = 20;
        auto r = lcd::try_write_image(display, decoder, {}, {.band_rows = 8});
        TEST_ASSERT_EQUAL_UINT32(std::to_underlying(errc::timeout), error_value(r));

        auto fb = lcd::rgb565_framebuffer::make(screen_w, screen_h);
        TEST_ASSERT_TRUE(fb.has_value());
        fake_decoder again(noise(40, 30));
        again.fail_at_row = 20;
        TEST_ASSERT_EQUAL_UINT32(std::to_underlying(errc::timeout), error_value(lcd::try_write_image(*fb, again)));
    }
}

TEST_CASE("lcd write_image rejects zero band rows", "[lcd_image]") {
    screen_panel display(screen_w, screen_h);
    fake_decoder decoder(noise(40, 30));
    auto r = lcd::try_write_image(display, decoder, {}, {.band_rows = 0});
    TEST_ASSERT_EQUAL_UINT32(std::to_underlying(errc::invalid_arg), error_value(r));
    TEST_ASSERT_EQUAL_UINT32(0, display.draws.size());

    // A surface still requires non-zero band_rows.
    auto fb = lcd::rgb565_framebuffer::make(screen_w, screen_h);
    TEST_ASSERT_TRUE(fb.has_value());
    fake_decoder surface_decoder(noise(40, 30));
    auto sr = lcd::try_write_image(*fb, surface_decoder, {}, {.band_rows = 0});
    TEST_ASSERT_EQUAL_UINT32(std::to_underlying(errc::invalid_arg), error_value(sr));
}

TEST_CASE("lcd write_image accepts a pointer to member function as the overlay", "[lcd_image]") {
    screen_panel display(screen_w, screen_h);
    fake_decoder decoder(noise(40, 30));
    auto r = lcd::try_write_image(display, decoder, {}, {.band_rows = 8}, &lcd::rgb565_framebuffer::get_pixel);
    TEST_ASSERT_TRUE(r.has_value());
    TEST_ASSERT_GREATER_THAN_UINT32(0, display.draws.size());
}

TEST_CASE("lcd write_image memory_limit bounds the renderer and the band together", "[lcd_image]") {
    // The smallest limit, to the nearest 16 bytes, that lets a band of this height through.
    const auto smallest_limit = [](size_t band_rows) {
        for (size_t limit = 0; limit < 256 * 1024; limit += 16) {
            screen_panel display(screen_w, screen_h);
            fake_decoder decoder(noise(40, 30));
            auto r = lcd::try_write_image(display, decoder, {}, {.band_rows = band_rows, .memory_limit = limit});
            if (r) {
                return limit;
            }
            TEST_ASSERT_TRUE(r.error() == image::errc::too_large);
            TEST_ASSERT_EQUAL_UINT32(0, display.draws.size());
        }
        TEST_FAIL_MESSAGE("no memory_limit let the picture through");
        return size_t{0};
    };
    const size_t one_row = smallest_limit(1);
    const size_t sixteen_rows = smallest_limit(16);
    // 15 more rows of 64 pixels at 2 bytes each.
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(15 * screen_w * 2 - 16, sixteen_rows - one_row);
    TEST_ASSERT_LESS_OR_EQUAL_UINT32(15 * screen_w * 2 + 16, sixteen_rows - one_row);

    // The limit changes nothing else about the output.
    const auto expected = reference(noise(40, 30), image::orientation::identity, {}, screen_w, screen_h);
    screen_panel display(screen_w, screen_h);
    fake_decoder decoder(noise(40, 30));
    TEST_ASSERT_TRUE(lcd::try_write_image(display, decoder, {}, {.band_rows = 16, .memory_limit = sixteen_rows}).has_value());
    assert_screens_equal(expected, display.pixels);

    // A surface has no band to count.
    auto fb = lcd::rgb565_framebuffer::make(screen_w, screen_h);
    TEST_ASSERT_TRUE(fb.has_value());
    fake_decoder into_fb(noise(40, 30));
    TEST_ASSERT_TRUE(lcd::try_write_image(*fb, into_fb, {}, {.band_rows = 16, .memory_limit = one_row}).has_value());
    fake_decoder too_small(noise(40, 30));
    TEST_ASSERT_TRUE(lcd::try_write_image(*fb, too_small, {}, {.memory_limit = 0}).error() == image::errc::too_large);
}

TEST_CASE("lcd write_image shows a real JPEG", "[lcd_image]") {
    // The same picture decoded by a second decoder is the reference.
    image::buffer<> decoded(img::baseline_420_width, img::baseline_420_height);
    {
        idfxx::memory_source src{std::span<const uint8_t>(img::baseline_420)};
        auto dec = idfxx::jpeg::decoder::make(src);
        TEST_ASSERT_TRUE(dec.has_value());
        TEST_ASSERT_TRUE(dec->try_decode(image::view<image::rgb888>(decoded)).has_value());
    }
    const auto expected = reference(decoded, image::orientation::identity, {}, screen_w, screen_h);

    idfxx::memory_source src{std::span<const uint8_t>(img::baseline_420)};
    auto dec = idfxx::jpeg::decoder::make(src);
    TEST_ASSERT_TRUE(dec.has_value());
    screen_panel display(screen_w, screen_h);
    TEST_ASSERT_TRUE(lcd::try_write_image(display, *dec, {}, {.band_rows = 10}).has_value());
    assert_screens_equal(expected, display.pixels);
}

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
TEST_CASE("lcd write_image throws on error", "[lcd_image]") {
    const auto expected = reference(noise(40, 30), image::orientation::identity, {}, screen_w, screen_h);

    // The panel form.
    screen_panel display(screen_w, screen_h);
    fake_decoder decoder(noise(40, 30));
    lcd::write_image(display, decoder, {}, {.band_rows = 11});
    assert_screens_equal(expected, display.pixels);

    // The overlay form.
    screen_panel overlaid(screen_w, screen_h);
    fake_decoder with_overlay(noise(40, 30));
    size_t calls = 0;
    lcd::write_image(overlaid, with_overlay, {}, {.band_rows = 11}, [&](lcd::rgb565_framebuffer&, size_t, size_t) {
        ++calls;
    });
    TEST_ASSERT_EQUAL_UINT32(5, calls);
    assert_screens_equal(expected, overlaid.pixels);

    // The overlay after the decoder.
    calls = 0;
    screen_panel after_decoder(screen_w, screen_h);
    fake_decoder for_decoder_form(noise(40, 30));
    lcd::write_image(after_decoder, for_decoder_form, [&](lcd::rgb565_framebuffer&, size_t, size_t) { ++calls; });
    TEST_ASSERT_EQUAL_UINT32(3, calls);
    assert_screens_equal(expected, after_decoder.pixels);

    // The overlay after a placement.
    calls = 0;
    screen_panel after_placement(screen_w, screen_h);
    fake_decoder for_placement_form(noise(40, 30));
    lcd::write_image(after_placement, for_placement_form, {}, [&](lcd::rgb565_framebuffer&, size_t, size_t) { ++calls; });
    TEST_ASSERT_EQUAL_UINT32(3, calls);
    assert_screens_equal(expected, after_placement.pixels);

    // The surface form.
    auto fb = lcd::rgb565_framebuffer::make(screen_w, screen_h);
    TEST_ASSERT_TRUE(fb.has_value());
    fake_decoder into_fb(noise(40, 30));
    lcd::write_image(*fb, into_fb);
    TEST_ASSERT_EQUAL_UINT16(expected[0], fb->get_pixel(0, 0).value());

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
            fake_decoder d(noise(40, 30));
            lcd::write_image(display, d, {}, {.band_rows = 0});
        },
        errc::invalid_arg
    );
    throws(
        [&] {
            fake_decoder d(noise(40, 30));
            lcd::write_image(display, d, {}, {.band_rows = 0}, [](lcd::rgb565_framebuffer&, size_t, size_t) {});
        },
        errc::invalid_arg
    );
    throws(
        [&] {
            fake_decoder d(noise(40, 30));
            d.fail_at_row = 3;
            lcd::write_image(display, d, [](lcd::rgb565_framebuffer&, size_t, size_t) {});
        },
        errc::timeout
    );
    throws(
        [&] {
            fake_decoder d(noise(40, 30));
            d.fail_at_row = 3;
            lcd::write_image(display, d, image::placement{}, [](lcd::rgb565_framebuffer&, size_t, size_t) {});
        },
        errc::timeout
    );
    throws(
        [&] {
            fake_decoder d(noise(40, 30));
            d.fail_at_row = 3;
            lcd::write_image(*fb, d);
        },
        errc::timeout
    );
}
#endif
