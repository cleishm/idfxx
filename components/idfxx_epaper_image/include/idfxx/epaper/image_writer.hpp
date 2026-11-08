// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

/**
 * @headerfile <idfxx/epaper/image_writer>
 * @file image_writer.hpp
 * @brief Shows decoded images on e-paper panels and surfaces, in a panel's measured colors.
 *
 * @defgroup idfxx_epaper_image ePaper Image Component
 * @ingroup idfxx_epaper
 * @brief Shows decoded images on e-paper panels and surfaces, in a panel's measured colors.
 *
 * @ref idfxx::epaper::write_image places the image from a decoder on an e-paper
 * panel: turned, cropped, resampled to its target rectangle and clipped to the
 * screen, as described by an @ref idfxx::image::placement, then converted to
 * the panel's inks using a @ref idfxx::epaper::calibration of what those inks
 * look like. A panel is written a band of rows at a time, so no whole frame is
 * held in memory. An overlay callback can draw over each band before it is
 * written, and the same function renders into a surface such as a framebuffer.
 *
 * Depends on @ref idfxx_image for placement and @ref idfxx_epaper for panels.
 *
 * @code
 * idfxx::memory_source src{jpeg_bytes};
 * idfxx::jpeg::decoder dec(src);
 *
 * display.set_color_mode(idfxx::epaper::color_mode::color6);
 * idfxx::epaper::write_image(display, idfxx::epaper::calibration::el133uf1(), dec);
 * display.refresh();
 * @endcode
 * @{
 */

#include "sdkconfig.h"

#include <idfxx/epaper/color6_framebuffer>
#include <idfxx/epaper/color>
#include <idfxx/epaper/gray4_framebuffer>
#include <idfxx/epaper/mono_framebuffer>
#include <idfxx/epaper/panel>
#include <idfxx/error>
#include <idfxx/image>
#include <idfxx/required>

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <vector>

/** @cond INTERNAL */
typedef struct epi_panel epi_panel_t;
/** @endcond */

/**
 * @headerfile <idfxx/epaper/image_writer>
 * @brief ePaper display driver classes.
 */
namespace idfxx::epaper {

/** @cond INTERNAL */
namespace detail {
struct calibration_access;
} // namespace detail
/** @endcond */

/**
 * @headerfile <idfxx/epaper/image_writer>
 * @brief How a picture is adapted to a panel's limited inks.
 *
 * The fields have no defaults: a meaningful starting point is one of a
 * calibration's @ref calibration::presets.
 */
struct rendering_intent {
    /// @brief The tone curve, 0 to 1: 0 is black-point-compensated lightness (Soft), and 1 a midtone curve (Deep).
    required<float> tone;
    /// @brief The share of local detail kept around the blurred base, 0 to 1.
    required<float> detail;
    /// @brief The adaptation to the paper's white below the highlights, 0 to 1. Has no effect on a gray panel.
    required<float> adapt;
    /// @brief The chroma boost, sparing skin and near-neutrals, 0 to 3: 1 is off. Has no effect on a gray panel.
    required<float> vibrance;
    /// @brief How far below the paper's white the source's white is placed, in L*, 0 to 3: 0 is off.
    required<float> veil;
    /// @brief The L* added to lower midtones, fading out by L* 82 and sparing saturated colors, 0 to 20: 0 is off.
    required<float> lift;
};

/**
 * @headerfile <idfxx/epaper/image_writer>
 * @brief A named rendering intent offered by a calibration.
 */
struct preset {
    const char* name;        ///< The preset's name, such as "Default".
    rendering_intent intent; ///< The rendering intent it stands for.
};

/**
 * @headerfile <idfxx/epaper/image_writer>
 * @brief One of a panel's inks, or one of its gray levels.
 */
struct ink {
    /**
     * @brief The value of this ink in a framebuffer's pixels.
     *
     * The `color6` value on a six-color panel, the `gray4` value on a gray panel
     * (0 is white and 3 is black), and the bit in a `mono_framebuffer`'s bytes on
     * a monochrome panel (1 is white). With @ref nominal or @ref measured, it is
     * what's needed to draw a framebuffer's bytes elsewhere, such as in a browser.
     *
     * It isn't the code in a `write_inks` frame on a gray panel: those codes run
     * from 0 the darkest to 3 the lightest, the reverse of `gray4` values.
     */
    uint8_t value;
    image::rgb888 nominal;  ///< The ink's ideal color.
    image::rgb888 measured; ///< The color the ink has on the panel, as measured.
};

/**
 * @headerfile <idfxx/epaper/image_writer>
 * @brief What a panel's inks look like, and how to render pictures for it.
 *
 * A picture is converted using the colors its panel's inks were measured to
 * have, so a calibration belongs to a glass, a color mode and a waveform. It
 * holds only for the waveform it was measured with, which cannot be detected:
 * a different frame rate, voltage or driver shows different colors, and the
 * picture is then converted for colors the panel isn't showing.
 *
 * This type is copyable and cheap to copy. An object made from a library panel
 * with @ref make refers to that panel, which must outlive it.
 */
class calibration {
public:
    /**
     * @brief The E Ink EL133UF1: a 13.3" 1200x1600 panel with six inks.
     *
     * @return The calibration, in @ref color_mode::color6.
     */
    [[nodiscard]] static const calibration& el133uf1();

