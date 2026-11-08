# idfxx_image

Pixel image views, owning buffers, EXIF orientation transforms, and placing images on a screen.

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
- `renderer`: places an image on a screen in bounded memory, row by row:
  turned to its displayed orientation, cropped, resampled to a target
  rectangle (`cover`, `fit` or `stretch`), clipped to the screen, and aligned
  for destinations that need it
- `render(decoder, out, placement, config)`: decodes through a renderer, at
  the smallest decode scale that covers the output, with any
  `idfxx::image::decoder`
- Four resampling filters (`box`, `bilinear`, `bicubic`, `lanczos3`), computed
  in fixed point with the same output on every target
- No failure modes in images, views and orientations: their operations are
  infallible given their documented preconditions; rendering returns or throws
  errors

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

### Placing an image on a screen

`render` decodes an image and delivers the rows of the placed picture to a
sink: top to bottom, at their position on the screen, each the full width of
the output. It works with any `idfxx::image::decoder`, not only JPEG.

```cpp
#include <idfxx/image>
#include <idfxx/jpeg>

namespace image = idfxx::image;

auto to_panel = [&](image::point at, image::view<const image::rgb888> rows) -> idfxx::result<void> {
    return write_rows(at, rows);        // your own display code
};

idfxx::memory_source src{jpeg_bytes};
idfxx::jpeg::decoder dec(src);

// Fill a 320x240 screen, upright according to the photo's EXIF orientation.
image::render(dec, to_panel, {}, {.screen_width = 320, .screen_height = 240});
```

With the result-based API, and a rectangle, a layout and a crop:

```cpp
idfxx::jpeg::decoder dec2 = /* ... */;

const image::placement where{
    .target = image::rect{.x = 160, .y = 0, .width = 160, .height = 240},
    .layout = image::layout::fit,
    .orientation = image::orientation::rotate_90,     // on top of the EXIF orientation
    .crop = image::rect{.x = 100, .y = 100, .width = 800, .height = 600},
    .background = {0, 0, 0},
    .filter = image::filter::bicubic,
};
const image::render_config config{
    .screen_width = 320,
    .screen_height = 240,
    .memory_limit = 512 * 1024,
};
if (auto r = image::try_render(dec2, to_panel, where, config); !r) {
    // ...
}
```

Or feed a `renderer` yourself, with source rows from anywhere:

