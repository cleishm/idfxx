// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

/**
 * @headerfile <idfxx/epaper/ink_writer>
 * @file ink_writer.hpp
 * @brief Writes frames of panel inks, read from a byte source, to a panel or framebuffer.
 * @ingroup idfxx_epaper
 *
 * For a picture already converted to a panel's inks elsewhere, such as on a
 * phone or in a browser, and delivered to the device as bytes. See
 * @ref idfxx::epaper::write_inks for the format of a frame.
 */

#include "sdkconfig.h"

#include <idfxx/byte_source>
#include <idfxx/epaper/color6_framebuffer>
#include <idfxx/epaper/gray4_framebuffer>
#include <idfxx/epaper/mono_framebuffer>
#include <idfxx/epaper/panel>
#include <idfxx/error>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>

/**
 * @headerfile <idfxx/epaper/ink_writer>
 * @brief ePaper display driver classes.
 */
namespace idfxx::epaper {

/**
 * @headerfile <idfxx/epaper/ink_writer>
 * @brief Where a frame of inks goes on its destination, and how much of it is read at a time.
 *
 * The left edge of the rectangle must be a multiple of the color mode's
 * `x_alignment`, 8 for @ref color_mode::mono and @ref color_mode::gray4 and 2
 * for @ref color_mode::color6, and so must its right edge unless it is the
 * destination's right edge. The rectangle must fit on the destination: a frame
 * has a fixed number of bytes, so it is never clipped.
 */
struct ink_options {
    /** @brief The column of the rectangle's left edge. */
    size_t x = 0;
    /** @brief The row of the rectangle's top edge. */
    size_t y = 0;
    /** @brief The rectangle's width in pixels; `std::nullopt` reaches the destination's right edge.
     */
    std::optional<size_t> width = std::nullopt;
    /** @brief The rectangle's height in pixels; `std::nullopt` reaches the destination's bottom
     * edge. */
    std::optional<size_t> height = std::nullopt;
    /**
     * @brief The number of rows read at a time.
     *
     * Must be non-zero. More rows mean fewer writes to a panel and more memory.
     */
    size_t band_rows = 32;
};

/** @cond INTERNAL */
namespace detail {

// A byte source of any type, for the code behind the templates below.
struct ink_source {
    void* source;
    result<size_t> (*read)(void* source, std::span<uint8_t> into);
};

template<byte_source Source>
[[nodiscard]] ink_source to_ink_source(Source& source) noexcept {
    return {
        .source = const_cast<void*>(static_cast<const void*>(std::addressof(source))),
        .read = [](void* from, std::span<uint8_t> into) -> result<size_t> {
            return static_cast<Source*>(from)->try_read(mutable_byte_view{into});
        },
    };
}

[[nodiscard]] result<void>
try_write_inks(panel& display, const ink_source& source, const ink_options& options);
[[nodiscard]] result<void>
try_write_inks(mono_framebuffer& framebuffer, const ink_source& source, const ink_options& options);
[[nodiscard]] result<void> try_write_inks(
    gray4_framebuffer& framebuffer,
    const ink_source& source,
    const ink_options& options
);
[[nodiscard]] result<void> try_write_inks(
    color6_framebuffer& framebuffer,
    const ink_source& source,
    const ink_options& options
);

} // namespace detail
/** @endcond */

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
/**
 * @headerfile <idfxx/epaper/ink_writer>
 * @brief Writes a frame of inks from a byte source to a panel.
 *
 * Reads the frame a band of rows at a time and writes each band with
 * @ref panel::write_rows. Nothing changes on the glass until the caller calls
 * @ref panel::refresh.
 *
 * A frame is rows from top to bottom with no header, each row padded to a whole
 * byte. The panel's @ref panel::color_mode picks the format, and `w` is the width
 * of the rectangle in @p options:
 *
 * - In @ref color_mode::color6, 4 bits per pixel, high nibble first, each a @ref color6
 *   value (0, 1, 2, 3, 5 or 6), with `(w + 1) / 2` bytes in a row.
 * - In @ref color_mode::gray4, 2 bits per pixel, high bits first, from 0, the darkest, to
 *   3, the lightest, with `(w + 3) / 4` bytes in a row. These codes are the reverse of
 *   @ref gray4 values, in which 0 is white.
 * - In @ref color_mode::mono, 1 bit per pixel, high bit first, 1 being white and 0 black,
 *   with `(w + 7) / 8` bytes in a row.
 *
 * The `cleishm/epaper-image` library's `epi_pack_row` produces these rows.
 *
 * Exactly the frame's bytes are read. Bytes after the frame are neither read nor
 * checked, and the end of the stream isn't looked for, so a source such as an
 * open connection doesn't block once the frame is complete. The arguments are
 * checked before any byte is read.
 *
 * What is and isn't checked in the bytes:
 * - In @ref color_mode::color6, every nibble must be a @ref color6 value,
 *   including the padding nibble of a row of odd width. A band is checked before
 *   any of it is written, so nothing of an invalid band reaches the panel.
 * - In @ref color_mode::mono and @ref color_mode::gray4, every value is a valid
 *   ink, so only the length of a frame can be checked: corruption isn't visible.
 * - A frame made for other glass of the same size and mode can't be detected.
 *   Checking which panel a frame is for is the application's job, for instance on
 *   connecting.
 *
 * A row's padding is always written as white, whatever arrives.
 *
 * When this fails part way, the bands written so far stay in the panel's memory,
 * and after an error from the panel so may part of the band being written. The
 * glass doesn't change until a refresh, but any later refresh shows them, so write
 * the frame again first. The partial-refresh baseline isn't affected: it remains
 * what is on the glass, so a partial refresh after the frame has been written again
 * is correct. Writing the frame to a framebuffer first, and only then the framebuffer
 * to the panel, gives all-or-nothing at the cost of a frame's memory.
 *
 * To verify a frame, wrap the source in a `byte_source` of the application's own
 * that computes a CRC as the bytes pass, and compare it before refreshing:
 *
 * @code
 * template<idfxx::byte_source Source>
 * class crc_source {
 * public:
 *     explicit crc_source(Source& source) : _source(source) {}
 *
 *     idfxx::result<size_t> try_read(std::span<uint8_t> buf) {
 *         auto n = _source.try_read(buf);
 *         if (n) {
 *             _crc = esp_rom_crc32_le(_crc, buf.data(), *n);
 *         }
 *         return n;
 *     }
 *
 *     uint32_t crc() const noexcept { return _crc; }
 *
 * private:
 *     Source& _source;
 *     uint32_t _crc = 0;
 * };
 *
 * crc_source checked(connection);
 * idfxx::epaper::write_inks(display, checked);
 * // The bytes after the frame are still unread, so the CRC can follow it.
 * if (checked.crc() == read_expected_crc(connection)) {
 *     display.refresh();
 * }
 * @endcode
 *
 * @tparam Source A @ref idfxx::byte_source.
 * @param display The panel to write to, already in the frame's color mode.
 * @param source  The source of the frame. Any bytes after the frame stay unread.
 * @param options The rectangle to fill, by default the whole panel, and the band size.
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
 * @throws std::system_error on error: `errc::invalid_state` if the panel is asleep;
 *         `errc::invalid_arg` if `band_rows` or the rectangle's size is zero, the
 *         rectangle doesn't fit or isn't aligned (see @ref ink_options), or a
 *         six-color band holds an invalid value; `errc::invalid_size` if the stream
 *         ends before the frame does; and any error from the source or the panel,
 *         unchanged.
 */
template<byte_source Source>
void write_inks(panel& display, Source& source, const ink_options& options = {}) {
    unwrap(detail::try_write_inks(display, detail::to_ink_source(source), options));
}

/**
 * @headerfile <idfxx/epaper/ink_writer>
 * @brief Writes a frame of 1-bit inks from a byte source to a monochrome framebuffer.
 *
 * Takes the same frames as the @ref write_inks overload for a panel in
 * @ref color_mode::mono, under the same alignment rules, as the frame is packed
 * into whole bytes. Pixels outside the rectangle aren't changed. Bands read before
 * a failure stay in the framebuffer, and nothing of the band being read reaches it.
 *
 * Every value is a valid ink, so only the length of a frame can be checked. A row's
 * padding is ignored, and the framebuffer's padding bits stay white.
 *
 * @tparam Source A @ref idfxx::byte_source.
 * @param framebuffer The framebuffer to write to.
 * @param source      The source of the frame. Any bytes after the frame stay unread.
 * @param options     The rectangle to fill, by default the whole framebuffer, and the
 *                    band size.
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
 * @throws std::system_error on error: `errc::invalid_arg` if `band_rows` or the
 *         rectangle's size is zero, or the rectangle doesn't fit or isn't aligned
 *         (see @ref ink_options); `errc::invalid_size` if the stream ends before the
 *         frame does; and any error from the source, unchanged.
 */
template<byte_source Source>
void write_inks(mono_framebuffer& framebuffer, Source& source, const ink_options& options = {}) {
    unwrap(detail::try_write_inks(framebuffer, detail::to_ink_source(source), options));
}

/**
 * @headerfile <idfxx/epaper/ink_writer>
 * @brief Writes a frame of 2-bit inks from a byte source to a grayscale framebuffer.
 *
 * Takes the same frames as the @ref write_inks overload for a panel in
 * @ref color_mode::gray4, under the same alignment rules, as the frame is packed
 * into whole bytes. Pixels outside the rectangle aren't changed. Bands read before
 * a failure stay in the framebuffer, and nothing of the band being read reaches it.
 *
 * Every value is a valid ink, so only the length of a frame can be checked. A row's
 * padding is ignored, and the framebuffer's padding bits stay white.
 *
 * @tparam Source A @ref idfxx::byte_source.
 * @param framebuffer The framebuffer to write to.
 * @param source      The source of the frame. Any bytes after the frame stay unread.
 * @param options     The rectangle to fill, by default the whole framebuffer, and the
 *                    band size.
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
 * @throws std::system_error on error: `errc::invalid_arg` if `band_rows` or the
 *         rectangle's size is zero, or the rectangle doesn't fit or isn't aligned
 *         (see @ref ink_options); `errc::invalid_size` if the stream ends before the
 *         frame does; and any error from the source, unchanged.
 */
template<byte_source Source>
void write_inks(gray4_framebuffer& framebuffer, Source& source, const ink_options& options = {}) {
    unwrap(detail::try_write_inks(framebuffer, detail::to_ink_source(source), options));
}

/**
 * @headerfile <idfxx/epaper/ink_writer>
 * @brief Writes a frame of 4-bit inks from a byte source to a six-color framebuffer.
 *
 * Takes the same frames as the @ref write_inks overload for a panel in
 * @ref color_mode::color6, under the same alignment rules, as the frame is packed
 * into whole bytes. Pixels outside the rectangle aren't changed. Bands read before
 * a failure stay in the framebuffer, and nothing of the band being read reaches it.
 *
 * Every nibble must be a @ref color6 value, including a row's padding nibble, though
 * the framebuffer's padding nibble stays white whichever value arrives.
 *
 * @tparam Source A @ref idfxx::byte_source.
 * @param framebuffer The framebuffer to write to.
 * @param source      The source of the frame. Any bytes after the frame stay unread.
 * @param options     The rectangle to fill, by default the whole framebuffer, and the
 *                    band size.
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
 * @throws std::system_error on error: `errc::invalid_arg` if `band_rows` or the
 *         rectangle's size is zero, the rectangle doesn't fit or isn't aligned (see
 *         @ref ink_options), or a band holds an invalid value; `errc::invalid_size` if
 *         the stream ends before the frame does; and any error from the source,
 *         unchanged.
 */
template<byte_source Source>
void write_inks(color6_framebuffer& framebuffer, Source& source, const ink_options& options = {}) {
    unwrap(detail::try_write_inks(framebuffer, detail::to_ink_source(source), options));
}
#endif

/**
 * @headerfile <idfxx/epaper/ink_writer>
 * @brief Writes a frame of inks from a byte source to a panel.
 *
 * Reads the frame a band of rows at a time and writes each band with
 * @ref panel::try_write_rows. Nothing changes on the glass until the caller calls
 * @ref panel::try_refresh. The panel's @ref panel::color_mode picks the format. See
 * the throwing @ref write_inks overload for a panel for the format of a frame, what
 * is checked in it, what a failure leaves in the panel, and how to verify a frame.
 *
 * @tparam Source A @ref idfxx::byte_source.
 * @param display The panel to write to, already in the frame's color mode.
 * @param source  The source of the frame. Any bytes after the frame stay unread.
 * @param options The rectangle to fill, by default the whole panel, and the band size.
 * @return Success, or an error.
 * @retval idfxx::errc::invalid_state The panel is asleep. Nothing has been read.
 * @retval idfxx::errc::invalid_arg `band_rows` or the rectangle's size is zero, or the
 *         rectangle doesn't fit or isn't aligned (see @ref ink_options), and nothing
 *         has been read; or a six-color band holds an invalid value, and nothing of
 *         that band has been written.
 * @retval idfxx::errc::invalid_size The stream ended before the frame did.
 * @retval other An error from the source or the panel, unchanged.
 */
template<byte_source Source>
[[nodiscard]] result<void>
try_write_inks(panel& display, Source& source, const ink_options& options = {}) {
    return detail::try_write_inks(display, detail::to_ink_source(source), options);
}

/**
 * @headerfile <idfxx/epaper/ink_writer>
 * @brief Writes a frame of 1-bit inks from a byte source to a monochrome framebuffer.
 *
 * Takes the same frames as the @ref try_write_inks overload for a panel in
 * @ref color_mode::mono, under the same alignment rules, as the frame is packed
 * into whole bytes. Pixels outside the rectangle aren't changed. Bands read before
 * a failure stay in the framebuffer, and nothing of the band being read reaches it.
 *
 * Every value is a valid ink, so only the length of a frame can be checked. A row's
 * padding is ignored, and the framebuffer's padding bits stay white.
 *
 * @tparam Source A @ref idfxx::byte_source.
 * @param framebuffer The framebuffer to write to.
 * @param source      The source of the frame. Any bytes after the frame stay unread.
 * @param options     The rectangle to fill, by default the whole framebuffer, and the
 *                    band size.
 * @return Success, or an error.
 * @retval idfxx::errc::invalid_arg `band_rows` or the rectangle's size is zero, or the
 *         rectangle doesn't fit or isn't aligned (see @ref ink_options). Nothing has
 *         been read.
 * @retval idfxx::errc::invalid_size The stream ended before the frame did.
 * @retval other An error from the source, unchanged.
 */
template<byte_source Source>
[[nodiscard]] result<void>
try_write_inks(mono_framebuffer& framebuffer, Source& source, const ink_options& options = {}) {
    return detail::try_write_inks(framebuffer, detail::to_ink_source(source), options);
}

/**
 * @headerfile <idfxx/epaper/ink_writer>
 * @brief Writes a frame of 2-bit inks from a byte source to a grayscale framebuffer.
 *
 * Takes the same frames as the @ref try_write_inks overload for a panel in
 * @ref color_mode::gray4, under the same alignment rules, as the frame is packed
 * into whole bytes. Pixels outside the rectangle aren't changed. Bands read before
 * a failure stay in the framebuffer, and nothing of the band being read reaches it.
 *
 * Every value is a valid ink, so only the length of a frame can be checked. A row's
 * padding is ignored, and the framebuffer's padding bits stay white.
 *
 * @tparam Source A @ref idfxx::byte_source.
 * @param framebuffer The framebuffer to write to.
 * @param source      The source of the frame. Any bytes after the frame stay unread.
 * @param options     The rectangle to fill, by default the whole framebuffer, and the
 *                    band size.
 * @return Success, or an error.
 * @retval idfxx::errc::invalid_arg `band_rows` or the rectangle's size is zero, or the
 *         rectangle doesn't fit or isn't aligned (see @ref ink_options). Nothing has
 *         been read.
 * @retval idfxx::errc::invalid_size The stream ended before the frame did.
 * @retval other An error from the source, unchanged.
 */
template<byte_source Source>
[[nodiscard]] result<void>
try_write_inks(gray4_framebuffer& framebuffer, Source& source, const ink_options& options = {}) {
    return detail::try_write_inks(framebuffer, detail::to_ink_source(source), options);
}

/**
 * @headerfile <idfxx/epaper/ink_writer>
 * @brief Writes a frame of 4-bit inks from a byte source to a six-color framebuffer.
 *
 * Takes the same frames as the @ref try_write_inks overload for a panel in
 * @ref color_mode::color6, under the same alignment rules, as the frame is packed
 * into whole bytes. Pixels outside the rectangle aren't changed. Bands read before
 * a failure stay in the framebuffer, and nothing of the band being read reaches it.
 *
 * Every nibble must be a @ref color6 value, including a row's padding nibble, though
 * the framebuffer's padding nibble stays white whichever value arrives.
 *
 * @tparam Source A @ref idfxx::byte_source.
 * @param framebuffer The framebuffer to write to.
 * @param source      The source of the frame. Any bytes after the frame stay unread.
 * @param options     The rectangle to fill, by default the whole framebuffer, and the
 *                    band size.
 * @return Success, or an error.
 * @retval idfxx::errc::invalid_arg `band_rows` or the rectangle's size is zero, or the
 *         rectangle doesn't fit or isn't aligned (see @ref ink_options), and nothing
 *         has been read; or a band holds an invalid value, and nothing of that band
 *         has reached the framebuffer.
 * @retval idfxx::errc::invalid_size The stream ended before the frame did.
 * @retval other An error from the source, unchanged.
 */
template<byte_source Source>
[[nodiscard]] result<void>
try_write_inks(color6_framebuffer& framebuffer, Source& source, const ink_options& options = {}) {
    return detail::try_write_inks(framebuffer, detail::to_ink_source(source), options);
}

} // namespace idfxx::epaper