    /**
     * @brief The GDEY075T7: a 7.5" 800x480 panel, in black and white.
     *
     * Holds only for the controller's built-in full-refresh waveform.
     *
     * @return The calibration, in @ref color_mode::mono.
     */
    [[nodiscard]] static const calibration& gdey075t7_mono();

    /**
     * @brief The GDEY075T7: a 7.5" 800x480 panel, in four gray levels.
     *
     * Holds only at `uc8179`'s default `gray4_frame_rate` and
     * `gray4_source_voltage`: its gray levels were measured with those.
     *
     * @return The calibration, in @ref color_mode::gray4.
     */
    [[nodiscard]] static const calibration& gdey075t7_gray4();

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Creates a calibration from a panel of the `cleishm/epaper-image` library.
     *
     * For a panel that has no calibration here, such as one measured and built
     * with `epi_grey_panel_init()` or `epi_colour_panel_init()`. See @ref make.
     *
     * @param panel The library's panel, which must outlive this object.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error with idfxx::errc::invalid_arg for what @ref make rejects.
     */
    [[nodiscard]] explicit calibration(const epi_panel_t& panel)
        : calibration(unwrap(make(panel))) {}
#endif

    /**
     * @brief Creates a calibration from a panel of the `cleishm/epaper-image` library.
     *
     * For a panel that has no calibration here, such as one measured and built
     * with `epi_grey_panel_init()` or `epi_colour_panel_init()`. The color mode
     * comes from the number of inks: 6 is @ref color_mode::color6, 4 is
     * @ref color_mode::gray4 and 2 is @ref color_mode::mono. The ink codes must be
     * able to reach a framebuffer:
     * each a `color6` value on a panel of 6, or 0, 1, 2, 3 and 0, 1 from darkest
     * to lightest on a panel of 4 and 2.
     *
     * @param panel The library's panel, which must outlive the calibration.
     *
     * @return The calibration, or an error.
     * @retval idfxx::errc::invalid_arg The panel has another number of inks, or
     *         codes that cannot be mapped.
     */
    [[nodiscard]] static result<calibration> make(const epi_panel_t& panel);

    /**
     * @brief Returns the name of the glass.
     *
     * @return The name, such as "EL133UF1", which outlives the calibration.
     */
    [[nodiscard]] const char* name() const noexcept;

    /**
     * @brief Returns the width of the panel the calibration is for.
     *
     * @return The native width in pixels.
     */
    [[nodiscard]] size_t width() const noexcept;

    /**
     * @brief Returns the height of the panel the calibration is for.
     *
     * @return The native height in pixels.
     */
    [[nodiscard]] size_t height() const noexcept;

    /**
     * @brief Returns the color mode the calibration is for.
     *
     * @return The color mode a panel must be in to be written with this calibration.
     */
    [[nodiscard]] enum color_mode color_mode() const noexcept { return _mode; }

    /**
     * @brief Returns the rendering intents offered for this panel.
     *
     * @return The presets; the first one is the panel's default.
     */
    [[nodiscard]] std::vector<preset> presets() const;

    /**
     * @brief Returns the panel's inks.
     *
     * Gray panels list their levels from darkest to lightest.
     *
     * @return The inks, stored in this object and valid as long as it is.
     */
    [[nodiscard]] std::span<const ink> inks() const noexcept { return std::span<const ink>(_inks).first(_ink_count); }

private:
    /** @cond INTERNAL */
    friend struct detail::calibration_access;

    calibration(
        const epi_panel_t* panel,
        enum color_mode mode,
        const std::array<ink, 6>& inks,
        size_t ink_count
    ) noexcept
        : _panel(panel)
        , _mode(mode)
        , _inks(inks)
        , _ink_count(ink_count) {}

    const epi_panel_t* _panel;
    enum color_mode _mode;
    std::array<ink, 6> _inks;
    size_t _ink_count;
    /** @endcond */
};

/**
 * @headerfile <idfxx/epaper/image_writer>
 * @brief Options for @ref write_image.
 */
struct write_options {
    /**
     * @brief The rendering intent; `std::nullopt` is the calibration's first preset.
     */
    std::optional<rendering_intent> intent = std::nullopt;
    /**
     * @brief The number of rows converted between writes to a panel.
     *
     * Must be non-zero. More rows mean fewer, larger transfers and more memory.
     */
    size_t band_rows = 32;
    /**
     * @brief Maximum working memory, in bytes.
     *
     * Bounds the memory for rendering, for the conversion and for the band
     * together. The conversion and the band are counted at the full screen
     * width, their largest possible size. The bound is checked before anything
     * is allocated, and allocator overhead is not counted. `std::nullopt` sets
     * no limit.
     */
    std::optional<size_t> memory_limit = std::nullopt;
};

/** @cond INTERNAL */
namespace detail {

// What each pixel type of a surface or framebuffer means, by calibration color mode.
// Ink codes are the library's: for gray, levels 0 to 3 from darkest to lightest.
template<typename Pixel>
struct pixel_traits;

template<>
struct pixel_traits<bool> {
    static constexpr enum color_mode mode = color_mode::mono;

