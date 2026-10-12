// SPDX-License-Identifier: Apache-2.0

#include <idfxx/image>
#include <idfxx/log>
#include <idfxx/memory>
#include <idfxx/sched>

#include <algorithm>
#include <array>
#include <chrono>
#include <format>
#include <string>
#include <string_view>
#include <utility>

using namespace std::chrono_literals;
namespace image = idfxx::image;
using image::orientation;
using image::rgb888;

static constexpr idfxx::log::logger logger{"example"};

static constexpr rgb888 BACKGROUND{0, 0, 0};
static constexpr rgb888 INK{255, 255, 255};
static constexpr rgb888 MARKER{255, 0, 0}; // the source's top-left pixel
static constexpr rgb888 GAP{64, 64, 64};   // empty space on the contact sheet

// An asymmetric "F" glyph: 'o' marks the top-left pixel, '#' is ink, '.' is background.
static constexpr std::array<std::string_view, 7> GLYPH{
    "o####",
    "#....",
    "#....",
    "###..",
    "#....",
    "#....",
    "#....",
};

// All eight orientations, in EXIF tag order.
static constexpr std::array<orientation, 8> ORIENTATIONS{
    orientation::identity,
    orientation::flip_horizontal,
    orientation::rotate_180,
    orientation::flip_vertical,
    orientation::transpose,
    orientation::rotate_90,
    orientation::transverse,
    orientation::rotate_270,
};

// The contact sheet: two rows of four cells, numbered by EXIF tag. Each cell fits the 5x7 glyph
// either way up, leaving a gap to its right and below.
static constexpr size_t CELL = 8;
static constexpr size_t COLUMNS = 4;
static constexpr size_t ROWS = 2;

// The pixel colour for a GLYPH character.
static rgb888 from_char(char c) {
    switch (c) {
    case 'o':
        return MARKER;
    case '#':
        return INK;
    default:
        return BACKGROUND;
    }
}

// The character that prints a pixel.
static char to_char(const rgb888& pixel) {
    if (pixel == MARKER) {
        return 'o';
    }
    if (pixel == INK) {
        return '#';
    }
    if (pixel == GAP) {
        return ' ';
    }
    return '.';
}

// The orientation's enumerator name.
static std::string_view name_of(orientation o) {
    switch (o) {
    case orientation::identity:
        return "identity";
    case orientation::flip_horizontal:
        return "flip_horizontal";
    case orientation::rotate_180:
        return "rotate_180";
    case orientation::flip_vertical:
        return "flip_vertical";
    case orientation::transpose:
        return "transpose";
    case orientation::rotate_90:
        return "rotate_90";
    case orientation::transverse:
        return "transverse";
    case orientation::rotate_270:
        return "rotate_270";
    }
    return "?";
}

// Prints an image as ASCII art, one character per pixel, a row at a time.
static void print_rows(image::view<const rgb888> img) {
    for (size_t y = 0; y < img.height(); ++y) {
        std::string line;
        for (const rgb888& pixel : img.row(y)) {
            line += to_char(pixel);
        }
        logger.info("  {}", line);
    }
}

// Prints a title and the image's size, then the image.
static void print_image(std::string_view title, image::view<const rgb888> img) {
    logger.info("{} ({}x{})", title, img.width(), img.height());
    print_rows(img);
}

// Pixel-for-pixel equality: neither buffer nor view provides operator==.
static bool same_pixels(image::view<const rgb888> a, image::view<const rgb888> b) {
    if (a.width() != b.width() || a.height() != b.height()) {
        return false;
    }
    for (size_t y = 0; y < a.height(); ++y) {
        if (!std::ranges::equal(a.row(y), b.row(y))) {
            return false;
        }
    }
    return true;
}

// The top-left corner of the contact sheet cell for ORIENTATIONS[index].
static image::point cell_corner(size_t index) {
    return {index % COLUMNS * CELL, index / COLUMNS * CELL};
}

// The EXIF tags of one row of contact sheet cells, each above its cell.
static std::string cell_labels(size_t row) {
    std::string labels;
    for (size_t column = 0; column < COLUMNS; ++column) {
        const size_t tag = row * COLUMNS + column + 1;
        labels += std::format("{:<{}}", tag, CELL);
    }
    return labels;
}

// The source image: the glyph, written a pixel at a time with operator[].
static image::buffer<> make_source() {
    const size_t width = GLYPH[0].size();
    const size_t height = GLYPH.size();
    image::buffer<> source(width, height);
    for (size_t y = 0; y < height; ++y) {
        for (size_t x = 0; x < width; ++x) {
            source[x, y] = from_char(GLYPH[y][x]);
        }
    }
    return source;
}

