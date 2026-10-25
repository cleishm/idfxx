# idfxx_image

Pixel image views, owning buffers, and EXIF orientation transforms.

📚 **[Full API Documentation](https://cleishm.github.io/idfxx/group__idfxx__image.html)**

## Features

- `rgb888`: a 24-bit RGB pixel, laid out as three packed bytes so arrays of
  it match packed RGB888 image data
- `view<P>`: a non-owning, row-major window onto pixels with an optional row
  stride, so a view can address a rectangle inside a larger image; cheap to
  copy, with `view<P>` converting implicitly to read-only `view<const P>`
- `buffer<P, Allocator>`: an owning, tightly packed image with value-semantic
  copy and move, and a configurable allocator (e.g. PSRAM for large images);
  converts implicitly to a view of its pixels
- Pixel access by `img[x, y]`, whole rows as `std::span`, and `subview(x, y, w, h)`
  for regions
- Drawing with idfxx_gfx: `set_pixel` ignores writes outside the image, so a
  buffer or a view of mutable pixels is an `idfxx::gfx::pixel_surface` that
  a `gfx::canvas` can draw on, clipped to its bounds
- `orientation`: the eight EXIF orientations, with values matching EXIF tag
  0x0112 so a parsed tag casts directly
- Orientation algebra: `inverse`, `compose` (fuse an image's EXIF orientation
  with a display rotation into a single transform), `swaps_axes`, and
  `source_point` for mapping displayed pixels back to source pixels
- `orient(src, dst, o)` and `oriented(src, o)` apply an orientation to any
  buffer or view, passed directly
- `sink<S, P>`: the concept for a destination that receives pixels block by
  block from a producer such as an image decoder, and can stop it by
  returning an error
- No failure modes: image operations are infallible given their documented
  preconditions

## Requirements

- ESP-IDF 5.5 or later
- C++23 compiler support
- idfxx_core

## Installation

### ESP-IDF Component Manager

Add to your project's `idf_component.yml`:

```yaml
dependencies:
  cleishm/idfxx_image: "^1.0.0"
```

## Usage

```cpp
#include <idfxx/image>

namespace image = idfxx::image;

// A 640x480 image, initially black, with pixels filled in by a decoder.
image::buffer<> photo(640, 480);
for (size_t y = 0; y < photo.height(); ++y) {
    decode_row(photo.row(y));           // std::span<image::rgb888>
}

// Display it upright according to its EXIF orientation tag (1-8).
auto o = static_cast<image::orientation>(exif_orientation_tag);
image::buffer<> upright = image::oriented(photo, o);   // 480x640 for rotate_90

// Or write it straight into a region of a larger image.
image::buffer<> screen(800, 800, {32, 32, 32});
image::orient(photo, screen.subview(160, 80, upright.width(), upright.height()), o);

// Fuse the EXIF orientation with a display mounted upside down, so the
// image is transformed once.
image::orientation total = image::compose(o, image::orientation::rotate_180);
```

### PSRAM-backed buffers

A large image can be placed in PSRAM by supplying an allocator from
`idfxx_core`'s `<idfxx/memory>`:

```cpp
#include <idfxx/image>
#include <idfxx/memory>

using psram_image = idfxx::image::buffer<idfxx::image::rgb888, idfxx::spiram_allocator<idfxx::image::rgb888>>;

psram_image frame(1024, 768);

// The result of oriented() uses the source buffer's allocator, so it is in PSRAM too.
psram_image rotated = idfxx::image::oriented(frame, idfxx::image::orientation::rotate_90);
```

### Receiving pixels through a sink

A producer such as a decoder hands an image to a `sink` one block at a time:
the block's top-left position in the full image and a read-only view of its
pixels. Returning an error stops the producer, which returns that error.

```cpp
#include <idfxx/image>

#include <algorithm>

idfxx::image::buffer<> screen(320, 240);

auto to_screen = [&](idfxx::image::point at, idfxx::image::view<const idfxx::image::rgb888> block)
    -> idfxx::result<void> {
    if (at.x + block.width() > screen.width() || at.y + block.height() > screen.height()) {
        return idfxx::error(idfxx::errc::invalid_size);
    }
    for (size_t y = 0; y < block.height(); ++y) {
        std::ranges::copy(block.row(y), screen.row(at.y + y).begin() + at.x);
    }
    return {};
};
static_assert(idfxx::image::sink<decltype(to_screen)>);
```

## API Overview

| Item | Description |
| ---- | ----------- |
| `rgb888` | 24-bit RGB pixel (`r`, `g`, `b`), three packed bytes, comparable with `==`. |
| `view<P>(data, w, h[, stride])` | Non-owning view of `w` x `h` pixels, rows `stride` pixels apart (default `w`). |
| `buffer<P, Allocator>(w, h[, fill][, alloc])` | Owning, tightly packed image; pixels value-initialized or set to `fill`. |
| `width()` / `height()` / `stride()` / `empty()` / `data()` | Dimensions and storage of a view or buffer. |
| `img[x, y]` | Pixel at column `x`, row `y`. |
| `set_pixel(x, y, p)` | Writes `p` at column `x`, row `y`; ignored outside the image. Makes a buffer or mutable view an `idfxx::gfx::pixel_surface`. |
| `row(y)` | Row `y` as a `std::span` of `width()` pixels. |
| `subview(x, y, w, h)` | View of a rectangular region (read-only for a const buffer). |
| `orientation` | `identity`, `flip_horizontal`, `rotate_180`, `flip_vertical`, `transpose`, `rotate_90`, `transverse`, `rotate_270` (EXIF values 1-8). |
| `swaps_axes(o)` | True when `o` exchanges width and height (EXIF 5-8). |
| `inverse(o)` | The orientation undoing `o`. |
| `compose(first, second)` | The single orientation equal to applying `first`, then `second`. |
| `source_point(o, p, src_w, src_h)` | The source pixel shown at `p` in the oriented image. |
| `orient(src, dst, o)` | Copy `src` into `dst` with `o` applied. |
| `oriented(src, o[, alloc])` | A new `buffer` holding `src` with `o` applied; allocated like `src` when it is a buffer, unless `alloc` is given. |
| `sink<S, P = rgb888>` | Concept: `S&` is callable as `(point, view<const P>)` returning `idfxx::result<void>`. |

## Error Handling

The image operations have no failure modes and no error codes: there are no
`try_*` variants, and the only `result` type is the one a `sink` returns to
the producer feeding it. Instead, functions have documented preconditions that
the caller is responsible for meeting, and which are not checked at run time:

- Pixel coordinates and rows must be within the image (`x < width()`,
  `y < height()`), except for `set_pixel`, which ignores writes outside it;
  subview regions must lie within their parent.
- A view's stride must be at least its width, and its storage must outlive it.
- For `orient`, `dst` must have `src`'s dimensions — exchanged when
  `swaps_axes(o)` — and must not overlap `src`.

Memory allocation is the allocator's responsibility: with the default
`std::allocator`, constructing a buffer that cannot be allocated throws
`std::bad_alloc` (or aborts when exceptions are disabled).

## Important Notes

- Each `orientation` value names the transform that makes the stored image
  display upright, matching the EXIF meaning of the tag: an image tagged 6 is
  stored rotated a quarter turn counter-clockwise, and `rotate_90` turns it
  clockwise to display. Rotations are clockwise.
- `compose(first, second)` is ordered: `first` is applied, then `second`.
  It is generally not commutative.
- A view is non-owning and its constness is shallow, as with `std::span`: a
  `const view<P>` still allows writes; use `view<const P>` for read-only
  access. Subviews of a buffer keep the buffer's stride.
- Moving a buffer transfers its pixels without copying them. The exception,
  as with `std::vector`, is move assignment with a stateful allocator that
  doesn't propagate on move: if the two allocators compare unequal, the
  pixels are copied into newly allocated storage. A moved-from buffer must
  not be used: any operation other than destruction or assignment is
  undefined behavior.
- `orient` and `oriented` accept a buffer or a view (const or not) directly
  for both source and destination; a temporary buffer cannot be a
  destination.
- `oriented` allocates its result with a copy of the source buffer's
  allocator (a PSRAM image stays in PSRAM), or with `std::allocator` when the
  source is a view. Pass an allocator explicitly to choose otherwise, e.g.
  `oriented(view, o, idfxx::spiram_allocator<idfxx::image::rgb888>())`.

## License

Apache-2.0
