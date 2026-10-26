// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Unit tests for idfxx_image
// Uses ESP-IDF Unity test framework with compile-time static_asserts

#include <idfxx/gfx>
#include <idfxx/image>
#include <unity.h>

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <span>
#include <type_traits>
#include <utility>

using namespace idfxx::image;

// =============================================================================
// Compile-time tests (static_assert)
// These verify correctness at compile time - if this file compiles, they pass.
// =============================================================================

// rgb888 is three packed bytes.
static_assert(sizeof(rgb888) == 3);
static_assert(alignof(rgb888) == 1);
static_assert(std::is_trivially_copyable_v<rgb888>);
static_assert(std::is_standard_layout_v<rgb888>);
static_assert(rgb888{} == rgb888{0, 0, 0});

// view types and conversions.
static_assert(std::same_as<view<rgb888>::pixel_type, rgb888>);
static_assert(std::same_as<view<rgb888>::value_type, rgb888>);
static_assert(std::same_as<view<const rgb888>::pixel_type, const rgb888>);
static_assert(std::same_as<view<const rgb888>::value_type, rgb888>);
static_assert(std::is_trivially_copyable_v<view<rgb888>>);
static_assert(std::is_convertible_v<view<rgb888>, view<const rgb888>>);
static_assert(!std::is_convertible_v<view<const rgb888>, view<rgb888>>);
static_assert(!std::is_convertible_v<view<rgb888>, view<const uint8_t>>);

// buffer defaults and conversions.
static_assert(std::same_as<buffer<>::pixel_type, rgb888>);
static_assert(std::same_as<buffer<>::allocator_type, std::allocator<rgb888>>);
static_assert(std::is_copy_constructible_v<buffer<>>);
static_assert(std::is_nothrow_move_constructible_v<buffer<>>);
static_assert(std::is_nothrow_move_assignable_v<buffer<>>);
static_assert(std::is_convertible_v<buffer<>&, view<rgb888>>);
static_assert(std::is_convertible_v<buffer<>&, view<const rgb888>>);
static_assert(std::is_convertible_v<const buffer<>&, view<const rgb888>>);
static_assert(!std::is_convertible_v<const buffer<>&, view<rgb888>>);

// Buffers and views of mutable pixels are gfx drawing targets; read-only views are not.
static_assert(idfxx::gfx::pixel_surface<view<rgb888>>);
static_assert(idfxx::gfx::pixel_surface<buffer<>>);
static_assert(!idfxx::gfx::pixel_surface<view<const rgb888>>);

// orientation values match the EXIF Orientation tag.
static_assert(static_cast<uint8_t>(orientation::identity) == 1);
static_assert(static_cast<uint8_t>(orientation::flip_horizontal) == 2);
static_assert(static_cast<uint8_t>(orientation::rotate_180) == 3);
static_assert(static_cast<uint8_t>(orientation::flip_vertical) == 4);
static_assert(static_cast<uint8_t>(orientation::transpose) == 5);
static_assert(static_cast<uint8_t>(orientation::rotate_90) == 6);
static_assert(static_cast<uint8_t>(orientation::transverse) == 7);
static_assert(static_cast<uint8_t>(orientation::rotate_270) == 8);

// Orientation algebra is usable at compile time.
static_assert(!swaps_axes(orientation::identity));
static_assert(!swaps_axes(orientation::flip_horizontal));
static_assert(!swaps_axes(orientation::rotate_180));
static_assert(!swaps_axes(orientation::flip_vertical));
static_assert(swaps_axes(orientation::transpose));
static_assert(swaps_axes(orientation::rotate_90));
static_assert(swaps_axes(orientation::transverse));
static_assert(swaps_axes(orientation::rotate_270));
static_assert(inverse(orientation::rotate_90) == orientation::rotate_270);
static_assert(inverse(orientation::rotate_270) == orientation::rotate_90);
static_assert(inverse(orientation::transverse) == orientation::transverse);
static_assert(compose(orientation::rotate_90, orientation::rotate_90) == orientation::rotate_180);
static_assert(compose(orientation::rotate_90, orientation::rotate_180) == orientation::rotate_270);
static_assert(compose(orientation::flip_horizontal, orientation::rotate_90) == orientation::transverse);
static_assert(compose(orientation::rotate_90, orientation::flip_horizontal) == orientation::transpose);
static_assert(source_point(orientation::rotate_90, {0, 0}, 640, 480) == point{0, 479});

// Views work in constant expressions.
static_assert([] {
    std::array<int, 6> pixels{1, 2, 3, 4, 5, 6};
    view<int> v(pixels.data(), 3, 2);
    v[2, 1] = 60;
    return v.subview(1, 0, 2, 2)[1, 1] == 60 && v.row(1)[0] == 4;
}());

