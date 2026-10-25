// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

/**
 * @headerfile <idfxx/jpeg>
 * @file jpeg.hpp
 * @brief JPEG decoding into RGB888 images.
 *
 * @defgroup idfxx_jpeg JPEG Component
 * @brief Decodes baseline and progressive JPEG images from any byte source.
 *
 * A @ref idfxx::jpeg::decoder reads a JPEG stream from any
 * `idfxx::byte_source` (memory, a file, a flash partition, a socket or an
 * HTTP response body), reports the image's size and EXIF orientation from its
 * headers, and decodes it to @ref idfxx::image::rgb888 pixels, optionally
 * scaled down by a factor of M/8. Pixels go either into an image view of the
 * output size or, row by row, to an @ref idfxx::image::sink, so an image can
 * be decoded without holding all of its pixels in memory.
 *
 * @code
 * idfxx::memory_source src{jpeg_bytes};
 * idfxx::jpeg::decoder dec(src);
 *
 * // Cover a 320x240 screen. The target is in the stored orientation, so swap
 * // it if the EXIF orientation exchanges width and height.
 * if (idfxx::image::swaps_axes(dec.info().orientation)) {
 *     dec.scale_to_cover(240, 320);
 * } else {
 *     dec.scale_to_cover(320, 240);
 * }
 * idfxx::image::buffer<> img(dec.output_width(), dec.output_height());
 * dec.decode(img);
 *
 * // Turn the image upright according to its EXIF orientation.
 * idfxx::image::buffer<> upright = idfxx::image::oriented(img, dec.info().orientation);
 * @endcode
 * @{
 */

#include "sdkconfig.h"

#include <idfxx/byte_source>
#include <idfxx/error>
#include <idfxx/image>

#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <system_error>
#include <type_traits>
#include <utility>

/**
 * @headerfile <idfxx/jpeg>
 * @brief JPEG decoding.
 */
namespace idfxx::jpeg {

/**
 * @headerfile <idfxx/jpeg>
 * @brief Error codes for JPEG decoding.
 */
enum class errc : int {
    // clang-format off
    invalid_data  = 1, /*!< The data is not a JPEG, or is corrupt. */
    truncated     = 2, /*!< The data ended before the end of the image. */
    not_supported = 3, /*!< The image uses a feature the decoder does not support. */
    too_large     = 4, /*!< Decoding needs more working memory than the configured limit. */
    // clang-format on
};

/**
 * @headerfile <idfxx/jpeg>
 * @brief Error category for JPEG decoding errors.
 */
class error_category : public std::error_category {
public:
    /**
     * @brief Returns the name of the error category.
     *
     * @return The category name.
     */
    [[nodiscard]] const char* name() const noexcept override final;