    // A mono surface's true is ink: code 0, the darker level.
    [[nodiscard]] static constexpr bool from_code(uint8_t code) noexcept { return code == 0; }
};

template<>
struct pixel_traits<gray4> {
    static constexpr enum color_mode mode = color_mode::gray4;

    [[nodiscard]] static constexpr gray4 from_code(uint8_t code) noexcept { return static_cast<gray4>(3 - code); }
};

template<>
struct pixel_traits<color6> {
    static constexpr enum color_mode mode = color_mode::color6;

    [[nodiscard]] static constexpr color6 from_code(uint8_t code) noexcept { return static_cast<color6>(code); }
};

// A surface that image writing can draw to: a framebuffer, and views of one such as
// gfx::rotated and gfx::canvas.
template<typename S>
concept image_surface = requires(S& s, const S& cs, size_t x, size_t y, typename S::pixel_type pixel) {
    pixel_traits<typename S::pixel_type>::mode;
    { s.set_pixel(x, y, pixel) } noexcept;
    { cs.width() } -> std::convertible_to<size_t>;
    { cs.height() } -> std::convertible_to<size_t>;
};

// The framebuffers a panel can be written from.
template<typename F>
concept image_framebuffer =
    std::same_as<F, mono_framebuffer> || std::same_as<F, gray4_framebuffer> || std::same_as<F, color6_framebuffer>;

// Where a picture is written: a panel, with an optional overlay, or a surface.
struct destination {
    panel* display = nullptr; // the panel, or nullptr for a surface

    // The color mode the caller's own types assume: the surface's, or the overlay's band.
    std::optional<enum color_mode> pixels = std::nullopt;

    // Draws over a band, of the framebuffer type for pixels.
    void (*draw_overlay)(void* overlay, void* band, size_t x, size_t y) = nullptr;
    void* overlay = nullptr;

    // Writes a row of ink codes at a surface's (x, y).
    void (*set_surface_row)(void* surface, size_t x, size_t y, std::span<const uint8_t> codes) noexcept = nullptr;
    void* surface = nullptr;
    size_t surface_width = 0;
    size_t surface_height = 0;
};

[[nodiscard]] inline destination to_panel(panel& display) noexcept {
    return {.display = &display};
}

template<typename Framebuffer, typename Overlay>
[[nodiscard]] destination to_panel(panel& display, Overlay& draw) noexcept {
    return {
        .display = &display,
        .pixels = pixel_traits<typename Framebuffer::pixel_type>::mode,
        .draw_overlay = [](
                            void* overlay, void* band, size_t x, size_t y
                        ) { std::invoke(*static_cast<Overlay*>(overlay), *static_cast<Framebuffer*>(band), x, y); },
        .overlay = const_cast<void*>(static_cast<const void*>(std::addressof(draw))),
    };
}

template<typename Surface>
[[nodiscard]] destination to_surface(Surface& surface) noexcept {
    using traits = pixel_traits<typename Surface::pixel_type>;
    return {
        .pixels = traits::mode,
        .set_surface_row =
            [](void* to, size_t x, size_t y, std::span<const uint8_t> codes) noexcept {
                auto& s = *static_cast<Surface*>(to);
                for (size_t i = 0; i < codes.size(); ++i) {
                    s.set_pixel(x + i, y, traits::from_code(codes[i]));
                }
            },
        .surface = std::addressof(surface),
        .surface_width = surface.width(),
        .surface_height = surface.height(),
    };
}

// Takes the rows of a placed picture, converts them to inks and writes them to a destination.
class image_stream {
public:
    // Checks the arguments, and works out what the renderer may use: the screen,
    // the alignment, and what's left of the memory limit.
    [[nodiscard]] static result<image::render_config>
    plan(const destination& to, const calibration& cal, const write_options& options);

    // A stream for a picture delivered to the given rectangle of rows.
    [[nodiscard]] static result<image_stream>
    make(const destination& to, const calibration& cal, const image::rect& output, const write_options& options);

    image_stream(image_stream&& other) noexcept;
    image_stream& operator=(image_stream&& other) noexcept;
    ~image_stream();

    // The next rows of the picture, from the top: an image::sink.
    [[nodiscard]] result<void> operator()(image::point at, image::view<const image::rgb888> rows);

    // Writes what remains after the last row.
    [[nodiscard]] result<void> try_finish();

private:
    struct state;

    explicit image_stream(std::unique_ptr<state> s) noexcept;