namespace {

template<typename S, typename D>
concept can_orient = requires(S&& s, D&& d) {
    orient(std::forward<S>(s), std::forward<D>(d), orientation::identity);
};

template<typename S>
concept can_orient_copy = requires(S&& s) { oriented(std::forward<S>(s), orientation::identity); };

} // namespace

// orient accepts any buffer or view as the source...
static_assert(can_orient<buffer<>&, buffer<>&>);
static_assert(can_orient<const buffer<>&, buffer<>&>);
static_assert(can_orient<buffer<>, buffer<>&>);
static_assert(can_orient<view<rgb888>, buffer<>&>);
static_assert(can_orient<view<const rgb888>, buffer<>&>);
static_assert(can_orient<const view<const rgb888>&, buffer<>&>);
// ...and any writable buffer or view as the destination.
static_assert(can_orient<buffer<>&, view<rgb888>>);
static_assert(can_orient<buffer<>&, view<rgb888>&>);
static_assert(can_orient<buffer<>&, const view<rgb888>&>);
// Read-only and temporary-buffer destinations are rejected.
static_assert(!can_orient<buffer<>&, const buffer<>&>);
static_assert(!can_orient<buffer<>&, buffer<>>);
static_assert(!can_orient<buffer<>&, view<const rgb888>>);
// Pixel types must match, and non-images are rejected.
static_assert(!can_orient<buffer<>&, buffer<int>&>);
static_assert(!can_orient<view<const int>, view<rgb888>>);
static_assert(!can_orient<int, buffer<>&>);
static_assert(!can_orient<buffer<>&, int&>);

static_assert(can_orient_copy<buffer<>&>);
static_assert(can_orient_copy<const buffer<>&>);
static_assert(can_orient_copy<view<rgb888>>);
static_assert(can_orient_copy<view<const rgb888>>);
static_assert(!can_orient_copy<int>);
static_assert(std::same_as<decltype(oriented(std::declval<view<const int>>(), orientation::identity)), buffer<int>>);

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

// A distinct pixel for each label, with every channel different.
constexpr rgb888 px(uint8_t n) {
    return {n, static_cast<uint8_t>(n + 100), static_cast<uint8_t>(n + 200)};
}

// Builds a width x height image from row-major pixel labels.
buffer<> make_image(size_t width, size_t height, std::initializer_list<uint8_t> labels) {
    buffer<> img(width, height);
    size_t i = 0;
    for (uint8_t n : labels) {
        img[i % width, i / width] = px(n);
        ++i;
    }
    return img;
}

// The 3x2 test image:
//   1 2 3
//   4 5 6
buffer<> sample() {
    return make_image(3, 2, {1, 2, 3, 4, 5, 6});
}

// An image whose pixels encode their own coordinates.
buffer<> coordinate_image(size_t width, size_t height) {
    buffer<> img(width, height);
    for (size_t y = 0; y < height; ++y) {
        for (size_t x = 0; x < width; ++x) {
            img[x, y] = {static_cast<uint8_t>(x), static_cast<uint8_t>(y), 0};
        }
    }
    return img;
}

bool same_pixels(view<const rgb888> a, view<const rgb888> b) {
    if (a.width() != b.width() || a.height() != b.height()) {
        return false;
    }
    for (size_t y = 0; y < a.height(); ++y) {
        for (size_t x = 0; x < a.width(); ++x) {
            if (a[x, y] != b[x, y]) {
                return false;
            }
        }
    }
    return true;
}

// A stateful allocator counting allocations and deallocations.
struct alloc_counts {
    size_t allocations = 0;
    size_t deallocations = 0;
};

template<typename T>
struct counting_allocator {
    using value_type = T;

    alloc_counts* counts;

    explicit counting_allocator(alloc_counts* c) noexcept
        : counts(c) {}

    template<typename U>
    counting_allocator(const counting_allocator<U>& other) noexcept
        : counts(other.counts) {}

    T* allocate(size_t n) {
        ++counts->allocations;
        return std::allocator<T>{}.allocate(n);
    }

    void deallocate(T* p, size_t n) noexcept {
        ++counts->deallocations;
        std::allocator<T>{}.deallocate(p, n);
    }

    template<typename U>
    bool operator==(const counting_allocator<U>& other) const noexcept {
        return counts == other.counts;
    }
};

} // namespace

// =============================================================================
// Runtime tests: rgb888
// =============================================================================

