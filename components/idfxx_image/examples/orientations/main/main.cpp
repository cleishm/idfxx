// SPDX-License-Identifier: Apache-2.0

// Shows a photo the right way up on a panel that is mounted rotated, working on images already in
// memory. To decode an image straight to a display, use idfxx::image::render(), which
// applies these orientations for you.

#include <idfxx/image>
#include <idfxx/log>
#include <idfxx/memory>
#include <idfxx/sched>

#include <algorithm>
#include <chrono>
#include <string>
#include <string_view>

using namespace std::chrono_literals;
namespace image = idfxx::image;
using image::orientation;
using image::rgb888;

static constexpr idfxx::log::logger logger{"example"};

static constexpr rgb888 PAPER{255, 255, 255};
static constexpr rgb888 INK{0, 0, 0};
static constexpr rgb888 BACKDROP{64, 64, 64};
static constexpr rgb888 MARKER{255, 0, 0};

// The photo: a letter F, which looks different under each of the eight orientations.
static image::buffer<> make_photo() {
    image::buffer<> photo(5, 7, PAPER);
    for (size_t y = 0; y < photo.height(); ++y) {
        photo[0, y] = INK;
    }
    std::ranges::fill(photo.row(0), INK);
    std::ranges::fill(photo.subview(0, 3, 3, 1).row(0), INK);
    return photo;
}

// Prints an image to the serial monitor, one character per pixel.
static void print_image(std::string_view title, image::view<const rgb888> img) {
    logger.info("{} ({}x{})", title, img.width(), img.height());
    for (size_t y = 0; y < img.height(); ++y) {
        std::string line;
        for (const rgb888& pixel : img.row(y)) {
            line += pixel == INK ? '#' : pixel == MARKER ? 'o' : pixel == BACKDROP ? ':' : '.';
        }
        logger.info("  {}", line);
    }
}

extern "C" void app_main() {
    const image::buffer<> photo = make_photo();
    print_image("Photo as stored", photo);

    // The photo needs one transform to undo how it was stored and the panel needs another to undo
    // how it is mounted. Combining them moves the pixels only once.
    const orientation exif = orientation::flip_horizontal; // from the photo's metadata
    const orientation panel = orientation::rotate_90;      // how the panel is mounted
    const orientation o = image::compose(exif, panel);

    // Get the result as a new image, here allocated from internal DRAM...
    const auto rotated = image::oriented(photo, o, idfxx::dram_allocator<rgb888>());
    print_image("New image", rotated);

    // ...or write it straight into a region of an existing image, with no allocation. The region
    // must have the oriented size, which swaps the photo's width and height here.
    image::buffer<> screen(15, 9, BACKDROP);
    const image::size shown = image::oriented_size(o, photo.width(), photo.height());
    const size_t left = (screen.width() - shown.width) / 2;
    const size_t top = (screen.height() - shown.height) / 2;
    image::orient(photo, screen.subview(left, top, shown.width, shown.height), o);

    // Find the photo pixel under a touch on the screen. The position must be inside the photo, so
    // a real application would check that first.
    const image::point touch{8, 6};
    const image::point hit =
        image::source_point(o, {touch.x - left, touch.y - top}, photo.width(), photo.height());
    logger.info(
        "Touch at screen ({}, {}) is on photo pixel ({}, {})", touch.x, touch.y, hit.x, hit.y
    );

    // Going the other way, find where a photo pixel landed, to mark its top-left corner. The
    // displayed image is the source here, and the inverse orientation leads back to the photo.
    const image::point corner =
        image::source_point(image::inverse(o), {0, 0}, shown.width, shown.height);
    screen[left + corner.x, top + corner.y] = MARKER;
    print_image("Screen, with the photo's top-left corner marked", screen);

    while (true) {
        idfxx::delay(1h);
    }
}
