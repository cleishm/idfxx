// SPDX-License-Identifier: Apache-2.0

#include <idfxx/byte_source>
#include <idfxx/image>
#include <idfxx/jpeg>
#include <idfxx/log>
#include <idfxx/sched>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <system_error>
#include <utility>

using namespace std::chrono_literals;

static constexpr idfxx::log::logger logger{"example"};

using idfxx::image::rgb888;

// The width of the preview, in terminal columns (one pixel per column).
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

// Shrinks an image to width x height, averaging the source pixels that fall into each output pixel.
static idfxx::image::buffer<> shrink(idfxx::image::view<const rgb888> source, size_t width, size_t height) {
    idfxx::image::buffer<> shrunk(width, height);
    for (size_t y = 0; y < height; ++y) {
        const size_t top = y * source.height() / height;
        const size_t bottom = std::max(top + 1, (y + 1) * source.height() / height);
        for (size_t x = 0; x < width; ++x) {
            const size_t left = x * source.width() / width;
            const size_t right = std::max(left + 1, (x + 1) * source.width() / width);
            uint32_t red = 0;
            uint32_t green = 0;
            uint32_t blue = 0;
            for (size_t sy = top; sy < bottom; ++sy) {
                for (size_t sx = left; sx < right; ++sx) {
                    red += source[sx, sy].r;
                    green += source[sx, sy].g;
                    blue += source[sx, sy].b;
                }
            }
            const auto count = static_cast<uint32_t>((right - left) * (bottom - top));
            const auto average = [count](uint32_t total) { return static_cast<uint8_t>(total / count); };
            shrunk[x, y] = {average(red), average(green), average(blue)};
        }
    }
    return shrunk;
}