TEST_CASE("image rgb888 defaults to black and compares by channel", "[idfxx][image]") {
    rgb888 black;
    TEST_ASSERT_EQUAL(0, black.r);
    TEST_ASSERT_EQUAL(0, black.g);
    TEST_ASSERT_EQUAL(0, black.b);

    rgb888 c{10, 20, 30};
    TEST_ASSERT_TRUE(c == (rgb888{10, 20, 30}));
    TEST_ASSERT_FALSE(c == (rgb888{11, 20, 30}));
    TEST_ASSERT_FALSE(c == (rgb888{10, 21, 30}));
    TEST_ASSERT_FALSE(c == (rgb888{10, 20, 31}));

    // An array of pixels is packed R, G, B bytes.
    std::array<rgb888, 2> pixels{rgb888{1, 2, 3}, rgb888{4, 5, 6}};
    const auto* bytes = reinterpret_cast<const uint8_t*>(pixels.data());
    for (uint8_t i = 0; i < 6; ++i) {
        TEST_ASSERT_EQUAL(i + 1, bytes[i]);
    }
}

// =============================================================================
// Runtime tests: view
// =============================================================================

TEST_CASE("image view default constructs empty", "[idfxx][image]") {
    view<rgb888> v;
    TEST_ASSERT_NULL(v.data());
    TEST_ASSERT_EQUAL(0, v.width());
    TEST_ASSERT_EQUAL(0, v.height());
    TEST_ASSERT_EQUAL(0, v.stride());
    TEST_ASSERT_TRUE(v.empty());
}

TEST_CASE("image view addresses packed pixels", "[idfxx][image]") {
    std::array<int, 12> pixels{};
    for (size_t i = 0; i < pixels.size(); ++i) {
        pixels[i] = static_cast<int>(i);
    }

    view<int> v(pixels.data(), 4, 3);
    TEST_ASSERT_EQUAL_PTR(pixels.data(), v.data());
    TEST_ASSERT_EQUAL(4, v.width());
    TEST_ASSERT_EQUAL(3, v.height());
    TEST_ASSERT_EQUAL(4, v.stride());
    TEST_ASSERT_FALSE(v.empty());

    TEST_ASSERT_EQUAL(0, (v[0, 0]));
    TEST_ASSERT_EQUAL(3, (v[3, 0]));
    TEST_ASSERT_EQUAL(4, (v[0, 1]));
    TEST_ASSERT_EQUAL(11, (v[3, 2]));

    std::span<int> r = v.row(1);
    TEST_ASSERT_EQUAL(4, r.size());
    TEST_ASSERT_EQUAL_PTR(pixels.data() + 4, r.data());
    TEST_ASSERT_EQUAL(7, r[3]);

    // Writes go through to the storage.
    v[2, 1] = 100;
    TEST_ASSERT_EQUAL(100, pixels[6]);

    // Zero width or height is empty.
    TEST_ASSERT_TRUE((view<int>(pixels.data(), 0, 3).empty()));
    TEST_ASSERT_TRUE((view<int>(pixels.data(), 4, 0).empty()));
}

TEST_CASE("image view honors an explicit stride", "[idfxx][image]") {
    // A 3x2 image stored with rows 5 pixels apart.
    std::array<int, 8> pixels{0, 1, 2, -1, -1, 10, 11, 12};
    view<int> v(pixels.data(), 3, 2, 5);
    TEST_ASSERT_EQUAL(3, v.width());
    TEST_ASSERT_EQUAL(2, v.height());
    TEST_ASSERT_EQUAL(5, v.stride());

    TEST_ASSERT_EQUAL(2, (v[2, 0]));
    TEST_ASSERT_EQUAL(10, (v[0, 1]));
    TEST_ASSERT_EQUAL(12, (v[2, 1]));
    TEST_ASSERT_EQUAL_PTR(pixels.data() + 5, v.row(1).data());
    TEST_ASSERT_EQUAL(3, v.row(1).size());
}

TEST_CASE("image view subview addresses a region", "[idfxx][image]") {
    // 5x4 image, pixel value = y * 10 + x.
    std::array<int, 20> pixels{};
    for (size_t y = 0; y < 4; ++y) {
        for (size_t x = 0; x < 5; ++x) {
            pixels[y * 5 + x] = static_cast<int>(y * 10 + x);
        }
    }
    view<int> v(pixels.data(), 5, 4);

    view<int> sub = v.subview(1, 1, 3, 2);
    TEST_ASSERT_EQUAL(3, sub.width());
    TEST_ASSERT_EQUAL(2, sub.height());
    TEST_ASSERT_EQUAL(5, sub.stride());
    TEST_ASSERT_EQUAL_PTR(pixels.data() + 6, sub.data());
    TEST_ASSERT_EQUAL(11, (sub[0, 0]));
    TEST_ASSERT_EQUAL(23, (sub[2, 1]));
    TEST_ASSERT_EQUAL(3, sub.row(1).size());
    TEST_ASSERT_EQUAL(21, sub.row(1)[0]);

    // A subview of a subview is relative to the inner region.
    view<int> subsub = sub.subview(1, 1, 2, 1);
    TEST_ASSERT_EQUAL(2, subsub.width());
    TEST_ASSERT_EQUAL(1, subsub.height());
    TEST_ASSERT_EQUAL(5, subsub.stride());
    TEST_ASSERT_EQUAL(22, (subsub[0, 0]));
    TEST_ASSERT_EQUAL(23, (subsub[1, 0]));

    // Writes through a subview reach the full image.
    subsub[1, 0] = -1;
    TEST_ASSERT_EQUAL(-1, (v[3, 2]));

    // The full region is the same view.
    view<int> whole = v.subview(0, 0, 5, 4);
    TEST_ASSERT_EQUAL_PTR(v.data(), whole.data());
    TEST_ASSERT_EQUAL(5, whole.width());
    TEST_ASSERT_EQUAL(4, whole.height());
}

