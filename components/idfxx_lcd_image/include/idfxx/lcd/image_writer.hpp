// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

/**
 * @headerfile <idfxx/lcd/image_writer>
 * @file image_writer.hpp
 * @brief Shows decoded images on RGB565 LCD panels and surfaces.
 *
 * @defgroup idfxx_lcd_image LCD Image Component
 * @ingroup idfxx_lcd
 * @brief Shows decoded images on RGB565 LCD panels and surfaces.
 *
 * @ref idfxx::lcd::write_image places the image from a decoder on an LCD
 * panel: turned, cropped, resampled to its target rectangle and clipped to
 * the screen, as described by an @ref idfxx::image::placement. A panel is
 * written a band of rows at a time, so no whole frame is held in memory. An
 * overlay callback can draw over each band before it is written, and the same
 * function renders into an RGB565 surface such as a framebuffer.
 *
 * Depends on @ref idfxx_image for placement and @ref idfxx_lcd for panels.
 *
 * @code
 * idfxx::memory_source src{jpeg_bytes};
 * idfxx::jpeg::decoder dec(src);
 *
 * // Fill the screen, upright according to the image's EXIF orientation.
 * idfxx::lcd::write_image(display, dec);
 * @endcode
 * @{
 */

#include "sdkconfig.h"

#include <idfxx/error>
#include <idfxx/image>
#include <idfxx/lcd/color>
#include <idfxx/lcd/panel>
#include <idfxx/lcd/rgb565_framebuffer>

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <functional>
#include <optional>
#include <type_traits>
#include <utility>

/**
 * @headerfile <idfxx/lcd/image_writer>
 * @brief LCD driver classes.
 */
