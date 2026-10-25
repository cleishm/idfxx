// SPDX-License-Identifier: Apache-2.0

#include <idfxx/error>
#include <idfxx/image>
#include <idfxx/log>
#include <idfxx/sched>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>
#include <system_error>

using namespace std::chrono_literals;
namespace image = idfxx::image;
using image::rgb888;

static constexpr idfxx::log::logger logger{"example"};

// The producer delivers the image in blocks of TILE x TILE pixels.
static constexpr size_t TILE = 8;

// The size of the generated pattern. Neither is a multiple of TILE, so the blocks on the right and
// bottom edges are smaller.
static constexpr size_t WIDTH = 27;
static constexpr size_t HEIGHT = 19;

// Prints an image as coarse ASCII art, one character per pixel, by brightness.
static void print_image(image::view<const rgb888> img) {
    static constexpr std::string_view ramp = " .:-=+*#%@";
    for (size_t y = 0; y < img.height(); ++y) {
        std::string line;
        for (const rgb888& pixel : img.row(y)) {
            const unsigned brightness = (pixel.r + pixel.g + pixel.b) / 3;
            line += ramp[brightness * (ramp.size() - 1) / 255];
        }
        logger.info("  {}", line);
    }
}

// The colour of pixel (x, y) in a pattern `width` pixels wide: a left-to-right
// gradient, grey on alternate tiles and green on the others, like a checkerboard.
static rgb888 pattern_pixel(size_t x, size_t y, size_t width) {
    const auto level = static_cast<uint8_t>(x * 255 / (width - 1));
    const bool grey_tile = ((x / TILE) + (y / TILE)) % 2 == 0;
    if (grey_tile) {
        return {level, level, level};
    }
    return {0, static_cast<uint8_t>(255 - level), 0};
}

// --- The producer side: what an image decoder does ---------------------------

// Generates the pattern and delivers it to `sink` as TILE x TILE blocks in
// raster order. Stops at the first error from the sink and returns it.
template<image::sink<rgb888> Sink>
idfxx::result<void> generate_pattern(size_t width, size_t height, Sink&& sink) {
    image::buffer<> tile(TILE, TILE); // reused for every block, like a decoder's MCU row buffer
    for (size_t top = 0; top < height; top += TILE) {
        for (size_t left = 0; left < width; left += TILE) {
            // Fill the tile with this block's pixels. Blocks on the right and
            // bottom edges are smaller, and use only part of the tile.
            const size_t block_width = std::min(TILE, width - left);
            const size_t block_height = std::min(TILE, height - top);
            for (size_t y = 0; y < block_height; ++y) {
                for (size_t x = 0; x < block_width; ++x) {
                    tile[x, y] = pattern_pixel(left + x, top + y, width);
                }
            }

            // Hand the sink a read-only view of the block and where it goes.
            const image::view<const rgb888> block = tile.subview(0, 0, block_width, block_height);
            const idfxx::result<void> accepted = sink(image::point{left, top}, block);
            if (!accepted) {
                return accepted;
            }
        }
    }
    return {};
}

// --- The consumer side: sinks that receive the blocks -----------------------

// Returns a sink that composites blocks into `canvas`, shifted by `offset`, and
// refuses any block that doesn't fit.
static auto composite_into(image::buffer<>& canvas, image::point offset) {
    return [&canvas, offset](image::point at, image::view<const rgb888> block) -> idfxx::result<void> {
        const size_t left = offset.x + at.x;
        const size_t top = offset.y + at.y;
        if (left + block.width() > canvas.width() || top + block.height() > canvas.height()) {
            return idfxx::error(idfxx::errc::invalid_size);
        }
        for (size_t y = 0; y < block.height(); ++y) {
            const auto destination = canvas.row(top + y).begin() + left;
            std::ranges::copy(block.row(y), destination);
        }
        return {};
    };
}

// 1. A lambda sink places the pattern in a larger image.
static void show_composite() {
    logger.info("=== Composite ===");
    // The pattern goes in the middle of the canvas, with a margin all round.
    image::buffer<> canvas(WIDTH + 8, HEIGHT + 4);
    auto compositor = composite_into(canvas, {4, 2});
    // Fails to compile here, rather than inside the producer, if this isn't a valid sink.
    static_assert(image::sink<decltype(compositor)>);

    const idfxx::result<void> delivered = generate_pattern(WIDTH, HEIGHT, compositor);
    if (!delivered) {
        throw std::system_error(delivered.error());
    }
    print_image(canvas);

    // A block that falls outside the canvas is refused, and that stops the producer.
    auto too_far = composite_into(canvas, {20, 10});
    const idfxx::result<void> outcome = generate_pattern(WIDTH, HEIGHT, too_far);
    logger.info("Composite at (20, 10): {}", outcome ? "ok" : outcome.error().message());
}

// Accumulates statistics over every block it is given.
struct block_statistics {
    size_t blocks = 0;
    size_t pixels = 0;
    uint64_t sum_r = 0;
    uint64_t sum_g = 0;
    uint64_t sum_b = 0;

    idfxx::result<void> operator()(image::point, image::view<const rgb888> block) {
        ++blocks;
        for (size_t y = 0; y < block.height(); ++y) {
            for (const rgb888& pixel : block.row(y)) {
                sum_r += pixel.r;
                sum_g += pixel.g;
                sum_b += pixel.b;
                ++pixels;
            }
        }
        return {};
    }

    rgb888 average() const {
        if (pixels == 0) {
            return {};
        }
        return {
            static_cast<uint8_t>(sum_r / pixels),
            static_cast<uint8_t>(sum_g / pixels),
            static_cast<uint8_t>(sum_b / pixels)
        };
    }
};
// Fails to compile here, rather than inside the producer, if this isn't a valid sink.
static_assert(image::sink<block_statistics>);

// 2. A function object sink gathers statistics as the blocks go by.
static void show_statistics() {
    logger.info("=== Statistics ===");
    // The producer takes the sink by reference, so `stats` holds the totals afterwards.
    block_statistics stats;
    const idfxx::result<void> delivered = generate_pattern(WIDTH, HEIGHT, stats);
    if (!delivered) {
        throw std::system_error(delivered.error());
    }
    const rgb888 average = stats.average();
    logger.info(
        "{} blocks, {} pixels, average colour ({}, {}, {})", stats.blocks, stats.pixels, average.r, average.g, average.b
    );
}

// Takes `limit` blocks, then refuses the next one.
struct stop_after {
    size_t limit;
    size_t taken = 0;

    idfxx::result<void> operator()(image::point, image::view<const rgb888>) {
        if (taken == limit) {
            return idfxx::error(idfxx::errc::not_finished);
        }
        ++taken;
        return {};
    }
};
// Fails to compile here, rather than inside the producer, if this isn't a valid sink.
static_assert(image::sink<stop_after>);

// 3. A sink stops the producer early by returning an error, and the producer
// returns that same error to its caller.
static void show_stop_early() {
    logger.info("=== Stop Early ===");
    stop_after three{3};
    const idfxx::result<void> outcome = generate_pattern(WIDTH, HEIGHT, three);
    if (!outcome) {
        const bool same_error = outcome.error() == idfxx::errc::not_finished;
        logger.info(
            "Producer stopped after {} blocks with: {} (same error: {})",
            three.taken,
            outcome.error().message(),
            same_error
        );
    }
}

extern "C" void app_main() {
    try {
        show_composite();
        show_statistics();
        show_stop_early();
        logger.info("Done!");
    } catch (const std::system_error& e) {
        logger.error("Error: {}", e.what());
    }

    while (true) {
        idfxx::delay(1h);
    }
}