TEST_CASE("image view set_pixel writes only inside the view", "[idfxx][image]") {
    // A 2x2 subview of a 4x3 image.
    std::array<int, 12> pixels{};
    view<int> sub = view<int>(pixels.data(), 4, 3).subview(1, 1, 2, 2);

    sub.set_pixel(1, 1, 7);
    TEST_ASSERT_EQUAL(7, pixels[2 * 4 + 2]);

    // Writes past the subview's edges are ignored, though the image has pixels there.
    sub.set_pixel(2, 0, 9);
    sub.set_pixel(0, 2, 9);
    sub.set_pixel(SIZE_MAX, 0, 9);
    for (size_t i = 0; i < pixels.size(); ++i) {
        TEST_ASSERT_EQUAL(i == 2 * 4 + 2 ? 7 : 0, pixels[i]);
    }
}

TEST_CASE("image view converts to a read-only view", "[idfxx][image]") {
    std::array<int, 6> pixels{1, 2, 3, 4, 5, 6};
    view<int> v(pixels.data(), 2, 2, 3);

    view<const int> cv = v;
    TEST_ASSERT_EQUAL_PTR(v.data(), cv.data());
    TEST_ASSERT_EQUAL(2, cv.width());
    TEST_ASSERT_EQUAL(2, cv.height());
    TEST_ASSERT_EQUAL(3, cv.stride());
    TEST_ASSERT_EQUAL(5, (cv[1, 1]));

    std::span<const int> r = cv.row(1);
    TEST_ASSERT_EQUAL(4, r[0]);

    // Read-only subviews stay read-only.
    view<const int> csub = cv.subview(1, 0, 1, 2);
    static_assert(std::same_as<decltype(csub[0, 0]), const int&>);
    TEST_ASSERT_EQUAL(2, (csub[0, 0]));
    TEST_ASSERT_EQUAL(5, (csub[0, 1]));
}

// =============================================================================
// Runtime tests: buffer
// =============================================================================

TEST_CASE("image buffer default constructs empty", "[idfxx][image]") {
    buffer<> b;
    TEST_ASSERT_EQUAL(0, b.width());
    TEST_ASSERT_EQUAL(0, b.height());
    TEST_ASSERT_EQUAL(0, b.stride());
    TEST_ASSERT_TRUE(b.empty());

    view<const rgb888> v = b;
    TEST_ASSERT_TRUE(v.empty());

    // 0x0 is valid to construct explicitly too.
    buffer<> z(0, 0);
    TEST_ASSERT_TRUE(z.empty());
    buffer<> w(5, 0);
    TEST_ASSERT_TRUE(w.empty());
    TEST_ASSERT_EQUAL(5, w.width());
}

TEST_CASE("image buffer value-initializes or fills its pixels", "[idfxx][image]") {
    buffer<> b(4, 3);
    TEST_ASSERT_EQUAL(4, b.width());
    TEST_ASSERT_EQUAL(3, b.height());
    TEST_ASSERT_EQUAL(4, b.stride());
    TEST_ASSERT_FALSE(b.empty());
    TEST_ASSERT_NOT_NULL(b.data());
    for (size_t i = 0; i < 12; ++i) {
        TEST_ASSERT_TRUE(b.data()[i] == rgb888{});
    }

    const rgb888 red{255, 0, 0};
    buffer<> f(4, 3, red);
    for (size_t y = 0; y < f.height(); ++y) {
        for (size_t x = 0; x < f.width(); ++x) {
            TEST_ASSERT_TRUE((f[x, y] == red));
        }
    }

    buffer<int> n(2, 2, 7);
    TEST_ASSERT_EQUAL(7, (n[1, 1]));
}