namespace idfxx::lcd {

/**
 * @headerfile <idfxx/lcd/image_writer>
 * @brief Options for @ref write_image.
 */
struct write_options {
    /**
     * @brief The number of rows buffered for each write to a panel.
     *
     * Must be non-zero, and is limited to the panel's height. More rows mean
     * fewer, larger transfers and more memory: a band takes the width of the
     * picture times this many rows times 2 bytes. It has no other effect when
     * writing to a surface.
     */
    size_t band_rows = 16;
    /**
     * @brief Maximum working memory, in bytes.
     *
     * Bounds the memory for rendering and for the band together. The band is
     * counted at the full display width, its largest possible size. The bound
     * is checked before anything is allocated, and allocator overhead is not
     * counted. `std::nullopt` sets no limit.
     */
    std::optional<size_t> memory_limit = std::nullopt;
};

/** @cond INTERNAL */
namespace detail {

// An RGB565 surface that image writing can draw to: rgb565_framebuffer, and
// views of one such as gfx::rotated and gfx::canvas.
template<typename S>
concept rgb565_surface = requires(S& s, const S& cs, size_t x, size_t y) {
    requires std::same_as<typename S::pixel_type, rgb565>;
    { s.set_pixel(x, y, rgb565{}) } noexcept;
    { cs.width() } -> std::convertible_to<size_t>;
    { cs.height() } -> std::convertible_to<size_t>;
};

// Stands for the absence of an overlay.
struct no_overlay {};

[[nodiscard]] constexpr rgb565 to_rgb565(image::rgb888 p) noexcept {
    return rgb565(p.r, p.g, p.b);
}

template<image::decoder Decoder, typename Overlay>
[[nodiscard]] result<void> try_write_bands(
    panel& display,
    Decoder& decoder,
    const image::placement& where,
    const write_options& options,
    Overlay& draw
) {
    if (options.band_rows == 0) {
        return error(errc::invalid_arg);
    }
    const size_t band_rows = std::min(options.band_rows, display.display_height());

    // The band is at most the display wide: take its bytes from the limit.
    image::render_config render{
        .screen_width = display.display_width(),
        .screen_height = display.display_height(),
        .memory_limit = options.memory_limit,
    };
    if (options.memory_limit) {
        const size_t band_bytes = band_rows * display.display_width() * sizeof(rgb565);
        if (band_bytes > *options.memory_limit) {
            return error(image::errc::too_large);
        }
        render.memory_limit = *options.memory_limit - band_bytes;
    }

    // The band's width is the picture's, known from its first row.
    std::optional<rgb565_framebuffer> band;
    size_t filled = 0;
    image::point origin{0, 0};

    auto flush = [&]() -> result<void> {
        if (filled == 0) {
            return {};
        }
        if constexpr (!std::is_same_v<Overlay, no_overlay>) {
            std::invoke(draw, *band, origin.x, origin.y);
        }
        const size_t rows = std::exchange(filled, 0);
        return display.try_write_rows(*band, 0, rows, origin.x, origin.y);
    };

    auto r = image::try_render(
        decoder,
        [&](image::point at, image::view<const image::rgb888> rows) -> result<void> {
            if (!band) {
                auto made = rgb565_framebuffer::make(rows.width(), band_rows);
                if (!made) {
                    return error(made.error());
                }
                band.emplace(std::move(*made));
            }
            for (size_t y = 0; y < rows.height(); ++y) {
                if (filled == 0) {
                    origin = {at.x, at.y + y};
                }
                const auto row = rows.row(y);
                for (size_t x = 0; x < row.size(); ++x) {
                    band->set_pixel(x, filled, to_rgb565(row[x]));
                }
                if (++filled == band_rows) {
                    if (auto f = flush(); !f) {
                        return f;
                    }
                }
            }
            return {};
        },
        where,
        render
    );
    if (!r) {
        return r;
    }
    return flush();
}

template<typename Surface, image::decoder Decoder>
[[nodiscard]] result<void>
try_write_surface(Surface& surface, Decoder& decoder, const image::placement& where, const write_options& options) {
    if (options.band_rows == 0) {
        return error(errc::invalid_arg);
    }
    return image::try_render(
        decoder,
        [&](image::point at, image::view<const image::rgb888> rows) -> result<void> {
            for (size_t y = 0; y < rows.height(); ++y) {
                const auto row = rows.row(y);
                for (size_t x = 0; x < row.size(); ++x) {
                    surface.set_pixel(at.x + x, at.y + y, to_rgb565(row[x]));
                }
            }
            return {};
        },
        where,
        image::render_config{
            .screen_width = surface.width(),
            .screen_height = surface.height(),
            .memory_limit = options.memory_limit,
        }
    );
}

} // namespace detail
/** @endcond */

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
/**
 * @headerfile <idfxx/lcd/image_writer>
 * @brief Shows an image on an LCD panel.
 *
 * Renders the decoder's image as @p where places it on the panel's current
 * addressed area (@ref panel::display_width by @ref panel::display_height), and
 * writes it to the panel a band of rows at a time. Pixels outside the target
 * are left as they are. A picture entirely off the screen succeeds without
 * decoding. The panel must use the RGB565 format.
 *
 * @code
 * idfxx::memory_source src{jpeg_bytes};
 * idfxx::jpeg::decoder dec(src);
 *
 * // Fit the picture in the top half of the screen, on black.
 * idfxx::lcd::write_image(
 *     display,
 *     dec,
 *     {
 *         .target = idfxx::image::rect{.x = 0, .y = 0, .width = 320, .height = 120},
 *         .layout = idfxx::image::layout::fit,
 *         .background = {0, 0, 0},
 *     }
 * );
 * @endcode
 *
 * @tparam Decoder A @ref idfxx::image::decoder, such as
 *         `idfxx::jpeg::decoder`.
 * @param display The panel to write to.
 * @param decoder The decoder of the image. It decodes once.
 * @param where   Where and how the picture is placed; by default it fills the
 *                screen.
 * @param options Band size and memory limit.
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
 * @throws std::system_error on error, including `errc::invalid_arg` for zero
 *         `band_rows`, `idfxx::image::errc::too_large` when `memory_limit` is too small,
 *         and any error from the decoder, the placement or the panel.
 */
template<image::decoder Decoder>
void write_image(
    panel& display,
    Decoder& decoder,
    const image::placement& where = {},
    const write_options& options = {}
) {
    detail::no_overlay none;
    unwrap(detail::try_write_bands(display, decoder, where, options, none));
}

/**
 * @headerfile <idfxx/lcd/image_writer>
 * @brief Shows an image filling an LCD panel, with an overlay drawn over each band.
 *
 * Renders the decoder's image to fill the panel's current addressed area
 * (@ref panel::display_width by @ref panel::display_height), upright according to
 * its own orientation, and writes it to the panel a band of rows at a time, calling
 * @p draw with each band just before it is written. The panel must use the RGB565
 * format.
 *
 * The band is drawn in screen coordinates through a canvas whose origin is the
 * band's top-left pixel on the screen:
 *
 * @code
 * idfxx::lcd::write_image(display, dec, [&](idfxx::lcd::rgb565_framebuffer& band, size_t x, size_t y) {
 *     idfxx::gfx::canvas canvas(band, x, y);
 *     canvas.draw_text(font, 10, 220, "Hello", idfxx::lcd::rgb565(255, 255, 255));
 * });
 * @endcode
 *
 * @tparam Decoder The decoder type; see @ref idfxx::image::decoder.
 * @tparam Overlay A callable taking `(rgb565_framebuffer& band, size_t x, size_t y)`.
 * @param display The panel to write to.
 * @param decoder The decoder of the image. It decodes once.
 * @param draw    Called once for each band with the band and the screen position of
 *                its top-left pixel. It draws over the picture in the band, and runs
 *                again for every band, so it must draw the same thing each time.
 *                The last band may have fewer rows written than the band has.
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
 * @throws std::system_error on error, including `idfxx::image::errc::too_large` when
 *         `memory_limit` is too small, and any error from the decoder, the placement or
 *         the panel.
 */
template<image::decoder Decoder, typename Overlay>
    requires std::invocable<Overlay&, rgb565_framebuffer&, size_t, size_t>
void write_image(panel& display, Decoder& decoder, Overlay&& draw) {
    unwrap(detail::try_write_bands(display, decoder, {}, {}, draw));
}

/**
 * @headerfile <idfxx/lcd/image_writer>
 * @brief Shows an image on an LCD panel, with an overlay drawn over each band.
 *
 * Renders the decoder's image as @p where places it on the panel's current
 * addressed area (@ref panel::display_width by @ref panel::display_height), and
 * writes it to the panel a band of rows at a time, calling @p draw with each band
 * just before it is written. Pixels outside the target are left as they are. A
 * picture entirely off the screen succeeds without decoding. The panel must use the
 * RGB565 format.
 *
 * The band is drawn in screen coordinates through a canvas whose origin is the
 * band's top-left pixel on the screen:
 *
 * @code
 * idfxx::lcd::write_image(
 *     display,
 *     dec,
 *     {.target = idfxx::image::rect{.x = 0, .y = 0, .width = 320, .height = 120}},
 *     [&](idfxx::lcd::rgb565_framebuffer& band, size_t x, size_t y) {
 *         idfxx::gfx::canvas canvas(band, x, y);
 *         canvas.draw_text(font, 10, 100, "Hello", idfxx::lcd::rgb565(255, 255, 255));
 *     }
 * );
 * @endcode
 *
 * @tparam Decoder The decoder type; see @ref idfxx::image::decoder.
 * @tparam Overlay A callable taking `(rgb565_framebuffer& band, size_t x, size_t y)`.
 * @param display The panel to write to.
 * @param decoder The decoder of the image. It decodes once.
 * @param where   Where and how the picture is placed.
 * @param draw    Called once for each band with the band and the screen position of
 *                its top-left pixel. It draws over the picture in the band, and runs
 *                again for every band, so it must draw the same thing each time.
 *                The last band may have fewer rows written than the band has.
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
 * @throws std::system_error on error, including `idfxx::image::errc::too_large` when
 *         `memory_limit` is too small, and any error from the decoder, the placement or
 *         the panel.
 */
template<image::decoder Decoder, typename Overlay>
    requires std::invocable<Overlay&, rgb565_framebuffer&, size_t, size_t>
void write_image(panel& display, Decoder& decoder, const image::placement& where, Overlay&& draw) {
    unwrap(detail::try_write_bands(display, decoder, where, {}, draw));
}

/**
 * @headerfile <idfxx/lcd/image_writer>
 * @brief Shows an image on an LCD panel, with an overlay drawn over each band.
 *
 * Renders the decoder's image as @p where places it on the panel's current
 * addressed area (@ref panel::display_width by @ref panel::display_height), and
 * writes it to the panel a band of rows at a time, calling @p draw with each band
 * just before it is written. Pixels outside the target are left as they are. A
 * picture entirely off the screen succeeds without decoding. The panel must use the
 * RGB565 format.
 *
 * The band is drawn in screen coordinates through a canvas whose origin is the
 * band's top-left pixel on the screen:
 *
 * @code
 * idfxx::lcd::write_image(
 *     display,
 *     dec,
 *     {.target = idfxx::image::rect{.x = 0, .y = 0, .width = 320, .height = 120}},
 *     {.band_rows = 8},
 *     [&](idfxx::lcd::rgb565_framebuffer& band, size_t x, size_t y) {
 *         idfxx::gfx::canvas canvas(band, x, y);
 *         canvas.draw_text(font, 10, 100, "Hello", idfxx::lcd::rgb565(255, 255, 255));
 *     }
 * );
 * @endcode
 *
 * @tparam Decoder The decoder type; see @ref idfxx::image::decoder.
 * @tparam Overlay A callable taking `(rgb565_framebuffer& band, size_t x, size_t y)`.
 * @param display The panel to write to.
 * @param decoder The decoder of the image. It decodes once.
 * @param where   Where and how the picture is placed.
 * @param options Band size and memory limit.
 * @param draw    Called once for each band with the band and the screen position of
 *                its top-left pixel. It draws over the picture in the band, and runs
 *                again for every band, so it must draw the same thing each time.
 *                The last band may have fewer rows written than the band has.
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
 * @throws std::system_error on error, including `errc::invalid_arg` for zero
 *         `band_rows`, `idfxx::image::errc::too_large` when `memory_limit` is too small,
 *         and any error from the decoder, the placement or the panel.
 */
template<image::decoder Decoder, typename Overlay>
    requires std::invocable<Overlay&, rgb565_framebuffer&, size_t, size_t>
void write_image(
    panel& display,
    Decoder& decoder,
    const image::placement& where,
    const write_options& options,
    Overlay&& draw
) {
    unwrap(detail::try_write_bands(display, decoder, where, options, draw));
}

/**
 * @headerfile <idfxx/lcd/image_writer>
 * @brief Shows an image in an RGB565 surface.
 *
 * Renders the decoder's image as @p where places it on the surface, setting
 * each pixel of the picture. Pixels outside the target are left as they are.
 * The surface is anything with an `rgb565` `pixel_type`, a `noexcept`
 * `set_pixel(x, y, color)`, and `width()` and `height()`: an
 * `rgb565_framebuffer`, or an `idfxx::gfx::rotated` or `idfxx::gfx::canvas`
 * view of one, to show the picture turned or in a window.
 *
 * @code
 * idfxx::lcd::rgb565_framebuffer fb(240, 320);
 * idfxx::lcd::write_image(fb, dec);
 * display.write(fb);
 * @endcode
 *
 * @tparam Surface An RGB565 surface.
 * @tparam Decoder The decoder type; see @ref idfxx::image::decoder.
 * @param surface The surface to render into.
 * @param decoder The decoder of the image. It decodes once.
 * @param where   Where and how the picture is placed; by default it fills the surface.
 * @param options `band_rows` must still be non-zero; otherwise only `memory_limit` applies.
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
 * @throws std::system_error on error, including `errc::invalid_arg` for zero `band_rows`,
 *         `idfxx::image::errc::too_large` when `memory_limit` is too small for the
 *         rendering, and any error from the decoder or the placement.
 */
template<typename Surface, image::decoder Decoder>
    requires detail::rgb565_surface<Surface>
void write_image(
    Surface& surface,
    Decoder& decoder,
    const image::placement& where = {},
    const write_options& options = {}
) {
    unwrap(detail::try_write_surface(surface, decoder, where, options));
}
#endif

/**
 * @headerfile <idfxx/lcd/image_writer>
 * @brief Shows an image on an LCD panel.
 *
 * Renders the decoder's image as @p where places it on the panel's current
 * addressed area (@ref panel::display_width by @ref panel::display_height), and
 * writes it to the panel a band of rows at a time. Pixels outside the target
 * are left as they are. A picture entirely off the screen succeeds without
 * decoding. The panel must use the RGB565 format.
 *
 * @tparam Decoder A @ref idfxx::image::decoder, such as
 *         `idfxx::jpeg::decoder`.
 * @param display The panel to write to.
 * @param decoder The decoder of the image. It decodes once.
 * @param where   Where and how the picture is placed; by default it fills the
 *                screen.
 * @param options Band size and memory limit.
 * @return Success, or an error.
 * @retval idfxx::errc::invalid_arg `band_rows` is zero.
 * @retval idfxx::image::errc::too_large `memory_limit` is too small for the band or the rendering.
 * @retval other An error from the decoder, the placement or the panel, unchanged.
 */
template<image::decoder Decoder>
[[nodiscard]] result<void> try_write_image(
    panel& display,
    Decoder& decoder,
    const image::placement& where = {},
    const write_options& options = {}
) {
    detail::no_overlay none;
    return detail::try_write_bands(display, decoder, where, options, none);
}

/**
 * @headerfile <idfxx/lcd/image_writer>
 * @brief Shows an image filling an LCD panel, with an overlay drawn over each band.
 *
 * Renders the decoder's image to fill the panel's current addressed area
 * (@ref panel::display_width by @ref panel::display_height), upright according to
 * its own orientation, and writes it to the panel a band of rows at a time, calling
 * @p draw with each band just before it is written. The panel must use the RGB565
 * format.
 *
 * @tparam Decoder The decoder type; see @ref idfxx::image::decoder.
 * @tparam Overlay A callable taking `(rgb565_framebuffer& band, size_t x, size_t y)`.
 * @param display The panel to write to.
 * @param decoder The decoder of the image. It decodes once.
 * @param draw    Called once for each band with the band and the screen position of
 *                its top-left pixel. It draws over the picture in the band, and runs
 *                again for every band, so it must draw the same thing each time.
 *                The last band may have fewer rows written than the band has.
 * @return Success, or an error.
 * @retval idfxx::image::errc::too_large `memory_limit` is too small for the band or the rendering.
 * @retval other An error from the decoder, the placement or the panel, unchanged.
 */
template<image::decoder Decoder, typename Overlay>
    requires std::invocable<Overlay&, rgb565_framebuffer&, size_t, size_t>
[[nodiscard]] result<void> try_write_image(panel& display, Decoder& decoder, Overlay&& draw) {
    return detail::try_write_bands(display, decoder, {}, {}, draw);
}

/**
 * @headerfile <idfxx/lcd/image_writer>
 * @brief Shows an image on an LCD panel, with an overlay drawn over each band.
 *
 * Renders the decoder's image as @p where places it on the panel's current
 * addressed area (@ref panel::display_width by @ref panel::display_height), and
 * writes it to the panel a band of rows at a time, calling @p draw with each band
 * just before it is written. Pixels outside the target are left as they are. A
 * picture entirely off the screen succeeds without decoding. The panel must use the
 * RGB565 format.
 *
 * @tparam Decoder The decoder type; see @ref idfxx::image::decoder.
 * @tparam Overlay A callable taking `(rgb565_framebuffer& band, size_t x, size_t y)`.
 * @param display The panel to write to.
 * @param decoder The decoder of the image. It decodes once.
 * @param where   Where and how the picture is placed.
 * @param draw    Called once for each band with the band and the screen position of
 *                its top-left pixel. It draws over the picture in the band, and runs
 *                again for every band, so it must draw the same thing each time.
 *                The last band may have fewer rows written than the band has.
 * @return Success, or an error.
 * @retval idfxx::image::errc::too_large `memory_limit` is too small for the band or the rendering.
 * @retval other An error from the decoder, the placement or the panel, unchanged.
 */
template<image::decoder Decoder, typename Overlay>
    requires std::invocable<Overlay&, rgb565_framebuffer&, size_t, size_t>
[[nodiscard]] result<void>
try_write_image(panel& display, Decoder& decoder, const image::placement& where, Overlay&& draw) {
    return detail::try_write_bands(display, decoder, where, {}, draw);
}

/**
 * @headerfile <idfxx/lcd/image_writer>
 * @brief Shows an image on an LCD panel, with an overlay drawn over each band.
 *
 * Renders the decoder's image as @p where places it on the panel's current
 * addressed area (@ref panel::display_width by @ref panel::display_height), and
 * writes it to the panel a band of rows at a time, calling @p draw with each band
 * just before it is written. Pixels outside the target are left as they are. A
 * picture entirely off the screen succeeds without decoding. The panel must use the
 * RGB565 format.
 *
 * @tparam Decoder The decoder type; see @ref idfxx::image::decoder.
 * @tparam Overlay A callable taking `(rgb565_framebuffer& band, size_t x, size_t y)`.
 * @param display The panel to write to.
 * @param decoder The decoder of the image. It decodes once.
 * @param where   Where and how the picture is placed.
 * @param options Band size and memory limit.
 * @param draw    Called once for each band with the band and the screen position of
 *                its top-left pixel; it must draw the same thing each time.
 * @return Success, or an error.
 * @retval idfxx::errc::invalid_arg `band_rows` is zero.
 * @retval idfxx::image::errc::too_large `memory_limit` is too small for the band or the rendering.
 * @retval other An error from the decoder, the placement or the panel, unchanged.
 */
template<image::decoder Decoder, typename Overlay>
    requires std::invocable<Overlay&, rgb565_framebuffer&, size_t, size_t>
[[nodiscard]] result<void> try_write_image(
    panel& display,
    Decoder& decoder,
    const image::placement& where,
    const write_options& options,
    Overlay&& draw
) {
    return detail::try_write_bands(display, decoder, where, options, draw);
}

/**
 * @headerfile <idfxx/lcd/image_writer>
 * @brief Shows an image in an RGB565 surface.
 *
 * Renders the decoder's image as @p where places it on the surface, setting
 * each pixel of the picture; see the throwing @ref write_image overload for a
 * surface.
 *
 * @tparam Surface An RGB565 surface.
 * @tparam Decoder The decoder type; see @ref idfxx::image::decoder.
 * @param surface The surface to render into.
 * @param decoder The decoder of the image. It decodes once.
 * @param where   Where and how the picture is placed; by default it fills the surface.
 * @param options `band_rows` must still be non-zero; otherwise only `memory_limit` applies.
 * @return Success, or an error.
 * @retval idfxx::errc::invalid_arg `band_rows` is zero.
 * @retval idfxx::image::errc::too_large `memory_limit` is too small for the rendering.
 * @retval other An error from the decoder or the placement, unchanged.
 */
template<typename Surface, image::decoder Decoder>
    requires detail::rgb565_surface<Surface>
[[nodiscard]] result<void> try_write_image(
    Surface& surface,
    Decoder& decoder,
    const image::placement& where = {},
    const write_options& options = {}
) {
    return detail::try_write_surface(surface, decoder, where, options);
}

} // namespace idfxx::lcd

/** @} */ // end of idfxx_lcd_image
