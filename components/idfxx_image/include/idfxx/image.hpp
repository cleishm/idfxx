// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

/**
 * @headerfile <idfxx/image>
 * @file image.hpp
 * @brief Pixel images: views, owning buffers, and orientation transforms.
 *
 * @defgroup idfxx_image Image Component
 * @brief In-memory pixel images and the eight EXIF orientations.
 *
 * Provides a small vocabulary for decoded images held in memory:
 *
 * - @ref idfxx::image::rgb888, a 24-bit RGB pixel.
 * - @ref idfxx::image::view, a non-owning, row-major window onto pixels
 *   (optionally strided, so a view can address a rectangle inside a larger
 *   image).
 * - @ref idfxx::image::buffer, an owning, tightly packed image with a
 *   configurable allocator (e.g. to place large images in PSRAM).
 * - @ref idfxx::image::orientation, the eight EXIF orientations, with
 *   functions to invert and compose them, map pixel coordinates through them,
 *   and apply them to an image (@ref idfxx::image::orient,
 *   @ref idfxx::image::oriented).
 * - @ref idfxx::image::sink, the interface through which a producer such as
 *   an image decoder delivers pixels block by block.
 *
 * The image operations cannot fail: functions have documented preconditions
 * instead of error results, and memory allocation follows the allocator's own
 * behavior. Only a @ref idfxx::image::sink reports errors, which it returns to
 * the producer feeding it.
 *
 * @code
 * idfxx::image::buffer<> photo(640, 480);
 * decode_jpeg_into(photo);                        // fills photo's pixels
 *
 * // Display upright according to the photo's EXIF orientation tag.
 * auto o = static_cast<idfxx::image::orientation>(exif_orientation_tag);
 * idfxx::image::buffer<> upright = idfxx::image::oriented(photo, o);
 * @endcode
 * @{
 */

#include <idfxx/error>

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

/**
 * @headerfile <idfxx/image>
 * @brief Pixel images, views, and orientation transforms.
 */
namespace idfxx::image {

/**
 * @headerfile <idfxx/image>
 * @brief A 24-bit RGB pixel with 8 bits per channel.
 *
 * Laid out as three consecutive bytes in red, green, blue order, so an array
 * of `rgb888` has the same memory layout as packed RGB888 image data.
 */
struct rgb888 {
    uint8_t r = 0; ///< Red channel (0-255).
    uint8_t g = 0; ///< Green channel (0-255).
    uint8_t b = 0; ///< Blue channel (0-255).

    /**
     * @brief Compares two pixels channel by channel.
     *
     * @param other The pixel to compare with.
     * @return true if all three channels are equal.
     */
    constexpr bool operator==(const rgb888& other) const = default;
};

static_assert(sizeof(rgb888) == 3, "rgb888 must be exactly three bytes");
static_assert(std::is_trivially_copyable_v<rgb888>, "rgb888 must be trivially copyable");
static_assert(std::is_standard_layout_v<rgb888>, "rgb888 must be standard layout");

/**
 * @headerfile <idfxx/image>
 * @brief A non-owning, row-major view of a two-dimensional pixel array.
 *
 * Pixels of a row are contiguous, and the first pixels of consecutive rows are
 * `stride()` pixels apart. A stride wider than the width lets a view address a
 * rectangular region inside a larger image (see @ref subview).
 *
 * A view is cheap to copy and does not own its pixels: the underlying storage
 * must outlive it. Constness is shallow, as with `std::span`: a `const view<P>`
 * still grants mutable access to the pixels, while `view<const P>` is
 * read-only. A `view<P>` converts implicitly to `view<const P>`.
 *
 * @tparam P The pixel type, optionally const-qualified.
 *
 * @code
 * std::array<idfxx::image::rgb888, 4 * 3> pixels{};
 * idfxx::image::view<idfxx::image::rgb888> v(pixels.data(), 4, 3);
 *
 * v[1, 2] = {255, 0, 0};                  // pixel at x = 1, y = 2
 * auto inner = v.subview(1, 1, 2, 2);     // 2x2 region, stride 4
 * for (auto& px : inner.row(0)) {
 *     px = {0, 0, 255};
 * }
 * @endcode
 */
template<typename P>
class view {
public:
    /// @brief The pixel type as accessed through the view (possibly const).
    using pixel_type = P;
    /// @brief The pixel type without const qualification.
    using value_type = std::remove_const_t<P>;