TEST_CASE("image buffer accesses pixels row-major", "[idfxx][image]") {
    buffer<int> b(3, 2);
    for (size_t y = 0; y < 2; ++y) {
        for (size_t x = 0; x < 3; ++x) {
            b[x, y] = static_cast<int>(y * 10 + x);
        }
    }
    TEST_ASSERT_EQUAL(12, b.data()[5]);
    TEST_ASSERT_EQUAL(10, b.data()[3]);

    std::span<int> r = b.row(1);
    TEST_ASSERT_EQUAL(3, r.size());
    TEST_ASSERT_EQUAL_PTR(b.data() + 3, r.data());
    r[1] = 99;
    TEST_ASSERT_EQUAL(99, (b[1, 1]));

    const buffer<int>& cb = b;
    static_assert(std::same_as<decltype(cb.data()), const int*>);
    static_assert(std::same_as<decltype(cb[0, 0]), const int&>);
    static_assert(std::same_as<decltype(cb.row(0)), std::span<const int>>);
    TEST_ASSERT_EQUAL(99, (cb[1, 1]));
    TEST_ASSERT_EQUAL(2, cb.row(0)[2]);

    view<int> sub = b.subview(1, 0, 2, 2);
    TEST_ASSERT_EQUAL(2, sub.width());
    TEST_ASSERT_EQUAL(2, sub.height());
    TEST_ASSERT_EQUAL(3, sub.stride());
    TEST_ASSERT_EQUAL(2, (sub[1, 0]));
    sub[0, 0] = -1;
    TEST_ASSERT_EQUAL(-1, (b[1, 0]));

    view<const int> csub = cb.subview(0, 1, 3, 1);
    TEST_ASSERT_EQUAL(3, csub.stride());
    TEST_ASSERT_EQUAL(12, (csub[2, 0]));
}

TEST_CASE("image buffer set_pixel writes only inside the buffer", "[idfxx][image]") {
    buffer<> img = sample();
    img.set_pixel(2, 1, px(9));
    img.set_pixel(3, 0, px(9));
    img.set_pixel(0, 2, px(9));
    img.set_pixel(SIZE_MAX, SIZE_MAX, px(9));

    const uint8_t expected[] = {1, 2, 3, 4, 5, 9};
    for (size_t i = 0; i < 6; ++i) {
        TEST_ASSERT_TRUE((img[i % 3, i / 3]) == px(expected[i]));
    }
}

TEST_CASE("image buffers and views can be drawn on with gfx", "[idfxx][image]") {
    const rgb888 red{255, 0, 0};
    const rgb888 blue{0, 0, 255};

    // A canvas over a whole buffer.
    buffer<> img(4, 3);
    idfxx::gfx::canvas canvas(img);
    canvas.fill(blue);
    canvas.draw_hline(0, 2, 4, red);
    for (size_t y = 0; y < 3; ++y) {
        for (size_t x = 0; x < 4; ++x) {
            TEST_ASSERT_TRUE((img[x, y]) == (y == 2 ? red : blue));
        }
    }

    // Drawing on a subview is clipped to it.
    view<rgb888> sub = img.subview(1, 0, 2, 2);
    idfxx::gfx::fill_rect(sub, 0, 0, 10, 10, red);
    for (size_t y = 0; y < 3; ++y) {
        for (size_t x = 0; x < 4; ++x) {
            const bool inside = (x == 1 || x == 2) && y < 2;
            TEST_ASSERT_TRUE((img[x, y]) == (inside || y == 2 ? red : blue));
        }
    }
}

TEST_CASE("image buffer converts to views of its pixels", "[idfxx][image]") {
    buffer<int> b(3, 2, 5);

    view<int> v = b;
    TEST_ASSERT_EQUAL_PTR(b.data(), v.data());
    TEST_ASSERT_EQUAL(3, v.width());
    TEST_ASSERT_EQUAL(2, v.height());
    TEST_ASSERT_EQUAL(3, v.stride());
    v[2, 1] = 8;
    TEST_ASSERT_EQUAL(8, (b[2, 1]));

    view<const int> cv = b;
    TEST_ASSERT_EQUAL_PTR(b.data(), cv.data());

    const buffer<int>& cb = b;
    view<const int> cv2 = cb;
    TEST_ASSERT_EQUAL_PTR(b.data(), cv2.data());
    TEST_ASSERT_EQUAL(8, (cv2[2, 1]));
}