    /**
     * @brief Returns a human-readable message for the given error code.
     *
     * @param ec The error code value.
     *
     * @return A descriptive error message.
     */
    [[nodiscard]] std::string message(int ec) const override final;
};

/**
 * @headerfile <idfxx/jpeg>
 * @brief Returns a reference to the JPEG error category singleton.
 *
 * @return Reference to the singleton jpeg::error_category instance.
 */
[[nodiscard]] const error_category& jpeg_category() noexcept;

/**
 * @headerfile <idfxx/jpeg>
 * @brief Creates an error code from an idfxx::jpeg::errc value.
 *
 * @param e The JPEG error code.
 *
 * @return The corresponding std::error_code in the JPEG category.
 */
[[nodiscard]] inline std::error_code make_error_code(errc e) noexcept {
    return {std::to_underlying(e), jpeg_category()};
}

/**
 * @headerfile <idfxx/jpeg>
 * @brief Properties of a JPEG image, read from its headers.
 */
struct info {
    /// @brief Full image width in pixels, in the stored (coded) orientation.
    size_t width = 0;
    /// @brief Full image height in pixels, in the stored (coded) orientation.
    size_t height = 0;
    /**
     * @brief The orientation from the EXIF Orientation tag.
     *
     * The transform that displays the stored image upright. It is
     * @ref idfxx::image::orientation::identity when the image has no EXIF
     * data, no Orientation tag, or an invalid one.
     */
    image::orientation orientation = image::orientation::identity;
    /// @brief true for a progressive JPEG, false for a sequential one.
    bool progressive = false;
};

/** @cond INTERNAL */
namespace detail {

// What went wrong when a type-erased byte source failed.
struct read_failure {
    std::error_code error;
#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    std::exception_ptr exception;
#endif
};

} // namespace detail
/** @endcond */

/**
 * @headerfile <idfxx/jpeg>
 * @brief Decodes one JPEG image from a byte source.
 *
 * Construction reads the JPEG headers, which provide the image's @ref info;
 * no pixels are decoded yet. The output is full size until
 * @ref scale_to_cover selects a smaller scale. A single decode then delivers
 * the pixels, as @ref idfxx::image::rgb888, either into a view of the output
 * size or row by row to an @ref idfxx::image::sink.
 *
 * Baseline and progressive JPEGs are supported, Huffman or arithmetic coded,
 * with any chroma subsampling, restart intervals, and grayscale, YCbCr or RGB
 * color. Grayscale images are output as RGB with equal channels. CMYK and
 * YCCK images, 12-bit and lossless JPEGs are rejected with
 * @ref errc::not_supported when the headers are read.
 *
 * Decoding is strict: corruption the decoder detects fails the decode with
 * @ref errc::invalid_data instead of being worked around. Stray bytes before a
 * marker, a JFIF version other than 1.x and an unrecognised Adobe color
 * transform are exceptions: they are logged and decoding continues. JPEG has no
 * checksum, so most damage to the compressed image data cannot be detected and
 * decodes to wrong pixels without an error. A stream that ends before the end of the image
 * (including a missing end-of-image marker) fails with @ref errc::truncated.
 * An error returned by the byte source is returned unchanged.
 *
 * The decoder holds a reference to the byte source, which must outlive it.
 * An exception thrown by the source's `try_read` (such as `std::bad_alloc`)
 * ends the decode and propagates from the decoder method that was reading.
 *
 * This type is non-copyable and move-only. A moved-from object must not be
 * used: any operation other than destruction or assignment is undefined
 * behavior.
 *
 * Running out of memory follows the idfxx policy: `std::bad_alloc` is thrown
 * when exceptions are enabled, otherwise the program aborts. How much working
 * memory a decode needs depends on the image's headers: it grows with the
 * image width, and a progressive JPEG needs a buffer for the coefficients of
 * the whole image. Set @ref config::memory_limit to the memory that can be
 * spared, and an image that would need more fails with @ref errc::too_large
 * instead.
 *
 * Decoding uses about 2 KB of the calling task's stack (measured on an
 * ESP32-S3), whatever the image size or output scale, plus whatever the byte
 * source's reads and a sink use.
 *
 * @code
 * idfxx::memory_source src{jpeg_bytes};
 * idfxx::jpeg::decoder dec(src);
 *
 * // Stream rows to a display without buffering the whole image. draw_row()
 * // stands for your own display code.
 * dec.decode([&](idfxx::image::point at, idfxx::image::view<const idfxx::image::rgb888> row)
 *                -> idfxx::result<void> {
 *     return draw_row(at.y, row);
 * });
 * @endcode
 */
class decoder {
public:
    /**
     * @brief Decoder configuration.
     */
    struct config {
        /**
         * @brief Maximum working memory for decoding, in bytes.
         *
         * Bounds the decoder's working memory for any JPEG: its state, input
         * buffer and tables, its row buffers, which grow with the image
         * width, and, for a progressive (or other multi-scan) JPEG, the
         * buffer holding every coefficient of the image, about 128 bytes per
         * 8x8 block per color component at any output scale. The bound is
         * approximate: allocator overhead is not counted. Even a small image
         * needs about 20 KB, most of it the decoder's state and fixed tables,
         * so a lower limit fails every decode.
         *
         * Memory used to read the headers counts, but the limit is checked
         * only from the start of decoding, so construction never fails
         * because of it. From then on, an allocation that would exceed the
         * limit is not made, and the decode fails with @ref errc::too_large
         * instead. `std::nullopt` sets no limit.
         */
        std::optional<size_t> memory_limit = std::nullopt;
    };

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Creates a decoder and reads the JPEG headers.
     *
     * Reads from @p src up to the start of the image data, filling in
     * @ref info. Pixels are decoded later, by @ref decode.
     *
     * @tparam Src The byte source type.
     * @param src The source of the JPEG data. Must outlive the decoder.
     * @param cfg Decoder configuration.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error with @ref errc::invalid_data if the data is not
     *         a JPEG or its headers are corrupt, @ref errc::truncated if the
     *         data ends within the headers, @ref errc::not_supported for CMYK,
     *         YCCK, 12-bit or lossless JPEGs, or the byte source's own error
     *         if reading fails.
     */
    template<byte_source Src>
    explicit decoder(Src& src, config cfg = {})
        : decoder(unwrap(make(src, cfg))) {}
#endif