    /**
     * @brief Constructs an empty view (0x0, no pixels).
     */
    constexpr view() noexcept = default;

    /**
     * @brief Constructs a view of tightly packed pixels.
     *
     * The stride equals the width.
     *
     * @param data Pointer to the first pixel (top-left). Must address at least
     *             `width * height` pixels.
     * @param width Width in pixels.
     * @param height Height in pixels.
     */
    constexpr view(P* data, size_t width, size_t height) noexcept
        : _data(data)
        , _width(width)
        , _height(height)
        , _stride(width) {}

    /**
     * @brief Constructs a view of pixels with a given row stride.
     *
     * @param data Pointer to the first pixel (top-left). Must address at least
     *             `(height - 1) * stride + width` pixels when the view is not
     *             empty.
     * @param width Width in pixels.
     * @param height Height in pixels.
     * @param stride Distance, in pixels, between the starts of consecutive
     *               rows. Precondition: `stride >= width`.
     */
    constexpr view(P* data, size_t width, size_t height, size_t stride) noexcept
        : _data(data)
        , _width(width)
        , _height(height)
        , _stride(stride) {}

    /**
     * @brief Converts a mutable view to a read-only view of the same pixels.
     *
     * @tparam U The source view's (non-const) pixel type; `P` must be `const U`.
     * @param other The view to convert.
     */
    template<typename U>
        requires(std::is_const_v<P> && std::same_as<const U, P> && !std::same_as<U, P>)
    constexpr view(const view<U>& other) noexcept
        : _data(other.data())
        , _width(other.width())
        , _height(other.height())
        , _stride(other.stride()) {}

    /**
     * @brief Returns a pointer to the top-left pixel.
     *
     * @return The pointer the view was constructed with, or nullptr for a
     *         default-constructed view.
     */
    [[nodiscard]] constexpr P* data() const noexcept { return _data; }

    /**
     * @brief Returns the width in pixels.
     *
     * @return The number of pixels in each row.
     */
    [[nodiscard]] constexpr size_t width() const noexcept { return _width; }

    /**
     * @brief Returns the height in pixels.
     *
     * @return The number of rows.
     */
    [[nodiscard]] constexpr size_t height() const noexcept { return _height; }

    /**
     * @brief Returns the row stride in pixels.
     *
     * @return The distance, in pixels, between the starts of consecutive rows.
     */
    [[nodiscard]] constexpr size_t stride() const noexcept { return _stride; }

    /**
     * @brief Checks whether the view contains no pixels.
     *
     * @return true if the width or height is zero.
     */
    [[nodiscard]] constexpr bool empty() const noexcept { return _width == 0 || _height == 0; }

    /**
     * @brief Returns the pixels of one row.
     *
     * @param y Row index. Precondition: `y < height()`.
     * @return A span of `width()` pixels, left to right.
     */
    [[nodiscard]] constexpr std::span<P> row(size_t y) const noexcept { return {_data + y * _stride, _width}; }

    /**
     * @brief Accesses the pixel at column x, row y.
     *
     * @param x Column index. Precondition: `x < width()`.
     * @param y Row index. Precondition: `y < height()`.
     * @return A reference to the pixel.
     */
    [[nodiscard]] constexpr P& operator[](size_t x, size_t y) const noexcept { return _data[y * _stride + x]; }

    /**
     * @brief Writes the pixel at column x, row y, if it is inside the view.
     *
     * Unlike `operator[]`, any coordinates are accepted: a write outside
     * `[0, width()) x [0, height())` is ignored, so it never reaches pixels
     * beyond the view, even within a larger image. This makes a view of
     * non-const pixels an `idfxx::gfx::pixel_surface`, so idfxx_gfx can draw
     * on it, clipped to the view.
     *
     * @param x Column index.
     * @param y Row index.
     * @param pixel The pixel value to write.
     */
    constexpr void
    set_pixel(size_t x, size_t y, value_type pixel) const noexcept(std::is_nothrow_copy_assignable_v<value_type>)
        requires(!std::is_const_v<P>)
    {
        if (x < _width && y < _height) {
            _data[y * _stride + x] = pixel;
        }
    }

