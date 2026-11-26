# idfxx_jpeg

JPEG decoding into RGB888 images, with output scaling and EXIF orientation.

📚 **[Full API Documentation](https://cleishm.github.io/idfxx/group__idfxx__jpeg.html)**

## Features

- Decodes from any `idfxx::byte_source`: memory, a file, a flash partition, a socket, or an HTTP
  response body, read forward in 4 KB chunks
- Baseline and progressive JPEGs, Huffman or arithmetic coded, with any chroma subsampling and
  restart intervals. Grayscale, YCbCr and RGB color
- Output is always `idfxx::image::rgb888`. Grayscale images are output with equal channels
- Reads the headers first, so the image size, EXIF orientation and coding mode are known before any
  pixels are decoded
- `scale_to_cover(w, h)` decodes at the smallest scale M/8 (1/8 to 8/8) that still covers a target
  size, which is much cheaper than decoding at full size
- Decodes straight into an `idfxx::image::view` (any stride, so into a region of a larger image), or
  row by row to an `idfxx::image::sink` without holding the whole image in memory
- Strict: truncated data and detected corruption fail the decode instead of producing a partly gray
  image. Stray bytes before a marker, a JFIF version other than 1.x and an unrecognized Adobe color
  transform are logged and skipped. Byte source errors are returned unchanged
- Optional limit on the decoder's working memory, so an image too large for the device (a
  progressive JPEG, or a very wide one) fails cleanly with `too_large` instead of exhausting the
  heap
- libjpeg-turbo's default, accurate decoding (integer IDCT, fancy upsampling), so output matches
  other libjpeg-turbo decoders

## Requirements

- ESP-IDF 5.5 or later
- C++23 compiler
- idfxx_core, idfxx_image
- espressif/libjpeg-turbo (fetched by the component manager)

## Installation

### ESP-IDF Component Manager

Add to your project's `idf_component.yml`:

```yaml
dependencies:
  cleishm/idfxx_jpeg: "^1.0.0"
```

## Usage

### Exception-based API

If `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled:

```cpp
#include <idfxx/byte_source>
#include <idfxx/image>
#include <idfxx/jpeg>

#include <utility>

namespace image = idfxx::image;

void show(std::span<const uint8_t> jpeg_bytes) {
    try {
        idfxx::memory_source src{jpeg_bytes};
        idfxx::jpeg::decoder dec(src);

        // Decode at the smallest scale that still fills a 320x240 display.
        // The target is in the stored orientation, so swap it when the EXIF
        // orientation turns the image a quarter turn.
        size_t w = 320;
        size_t h = 240;
        if (image::swaps_axes(dec.info().orientation)) {
            std::swap(w, h);
        }
        dec.scale_to_cover(w, h);

        image::buffer<> photo(dec.output_width(), dec.output_height());
        dec.decode(photo);

        image::buffer<> upright = image::oriented(photo, dec.info().orientation);
        display(upright);
    } catch (const std::system_error& e) {
        ESP_LOGE(TAG, "JPEG decode failed: %s", e.what());
    }
}
```

### Result-based API

Always available:

```cpp
#include <idfxx/byte_source>
#include <idfxx/image>
#include <idfxx/jpeg>

// Your own display code: draws one row of pixels at row y of the screen.
idfxx::result<void> draw_row(size_t y, idfxx::image::view<const idfxx::image::rgb888> row);