    /**
     * @brief Creates a decoder and reads the JPEG headers.
     *
     * Reads from @p src up to the start of the image data, filling in
     * @ref info. Pixels are decoded later, by @ref try_decode.
     *
     * @tparam Src The byte source type.
     * @param src The source of the JPEG data. Must outlive the decoder.
     * @param cfg Decoder configuration.
     *
     * @return The decoder, or an error.
     * @retval jpeg::errc::invalid_data The data is not a JPEG, or its headers are corrupt.
     * @retval jpeg::errc::truncated The data ends within the headers.
     * @retval jpeg::errc::not_supported The image is CMYK, YCCK, 12-bit or lossless.
     * @retval other The byte source's own error, unchanged, if reading fails.
     */
    template<byte_source Src>
    [[nodiscard]] static result<decoder> make(Src& src, config cfg = {}) {
        return _make(&src, &_read<Src>, cfg);
    }

    decoder(const decoder&) = delete;
    decoder& operator=(const decoder&) = delete;

    /**
     * @brief Move-constructs a decoder, taking over its state.
     *
     * @param other The decoder to move from. It must not be used afterwards,
     *              other than to be destroyed or assigned to.
     */
    decoder(decoder&& other) noexcept;

    /**
     * @brief Move-assigns a decoder, releasing this one's state.
     *
     * @param other The decoder to move from. It must not be used afterwards,
     *              other than to be destroyed or assigned to.
     * @return A reference to this decoder.
     */
    decoder& operator=(decoder&& other) noexcept;

    /**
     * @brief Destroys the decoder and releases its working memory.
     */
    ~decoder();

    /**
     * @brief Returns the image properties read from the JPEG headers.
     *
     * @return The full image size in the stored orientation, the EXIF
     *         orientation, and whether the image is progressive.
     */
    [[nodiscard]] const jpeg::info& info() const noexcept { return _info; }

    /**
     * @brief Selects the smallest output scale that covers a target size.
     *
     * Chooses the smallest scale M/8, for M from 1 to 8, at which the output
     * is at least @p width wide and at least @p height high, and updates
     * @ref output_width and @ref output_height to match. If even full size
     * does not cover the target, the output is full size: the decoder never
     * upscales. Decoding at a reduced scale is much faster and needs less
     * memory than decoding at full size.
     *
     * The target is in the stored orientation, like @ref info. When the image
     * is displayed with its EXIF orientation applied and that orientation
     * swaps the axes, exchange the target's width and height:
     *
     * @code
     * size_t w = 320;
     * size_t h = 240;
     * if (idfxx::image::swaps_axes(dec.info().orientation)) {
     *     std::swap(w, h);
     * }
     * dec.scale_to_cover(w, h);
     * @endcode
     *
     * May be called more than once; the last call decides.
     *
     * @param width The minimum output width in pixels.
     * @param height The minimum output height in pixels.
     *
     * @pre The image has not been decoded yet.
     */
    void scale_to_cover(size_t width, size_t height);

    /**
     * @brief Returns the width of the decoded output.
     *
     * @return The output width in pixels: the image width until
     *         @ref scale_to_cover selects a smaller scale.
     */
    [[nodiscard]] size_t output_width() const noexcept { return _output_width; }

    /**
     * @brief Returns the height of the decoded output.
     *
     * @return The output height in pixels: the image height until
     *         @ref scale_to_cover selects a smaller scale.
     */
    [[nodiscard]] size_t output_height() const noexcept { return _output_height; }

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Decodes the image into a view.
     *
     * Writes each output row straight into @p dst, honoring its stride.
     * The image can be decoded only once.
     *
     * @param dst The destination, exactly @ref output_width by
     *            @ref output_height pixels. A buffer of that size converts
     *            to a view implicitly.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error with idfxx::errc::invalid_state if the image
     *         has already been decoded, idfxx::errc::invalid_size if @p dst
     *         is not the output size, @ref errc::invalid_data if the data is
     *         corrupt, @ref errc::truncated if it ends before the end of the
     *         image, @ref errc::too_large if decoding needs more memory than
     *         @ref config::memory_limit, @ref errc::not_supported if the
     *         image uses an unsupported feature, or the byte source's own
     *         error if reading fails. On error, the contents of @p dst are
     *         unspecified.
     */
    void decode(image::view<image::rgb888> dst) { unwrap(try_decode(dst)); }
#endif