    /**
     * @brief Returns a view of a rectangular region of this view.
     *
     * The returned view shares this view's pixels and stride, so writes
     * through it are visible here.
     *
     * @param x Left column of the region.
     * @param y Top row of the region.
     * @param w Width of the region in pixels.
     * @param h Height of the region in pixels.
     * @return A view of the region, with (0, 0) at (x, y) of this view.
     *         Precondition: `x + w <= width()` and `y + h <= height()`.
     */
    [[nodiscard]] constexpr view subview(size_t x, size_t y, size_t w, size_t h) const noexcept {
        return view(_data + y * _stride + x, w, h, _stride);
    }

private:
    P* _data = nullptr;
    size_t _width = 0;
    size_t _height = 0;
    size_t _stride = 0;
};

/**
 * @headerfile <idfxx/image>
 * @brief An owning image of tightly packed pixels.
 *
 * Holds `width * height` pixels in row-major order with no row padding (the
 * stride equals the width). Copying a buffer copies its pixels and moving it
 * transfers them, as for `std::vector`; a moved-from buffer must not be used:
 * any operation other than destruction or assignment is undefined behavior.
 * A buffer converts implicitly
 * to a @ref view of its pixels, so it can be passed anywhere a view is
 * accepted.
 *
 * Allocation failure is handled by the allocator: with the default
 * `std::allocator`, construction throws `std::bad_alloc` (or aborts when
 * exceptions are disabled).
 *
 * @tparam P The pixel type.
 * @tparam Allocator The allocator for the pixel storage, e.g.
 *                   `idfxx::spiram_allocator<P>` from `<idfxx/memory>` to
 *                   place a large image in PSRAM.
 *
 * @code
 * idfxx::image::buffer<> img(320, 240, {0, 0, 0});     // black 320x240
 * img[10, 20] = {255, 255, 255};
 *
 * // PSRAM-backed buffer on ESP-IDF.
 * idfxx::image::buffer<idfxx::image::rgb888, idfxx::spiram_allocator<idfxx::image::rgb888>> big(1024, 768);
 * @endcode
 */
template<typename P = rgb888, typename Allocator = std::allocator<P>>
class buffer {
public:
    /// @brief The pixel type.
    using pixel_type = P;
    /// @brief The pixel type (same as @ref pixel_type).
    using value_type = P;
    /// @brief The allocator type.
    using allocator_type = Allocator;

    /**
     * @brief Constructs an empty (0x0) buffer.
     */
    constexpr buffer() = default;

    /**
     * @brief Constructs a buffer of value-initialized pixels.
     *
     * For @ref rgb888 every pixel is black (0, 0, 0). A width or height of
     * zero produces an empty buffer.
     *
     * @param width Width in pixels.
     * @param height Height in pixels.
     * @param alloc The allocator for the pixel storage.
     */
    constexpr buffer(size_t width, size_t height, const Allocator& alloc = Allocator())
        : _pixels(width * height, alloc)
        , _width(width)
        , _height(height) {}

    /**
     * @brief Constructs a buffer with every pixel set to a given value.
     *
     * @param width Width in pixels.
     * @param height Height in pixels.
     * @param fill The value of every pixel.
     * @param alloc The allocator for the pixel storage.
     */
    constexpr buffer(size_t width, size_t height, const P& fill, const Allocator& alloc = Allocator())
        : _pixels(width * height, fill, alloc)
        , _width(width)
        , _height(height) {}

    /**
     * @brief Constructs a copy of another buffer's pixels.
     *
     * @param other The buffer to copy.
     */
    constexpr buffer(const buffer& other) = default;

    /**
     * @brief Constructs a buffer by taking another buffer's pixels.
     *
     * @param other The buffer to move from. A moved-from buffer must not be
     *              used: any operation other than destruction or assignment is
     *              undefined behavior.
     */
    constexpr buffer(buffer&& other) noexcept
        : _pixels(std::move(other._pixels))
        , _width(std::exchange(other._width, 0))
        , _height(std::exchange(other._height, 0)) {}

    /**
     * @brief Replaces this buffer's pixels with a copy of another's.
     *
     * @param other The buffer to copy.
     * @return A reference to this buffer.
     */
    constexpr buffer& operator=(const buffer& other) = default;