TEST_CASE("image buffer copies and moves", "[idfxx][image]") {
    buffer<> a = sample();

    // Copies are independent.
    buffer<> copy(a);
    TEST_ASSERT_TRUE(same_pixels(a, copy));
    TEST_ASSERT_TRUE(a.data() != copy.data());
    copy[0, 0] = px(9);
    TEST_ASSERT_TRUE((a[0, 0] == px(1)));

    buffer<> assigned;
    assigned = a;
    TEST_ASSERT_TRUE(same_pixels(a, assigned));

    // Moves transfer the pixels without copying them.
    const rgb888* storage = a.data();
    buffer<> moved(std::move(a));
    TEST_ASSERT_EQUAL_PTR(storage, moved.data());
    TEST_ASSERT_EQUAL(3, moved.width());
    TEST_ASSERT_EQUAL(2, moved.height());

    buffer<> move_assigned(1, 1);
    move_assigned = std::move(moved);
    TEST_ASSERT_EQUAL_PTR(storage, move_assigned.data());
    TEST_ASSERT_TRUE(same_pixels(move_assigned, sample()));

    // A moved-from buffer can be assigned to.
    moved = copy;
    TEST_ASSERT_TRUE(same_pixels(moved, copy));
}

TEST_CASE("image buffer uses the supplied allocator", "[idfxx][image]") {
    alloc_counts counts;
    using counted_buffer = buffer<rgb888, counting_allocator<rgb888>>;
    {
        counted_buffer b(4, 4, counting_allocator<rgb888>(&counts));
        TEST_ASSERT_EQUAL(1, counts.allocations);
        TEST_ASSERT_TRUE(b.get_allocator().counts == &counts);

        counted_buffer f(2, 2, rgb888{1, 2, 3}, counting_allocator<rgb888>(&counts));
        TEST_ASSERT_EQUAL(2, counts.allocations);
        TEST_ASSERT_TRUE((f[1, 1] == rgb888{1, 2, 3}));

        // Copies allocate through the same allocator; moves do not allocate.
        counted_buffer copy(b);
        TEST_ASSERT_EQUAL(3, counts.allocations);
        TEST_ASSERT_TRUE(copy.get_allocator().counts == &counts);
        counted_buffer moved(std::move(copy));
        TEST_ASSERT_EQUAL(3, counts.allocations);

        // oriented allocates its result through the allocator it is given.
        auto rotated = oriented(b, orientation::rotate_90, counting_allocator<rgb888>(&counts));
        static_assert(std::same_as<decltype(rotated), counted_buffer>);
        TEST_ASSERT_EQUAL(4, counts.allocations);
        TEST_ASSERT_TRUE(rotated.get_allocator().counts == &counts);
        TEST_ASSERT_EQUAL(0, counts.deallocations);
    }
    TEST_ASSERT_EQUAL(4, counts.deallocations);
}

TEST_CASE("image oriented defaults to the source buffer's allocator", "[idfxx][image]") {
    alloc_counts counts;
    alloc_counts other;
    using counted_buffer = buffer<rgb888, counting_allocator<rgb888>>;

    counted_buffer src(3, 2, counting_allocator<rgb888>(&counts));
    orient(sample(), src, orientation::identity);
    TEST_ASSERT_EQUAL(1, counts.allocations);

    // From a buffer, the result inherits the buffer's allocator.
    auto r = oriented(src, orientation::rotate_90);
    static_assert(std::same_as<decltype(r), counted_buffer>);
    TEST_ASSERT_EQUAL(2, counts.allocations);
    TEST_ASSERT_TRUE(r.get_allocator().counts == &counts);
    TEST_ASSERT_TRUE(same_pixels(r, oriented(sample(), orientation::rotate_90)));

    const counted_buffer& csrc = src;
    auto cr = oriented(csrc, orientation::flip_vertical);
    static_assert(std::same_as<decltype(cr), counted_buffer>);
    TEST_ASSERT_EQUAL(3, counts.allocations);
    TEST_ASSERT_TRUE(cr.get_allocator().counts == &counts);

    // From a view, the result uses std::allocator.
    view<const rgb888> v = src;
    auto vr = oriented(v, orientation::rotate_90);
    static_assert(std::same_as<decltype(vr), buffer<>>);
    auto sr = oriented(src.subview(0, 0, 2, 2), orientation::rotate_180);
    static_assert(std::same_as<decltype(sr), buffer<>>);
    TEST_ASSERT_EQUAL(3, counts.allocations);

    // An explicit allocator wins over the default.
    auto er = oriented(src, orientation::rotate_90, counting_allocator<rgb888>(&other));
    static_assert(std::same_as<decltype(er), counted_buffer>);
    TEST_ASSERT_EQUAL(3, counts.allocations);
    TEST_ASSERT_EQUAL(1, other.allocations);
    TEST_ASSERT_TRUE(er.get_allocator().counts == &other);

    auto sa = oriented(src, orientation::rotate_90, std::allocator<rgb888>());
    static_assert(std::same_as<decltype(sa), buffer<>>);
    TEST_ASSERT_EQUAL(3, counts.allocations);
    TEST_ASSERT_TRUE(same_pixels(sa, r));
}

// =============================================================================
// Runtime tests: orientation
// =============================================================================