    /**
     * @brief Decodes the image into a view.
     *
     * Writes each output row straight into @p dst, honoring its stride.
     * The image can be decoded only once.
     *
     * @param dst The destination, exactly @ref output_width by
     *            @ref output_height pixels. A buffer of that size converts
     *            to a view implicitly.
     *
     * @return Success, or an error. On error, the contents of @p dst are
     *         unspecified.
     * @retval idfxx::errc::invalid_state The image has already been decoded.
     * @retval idfxx::errc::invalid_size @p dst is not the output size.
     * @retval jpeg::errc::invalid_data The data is corrupt.
     * @retval jpeg::errc::truncated The data ends before the end of the image.
     * @retval jpeg::errc::too_large Decoding needs more memory than config::memory_limit.
     * @retval jpeg::errc::not_supported The image uses an unsupported feature.
     * @retval other The byte source's own error, unchanged, if reading fails.
     */
    [[nodiscard]] result<void> try_decode(image::view<image::rgb888> dst);

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Decodes the image, delivering it to a sink row by row.
     *
     * Calls @p sink once per output row, from top to bottom, with the point
     * `{0, y}` and a view of the row's @ref output_width pixels, valid only
     * during the call. Only one row of pixels is held at a time. The image
     * can be decoded only once.
     *
     * @tparam S The sink type; see @ref idfxx::image::sink.
     * @param sink The destination for the rows. Returning an error from it
     *             stops the decode, which then fails with that error. An
     *             exception thrown by it ends the decode and propagates from
     *             this call.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error with the error returned by @p sink,
     *         idfxx::errc::invalid_state if the image has already been
     *         decoded, @ref errc::invalid_data if the data is corrupt,
     *         @ref errc::truncated if it ends before the end of the image,
     *         @ref errc::too_large if decoding needs more memory than
     *         @ref config::memory_limit, @ref errc::not_supported if the image
     *         uses an unsupported feature, or the byte source's own error if
     *         reading fails.
     */
    template<image::sink S>
    void decode(S&& sink) {
        unwrap(try_decode(std::forward<S>(sink)));
    }
#endif

    /**
     * @brief Decodes the image, delivering it to a sink row by row.
     *
     * Calls @p sink once per output row, from top to bottom, with the point
     * `{0, y}` and a view of the row's @ref output_width pixels, valid only
     * during the call. Only one row of pixels is held at a time. The image
     * can be decoded only once.
     *
     * @tparam S The sink type; see @ref idfxx::image::sink.
     * @param sink The destination for the rows. Returning an error from it
     *             stops the decode, which then fails with that error. An
     *             exception thrown by it ends the decode and propagates from
     *             this call.
     *
     * @return Success, or an error.
     * @retval idfxx::errc::invalid_state The image has already been decoded.
     * @retval jpeg::errc::invalid_data The data is corrupt.
     * @retval jpeg::errc::truncated The data ends before the end of the image.
     * @retval jpeg::errc::too_large Decoding needs more memory than config::memory_limit.
     * @retval jpeg::errc::not_supported The image uses an unsupported feature.
     * @retval other The error returned by @p sink, or the byte source's own
     *         error if reading fails, unchanged.
     */
    template<image::sink S>
    [[nodiscard]] result<void> try_decode(S&& sink) {
        if (auto r = _begin(); !r) {
            return r;
        }
        auto row = _allocate_row();
        if (!row) {
            return error(row.error());
        }
        for (size_t y = 0; y < _output_height; ++y) {
            if (auto r = _read_row(*row); !r) {
                return r;
            }
            result<void> r =
                std::invoke(sink, image::point{0, y}, image::view<const image::rgb888>(*row, _output_width, 1));
            if (!r) {
                _abort();
                return r;
            }
        }
        return _finish();
    }

private:
    /** @cond INTERNAL */
    struct impl;

    // Reads from a type-erased byte source. Returns false on failure, with the
    // reason stored in `failure`.
    using read_fn =
        bool (*)(void* source, std::span<uint8_t> buf, size_t& count, detail::read_failure& failure) noexcept;

    template<byte_source Src>
    static bool _read(void* source, std::span<uint8_t> buf, size_t& count, detail::read_failure& failure) noexcept {
#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
        try {
#endif
            result<size_t> r = static_cast<Src*>(source)->try_read(buf);
            if (!r) {
                failure.error = r.error();
                return false;
            }
            count = *r;
            return true;
#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
        } catch (...) {
            failure.exception = std::current_exception();
            return false;
        }
#endif
    }

    explicit decoder(std::unique_ptr<impl> state) noexcept;

    [[nodiscard]] static result<decoder> _make(void* source, read_fn read, config cfg);
    [[nodiscard]] result<void> _begin();
    // Allocates one output row from the decoder's working memory. Call after
    // _begin(); the row is freed by _finish() or _abort().
    [[nodiscard]] result<image::rgb888*> _allocate_row();
    [[nodiscard]] result<void> _read_row(image::rgb888* row);
    [[nodiscard]] result<void> _finish();
    void _abort() noexcept;

    std::unique_ptr<impl> _impl;
    jpeg::info _info;
    size_t _output_width = 0;
    size_t _output_height = 0;
    bool _decoded = false;
    /** @endcond */
};

} // namespace idfxx::jpeg

/** @cond INTERNAL */
namespace std {
template<>
struct is_error_code_enum<idfxx::jpeg::errc> : true_type {};
} // namespace std
/** @endcond */

/** @} */ // end of idfxx_jpeg