    /**
     * @brief Replaces this buffer's pixels with another buffer's.
     *
     * @param other The buffer to move from. A moved-from buffer must not be
     *              used: any operation other than destruction or assignment is
     *              undefined behavior.
     * @return A reference to this buffer.
     */
    constexpr buffer& operator=(buffer&& other) noexcept(
        std::allocator_traits<Allocator>::propagate_on_container_move_assignment::value ||
        std::allocator_traits<Allocator>::is_always_equal::value
    ) {
        if (this != &other) {
            _pixels = std::move(other._pixels);
            other._pixels.clear();
            _width = std::exchange(other._width, 0);
            _height = std::exchange(other._height, 0);
        }
        return *this;
    }

    /**
     * @brief Destroys the buffer and releases its pixels.
     */
    constexpr ~buffer() = default;

    /**
     * @brief Returns the width in pixels.
     *
     * @return The number of pixels in each row.
     */
    [[nodiscard]] constexpr size_t width() const noexcept { return _width; }

    /**
     * @brief Returns the height in pixels.
     *
     * @return The number of rows.
     */
    [[nodiscard]] constexpr size_t height() const noexcept { return _height; }

    /**
     * @brief Returns the row stride in pixels.
     *
     * @return Always equal to @ref width, as buffers are tightly packed.
     */
    [[nodiscard]] constexpr size_t stride() const noexcept { return _width; }

    /**
     * @brief Checks whether the buffer contains no pixels.
     *
     * @return true if the width or height is zero.
     */
    [[nodiscard]] constexpr bool empty() const noexcept { return _width == 0 || _height == 0; }

    /**
     * @brief Returns a pointer to the top-left pixel.
     *
     * @return A pointer to `width() * height()` contiguous pixels in row-major
     *         order; may be nullptr for an empty buffer.
     */
    [[nodiscard]] constexpr P* data() noexcept { return _pixels.data(); }

    /**
     * @brief Returns a read-only pointer to the top-left pixel.
     *
     * @return A pointer to `width() * height()` contiguous pixels in row-major
     *         order; may be nullptr for an empty buffer.
     */
    [[nodiscard]] constexpr const P* data() const noexcept { return _pixels.data(); }

    /**
     * @brief Returns the pixels of one row.
     *
     * @param y Row index. Precondition: `y < height()`.
     * @return A span of `width()` pixels, left to right.
     */
    [[nodiscard]] constexpr std::span<P> row(size_t y) noexcept { return {_pixels.data() + y * _width, _width}; }

    /**
     * @brief Returns the read-only pixels of one row.
     *
     * @param y Row index. Precondition: `y < height()`.
     * @return A span of `width()` pixels, left to right.
     */
    [[nodiscard]] constexpr std::span<const P> row(size_t y) const noexcept {
        return {_pixels.data() + y * _width, _width};
    }

    /**
     * @brief Accesses the pixel at column x, row y.
     *
     * @param x Column index. Precondition: `x < width()`.
     * @param y Row index. Precondition: `y < height()`.
     * @return A reference to the pixel.
     */
    [[nodiscard]] constexpr P& operator[](size_t x, size_t y) noexcept { return _pixels[y * _width + x]; }

    /**
     * @brief Accesses the pixel at column x, row y (read-only).
     *
     * @param x Column index. Precondition: `x < width()`.
     * @param y Row index. Precondition: `y < height()`.
     * @return A const reference to the pixel.
     */
    [[nodiscard]] constexpr const P& operator[](size_t x, size_t y) const noexcept { return _pixels[y * _width + x]; }

    /**
     * @brief Writes the pixel at column x, row y, if it is inside the buffer.
     *
     * Unlike `operator[]`, any coordinates are accepted: a write outside
     * `[0, width()) x [0, height())` is ignored. This makes a buffer an
     * `idfxx::gfx::pixel_surface`, so idfxx_gfx can draw on it.
     *
     * @param x Column index.
     * @param y Row index.
     * @param pixel The pixel value to write.
     */
    constexpr void set_pixel(size_t x, size_t y, P pixel) noexcept(std::is_nothrow_copy_assignable_v<P>) {
        if (x < _width && y < _height) {
            _pixels[y * _width + x] = pixel;
        }
    }

    /**
     * @brief Returns a view of a rectangular region of the buffer.
     *
     * @param x Left column of the region.
     * @param y Top row of the region.
     * @param w Width of the region in pixels.
     * @param h Height of the region in pixels.
     * @return A view of the region with stride `width()`. Precondition:
     *         `x + w <= width()` and `y + h <= height()`.
     */
    [[nodiscard]] constexpr view<P> subview(size_t x, size_t y, size_t w, size_t h) noexcept {
        return view<P>(*this).subview(x, y, w, h);
    }

