// SPDX-License-Identifier: Apache-2.0

// Makes a new kind of image source work with the renderer, and with anything else that takes an
// image::decoder, such as the LCD and e-paper image writers. A real decoder would read a file
// format; this one draws a disc.

#include <idfxx/error>
#include <idfxx/image>
#include <idfxx/log>
#include <idfxx/sched>

#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>
#include <system_error>

using namespace std::chrono_literals;
namespace image = idfxx::image;
using image::rgb888;

static constexpr idfxx::log::logger logger{"example"};

static constexpr rgb888 DISC{255, 255, 255};
static constexpr rgb888 FIELD{0, 0, 0};
static constexpr rgb888 BACKGROUND{64, 64, 64};

class disc_decoder {
public:
    // What a decoder must report before decoding.
    struct properties {
        size_t width;
        size_t height;
        image::orientation orientation;
    };

    [[nodiscard]] properties info() const { return {WIDTH, HEIGHT, image::orientation::identity}; }

    // Delivers the image as full-width rows, from the top row down, holding only one row in memory.
    template<image::sink Sink>
    [[nodiscard]] idfxx::result<void> try_decode(Sink&& sink) {
        image::buffer<> row(WIDTH, 1);
        for (size_t y = 0; y < HEIGHT; ++y) {
            for (size_t x = 0; x < WIDTH; ++x) {
                row[x, 0] = _pixel_at(x, y);
            }
            const image::view<const rgb888> pixels = row;
            // An error from the sink stops the decode, and is returned unchanged.
            if (auto accepted = sink(image::point{0, y}, pixels); !accepted) {
                return accepted;
            }
        }
        return {};
    }

private:
    static constexpr size_t WIDTH = 64;
    static constexpr size_t HEIGHT = 48;
    static constexpr int32_t RADIUS = 20;

    static rgb888 _pixel_at(size_t x, size_t y) {
        const int32_t dx = static_cast<int32_t>(x) - WIDTH / 2;
        const int32_t dy = static_cast<int32_t>(y) - HEIGHT / 2;
        return dx * dx + dy * dy <= RADIUS * RADIUS ? DISC : FIELD;
    }
};
// Fails to compile here, rather than at the call to render(), if this isn't a valid decoder.
static_assert(image::decoder<disc_decoder>);

// A sink that prints rows as they arrive, one character per pixel, by brightness.
static idfxx::result<void> print_rows(image::point, image::view<const rgb888> rows) {
    static constexpr std::string_view ramp = " .:-=+*#%@";
    for (size_t y = 0; y < rows.height(); ++y) {
        std::string line;
        for (const rgb888& pixel : rows.row(y)) {
            const unsigned brightness = (pixel.r + pixel.g + pixel.b) / 3;
            line += ramp[brightness * (ramp.size() - 1) / 255];
        }
        logger.info("  {}", line);
    }
    return {};
}

extern "C" void app_main() {
    // Scale the 64x48 image down to fit a 32x12 screen, centered on the background. The default
    // filter is sharper, but ripples next to hard edges, which would make the printout noisy.
    const image::placement where{
        .layout = image::layout::fit,
        .background = BACKGROUND,
        .filter = image::filter::box,
    };
    const image::render_config screen{.screen_width = 32, .screen_height = 12};

    try {
        disc_decoder decoder;
        image::render(decoder, print_rows, where, screen);
    } catch (const std::system_error& e) {
        logger.error("Error: {}", e.what());
    }

    // A sink can also stop the decode early, for example because the user moved to another screen.
    // The error it returns comes back from the render.
    size_t rows_wanted = 3;
    auto first_rows_only = [&](image::point, image::view<const rgb888>) -> idfxx::result<void> {
        if (rows_wanted == 0) {
            return idfxx::error(idfxx::errc::not_finished);
        }
        --rows_wanted;
        return {};
    };
    disc_decoder decoder;
    const idfxx::result<void> outcome = image::try_render(decoder, first_rows_only, where, screen);
    logger.info("Stopping early: {}", outcome ? "no error" : outcome.error().message());

    while (true) {
        idfxx::delay(1h);
    }
}