```cpp
image::renderer r(width, height, image::orientation::identity, where, config);
for (size_t y = 0; y < height; ++y) {
    r.push({0, y}, next_source_row(), to_panel);
}
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
| `get_pixel(x, y)` | The value of the pixel at column `x`, row `y`; a value-initialized pixel outside the image. |
| `fill(p)` / `clear()` | Sets every pixel of a buffer or mutable view to `p`, or to a value-initialized pixel. A view's rows are filled one at a time, so nothing between them changes. |
| `row(y)` | Row `y` as a `std::span` of `width()` pixels. |
| `subview(x, y, w, h)` | View of a rectangular region (read-only for a const buffer). |
| `orientation` | `identity`, `flip_horizontal`, `rotate_180`, `flip_vertical`, `transpose`, `rotate_90`, `transverse`, `rotate_270` (EXIF values 1-8). |
| `to_string(o)` | The enumerator's name, such as `"rotate_90"`; also formattable with `std::format` when `CONFIG_IDFXX_STD_FORMAT` is enabled. |
| `swaps_axes(o)` | True when `o` exchanges width and height (EXIF 5-8). |
| `inverse(o)` | The orientation undoing `o`. |
| `compose(first, second)` | The single orientation equal to applying `first`, then `second`. |
| `source_point(o, p, src_w, src_h)` | The source pixel shown at `p` in the oriented image. |
| `orient(src, dst, o)` | Copy `src` into `dst` with `o` applied. |
| `oriented(src, o[, alloc])` | A new `buffer` holding `src` with `o` applied; allocated like `src` when it is a buffer, unless `alloc` is given. |
| `sink<S, P = rgb888>` | Concept: `S&` is callable as `(point, view<const P>)` returning `idfxx::result<void>`. |
| `rect` | A rectangle with a signed origin, so it can overhang: `x`, `y`, `width`, `height`. |
| `size` | A `width` and a `height`. |
| `oriented_size(o, w, h)` | The size of a `w` x `h` image with `o` applied. |
| `filter` | `box`, `bilinear`, `bicubic`, `lanczos3`. |
| `layout` | `cover` fills the target and crops the excess, `fit` fits inside it on the background, `stretch` scales each axis to it. |
| `placement` | Where and how: `target` (default the whole screen), `layout`, `orientation`, `crop`, `background`, `filter`, `fill_to_alignment`. |
| `render_config` | `screen_width` and `screen_height` (required), `x_alignment`, `memory_limit`, `frame_mem`. |
| `renderer(w, h, source_orientation, placement, config)` / `renderer::make(...)` | A renderer for an image of `w` x `h`. |
| `output()` / `empty()` | The rectangle of rows delivered, in screen coordinates; whether it has none. |
| `push(at, rows, out)` / `try_push(at, rows, out)` | Takes the next source rows, and delivers the output rows they complete to the sink `out`. |
| `decoder`, `scaling_decoder` | Concepts for what `render` and `renderer` read from a decoder; a `scaling_decoder` also decodes at a reduced scale. |
| `render(decoder, out, placement, config)` / `try_render(...)` | Decodes through a renderer, at the decode scale the output needs. |
| `renderer(decoder, placement, config)` / `renderer::make(decoder, ...)` | A renderer for a decoder's image, at the decode scale the output needs, for a destination that has to know `output()` before the first row. |

## Error Handling

Images, views and orientations have no failure modes and no error codes:
there are no `try_*` variants for them, and the only `result` type they involve
is the one a `sink` returns to the producer feeding it. Instead, their
functions have documented preconditions that the caller is responsible for
meeting, and which are not checked at run time:

- Pixel coordinates and rows must be within the image (`x < width()`,
  `y < height()`), except for `set_pixel`, which ignores writes outside it, and
  `get_pixel`, which returns a value-initialized pixel; subview regions must lie
  within their parent.
- A view's stride must be at least its width, and its storage must outlive it.
- For `orient`, `dst` must have `src`'s dimensions — exchanged when
  `swaps_axes(o)` — and must not overlap `src`.

Memory allocation is the allocator's responsibility: with the default
`std::allocator`, constructing a buffer that cannot be allocated throws
`std::bad_alloc` (or aborts when exceptions are disabled).

Rendering has both API styles. It uses these error codes:

| Error | Meaning |
| ----- | ------- |
| `idfxx::errc::invalid_arg` | At creation: an empty image, screen or target, a zero `x_alignment`, a crop that is empty or outside the displayed image, or an unaligned output without `fill_to_alignment`. |
| `idfxx::errc::invalid_size` | At creation: an image over 65535 pixels a side or 100 megapixels, or needing more working memory than a `size_t` can count. When pushing: rows of the wrong width. |
| `idfxx::image::errc::too_large` | At creation: more working memory than `memory_limit`. |
| `idfxx::errc::invalid_state` | When pushing: rows out of sequence or past the end of the image, or after a sink failed. |

An error from the sink or the decoder is returned unchanged. Running out of
memory throws `std::bad_alloc`, or aborts when exceptions are disabled; set
`memory_limit` to get `image::errc::too_large` instead.

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
- The renderer needs no frame when the source's orientation composed with the
  placement's is `identity` or `flip_horizontal`. Its memory then grows with
  the output width and the filter's reach: about 106 KB for 4000x3000 onto
  1200x1600 with `lanczos3`, 41 KB with `bilinear`. Any other orientation also
  holds one frame of the output at 3 bytes a pixel (5.76 MB at 1200x1600),
  allocated from `render_config::frame_mem` (the default heap unless set, e.g.
  to `idfxx::memory::capabilities::spiram`), and delivers its rows after the
  last source row. The image itself is never held.
- `render` decodes nothing when the target is fully off-screen. A target whose
  visible part shows only background, such as `fit` with the picture
  off-screen, is still decoded, at the smallest scale the decoder offers.
- A target and its alignment are in the coordinates of the screen, and a crop
  in those of the full-size image with both orientations applied, also when it
  is decoded at a reduced scale.
- Every clipped pixel is the same as in the whole picture, but the output is
  only the same across orientations where the arithmetic is: the resampler
  works in the stored orientation, and its passes round in turn, so a picture
  and the same picture stored rotated can differ by rounding.
- `render` uses up the decoder: a decoder decodes once.
- Pushing and `render` give way to lower-priority tasks about every half second
  of a long operation, including while a slow sink runs, so that the idle task
  the task watchdog looks after can run. Don't call them from an ISR or critical
  section.

## License

Apache-2.0