    /**
     * @brief Returns a read-only view of a rectangular region of the buffer.
     *
     * @param x Left column of the region.
     * @param y Top row of the region.
     * @param w Width of the region in pixels.
     * @param h Height of the region in pixels.
     * @return A read-only view of the region with stride `width()`.
     *         Precondition: `x + w <= width()` and `y + h <= height()`.
     */
    [[nodiscard]] constexpr view<const P> subview(size_t x, size_t y, size_t w, size_t h) const noexcept {
        return view<const P>(*this).subview(x, y, w, h);
    }

    /**
     * @brief Returns a copy of the allocator.
     *
     * @return The allocator used for the pixel storage.
     */
    [[nodiscard]] constexpr allocator_type get_allocator() const noexcept { return _pixels.get_allocator(); }

    /**
     * @brief Returns a view of all pixels.
     *
     * @return A mutable view of the whole buffer.
     */
    constexpr operator view<P>() noexcept { return view<P>(_pixels.data(), _width, _height); }

    /**
     * @brief Returns a read-only view of all pixels.
     *
     * @return A read-only view of the whole buffer.
     */
    constexpr operator view<const P>() const noexcept { return view<const P>(_pixels.data(), _width, _height); }

private:
    std::vector<P, Allocator> _pixels;
    size_t _width = 0;
    size_t _height = 0;
};

/**
 * @headerfile <idfxx/image>
 * @brief The eight image orientations defined by the EXIF Orientation tag.
 *
 * The enumerator values are those of EXIF tag 0x0112, so a parsed tag value in
 * the range 1-8 can be cast directly to this type. Each value names the
 * transform that makes the stored image display upright; apply it with
 * @ref orient or @ref oriented. Rotations are clockwise.
 *
 * Orientations @ref orientation::transpose, @ref orientation::rotate_90,
 * @ref orientation::transverse, and @ref orientation::rotate_270 exchange width and height (see @ref swaps_axes).
 */
enum class orientation : uint8_t {
    /// @brief EXIF 1: stored upright; displayed as is.
    identity = 1,
    /// @brief EXIF 2: stored mirrored left-to-right; flip horizontally to display.
    flip_horizontal = 2,
    /// @brief EXIF 3: stored upside down; rotate 180 degrees to display.
    rotate_180 = 3,
    /// @brief EXIF 4: stored mirrored top-to-bottom; flip vertically to display.
    flip_vertical = 4,
    /// @brief EXIF 5: stored mirrored across the main diagonal; transpose (swap rows and columns) to display.
    transpose = 5,
    /// @brief EXIF 6: stored rotated 90 degrees counter-clockwise; rotate 90 degrees clockwise to display.
    rotate_90 = 6,
    /// @brief EXIF 7: stored mirrored across the anti-diagonal; transverse (flip across the anti-diagonal) to display.
    transverse = 7,
    /// @brief EXIF 8: stored rotated 90 degrees clockwise; rotate 270 degrees clockwise to display.
    rotate_270 = 8,
};

/** @cond INTERNAL */
namespace detail {

// Each orientation is a signed axis permutation: optionally swap the axes,
// then optionally negate each displayed axis (about the image center). Bit 0
// negates the displayed x axis, bit 1 the displayed y axis, bit 2 swaps axes.
constexpr uint8_t orientation_bits(orientation o) noexcept {
    switch (o) {
    case orientation::identity:
        return 0b000;
    case orientation::flip_horizontal:
        return 0b001;
    case orientation::rotate_180:
        return 0b011;
    case orientation::flip_vertical:
        return 0b010;
    case orientation::transpose:
        return 0b100;
    case orientation::rotate_90:
        return 0b101;
    case orientation::transverse:
        return 0b111;
    case orientation::rotate_270:
        return 0b110;
    }
    return 0b000;
}

constexpr orientation orientation_from_bits(uint8_t bits) noexcept {
    constexpr orientation table[8] = {
        orientation::identity,
        orientation::flip_horizontal,
        orientation::flip_vertical,
        orientation::rotate_180,
        orientation::transpose,
        orientation::rotate_90,
        orientation::rotate_270,
        orientation::transverse,
    };
    return table[bits & 0b111];
}

// Exchanges the two negation bits.
constexpr uint8_t swap_negations(uint8_t bits) noexcept {
    return static_cast<uint8_t>((bits & 0b100) | ((bits & 0b001) << 1) | ((bits & 0b010) >> 1));
}

} // namespace detail
/** @endcond */

/**
 * @headerfile <idfxx/image>
 * @brief Checks whether an orientation exchanges width and height.
 *
 * @param o The orientation.
 * @return true for @ref orientation::transpose, @ref orientation::rotate_90,
 *         @ref orientation::transverse, and @ref orientation::rotate_270.
 */
[[nodiscard]] constexpr bool swaps_axes(orientation o) noexcept {
    return (detail::orientation_bits(o) & 0b100) != 0;
}

/**
 * @headerfile <idfxx/image>
 * @brief Returns the orientation that undoes another.
 *
 * Applying `o` and then `inverse(o)` restores the original image.
 *
 * @param o The orientation to invert.
 * @return @ref orientation::rotate_270 for @ref orientation::rotate_90 and
 *         vice versa; every other orientation is its own inverse.
 */
[[nodiscard]] constexpr orientation inverse(orientation o) noexcept {
    const uint8_t bits = detail::orientation_bits(o);
    return detail::orientation_from_bits((bits & 0b100) != 0 ? detail::swap_negations(bits) : bits);
}

/**
 * @headerfile <idfxx/image>
 * @brief Combines two orientations into one.
 *
 * Useful for fusing transforms, e.g. an image's EXIF orientation with the
 * rotation of the panel it is shown on, so the image is transformed once.
 *
 * @param first The orientation applied first.
 * @param second The orientation applied to the result of `first`.
 * @return The single orientation equivalent to applying `first` and then
 *         `second`.
 *
 * @code
 * using idfxx::image::orientation;
 * static_assert(compose(orientation::rotate_90, orientation::rotate_90) == orientation::rotate_180);
 * static_assert(compose(orientation::flip_horizontal, orientation::rotate_90) == orientation::transverse);
 * @endcode
 */
[[nodiscard]] constexpr orientation compose(orientation first, orientation second) noexcept {
    const uint8_t a = detail::orientation_bits(first);
    const uint8_t b = detail::orientation_bits(second);
    // second is (negate N2) after (swap S2); moving swap S2 past first's
    // negations N1 exchanges N1's axes, giving N2 * S2(N1) * S2 * S1.
    const uint8_t n1 = (b & 0b100) != 0 ? detail::swap_negations(a) : a;
    return detail::orientation_from_bits(static_cast<uint8_t>(((a ^ b) & 0b100) | ((n1 ^ b) & 0b011)));
}

/**
 * @headerfile <idfxx/image>
 * @brief A pixel coordinate.
 */
struct point {
    size_t x; ///< Column, counted from the left edge.
    size_t y; ///< Row, counted from the top edge.