idfxx::result<void> stream_to_screen(idfxx::byte_source auto& src, size_t screen_w, size_t screen_h) {
    auto dec = idfxx::jpeg::decoder::make(src);
    if (!dec) {
        return idfxx::error(dec.error());
    }
    dec->scale_to_cover(screen_w, screen_h);

    // Rows arrive one at a time. Only one row of pixels is held in memory.
    return dec->try_decode(
        [&](idfxx::image::point at, idfxx::image::view<const idfxx::image::rgb888> row) -> idfxx::result<void> {
            return draw_row(at.y, row); // an error here stops the decode
        }
    );
}
```

### Limiting working memory

```cpp
idfxx::jpeg::decoder dec(src, {.memory_limit = 512 * 1024});
// An image that would need more than 512 KB of working memory, such as a
// large progressive JPEG, fails with idfxx::image::errc::too_large when
// decoding starts, before that memory is allocated.
```

## API Overview

| Item | Description |
| ---- | ----------- |
| `decoder(src[, config])` / `decoder::make(src[, config])` | Creates a decoder over a byte source and reads the JPEG headers. The source is held by reference and must outlive the decoder. |
| `config::memory_limit` | Optional cap, in bytes, on the decoder's working memory for any JPEG (approximate: allocator overhead isn't counted). Checked from the start of decoding, so construction never fails because of it. `std::nullopt` means no limit. |
| `info()` | `width`, `height` (stored orientation), `orientation` (EXIF; `identity` if absent or invalid), `progressive`. |
| `scale_to_cover(w, h)` | Selects the smallest scale M/8 whose output is at least `w` x `h`, or full size if no scale covers it. Never upscales. |
| `output_width()` / `output_height()` | The size that the decode produces: the image size until `scale_to_cover` reduces it. |
| `decode(view)` / `try_decode(view)` | Decodes into a view of exactly the output size, honoring its stride. |
| `decode(sink)` / `try_decode(sink)` | Decodes row by row, calling `sink({0, y}, row_view)` for each output row from top to bottom. |
| `jpeg::errc` | `invalid_data`, `truncated`, `not_supported`. |

## Error Handling

| Error | Meaning |
| ----- | ------- |
| `jpeg::errc::invalid_data` | Not a JPEG, or corrupt. Any corruption that libjpeg-turbo detects fails the decode, including problems that it could otherwise work around. The exceptions are stray bytes before a marker, a JFIF version other than 1.x and an unrecognized Adobe color transform. These are logged, and decoding continues. JPEG has no checksum, so most damage to the compressed image data cannot be detected and decodes to wrong pixels without an error. |
| `jpeg::errc::truncated` | The byte source ended before the end of the image, including a missing end-of-image marker. |
| `jpeg::errc::not_supported` | CMYK or YCCK color, 12-bit or lossless JPEG (all rejected when the headers are read), or another feature the decoder does not support. Also compares equal to `std::errc::not_supported`. |
| `idfxx::image::errc::too_large` | Decoding needs more working memory than `config::memory_limit`. Reported by the decode, never when the headers are read. |
| `idfxx::errc::invalid_state` | The image has already been decoded. A decoder decodes once. |
| `idfxx::errc::invalid_size` | The destination view is not the output size. |
| Source or sink error | An error returned by the byte source's `try_read` or by the sink is returned unchanged. |

Running out of memory is not an error code. As everywhere in idfxx, `std::bad_alloc` is thrown when
exceptions are enabled, and the program aborts otherwise. Set `memory_limit` to the memory that can
be spared, so that an image that would need more fails with `too_large` instead.

## Important Notes

- **Orientation is reported, never applied.** The decoder outputs the image as stored. Apply
  `info().orientation` with `idfxx::image::oriented` or `idfxx::image::orient`. Width, height and
  `scale_to_cover` targets are all in the stored orientation, so swap the target when
  `idfxx::image::swaps_axes(info().orientation)` is true.
- **Decoding at reduced scale is much cheaper.** A scaled decode produces fewer pixels from the same
  coefficients, so decode at the smallest scale that covers the display rather than decoding at full
  size and downscaling.
- **Working memory depends on the image.** A baseline (single-scan) JPEG needs tables and a few rows
  of pixels. The row memory grows with the width that the image is decoded at. As `memory_limit`
  counts it, a 1600x1200 image needs about 75 KB at full size and 39 KB at 1/2 scale. A progressive
  JPEG also needs a whole-image coefficient buffer of about 128 bytes per 8x8 block per color
  component, at any output scale. This is about 5.8 MB for a 1600x1200 4:2:0 image, which in
  practice requires PSRAM. `memory_limit` bounds both, so an image too large for the device fails
  cleanly with `too_large` instead of exhausting the heap. Even a small image needs about 20 KB,
  mostly for the decoder's state and fixed tables. A lower limit fails every decode.
- **`CONFIG_LIBJPEG_TURBO_ALLOC_PREFER_SPIRAM`** (libjpeg-turbo's own option, default `y` when PSRAM
  is enabled) places libjpeg-turbo's working memory in PSRAM first, including the row buffer that
  decoding to a sink passes to it. It is applied at link time, so it affects every user of
  libjpeg-turbo in the application and cannot be chosen per decoder. If about 41 KB of internal RAM
  can be spared, consider setting it to `n` together with `CONFIG_SPIRAM_USE_MALLOC`. On an
  ESP32-S3, this makes decoding 1600x1200 images 13-29% faster.
- **Long decodes yield to other tasks.** Lower-priority tasks can run about every half second,
  including while a progressive JPEG's scans are read. The idle task, which the task watchdog
  monitors, can therefore run. Don't decode from an ISR or critical section.
- **Build optimization level matters.** libjpeg-turbo is compiled with the project's optimization
  level, so `CONFIG_COMPILER_OPTIMIZATION_PERF` decodes about 15% faster than the default debug
  level (measured at 1600x1200 on an ESP32-S3).
- **Flash cost** is roughly 150 KB, almost all of it libjpeg-turbo.
- **Decoding uses about 2 KB of the calling task's stack** (measured on an ESP32-S3), whatever the
  image size or output scale, plus whatever the byte source's reads and a sink use. Allow for it
  when sizing the decoding task's stack. It is more than half of the default main task stack
  (`CONFIG_ESP_MAIN_TASK_STACK_SIZE`, 3584 bytes).
- The decoder holds its byte source by reference and reads it forward only. The source must outlive
  the decoder. A decoder decodes once. Create a new one (over a new source) to decode again.
- The decoder is move-only. A moved-from decoder must not be used: any operation other than
  destruction or assignment is undefined behavior.

## License

Apache-2.0
