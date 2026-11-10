// SPDX-License-Identifier: Apache-2.0

#include "terminal_preview.hpp"

#include <idfxx/byte_source>
#include <idfxx/image>
#include <idfxx/jpeg>
#include <idfxx/log>
#include <idfxx/sched>

#include <array>
#include <chrono>
#include <cstdint>
#include <span>
#include <system_error>
#include <utility>

using namespace std::chrono_literals;

static constexpr idfxx::log::logger logger{"example"};

using idfxx::image::rgb888;

// The least width to decode the preview at, in terminal columns (one pixel per column).
static constexpr size_t PREVIEW_COLUMNS = 56;

// The photo and a progressive re-encoding of it, embedded in flash by EMBED_FILES.
extern const uint8_t photo_start[] asm("_binary_photo_jpg_start");
extern const uint8_t photo_end[] asm("_binary_photo_jpg_end");
extern const uint8_t progressive_start[] asm("_binary_photo_progressive_jpg_start");
extern const uint8_t progressive_end[] asm("_binary_photo_progressive_jpg_end");

// The photo's JPEG bytes.
static std::span<const uint8_t> photo() {
    return {photo_start, photo_end};
}

// The progressive re-encoding's JPEG bytes.
static std::span<const uint8_t> progressive_photo() {
    return {progressive_start, progressive_end};
}

// 1. Reads the photo's headers, without decoding any pixels.
static void show_header_info() {
    logger.info("=== Header info ===");
    const auto jpeg_bytes = photo();
    idfxx::memory_source source{jpeg_bytes};
    idfxx::jpeg::decoder decoder(source);

    // The size is as stored. The EXIF orientation says how to turn the photo upright.
    const auto& info = decoder.info();
    const auto exif_tag = std::to_underlying(info.orientation);
    logger.info("{} bytes, {}x{}", jpeg_bytes.size(), info.width, info.height);
    logger.info("EXIF orientation {}, progressive: {}", exif_tag, info.progressive);
}

// 2. Decodes the photo small and prints it upright in the serial monitor.
static void show_color_preview() {
    logger.info("=== Color preview ===");
    // A source reads forward only, and a decoder decodes once, so each section makes a new pair.
    idfxx::memory_source source{photo()};
    idfxx::jpeg::decoder decoder(source);
    const auto& info = decoder.info();

    // The upright photo's aspect ratio gives the preview's height.
    const idfxx::image::size shown =
        idfxx::image::oriented_size(info.orientation, info.width, info.height);
    const size_t preview_height = PREVIEW_COLUMNS * shown.height / shown.width;

    // The decoder works in the stored orientation, so its target is turned back. It decodes at the
    // smallest of its fixed scales that covers the target, so the printed size is only approximate.
    // For an exact size, use idfxx::image::render().
    const idfxx::image::size target =
        idfxx::image::oriented_size(info.orientation, PREVIEW_COLUMNS, preview_height);
    decoder.scale_to_cover(target.width, target.height);
    logger.info("Decoding at {}x{}", decoder.output_width(), decoder.output_height());

    idfxx::image::buffer<> stored(decoder.output_width(), decoder.output_height());
    decoder.decode(stored);
    terminal_preview::print_image(idfxx::image::oriented(stored, info.orientation));
}

// 3. Decodes the photo at full size without ever holding more than one row of it.
static void show_streaming() {
    logger.info("=== Streaming ===");
    idfxx::memory_source source{photo()};
    idfxx::jpeg::decoder decoder(source);

    uint64_t red = 0;
    uint64_t green = 0;
    uint64_t blue = 0;

    // The sink is called once per row, top to bottom in the stored orientation, with a block one
    // pixel high. Only that row is in memory, however large the photo is.
    auto add_row = [&](idfxx::image::point,
                       idfxx::image::view<const rgb888> block) -> idfxx::result<void> {
        for (const rgb888& pixel : block.row(0)) {
            red += pixel.r;
            green += pixel.g;
            blue += pixel.b;
        }
        return {};
    };
    decoder.decode(add_row);

    const size_t pixel_count = decoder.output_width() * decoder.output_height();
    logger.info(
        "Average color: ({}, {}, {})", red / pixel_count, green / pixel_count, blue / pixel_count
    );
}

// Reads a JPEG's headers and decodes it at the smallest scale (1/8), passing any error on to the
// caller. The full-size photo would not fit in memory here, and errors are the same at any scale.
static idfxx::result<void>
try_decode_small(std::span<const uint8_t> jpeg, idfxx::jpeg::decoder::config config = {}) {
    idfxx::memory_source source{jpeg};
    auto decoder = idfxx::jpeg::decoder::make(source, config);
    if (!decoder) {
        return idfxx::error(decoder.error());
    }
    // Any target smaller than 1/8 scale selects 1/8.
    decoder->scale_to_cover(1, 1);
    idfxx::image::buffer<> pixels(decoder->output_width(), decoder->output_height());
    return decoder->try_decode(pixels);
}

// 4. The result-based API, for data that might not decode.
static void show_error_handling() {
    logger.info("=== Error handling ===");
    const auto log_outcome = [](const char* what, const idfxx::result<void>& outcome) {
        logger.info("{}: {}", what, outcome ? "decoded" : outcome.error().message());
    };

    // Bytes that aren't a JPEG, here the start of a PNG file, are rejected when the headers are
    // read.
    static constexpr std::array<uint8_t, 8> png_signature{
        0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'
    };
    log_outcome("Not a JPEG", try_decode_small(png_signature));

    // A photo cut in half still has its headers, but the image data ends too soon.
    const auto jpeg_bytes = photo();
    log_outcome("Truncated", try_decode_small(jpeg_bytes.first(jpeg_bytes.size() / 2)));

    // A progressive JPEG refines the whole picture scan by scan, so the decoder keeps every
    // coefficient until the last scan: about 1.2 MB here, at any scale. A memory limit rejects it
    // before that is allocated.
    log_outcome(
        "Progressive with a 64 KiB limit",
        try_decode_small(progressive_photo(), {.memory_limit = 64 * 1024})
    );
}

extern "C" void app_main() {
    try {
        show_header_info();
        show_color_preview();
        show_streaming();
        show_error_handling();
        logger.info("Done!");
    } catch (const std::system_error& e) {
        logger.error("Error: {}", e.what());
    }

    while (true) {
        idfxx::delay(1h);
    }
}