    std::unique_ptr<state> _state;
};

// Everything between the arguments and the destination, for every kind of destination.
template<image::decoder Decoder>
[[nodiscard]] result<void> try_write(
    const destination& to,
    const calibration& cal,
    Decoder& decoder,
    const image::placement& where,
    const write_options& options
) {
    auto render = image_stream::plan(to, cal, options);
    if (!render) {
        return error(render.error());
    }
    auto picture = image::renderer::make(decoder, where, *render);
    if (!picture) {
        return error(picture.error());
    }
    if (picture->empty()) {
        return {};
    }
    auto stream = image_stream::make(to, cal, picture->output(), options);
    if (!stream) {
        return error(stream.error());
    }
    auto decoded = decoder.try_decode([&](image::point at, image::view<const image::rgb888> rows) -> result<void> {
        return picture->try_push(at, rows, *stream);
    });
    if (!decoded) {
        return decoded;
    }
    return stream->try_finish();
}

} // namespace detail
/** @endcond */

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
/**
 * @headerfile <idfxx/epaper/image_writer>
 * @brief Shows an image on an e-paper panel.
 *
 * Renders the decoder's image as @p where places it on the panel, converts it
 * to inks with @p cal, and writes it to the panel a band of rows at a time.
 * Nothing changes on the glass until the caller calls @ref panel::refresh.
 * Pixels outside the target are left as they are. A picture entirely off the
 * screen succeeds without decoding.
 *
 * The panel writes whole bytes, so the left edge of what is written must be a
 * multiple of 2 columns in @ref color_mode::color6, or 8 in the others, and so
 * must its right edge unless it reaches the right edge of the screen. A
 * placement that doesn't meet this is an error, unless
 * @ref idfxx::image::placement::fill_to_alignment asks for the gap to be filled.
 *
 * @code
 * idfxx::memory_source src{jpeg_bytes};
 * idfxx::jpeg::decoder dec(src);
 *
 * // Fit the picture in the top half of an 800x480 screen, on black.
 * display.set_color_mode(idfxx::epaper::color_mode::gray4);
 * idfxx::epaper::write_image(
 *     display,
 *     idfxx::epaper::calibration::gdey075t7_gray4(),
 *     dec,
 *     {
 *         .target = idfxx::image::rect{.x = 0, .y = 0, .width = 800, .height = 240},
 *         .layout = idfxx::image::layout::fit,
 *         .background = {0, 0, 0},
 *     }
 * );
 * display.refresh();
 * @endcode
 *
 * @tparam Decoder A @ref idfxx::image::decoder, such as
 *         `idfxx::jpeg::decoder`.
 * @param display The panel to write to, already in the calibration's color mode.
 * @param cal     The calibration for the panel.
 * @param decoder The decoder of the image. It decodes once.
 * @param where   Where and how the picture is placed; by default it fills the
 *                screen.
 * @param options Rendering intent, band size and memory limit.
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
 * @throws std::system_error on error, including `errc::invalid_arg` when the
 *         panel's size isn't the calibration's, `band_rows` is zero, the rendering
 *         intent is out of range or the placement isn't aligned;
 *         `idfxx::image::errc::too_large` when `memory_limit` is too small;
 *         `errc::invalid_state` when the panel's color mode isn't the calibration's or
 *         the panel is asleep, before anything is decoded; and any error from the
 *         decoder, the placement or the panel.
 */
template<image::decoder Decoder>
void write_image(
    panel& display,
    const calibration& cal,
    Decoder& decoder,
    const image::placement& where = {},
    const write_options& options = {}
) {
    unwrap(detail::try_write(detail::to_panel(display), cal, decoder, where, options));
}

/**
 * @headerfile <idfxx/epaper/image_writer>
 * @brief Shows an image filling an e-paper panel, with an overlay drawn over each band.
 *
 * Renders the decoder's image to fill the panel, upright according to its own
 * orientation, converts it to inks with @p cal, and writes it to the panel a band of
 * rows at a time, calling @p draw with each band just before it is written. Nothing
 * changes on the glass until the caller calls @ref panel::refresh. The caller names the type of
 * the band, which must match the calibration's color mode.
 *
 * The band is drawn in screen coordinates through a canvas whose origin is the
 * band's top-left pixel on the screen:
 *
 * @code
 * idfxx::epaper::write_image<idfxx::epaper::gray4_framebuffer>(
 *     display,
 *     idfxx::epaper::calibration::gdey075t7_gray4(),
 *     dec,
 *     [&](idfxx::epaper::gray4_framebuffer& band, size_t x, size_t y) {
 *         idfxx::gfx::canvas canvas(band, x, y);
 *         canvas.draw_text(font, 10, 460, "Hello", idfxx::epaper::gray4::black);
 *     }
 * );
 * @endcode
 *
 * @tparam Framebuffer The band's type: `mono_framebuffer`, `gray4_framebuffer` or
 *         `color6_framebuffer`.
 * @tparam Decoder The decoder type; see @ref idfxx::image::decoder.
 * @tparam Overlay A callable taking `(Framebuffer& band, size_t x, size_t y)`.
 * @param display The panel to write to, already in the calibration's color mode.
 * @param cal     The calibration for the panel.
 * @param decoder The decoder of the image. It decodes once.
 * @param draw    Called once for each band with the band and the screen position of
 *                its top-left pixel. It draws over the picture in the band, and runs
 *                again for every band, so it must draw the same thing each time.
 *                The last band may have fewer rows written than the band has.
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
 * @throws std::system_error on error, including `errc::invalid_arg` when the
 *         panel's size isn't the calibration's or @p Framebuffer doesn't match the
 *         calibration's color mode; `idfxx::image::errc::too_large` when `memory_limit`
 *         is too small; `errc::invalid_state` when the panel's color mode isn't the
 *         calibration's or the panel is asleep, before anything is decoded; and any
 *         error from the decoder, the placement or the panel.
 */
template<typename Framebuffer, image::decoder Decoder, typename Overlay>
    requires detail::image_framebuffer<Framebuffer> && std::invocable<Overlay&, Framebuffer&, size_t, size_t>
void write_image(panel& display, const calibration& cal, Decoder& decoder, Overlay&& draw) {
    unwrap(detail::try_write(detail::to_panel<Framebuffer>(display, draw), cal, decoder, {}, {}));
}

/**
 * @headerfile <idfxx/epaper/image_writer>
 * @brief Shows an image on an e-paper panel, with an overlay drawn over each band.
 *
 * Renders the decoder's image as @p where places it on the panel, converts it to
 * inks with @p cal, and writes it to the panel a band of rows at a time, calling
 * @p draw with each band just before it is written. Nothing changes on the glass
 * until the caller calls @ref panel::refresh. Pixels outside the target are left as they are.
 * A picture entirely off the screen succeeds without decoding. The caller names the
 * type of the band, which must match the calibration's color mode.
 *
 * The panel writes whole bytes, so the left edge of what is written must be a
 * multiple of 2 columns in @ref color_mode::color6, or 8 in the others, and so
 * must its right edge unless it reaches the right edge of the screen. A
 * placement that doesn't meet this is an error, unless
 * @ref idfxx::image::placement::fill_to_alignment asks for the gap to be filled.
 *
 * The band is drawn in screen coordinates through a canvas whose origin is the
 * band's top-left pixel on the screen:
 *
 * @code
 * idfxx::epaper::write_image<idfxx::epaper::gray4_framebuffer>(
 *     display,
 *     idfxx::epaper::calibration::gdey075t7_gray4(),
 *     dec,
 *     {.target = idfxx::image::rect{.x = 0, .y = 0, .width = 800, .height = 240}},
 *     [&](idfxx::epaper::gray4_framebuffer& band, size_t x, size_t y) {
 *         idfxx::gfx::canvas canvas(band, x, y);
 *         canvas.draw_text(font, 10, 220, "Hello", idfxx::epaper::gray4::black);
 *     }
 * );
 * @endcode
 *
 * @tparam Framebuffer The band's type: `mono_framebuffer`, `gray4_framebuffer` or
 *         `color6_framebuffer`.
 * @tparam Decoder The decoder type; see @ref idfxx::image::decoder.
 * @tparam Overlay A callable taking `(Framebuffer& band, size_t x, size_t y)`.
 * @param display The panel to write to, already in the calibration's color mode.
 * @param cal     The calibration for the panel.
 * @param decoder The decoder of the image. It decodes once.
 * @param where   Where and how the picture is placed.
 * @param draw    Called once for each band with the band and the screen position of
 *                its top-left pixel. It draws over the picture in the band, and runs
 *                again for every band, so it must draw the same thing each time.
 *                The last band may have fewer rows written than the band has.
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
 * @throws std::system_error on error, including `errc::invalid_arg` when the
 *         panel's size isn't the calibration's, @p Framebuffer doesn't match the
 *         calibration's color mode or the placement isn't aligned;
 *         `idfxx::image::errc::too_large` when `memory_limit` is too small;
 *         `errc::invalid_state` when the panel's color mode isn't the calibration's or
 *         the panel is asleep, before anything is decoded; and any error from the
 *         decoder, the placement or the panel.
 */
template<typename Framebuffer, image::decoder Decoder, typename Overlay>
    requires detail::image_framebuffer<Framebuffer> && std::invocable<Overlay&, Framebuffer&, size_t, size_t>
void write_image(
    panel& display,
    const calibration& cal,
    Decoder& decoder,
    const image::placement& where,
    Overlay&& draw
) {
    unwrap(detail::try_write(detail::to_panel<Framebuffer>(display, draw), cal, decoder, where, {}));
}

/**
 * @headerfile <idfxx/epaper/image_writer>
 * @brief Shows an image on an e-paper panel, with an overlay drawn over each band.
 *
 * Renders the decoder's image as @p where places it on the panel, converts it to
 * inks with @p cal, and writes it to the panel a band of rows at a time, calling
 * @p draw with each band just before it is written. Nothing changes on the glass
 * until the caller calls @ref panel::refresh. Pixels outside the target are left as they are.
 * A picture entirely off the screen succeeds without decoding. The caller names the
 * type of the band, which must match the calibration's color mode.
 *
 * The panel writes whole bytes, so the left edge of what is written must be a
 * multiple of 2 columns in @ref color_mode::color6, or 8 in the others, and so
 * must its right edge unless it reaches the right edge of the screen. A
 * placement that doesn't meet this is an error, unless
 * @ref idfxx::image::placement::fill_to_alignment asks for the gap to be filled.
 *
 * The band is drawn in screen coordinates through a canvas whose origin is the
 * band's top-left pixel on the screen:
 *
 * @code
 * idfxx::epaper::write_image<idfxx::epaper::gray4_framebuffer>(
 *     display,
 *     idfxx::epaper::calibration::gdey075t7_gray4(),
 *     dec,
 *     {.target = idfxx::image::rect{.x = 0, .y = 0, .width = 800, .height = 240}},
 *     {.band_rows = 16},
 *     [&](idfxx::epaper::gray4_framebuffer& band, size_t x, size_t y) {
 *         idfxx::gfx::canvas canvas(band, x, y);
 *         canvas.draw_text(font, 10, 220, "Hello", idfxx::epaper::gray4::black);
 *     }
 * );
 * @endcode
 *
 * @tparam Framebuffer The band's type: `mono_framebuffer`, `gray4_framebuffer` or
 *         `color6_framebuffer`.
 * @tparam Decoder The decoder type; see @ref idfxx::image::decoder.
 * @tparam Overlay A callable taking `(Framebuffer& band, size_t x, size_t y)`.
 * @param display The panel to write to, already in the calibration's color mode.
 * @param cal     The calibration for the panel.
 * @param decoder The decoder of the image. It decodes once.
 * @param where   Where and how the picture is placed.
 * @param options Rendering intent, band size and memory limit.
 * @param draw    Called once for each band with the band and the screen position of
 *                its top-left pixel. It draws over the picture in the band, and runs
 *                again for every band, so it must draw the same thing each time.
 *                The last band may have fewer rows written than the band has.
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
 * @throws std::system_error on error, including `errc::invalid_arg` when the
 *         panel's size isn't the calibration's, @p Framebuffer doesn't match the
 *         calibration's color mode, `band_rows` is zero, the rendering intent is out
 *         of range or the placement isn't aligned; `idfxx::image::errc::too_large` when
 *         `memory_limit` is too small; `errc::invalid_state` when the panel's color mode
 *         isn't the calibration's or the panel is asleep, before anything is decoded;
 *         and any error from the decoder, the placement or the panel.
 */
template<typename Framebuffer, image::decoder Decoder, typename Overlay>
    requires detail::image_framebuffer<Framebuffer> && std::invocable<Overlay&, Framebuffer&, size_t, size_t>
void write_image(
    panel& display,
    const calibration& cal,
    Decoder& decoder,
    const image::placement& where,
    const write_options& options,
    Overlay&& draw
) {
    unwrap(detail::try_write(detail::to_panel<Framebuffer>(display, draw), cal, decoder, where, options));
}

/**
 * @headerfile <idfxx/epaper/image_writer>
 * @brief Shows an image in a surface.
 *
 * Renders the decoder's image as @p where places it on the surface, converts it
 * to inks with @p cal and sets each pixel of the picture. Pixels outside the
 * target are left as they are. There are no alignment rules, and the surface
 * doesn't have to be the size of the calibration's panel. The surface is anything
 * with a `pixel_type` of `bool`, `gray4` or `color6`, a `noexcept`
 * `set_pixel(x, y, color)`, and `width()` and `height()`: a framebuffer, or an
 * `idfxx::gfx::rotated` or `idfxx::gfx::canvas` view of one, to show the picture
 * turned or in a window. Its pixel type must match the calibration's color mode.
 *
 * @code
 * idfxx::epaper::gray4_framebuffer fb(800, 480);
 * idfxx::epaper::write_image(fb, idfxx::epaper::calibration::gdey075t7_gray4(), dec);
 * display.write(fb);
 * display.refresh();
 * @endcode
 *
 * @tparam Surface A surface.
 * @tparam Decoder The decoder type; see @ref idfxx::image::decoder.
 * @param surface The surface to render into.
 * @param cal     The calibration for the panel the surface is meant for.
 * @param decoder The decoder of the image. It decodes once.
 * @param where   Where and how the picture is placed; by default it fills the surface.
 * @param options `band_rows` must still be non-zero; otherwise only the rendering intent
 *                and `memory_limit` apply.
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
 * @throws std::system_error on error, including `errc::invalid_arg` when the
 *         surface's pixel type doesn't match the calibration's color mode or `band_rows`
 *         is zero, and any error from the decoder or the placement.
 */
template<typename Surface, image::decoder Decoder>
    requires detail::image_surface<Surface>
void write_image(
    Surface& surface,
    const calibration& cal,
    Decoder& decoder,
    const image::placement& where = {},
    const write_options& options = {}
) {
    unwrap(detail::try_write(detail::to_surface(surface), cal, decoder, where, options));
}
#endif

/**
 * @headerfile <idfxx/epaper/image_writer>
 * @brief Shows an image on an e-paper panel.
 *
 * Renders the decoder's image as @p where places it on the panel, converts it
 * to inks with @p cal, and writes it to the panel a band of rows at a time.
 * Nothing changes on the glass until the caller calls @ref panel::try_refresh.
 * See the throwing @ref write_image overload for a panel, including for the
 * alignment rules.
 *
 * @tparam Decoder A @ref idfxx::image::decoder, such as
 *         `idfxx::jpeg::decoder`.
 * @param display The panel to write to, already in the calibration's color mode.
 * @param cal     The calibration for the panel.
 * @param decoder The decoder of the image. It decodes once.
 * @param where   Where and how the picture is placed; by default it fills the
 *                screen.
 * @param options Rendering intent, band size and memory limit.
 * @return Success, or an error.
 * @retval idfxx::errc::invalid_arg The panel's size isn't the calibration's,
 *         `band_rows` is zero, the rendering intent is out of range, or the placement
 *         isn't aligned.
 * @retval idfxx::image::errc::too_large `memory_limit` is too small.
 * @retval idfxx::errc::invalid_size The image is too large to render.
 * @retval idfxx::errc::invalid_state The panel's color mode isn't the calibration's,
 *         or the panel is asleep. Nothing has been decoded.
 * @retval other An error from the decoder, the placement or the panel, unchanged.
 */
template<image::decoder Decoder>
[[nodiscard]] result<void> try_write_image(
    panel& display,
    const calibration& cal,
    Decoder& decoder,
    const image::placement& where = {},
    const write_options& options = {}
) {
    return detail::try_write(detail::to_panel(display), cal, decoder, where, options);
}

/**
 * @headerfile <idfxx/epaper/image_writer>
 * @brief Shows an image filling an e-paper panel, with an overlay drawn over each band.
 *
 * Renders the decoder's image to fill the panel, upright according to its own
 * orientation, converts it to inks with @p cal, and writes it to the panel a band of
 * rows at a time, calling @p draw with each band just before it is written. Nothing
 * changes on the glass until the caller calls @ref panel::try_refresh. The caller names the type of
 * the band, which must match the calibration's color mode.
 *
 * @tparam Framebuffer The band's type: `mono_framebuffer`, `gray4_framebuffer` or
 *         `color6_framebuffer`.
 * @tparam Decoder The decoder type; see @ref idfxx::image::decoder.
 * @tparam Overlay A callable taking `(Framebuffer& band, size_t x, size_t y)`.
 * @param display The panel to write to, already in the calibration's color mode.
 * @param cal     The calibration for the panel.
 * @param decoder The decoder of the image. It decodes once.
 * @param draw    Called once for each band with the band and the screen position of
 *                its top-left pixel. It draws over the picture in the band, and runs
 *                again for every band, so it must draw the same thing each time.
 *                The last band may have fewer rows written than the band has.
 * @return Success, or an error.
 * @retval idfxx::errc::invalid_arg The panel's size isn't the calibration's, or
 *         @p Framebuffer doesn't match the calibration's color mode.
 * @retval idfxx::image::errc::too_large `memory_limit` is too small.
 * @retval idfxx::errc::invalid_size The image is too large to render.
 * @retval idfxx::errc::invalid_state The panel's color mode isn't the calibration's,
 *         or the panel is asleep. Nothing has been decoded.
 * @retval other An error from the decoder, the placement or the panel, unchanged.
 */
template<typename Framebuffer, image::decoder Decoder, typename Overlay>
    requires detail::image_framebuffer<Framebuffer> && std::invocable<Overlay&, Framebuffer&, size_t, size_t>
[[nodiscard]] result<void> try_write_image(panel& display, const calibration& cal, Decoder& decoder, Overlay&& draw) {
    return detail::try_write(detail::to_panel<Framebuffer>(display, draw), cal, decoder, {}, {});
}

/**
 * @headerfile <idfxx/epaper/image_writer>
 * @brief Shows an image on an e-paper panel, with an overlay drawn over each band.
 *
 * Renders the decoder's image as @p where places it on the panel, converts it to
 * inks with @p cal, and writes it to the panel a band of rows at a time, calling
 * @p draw with each band just before it is written. Nothing changes on the glass
 * until the caller calls @ref panel::try_refresh. Pixels outside the target are left as they are.
 * A picture entirely off the screen succeeds without decoding. The caller names the
 * type of the band, which must match the calibration's color mode.
 *
 * The panel writes whole bytes, so the left edge of what is written must be a
 * multiple of 2 columns in @ref color_mode::color6, or 8 in the others, and so
 * must its right edge unless it reaches the right edge of the screen. A
 * placement that doesn't meet this is an error, unless
 * @ref idfxx::image::placement::fill_to_alignment asks for the gap to be filled.
 *
 * @tparam Framebuffer The band's type: `mono_framebuffer`, `gray4_framebuffer` or
 *         `color6_framebuffer`.
 * @tparam Decoder The decoder type; see @ref idfxx::image::decoder.
 * @tparam Overlay A callable taking `(Framebuffer& band, size_t x, size_t y)`.
 * @param display The panel to write to, already in the calibration's color mode.
 * @param cal     The calibration for the panel.
 * @param decoder The decoder of the image. It decodes once.
 * @param where   Where and how the picture is placed.
 * @param draw    Called once for each band with the band and the screen position of
 *                its top-left pixel. It draws over the picture in the band, and runs
 *                again for every band, so it must draw the same thing each time.
 *                The last band may have fewer rows written than the band has.
 * @return Success, or an error.
 * @retval idfxx::errc::invalid_arg The panel's size isn't the calibration's,
 *         @p Framebuffer doesn't match the calibration's color mode, or the placement
 *         isn't aligned.
 * @retval idfxx::image::errc::too_large `memory_limit` is too small.
 * @retval idfxx::errc::invalid_size The image is too large to render.
 * @retval idfxx::errc::invalid_state The panel's color mode isn't the calibration's,
 *         or the panel is asleep. Nothing has been decoded.
 * @retval other An error from the decoder, the placement or the panel, unchanged.
 */
template<typename Framebuffer, image::decoder Decoder, typename Overlay>
    requires detail::image_framebuffer<Framebuffer> && std::invocable<Overlay&, Framebuffer&, size_t, size_t>
[[nodiscard]] result<void> try_write_image(
    panel& display,
    const calibration& cal,
    Decoder& decoder,
    const image::placement& where,
    Overlay&& draw
) {
    return detail::try_write(detail::to_panel<Framebuffer>(display, draw), cal, decoder, where, {});
}

/**
 * @headerfile <idfxx/epaper/image_writer>
 * @brief Shows an image on an e-paper panel, with an overlay drawn over each band.
 *
 * Renders the decoder's image as @p where places it on the panel, converts it to
 * inks with @p cal, and writes it to the panel a band of rows at a time, calling
 * @p draw with each band just before it is written. Nothing changes on the glass
 * until the caller calls @ref panel::try_refresh. Pixels outside the target are left as they are.
 * A picture entirely off the screen succeeds without decoding. The caller names the
 * type of the band, which must match the calibration's color mode.
 *
 * The panel writes whole bytes, so the left edge of what is written must be a
 * multiple of 2 columns in @ref color_mode::color6, or 8 in the others, and so
 * must its right edge unless it reaches the right edge of the screen. A
 * placement that doesn't meet this is an error, unless
 * @ref idfxx::image::placement::fill_to_alignment asks for the gap to be filled.
 *
 * @tparam Framebuffer The band's type: `mono_framebuffer`, `gray4_framebuffer` or
 *         `color6_framebuffer`.
 * @tparam Decoder The decoder type; see @ref idfxx::image::decoder.
 * @tparam Overlay A callable taking `(Framebuffer& band, size_t x, size_t y)`.
 * @param display The panel to write to, already in the calibration's color mode.
 * @param cal     The calibration for the panel.
 * @param decoder The decoder of the image. It decodes once.
 * @param where   Where and how the picture is placed.
 * @param options Rendering intent, band size and memory limit.
 * @param draw    Called once for each band with the band and the screen position of
 *                its top-left pixel; it must draw the same thing each time.
 * @return Success, or an error.
 * @retval idfxx::errc::invalid_arg The panel's size isn't the calibration's,
 *         @p Framebuffer doesn't match the calibration's color mode, `band_rows` is
 *         zero, the rendering intent is out of range, or the placement isn't aligned.
 * @retval idfxx::image::errc::too_large `memory_limit` is too small.
 * @retval idfxx::errc::invalid_size The image is too large to render.
 * @retval idfxx::errc::invalid_state The panel's color mode isn't the calibration's,
 *         or the panel is asleep. Nothing has been decoded.
 * @retval other An error from the decoder, the placement or the panel, unchanged.
 */
template<typename Framebuffer, image::decoder Decoder, typename Overlay>
    requires detail::image_framebuffer<Framebuffer> && std::invocable<Overlay&, Framebuffer&, size_t, size_t>
[[nodiscard]] result<void> try_write_image(
    panel& display,
    const calibration& cal,
    Decoder& decoder,
    const image::placement& where,
    const write_options& options,
    Overlay&& draw
) {
    return detail::try_write(detail::to_panel<Framebuffer>(display, draw), cal, decoder, where, options);
}

/**
 * @headerfile <idfxx/epaper/image_writer>
 * @brief Shows an image in a surface.
 *
 * Renders the decoder's image as @p where places it on the surface, converts it
 * to inks with @p cal and sets each pixel of the picture; see the throwing
 * @ref write_image overload for a surface.
 *
 * @tparam Surface A surface.
 * @tparam Decoder The decoder type; see @ref idfxx::image::decoder.
 * @param surface The surface to render into.
 * @param cal     The calibration for the panel the surface is meant for.
 * @param decoder The decoder of the image. It decodes once.
 * @param where   Where and how the picture is placed; by default it fills the surface.
 * @param options `band_rows` must still be non-zero; otherwise only the rendering intent
 *                and `memory_limit` apply.
 * @return Success, or an error.
 * @retval idfxx::errc::invalid_arg The surface's pixel type doesn't match the
 *         calibration's color mode, `band_rows` is zero, or the rendering intent is
 *         out of range.
 * @retval idfxx::image::errc::too_large `memory_limit` is too small.
 * @retval idfxx::errc::invalid_size The image is too large to render, or the surface
 *         is over 65535 pixels a side.
 * @retval other An error from the decoder or the placement, unchanged.
 */
template<typename Surface, image::decoder Decoder>
    requires detail::image_surface<Surface>
[[nodiscard]] result<void> try_write_image(
    Surface& surface,
    const calibration& cal,
    Decoder& decoder,
    const image::placement& where = {},
    const write_options& options = {}
) {
    return detail::try_write(detail::to_surface(surface), cal, decoder, where, options);
}

} // namespace idfxx::epaper

/** @} */ // end of idfxx_epaper_image