// Prints an image as 24-bit colour text, two rows of pixels to a line.
static void print_image(idfxx::image::view<const rgb888> picture) {
    for (size_t y = 0; y < picture.height(); y += 2) {
        std::string line;
        std::optional<rgb888> foreground;
        std::optional<rgb888> background;
        for (size_t x = 0; x < picture.width(); ++x) {
            const rgb888 upper = picture[x, y];
            const rgb888 lower = y + 1 < picture.height() ? picture[x, y + 1] : rgb888{};
            if (foreground != upper) {
                line += std::format("\x1b[38;2;{};{};{}m", upper.r, upper.g, upper.b);
                foreground = upper;
            }
            if (background != lower) {
                line += std::format("\x1b[48;2;{};{};{}m", lower.r, lower.g, lower.b);
                background = lower;
            }
            // An upper half block (U+2580): the foreground colour fills its top half, the background its bottom.
            line += "▀";
        }
        line += "\x1b[0m\n";
        fputs(line.c_str(), stdout);
    }
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
static void show_colour_preview() {
    logger.info("=== Colour preview ===");
    // A source reads forward only, and a decoder decodes once, so each section makes a new pair.
    idfxx::memory_source source{photo()};
    idfxx::jpeg::decoder decoder(source);
    const auto& info = decoder.info();

    // An orientation that turns the photo a quarter turn exchanges its width and height.
    const bool sideways = idfxx::image::swaps_axes(info.orientation);
    const size_t upright_width = sideways ? info.height : info.width;
    const size_t upright_height = sideways ? info.width : info.height;

    // The preview is PREVIEW_COLUMNS wide, with the upright photo's aspect ratio.
    const size_t preview_width = PREVIEW_COLUMNS;
    const size_t preview_height = PREVIEW_COLUMNS * upright_height / upright_width;

    // The decoder works in the stored orientation, so its target exchanges them back.
    const size_t target_width = sideways ? preview_height : preview_width;
    const size_t target_height = sideways ? preview_width : preview_height;
    decoder.scale_to_cover(target_width, target_height);
    logger.info("Decoding at {}x{}", decoder.output_width(), decoder.output_height());

    idfxx::image::buffer<> stored(decoder.output_width(), decoder.output_height());
    decoder.decode(stored);
    idfxx::image::buffer<> upright = idfxx::image::oriented(stored, info.orientation);

    // The smallest scale is 1/8, which can still leave more pixels than the preview has columns.
    if (upright.width() > preview_width) {
        upright = shrink(upright, preview_width, preview_height);
    }
    print_image(upright);
}

// 3. Decodes the photo at full size without ever holding more than one row of it.
static void show_streaming() {
    logger.info("=== Streaming ===");
    idfxx::memory_source source{photo()};
    idfxx::jpeg::decoder decoder(source);

    uint64_t red = 0;
    uint64_t green = 0;
    uint64_t blue = 0;
    size_t rows = 0;

    // The sink is called once per row, top to bottom in the stored orientation, with a block one
    // pixel high.
    auto add_row = [&](idfxx::image::point, idfxx::image::view<const rgb888> block) -> idfxx::result<void> {
        for (const rgb888& pixel : block.row(0)) {
            red += pixel.r;
            green += pixel.g;
            blue += pixel.b;
        }
        ++rows;
        return {};
    };
    decoder.decode(add_row);

    const size_t pixel_count = decoder.output_width() * decoder.output_height();
    logger.info("Average colour: ({}, {}, {})", red / pixel_count, green / pixel_count, blue / pixel_count);

    const size_t row_bytes = decoder.output_width() * sizeof(rgb888);
    const size_t image_bytes = pixel_count * sizeof(rgb888);
    logger.info(
        "Streamed {} rows of {} pixels; held {} bytes of pixels at a time instead of {}",
        rows,
        decoder.output_width(),
        row_bytes,
        image_bytes
    );
}

// Reads a JPEG's headers and decodes it at the smallest scale (1/8), passing any error on to the
// caller. The full-size photo would not fit in memory here, and errors are the same at any scale.
static idfxx::result<void> try_decode_small(idfxx::byte_source auto& source, idfxx::jpeg::decoder::config config = {}) {
    auto decoder = idfxx::jpeg::decoder::make(source, config);
    if (!decoder) {
        return idfxx::error(decoder.error());
    }
    // Any target smaller than 1/8 scale selects 1/8.
    decoder->scale_to_cover(1, 1);
    idfxx::image::buffer<> pixels(decoder->output_width(), decoder->output_height());
    return decoder->try_decode(pixels);
}

// Logs the outcome of a decode that is expected to fail with a given error.
static void report(const char* what, const idfxx::result<void>& outcome, std::error_code expected) {
    if (outcome) {
        logger.warn("{}: decoded without error", what);
        return;
    }
    const std::error_code error = outcome.error();
    logger.info("{}: {} (as expected: {})", what, error.message(), error == expected);
}

// 4. The result-based API, for data that might not decode.
static void show_error_handling() {
    logger.info("=== Error handling ===");

    // Bytes that aren't a JPEG, here the start of a PNG file, are rejected when the headers are read.
    {
        constexpr std::array<uint8_t, 8> png_signature{0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
        idfxx::memory_source source{png_signature};
        const auto outcome = try_decode_small(source);
        report("Not a JPEG", outcome, idfxx::jpeg::errc::invalid_data);
    }

    // A photo cut in half still has its headers, but the image data ends too soon.
    {
        const auto jpeg_bytes = photo();
        const auto first_half = jpeg_bytes.first(jpeg_bytes.size() / 2);
        idfxx::memory_source source{first_half};
        const auto outcome = try_decode_small(source);
        report("Truncated", outcome, idfxx::jpeg::errc::truncated);
    }

    // A progressive JPEG refines the whole picture scan by scan, so the decoder keeps every coefficient
    // until the last scan: about 1.4 MB here, at any scale. A memory limit rejects it before that is allocated.
    {
        idfxx::memory_source source{progressive_photo()};
        const auto outcome = try_decode_small(source, {.memory_limit = 64 * 1024});
        report("Progressive with a 64 KiB limit", outcome, idfxx::jpeg::errc::too_large);
    }

    // The destination must be exactly the output size.
    {
        idfxx::memory_source source{photo()};
        auto decoder = idfxx::jpeg::decoder::make(source);
        if (!decoder) {
            logger.error("Can't read the photo: {}", decoder.error().message());
            return;
        }
        idfxx::image::buffer<> wrong_size(16, 16);
        const auto outcome = decoder->try_decode(wrong_size);
        report("Wrong size destination", outcome, idfxx::errc::invalid_size);
    }
}

extern "C" void app_main() {
    try {
        show_header_info();
        show_colour_preview();
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