TEST_CASE("image orient applies each EXIF orientation", "[idfxx][image]") {
    struct expectation {
        orientation o;
        size_t width;
        size_t height;
        std::initializer_list<uint8_t> labels;
    };
    // The sample image is
    //   1 2 3
    //   4 5 6
    const std::array<expectation, 8> expected{{
        {orientation::identity, 3, 2, {1, 2, 3, 4, 5, 6}},
        {orientation::flip_horizontal, 3, 2, {3, 2, 1, 6, 5, 4}},
        {orientation::rotate_180, 3, 2, {6, 5, 4, 3, 2, 1}},
        {orientation::flip_vertical, 3, 2, {4, 5, 6, 1, 2, 3}},
        {orientation::transpose, 2, 3, {1, 4, 2, 5, 3, 6}},
        {orientation::rotate_90, 2, 3, {4, 1, 5, 2, 6, 3}},
        {orientation::transverse, 2, 3, {6, 3, 5, 2, 4, 1}},
        {orientation::rotate_270, 2, 3, {3, 6, 2, 5, 1, 4}},
    }};

    const buffer<> src = sample();
    for (const auto& e : expected) {
        const buffer<> want = make_image(e.width, e.height, e.labels);

        buffer<> dst(e.width, e.height);
        orient(src, dst, e.o);
        TEST_ASSERT_TRUE(same_pixels(want, dst));

        buffer<> copy = oriented(src, e.o);
        TEST_ASSERT_EQUAL(e.width, copy.width());
        TEST_ASSERT_EQUAL(e.height, copy.height());
        TEST_ASSERT_TRUE(same_pixels(want, copy));
    }
}

TEST_CASE("image orient handles single-pixel and empty images", "[idfxx][image]") {
    const buffer<> one = make_image(1, 1, {7});
    for (orientation o : all_orientations) {
        buffer<> r = oriented(one, o);
        TEST_ASSERT_EQUAL(1, r.width());
        TEST_ASSERT_EQUAL(1, r.height());
        TEST_ASSERT_TRUE((r[0, 0] == px(7)));
    }

    const buffer<> empty;
    for (orientation o : all_orientations) {
        buffer<> r = oriented(empty, o);
        TEST_ASSERT_TRUE(r.empty());
    }
}

TEST_CASE("image inverse undoes every orientation", "[idfxx][image]") {
    const buffer<> src = sample();
    for (orientation o : all_orientations) {
        TEST_ASSERT_TRUE(compose(o, inverse(o)) == orientation::identity);
        TEST_ASSERT_TRUE(compose(inverse(o), o) == orientation::identity);
        TEST_ASSERT_TRUE(inverse(inverse(o)) == o);
        TEST_ASSERT_EQUAL(swaps_axes(o), swaps_axes(inverse(o)));

        buffer<> there = oriented(src, o);
        buffer<> back = oriented(there, inverse(o));
        TEST_ASSERT_TRUE(same_pixels(src, back));
    }
}

TEST_CASE("image compose equals applying orientations in sequence", "[idfxx][image]") {
    const buffer<> src = sample();
    for (orientation first : all_orientations) {
        for (orientation second : all_orientations) {
            const orientation both = compose(first, second);
            TEST_ASSERT_EQUAL(swaps_axes(first) != swaps_axes(second), swaps_axes(both));

            buffer<> sequential = oriented(oriented(src, first), second);
            buffer<> fused = oriented(src, both);
            TEST_ASSERT_TRUE(same_pixels(sequential, fused));
        }
    }
}

TEST_CASE("image identity composes neutrally", "[idfxx][image]") {
    for (orientation o : all_orientations) {
        TEST_ASSERT_TRUE(compose(orientation::identity, o) == o);
        TEST_ASSERT_TRUE(compose(o, orientation::identity) == o);
    }
}

TEST_CASE("image source_point agrees with orient", "[idfxx][image]") {
    const size_t w = 5;
    const size_t h = 3;
    const buffer<> src = coordinate_image(w, h);
    for (orientation o : all_orientations) {
        const buffer<> dst = oriented(src, o);
        TEST_ASSERT_EQUAL(swaps_axes(o) ? h : w, dst.width());
        TEST_ASSERT_EQUAL(swaps_axes(o) ? w : h, dst.height());
        for (size_t y = 0; y < dst.height(); ++y) {
            for (size_t x = 0; x < dst.width(); ++x) {
                const point sp = source_point(o, {x, y}, w, h);
                TEST_ASSERT_TRUE(sp.x < w);
                TEST_ASSERT_TRUE(sp.y < h);
                TEST_ASSERT_EQUAL(sp.x, (dst[x, y].r));
                TEST_ASSERT_EQUAL(sp.y, (dst[x, y].g));
            }
        }
    }
}