// 1. Draws all eight orientations side by side, each into its own region of one image.
static void show_contact_sheet(const image::buffer<>& source) {
    logger.info("=== Contact Sheet ===");
    image::buffer<> sheet(COLUMNS * CELL, ROWS * CELL, GAP);

    // orient() writes straight into a subview of the sheet. The subview must have the oriented
    // size, which swaps_axes() says is 7x5 rather than 5x7 for half of the orientations.
    for (size_t index = 0; index < ORIENTATIONS.size(); ++index) {
        const orientation o = ORIENTATIONS[index];
        const bool swapped = image::swaps_axes(o);
        const size_t width = swapped ? source.height() : source.width();
        const size_t height = swapped ? source.width() : source.height();
        const image::point corner = cell_corner(index);
        const image::view<rgb888> cell = sheet.subview(corner.x, corner.y, width, height);
        image::orient(source, cell, o);
    }

    // Print a row of cells at a time, under their EXIF tags. The bottom pixel row of every cell
    // is gap, so it is left out.
    for (size_t row = 0; row < ROWS; ++row) {
        logger.info("  {}", cell_labels(row));
        const size_t top = row * CELL;
        // A subview keeps the sheet's stride, and a mutable view passes as a read-only one.
        const image::view<rgb888> band = sheet.subview(0, top, sheet.width(), CELL - 1);
        print_rows(band);
    }

    // An orientation's value is its EXIF tag.
    for (const orientation o : ORIENTATIONS) {
        logger.info("  {} = {}", std::to_underlying(o), name_of(o));
    }
}

// 2. Gets an oriented image as a new buffer, rather than drawing into an existing one.
static void show_oriented(const image::buffer<>& source) {
    logger.info("=== oriented() ===");
    const image::buffer<> upright = image::oriented(source, orientation::rotate_90);
    print_image("rotate_90", upright);

    // The same, with the result placed in internal DRAM (no PSRAM needed).
    const auto in_dram = image::oriented(source, orientation::rotate_90, idfxx::dram_allocator<rgb888>());
    const bool matches = same_pixels(upright, in_dram);
    logger.info("DRAM allocator result matches: {}", matches);

    // A strided view of the glyph's top two rows, oriented on its own.
    const image::view<const rgb888> top_rows = source.subview(0, 0, source.width(), 2);
    const image::buffer<> top_rotated = image::oriented(top_rows, orientation::rotate_180);
    print_image("top two rows, rotate_180", top_rotated);
}

// 3. Fuses an EXIF orientation with the display's rotation, so the image is oriented once.
static void show_compose(const image::buffer<>& source) {
    logger.info("=== compose() ===");
    const orientation exif = orientation::flip_horizontal; // the photo was shot mirrored
    const orientation panel = orientation::rotate_90;      // the panel is mounted rotated
    const orientation fused = image::compose(exif, panel);
    logger.info("compose({}, {}) = {}", name_of(exif), name_of(panel), name_of(fused));

    const auto first_pass = image::oriented(source, exif);
    const auto two_passes = image::oriented(first_pass, panel);
    const auto one_pass = image::oriented(source, fused);
    const bool same = same_pixels(one_pass, two_passes);
    logger.info("One pass equals two passes: {}", same);
}

// 4. Undoes each orientation. Only the quarter turns need a different orientation to undo them;
// the rest undo themselves.
static void show_inverse(const image::buffer<>& source) {
    logger.info("=== inverse() ===");
    size_t restored = 0;
    for (const orientation o : ORIENTATIONS) {
        const orientation undo = image::inverse(o);
        const auto applied = image::oriented(source, o);
        const auto undone = image::oriented(applied, undo);
        const bool matches = same_pixels(undone, source);
        if (matches) {
            ++restored;
        }
        logger.info("{} then {}: {}", name_of(o), name_of(undo), matches ? "restored" : "MISMATCH");
    }
    logger.info("Round trips restored: {}/8", restored);
}

// 5. Finds which source pixel appears at the displayed top-left, without orienting the image.
static void show_source_point(const image::buffer<>& source) {
    logger.info("=== source_point() ===");
    for (const orientation o : ORIENTATIONS) {
        const image::point from = image::source_point(o, {0, 0}, source.width(), source.height());

        // Check the answer against the oriented image.
        const auto shown = image::oriented(source, o);
        const bool agrees = source[from.x, from.y] == shown[0, 0];
        logger.info("{}: displayed (0, 0) is source ({}, {}) [{}]", name_of(o), from.x, from.y, agrees ? "ok" : "BAD");
    }
}

extern "C" void app_main() {
    const image::buffer<> source = make_source();
    print_image("Source", source);

    show_contact_sheet(source);
    show_oriented(source);
    show_compose(source);
    show_inverse(source);
    show_source_point(source);

    logger.info("Done!");

    while (true) {
        idfxx::delay(1h);
    }
}