    /**
     * @brief Compares two points.
     *
     * @param other The point to compare with.
     * @return true if both coordinates are equal.
     */
    constexpr bool operator==(const point& other) const = default;
};

/**
 * @headerfile <idfxx/image>
 * @brief A destination that receives decoded pixels as blocks.
 *
 * A producer, such as an image decoder, delivers an image to a sink in
 * rectangular blocks. Each call passes the block's top-left position in the
 * full image and a read-only view of the block's pixels, which is valid only
 * for the duration of the call. Blocks arrive in the producer's order (e.g.
 * top to bottom).
 *
 * The sink returns success to receive the next block, or an error to stop the
 * producer, which then returns that same error to its caller.
 *
 * A type models `sink<S, P>` when an lvalue `S&` is invocable with
 * `(point, view<const P>)` and returns `idfxx::result<void>`. Lambdas,
 * function objects and function pointers all qualify.
 *
 * @tparam S The sink type.
 * @tparam P The pixel type the producer delivers.
 *
 * @code
 * idfxx::image::buffer<> screen(320, 240);
 * auto to_screen = [&](idfxx::image::point at, idfxx::image::view<const idfxx::image::rgb888> block)
 *     -> idfxx::result<void> {
 *     if (at.x + block.width() > screen.width() || at.y + block.height() > screen.height()) {
 *         return idfxx::error(idfxx::errc::invalid_size); // stops the producer
 *     }
 *     for (size_t y = 0; y < block.height(); ++y) {
 *         std::ranges::copy(block.row(y), screen.row(at.y + y).begin() + at.x);
 *     }
 *     return {};
 * };
 * static_assert(idfxx::image::sink<decltype(to_screen)>);
 * @endcode
 */
template<typename S, typename P = rgb888>
concept sink = std::invocable<S&, point, view<const P>> &&
    std::same_as<std::invoke_result_t<S&, point, view<const P>>, result<void>>;

/**
 * @headerfile <idfxx/image>
 * @brief Maps a pixel of an oriented image back to the source pixel it shows.
 *
 * For a source image of `src_width` x `src_height` pixels displayed with
 * orientation `o` applied, returns the source coordinate whose pixel appears
 * at `p` in the displayed image. The displayed image is `src_width` x
 * `src_height`, or `src_height` x `src_width` when @ref swaps_axes is true for `o`.
 *
 * @param o The orientation applied to the source image.
 * @param p A pixel of the displayed image. Precondition: within the displayed
 *          image's bounds.
 * @param src_width Width of the source image in pixels.
 * @param src_height Height of the source image in pixels.
 * @return The corresponding source pixel coordinate.
 *
 * @code
 * using idfxx::image::orientation;
 * // Rotating 90 degrees clockwise moves the source's bottom-left pixel to the
 * // displayed top-left.
 * auto s = idfxx::image::source_point(orientation::rotate_90, {0, 0}, 640, 480);
 * // s == {0, 479}
 * @endcode
 */
[[nodiscard]] constexpr point source_point(orientation o, point p, size_t src_width, size_t src_height) noexcept {
    switch (o) {
    case orientation::identity:
        break;
    case orientation::flip_horizontal:
        return {src_width - 1 - p.x, p.y};
    case orientation::rotate_180:
        return {src_width - 1 - p.x, src_height - 1 - p.y};
    case orientation::flip_vertical:
        return {p.x, src_height - 1 - p.y};
    case orientation::transpose:
        return {p.y, p.x};
    case orientation::rotate_90:
        return {p.y, src_height - 1 - p.x};
    case orientation::transverse:
        return {src_width - 1 - p.y, src_height - 1 - p.x};
    case orientation::rotate_270:
        return {src_width - 1 - p.y, p.x};
    }
    return p;
}

/** @cond INTERNAL */
namespace detail {

template<typename P>
constexpr view<P> as_view(view<P> v) noexcept {
    return v;
}

template<typename P, typename A>
constexpr view<P> as_view(buffer<P, A>& b) noexcept {
    return b;
}

template<typename P, typename A>
constexpr view<const P> as_view(const buffer<P, A>& b) noexcept {
    return b;
}

// The view an argument of type T (with value category) is accessed through.
template<typename T>
using view_type_t = decltype(detail::as_view(std::declval<T>()));

// T is a buffer or view (of any constness).
template<typename T>
concept image_like = requires { typename view_type_t<T>; };

// T is a buffer or view whose pixels can be written.
template<typename T>
concept writable_image = image_like<T> && !std::is_const_v<typename view_type_t<T>::pixel_type>;

template<typename T>
using image_value_t = typename view_type_t<T>::value_type;

// The allocator a derived image uses by default: a buffer's own, else std::allocator.
template<typename P>
constexpr std::allocator<std::remove_const_t<P>> default_allocator(const view<P>&) noexcept {
    return {};
}

template<typename P, typename A>
constexpr A default_allocator(const buffer<P, A>& b) noexcept {
    return b.get_allocator();
}

template<typename T>
using default_allocator_t = decltype(detail::default_allocator(std::declval<T>()));

} // namespace detail
/** @endcond */

/**
 * @headerfile <idfxx/image>
 * @brief Copies an image into another with an orientation applied.
 *
 * Writes every pixel of `dst` from the pixel of `src` that appears there when
 * `o` is applied (see @ref source_point). Both arguments may be any
 * @ref buffer or @ref view, const or not, and are passed directly:
 *
 * @code
 * idfxx::image::buffer<> photo(640, 480);
 * idfxx::image::buffer<> upright(480, 640);
 * idfxx::image::orient(photo, upright, idfxx::image::orientation::rotate_90);
 *
 * // Or into a region of a larger image.
 * idfxx::image::buffer<> screen(800, 800);
 * idfxx::image::orient(photo, screen.subview(160, 80, 480, 640), idfxx::image::orientation::rotate_90);
 * @endcode
 *
 * @tparam Src A @ref buffer or @ref view type.
 * @tparam Dst A non-const @ref buffer, or a @ref view of non-const pixels,
 *             with the same pixel type as `Src`.
 * @param src The source image.
 * @param dst The destination image. Preconditions: its dimensions are those
 *            of `src`, exchanged when @ref swaps_axes is true for `o`; and it does not
 *            overlap `src`.
 * @param o The orientation to apply.
 */
template<typename Src, typename Dst>
    requires detail::image_like<const Src&> && detail::writable_image<Dst&&> &&
    std::same_as<detail::image_value_t<const Src&>, detail::image_value_t<Dst&&>>
constexpr void orient(const Src& src, Dst&& dst, orientation o) noexcept(
    std::is_nothrow_copy_assignable_v<detail::image_value_t<Dst&&>>
) {
    const view<const detail::image_value_t<const Src&>> s = detail::as_view(src);
    const auto d = detail::as_view(std::forward<Dst>(dst));
    for (size_t y = 0; y < d.height(); ++y) {
        for (size_t x = 0; x < d.width(); ++x) {
            const point sp = source_point(o, {x, y}, s.width(), s.height());
            d[x, y] = s[sp.x, sp.y];
        }
    }
}

/**
 * @headerfile <idfxx/image>
 * @brief Returns a new image with an orientation applied, using a given allocator.
 *
 * The result is `src`'s width and height, exchanged when @ref swaps_axes is true for `o`.
 * `src` may be any @ref buffer or @ref view, const or not.
 *
 * @code
 * auto o = static_cast<idfxx::image::orientation>(exif_orientation_tag);
 * auto upright = idfxx::image::oriented(photo, o, idfxx::spiram_allocator<idfxx::image::rgb888>());
 * @endcode
 *
 * @tparam Src A @ref buffer or @ref view type.
 * @tparam Allocator The allocator type for the result's pixel storage; its
 *                   value type must be the pixel type of `Src`.
 * @param src The source image.
 * @param o The orientation to apply.
 * @param alloc The allocator for the result's pixel storage.
 * @return A buffer holding the oriented image, allocated with `alloc`.
 */
template<typename Src, typename Allocator>
    requires detail::image_like<const Src&> &&
    std::same_as<typename std::allocator_traits<Allocator>::value_type, detail::image_value_t<const Src&>>
[[nodiscard]] constexpr buffer<detail::image_value_t<const Src&>, Allocator>
oriented(const Src& src, orientation o, const Allocator& alloc) {
    const view<const detail::image_value_t<const Src&>> s = detail::as_view(src);
    const bool swap = swaps_axes(o);
    buffer<detail::image_value_t<const Src&>, Allocator> result(
        swap ? s.height() : s.width(), swap ? s.width() : s.height(), alloc
    );
    orient(s, result, o);
    return result;
}

/**
 * @headerfile <idfxx/image>
 * @brief Returns a new image with an orientation applied.
 *
 * The result is `src`'s width and height, exchanged when @ref swaps_axes is true for `o`.
 * `src` may be any @ref buffer or @ref view, const or not.
 *
 * When `src` is a @ref buffer, the result uses the same allocator type and a
 * copy of `src`'s allocator, so e.g. an image in PSRAM is oriented into PSRAM.
 * When `src` is a @ref view, the result uses `std::allocator`. Pass an
 * allocator explicitly to choose otherwise.
 *
 * @code
 * auto o = static_cast<idfxx::image::orientation>(exif_orientation_tag);
 * idfxx::image::buffer<> upright = idfxx::image::oriented(photo, o);
 * @endcode
 *
 * @tparam Src A @ref buffer or @ref view type.
 * @param src The source image.
 * @param o The orientation to apply.
 * @return A buffer holding the oriented image.
 */
template<typename Src>
    requires detail::image_like<const Src&>
[[nodiscard]] constexpr buffer<detail::image_value_t<const Src&>, detail::default_allocator_t<const Src&>>
oriented(const Src& src, orientation o) {
    return oriented(src, o, detail::default_allocator(src));
}

} // namespace idfxx::image

/** @} */ // end of idfxx_image