TEST_CASE("image orient accepts buffers and views directly", "[idfxx][image]") {
    const orientation o = orientation::rotate_90;
    buffer<> src = sample();
    const buffer<> want = oriented(src, o);
    const buffer<>& csrc = src;
    view<rgb888> vsrc = src;
    view<const rgb888> cvsrc = src;
    const view<const rgb888> ccvsrc = src;

    // Every source kind into a buffer.
    {
        buffer<> dst(2, 3);
        orient(src, dst, o);
        TEST_ASSERT_TRUE(same_pixels(want, dst));
    }
    {
        buffer<> dst(2, 3);
        orient(csrc, dst, o);
        TEST_ASSERT_TRUE(same_pixels(want, dst));
    }
    {
        buffer<> dst(2, 3);
        orient(vsrc, dst, o);
        TEST_ASSERT_TRUE(same_pixels(want, dst));
    }
    {
        buffer<> dst(2, 3);
        orient(cvsrc, dst, o);
        TEST_ASSERT_TRUE(same_pixels(want, dst));
    }
    {
        buffer<> dst(2, 3);
        orient(ccvsrc, dst, o);
        TEST_ASSERT_TRUE(same_pixels(want, dst));
    }
    {
        buffer<> dst(2, 3);
        orient(sample(), dst, o);
        TEST_ASSERT_TRUE(same_pixels(want, dst));
    }

    // Into views: named, const-qualified (shallow), and temporary subviews.
    {
        buffer<> dst(2, 3);
        view<rgb888> vdst = dst;
        orient(src, vdst, o);
        TEST_ASSERT_TRUE(same_pixels(want, dst));
    }
    {
        buffer<> dst(2, 3);
        const view<rgb888> cvdst = dst;
        orient(cvsrc, cvdst, o);
        TEST_ASSERT_TRUE(same_pixels(want, dst));
    }
    {
        buffer<> dst(2, 3);
        orient(vsrc, view<rgb888>(dst), o);
        TEST_ASSERT_TRUE(same_pixels(want, dst));
    }
    {
        // Into a region of a larger image, leaving the rest untouched.
        const rgb888 bg{9, 9, 9};
        buffer<> screen(4, 5, bg);
        orient(src, screen.subview(1, 1, 2, 3), o);
        TEST_ASSERT_TRUE(same_pixels(want, screen.subview(1, 1, 2, 3)));
        TEST_ASSERT_TRUE((screen[0, 0] == bg));
        TEST_ASSERT_TRUE((screen[3, 1] == bg));
        TEST_ASSERT_TRUE((screen[1, 4] == bg));
    }
    {
        // From a strided subview of a larger image.
        buffer<> big(5, 4, rgb888{});
        orient(src, big.subview(1, 1, 3, 2), orientation::identity);
        buffer<> dst(2, 3);
        orient(big.subview(1, 1, 3, 2), dst, o);
        TEST_ASSERT_TRUE(same_pixels(want, dst));
        const buffer<>& cbig = big;
        buffer<> dst2(2, 3);
        orient(cbig.subview(1, 1, 3, 2), dst2, o);
        TEST_ASSERT_TRUE(same_pixels(want, dst2));
    }

    // oriented from every source kind.
    TEST_ASSERT_TRUE(same_pixels(want, oriented(src, o)));
    TEST_ASSERT_TRUE(same_pixels(want, oriented(csrc, o)));
    TEST_ASSERT_TRUE(same_pixels(want, oriented(vsrc, o)));
    TEST_ASSERT_TRUE(same_pixels(want, oriented(cvsrc, o)));
    TEST_ASSERT_TRUE(same_pixels(want, oriented(ccvsrc, o)));
    TEST_ASSERT_TRUE(same_pixels(want, oriented(sample(), o)));
    TEST_ASSERT_TRUE(same_pixels(want, oriented(src.subview(0, 0, 3, 2), o)));
}

TEST_CASE("image orient works with non-rgb888 pixels", "[idfxx][image]") {
    buffer<uint16_t> src(3, 2);
    for (size_t y = 0; y < 2; ++y) {
        for (size_t x = 0; x < 3; ++x) {
            src[x, y] = static_cast<uint16_t>(1000 + y * 3 + x);
        }
    }
    buffer<uint16_t> r = oriented(src, orientation::rotate_270);
    TEST_ASSERT_EQUAL(2, r.width());
    TEST_ASSERT_EQUAL(3, r.height());
    // Top row of a 270-degree rotation is the source's right column.
    TEST_ASSERT_EQUAL(1002, (r[0, 0]));
    TEST_ASSERT_EQUAL(1005, (r[1, 0]));
    TEST_ASSERT_EQUAL(1000, (r[0, 2]));
}
