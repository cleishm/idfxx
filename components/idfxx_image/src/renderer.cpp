// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#include "resampler.hpp"

#include <idfxx/image>
#include <idfxx/sched>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace idfxx::image {

namespace {

using detail::picture_layout;
using detail::resampler;
using detail::resampler_config;
using detail::resampler_plan;
using detail::source_rect;

constexpr uint64_t max_source_pixels = 100000000;
constexpr size_t max_source_side = 65535;

// Often enough for the idle task to satisfy the task watchdog, rarely enough to cost little time.
constexpr std::chrono::milliseconds yield_interval{500};

// An image as the renderer sees it: the pixels pushed, and the size the crop's
// coordinates refer to. The two differ when the image is decoded at a reduced scale.
struct source_image {
    size_t width;
    size_t height;
    size_t full_width;
    size_t full_height;
    orientation own_orientation;
};

// A rectangle by its edges, with room for any sum of an origin and a size.
struct box {
    int64_t x0;
    int64_t y0;
    int64_t x1;
    int64_t y1;

    [[nodiscard]] bool empty() const { return x1 <= x0 || y1 <= y0; }
    [[nodiscard]] int64_t width() const { return x1 - x0; }
    [[nodiscard]] int64_t height() const { return y1 - y0; }
};

box to_box(const rect& r) {
    return {r.x, r.y, r.x + static_cast<int64_t>(r.width), r.y + static_cast<int64_t>(r.height)};
}

box intersect(const box& a, const box& b) {
    return {std::max(a.x0, b.x0), std::max(a.y0, b.y0), std::min(a.x1, b.x1), std::min(a.y1, b.y1)};
}

// Everything derived from the arguments; nothing allocated yet.
struct render_geometry {
    orientation transform; // stored orientation to displayed orientation
    box output;            // on the screen; empty when the target is fully off-screen
    size picture;          // the whole picture, displayed orientation
    int64_t picture_x;     // the picture's origin relative to the output
    int64_t picture_y;
    box visible;               // the visible picture, relative to the output; may be empty
    resampler_config resample; // in the stored orientation; valid when visible isn't empty
};

bool needs_frame(orientation transform) {
    return transform != orientation::identity && transform != orientation::flip_horizontal;
}

bool source_too_large(size_t width, size_t height) {
    return width > max_source_side || height > max_source_side ||
        static_cast<uint64_t>(width) * height > max_source_pixels;
}

// The part of the displayed image to show, in pixels of the pushed image.
result<source_rect> displayed_crop(const std::optional<rect>& crop, const source_image& src, orientation transform) {
    const size pushed = oriented_size(transform, src.width, src.height);
    const float pushed_w = static_cast<float>(pushed.width);
    const float pushed_h = static_cast<float>(pushed.height);
    if (!crop) {
        return source_rect{0.0f, 0.0f, pushed_w, pushed_h};
    }
    const size full = oriented_size(transform, src.full_width, src.full_height);
    if (crop->width == 0 || crop->height == 0 || crop->x < 0 || crop->y < 0 ||
        static_cast<uint64_t>(crop->x) + crop->width > full.width ||
        static_cast<uint64_t>(crop->y) + crop->height > full.height) {
        return error(idfxx::errc::invalid_arg);
    }
    // Exactly 1 unless the image is scaled, which leaves full-size crops untouched.
    const float scale_x = pushed_w / static_cast<float>(full.width);
    const float scale_y = pushed_h / static_cast<float>(full.height);
    const float x = static_cast<float>(crop->x) * scale_x;
    const float y = static_cast<float>(crop->y) * scale_y;
    return source_rect{
        x,
        y,
        std::min(static_cast<float>(crop->width) * scale_x, pushed_w - x),
        std::min(static_cast<float>(crop->height) * scale_y, pushed_h - y),
    };
}

// Lays the crop out in an area the size of the target.
picture_layout lay_out(layout how, const source_rect& crop, size_t area_width, size_t area_height) {
    const uint32_t area_w = static_cast<uint32_t>(area_width);
    const uint32_t area_h = static_cast<uint32_t>(area_height);
    picture_layout l{};
    switch (how) {
    case layout::cover:
        l = detail::layout_cover(crop.w, crop.h, area_w, area_h);
        break;
    case layout::fit:
        l = detail::layout_fit(crop.w, crop.h, area_w, area_h);
        break;
    case layout::stretch:
        l.source = {0.0f, 0.0f, crop.w, crop.h};
        l.dst_w = area_w;
        l.dst_h = area_h;
        break;
    }
    l.source.x += crop.x;
    l.source.y += crop.y;
    return l;
}

// Applies the destination's alignment to the visible part of the target.
result<box> aligned_output(const box& visible, const placement& where, const render_config& config) {
    const int64_t a = static_cast<int64_t>(config.x_alignment);
    const int64_t screen_w = static_cast<int64_t>(config.screen_width);
    box aligned = visible;
    aligned.x0 = visible.x0 / a * a;
    if (visible.x1 < screen_w) {
        aligned.x1 = std::min((visible.x1 + a - 1) / a * a, screen_w);
    }
    if ((aligned.x0 != visible.x0 || aligned.x1 != visible.x1) && !where.fill_to_alignment) {
        return error(idfxx::errc::invalid_arg);
    }
    return aligned;
}

// The source rectangle in the stored orientation, given it in the displayed one.
source_rect to_stored(orientation transform, const source_rect& r, const source_image& src) {
    const bool swap = swaps_axes(transform);
    source_rect stored = swap ? source_rect{r.y, r.x, r.h, r.w} : r;
    // Which stored axes run backwards: where the displayed origin comes from.
    const point origin = source_point(transform, {0, 0}, 2, 2);
    if (origin.x != 0) {
        stored.x = std::max(static_cast<float>(src.width) - (stored.x + stored.w), 0.0f);
    }
    if (origin.y != 0) {
        stored.y = std::max(static_cast<float>(src.height) - (stored.y + stored.h), 0.0f);
    }
    return stored;
}

// The resampling that produces the visible picture, in the stored orientation.
resampler_config stored_resampling(
    const source_image& src,
    filter kernel,
    orientation transform,
    const source_rect& displayed_source,
    const size& picture,
    const box& window // the visible picture, in displayed picture coordinates
) {
    const size dst = oriented_size(transform, picture.width, picture.height);
    const point a = source_point(
        transform, {static_cast<size_t>(window.x0), static_cast<size_t>(window.y0)}, dst.width, dst.height
    );
    const point b = source_point(
        transform, {static_cast<size_t>(window.x1 - 1), static_cast<size_t>(window.y1 - 1)}, dst.width, dst.height
    );
    return {
        .src_width = static_cast<uint32_t>(src.width),
        .src_height = static_cast<uint32_t>(src.height),
        .source = to_stored(transform, displayed_source, src),
        .kernel = kernel,
        .dst_w = static_cast<uint32_t>(dst.width),
        .dst_h = static_cast<uint32_t>(dst.height),
        .col0 = static_cast<uint32_t>(std::min(a.x, b.x)),
        .vis_w = static_cast<uint32_t>(std::max(a.x, b.x) - std::min(a.x, b.x) + 1),
        .row0 = static_cast<uint32_t>(std::min(a.y, b.y)),
        .vis_h = static_cast<uint32_t>(std::max(a.y, b.y) - std::min(a.y, b.y) + 1),
    };
}

result<render_geometry> compute_geometry(const source_image& src, const placement& where, const render_config& config) {
    if (src.width == 0 || src.height == 0 || config.screen_width == 0 || config.screen_height == 0 ||
        config.x_alignment == 0) {
        return error(idfxx::errc::invalid_arg);
    }
    const rect target = where.target.value_or(rect{0, 0, config.screen_width, config.screen_height});
    if (target.width == 0 || target.height == 0) {
        return error(idfxx::errc::invalid_arg);
    }

    render_geometry g{};
    g.transform = compose(src.own_orientation, where.orientation);

    // Validated even for an off-screen target, whose placement is just as wrong.
    auto crop = displayed_crop(where.crop, src, g.transform);
    if (!crop) {
        return error(crop.error());
    }

    const box screen{0, 0, static_cast<int64_t>(config.screen_width), static_cast<int64_t>(config.screen_height)};
    const box visible_target = intersect(to_box(target), screen);
    if (visible_target.empty()) {
        return g;
    }
    auto output = aligned_output(visible_target, where, config);
    if (!output) {
        return error(output.error());
    }
    g.output = *output;

    const picture_layout l = lay_out(where.layout, *crop, target.width, target.height);
    const box picture_on_screen{
        target.x + static_cast<int64_t>(l.dst_x),
        target.y + static_cast<int64_t>(l.dst_y),
        target.x + static_cast<int64_t>(l.dst_x) + l.dst_w,
        target.y + static_cast<int64_t>(l.dst_y) + l.dst_h,
    };
    const box visible = intersect(picture_on_screen, screen);
    g.picture = {l.dst_w, l.dst_h};
    g.picture_x = picture_on_screen.x0 - g.output.x0;
    g.picture_y = picture_on_screen.y0 - g.output.y0;
    if (visible.empty()) {
        return g;
    }
    g.visible = {
        visible.x0 - g.output.x0,
        visible.y0 - g.output.y0,
        visible.x1 - g.output.x0,
        visible.y1 - g.output.y0,
    };
    const box window{
        visible.x0 - picture_on_screen.x0,
        visible.y0 - picture_on_screen.y0,
        visible.x1 - picture_on_screen.x0,
        visible.y1 - picture_on_screen.y0,
    };
    g.resample = stored_resampling(src, where.filter, g.transform, l.source, g.picture, window);
    return g;
}

// The smallest side of a decode that gives the resampling a whole pixel per output
// pixel. The tolerance keeps a float extent's rounding from asking for a larger decode.
size_t covering_side(uint32_t side, uint32_t dst, float extent) {
    const double needed = static_cast<double>(side) * dst / static_cast<double>(extent);
    return static_cast<size_t>(std::ceil(std::min(needed, static_cast<double>(side)) - 1.0 / 1024));
}

} // namespace

struct renderer::state {
    render_geometry geometry;
    uint32_t source_width;
    uint32_t source_height;
    std::optional<resampler> resampling; // absent when no picture is visible
    std::vector<rgb888> row;             // the row being delivered, or the resampled row bound for the frame
    std::vector<rgb888> frame;           // output-sized, for orientations that need one
    rgb888 background;
    uint32_t next_source_row = 0;
    uint32_t next_output_row = 0;
    uint32_t next_resampled_row = 0;
    bool failed = false;
    std::chrono::steady_clock::time_point last_yield = std::chrono::steady_clock::now();

    [[nodiscard]] bool buffered() const { return !frame.empty(); }
    [[nodiscard]] const rgb888* next_streamed_row();
    [[nodiscard]] const rgb888* next_frame_row();
    void store_resampled_row();
    void yield_if_due();
};

const rgb888* renderer::state::next_streamed_row() {
    const int64_t y = next_output_row;
    if (y >= geometry.visible.y0 && y < geometry.visible.y1) {
        // The gap around the picture is background from every earlier row, or from make().
        rgb888* picture_row = row.data() + geometry.visible.x0;
        if (!resampling->pop(picture_row)) {
            return nullptr;
        }
        if (geometry.transform == orientation::flip_horizontal) {
            std::reverse(picture_row, picture_row + geometry.visible.width());
        }
    } else {
        std::fill(row.begin(), row.end(), background);
    }
    return row.data();
}

const rgb888* renderer::state::next_frame_row() {
    if (next_source_row < source_height) {
        return nullptr;
    }
    return frame.data() + static_cast<size_t>(next_output_row) * static_cast<size_t>(geometry.output.width());
}

// Copies a resampled row, in the stored orientation, to where it is displayed.
void renderer::state::store_resampled_row() {
    const resampler_config& rs = geometry.resample;
    const orientation back = inverse(geometry.transform);
    const size_t stride = static_cast<size_t>(geometry.output.width());
    const size_t y = rs.row0 + next_resampled_row++;
    const auto frame_index = [&](size_t x) {
        const point p = source_point(back, {x, y}, geometry.picture.width, geometry.picture.height);
        return (static_cast<int64_t>(p.y) + geometry.picture_y) * static_cast<int64_t>(stride) +
            static_cast<int64_t>(p.x) + geometry.picture_x;
    };
    int64_t at = frame_index(rs.col0);
    const int64_t step = rs.vis_w > 1 ? frame_index(rs.col0 + 1) - at : 0;
    for (uint32_t j = 0; j < rs.vis_w; ++j) {
        frame[static_cast<size_t>(at)] = row[j];
        at += step;
    }
}

void renderer::state::yield_if_due() {
    if (std::chrono::steady_clock::now() - last_yield < yield_interval) {
        return;
    }
    delay(next_tick);
    last_yield = std::chrono::steady_clock::now();
}

namespace detail {

result<decode_plan>
plan_decode(size_t width, size_t height, orientation source, const placement& where, const render_config& config) {
    auto geometry = compute_geometry({width, height, width, height, source}, where, config);
    if (!geometry) {
        return error(geometry.error());
    }
    if (geometry->output.empty()) {
        return decode_plan{.visible = false, .cover_size = {0, 0}};
    }
    if (geometry->visible.empty()) {
        // Only background is output, and the rows just have to pass through.
        return decode_plan{.visible = true, .cover_size = {1, 1}};
    }
    const resampler_config& rs = geometry->resample;
    return decode_plan{
        .visible = true,
        .cover_size = {
            covering_side(rs.src_width, rs.dst_w, rs.source.w),
            covering_side(rs.src_height, rs.dst_h, rs.source.h),
        },
    };
}

result<renderer> make_decoded_renderer(
    size_t decoded_width,
    size_t decoded_height,
    size_t full_width,
    size_t full_height,
    orientation source,
    const placement& where,
    const render_config& config
) {
    auto geometry = compute_geometry({decoded_width, decoded_height, full_width, full_height, source}, where, config);
    if (!geometry) {
        return error(geometry.error());
    }
    if (source_too_large(decoded_width, decoded_height)) {
        return error(idfxx::errc::invalid_size);
    }

    const render_geometry& g = *geometry;
    const bool has_picture = !g.visible.empty();
    const bool has_frame = has_picture && needs_frame(g.transform);
    const uint64_t output_pixels = static_cast<uint64_t>(g.output.width()) * static_cast<uint64_t>(g.output.height());
    const uint64_t row_pixels = has_frame ? g.resample.vis_w : static_cast<uint64_t>(g.output.width());
    const uint64_t frame_pixels = has_frame ? output_pixels : 0;

    std::optional<resampler_plan> plan;
    if (has_picture) {
        plan = resampler::plan(g.resample);
    }
    // An off-screen target allocates nothing to speak of, so no limit rejects it.
    const uint64_t bytes = g.output.empty() ? 0
                                            : sizeof(renderer::state) + (plan ? resampler::memory_bytes(*plan) : 0) +
            (row_pixels + frame_pixels) * sizeof(rgb888);
    if (config.memory_limit && bytes > *config.memory_limit) {
        return error(errc::too_large);
    }
    if (bytes > SIZE_MAX) {
        return error(idfxx::errc::invalid_size);
    }

    auto s = std::make_unique<renderer::state>();
    s->geometry = g;
    s->background = where.background;
    s->source_width = static_cast<uint32_t>(decoded_width);
    s->source_height = static_cast<uint32_t>(decoded_height);
    if (plan) {
        s->resampling.emplace(*plan);
    }
    s->row.assign(static_cast<size_t>(row_pixels), where.background);
    s->frame.assign(static_cast<size_t>(frame_pixels), where.background);

    const rect output{
        .x = static_cast<int32_t>(g.output.x0),
        .y = static_cast<int32_t>(g.output.y0),
        .width = static_cast<size_t>(g.output.width()),
        .height = static_cast<size_t>(g.output.height()),
    };
    return renderer(std::move(s), output);
}

result<renderer> make_offscreen_renderer(size_t width, size_t height) {
    auto s = std::make_unique<renderer::state>();
    s->source_width = static_cast<uint32_t>(width);
    s->source_height = static_cast<uint32_t>(height);
    return renderer(std::move(s), rect{});
}

} // namespace detail

result<renderer>
renderer::make(size_t width, size_t height, orientation source, const placement& where, const render_config& config) {
    return detail::make_decoded_renderer(width, height, width, height, source, where, config);
}

renderer::renderer(std::unique_ptr<state> s, rect output) noexcept
    : _state(std::move(s))
    , _output(output) {}

renderer::renderer(renderer&& other) noexcept = default;
renderer& renderer::operator=(renderer&& other) noexcept = default;
renderer::~renderer() = default;

result<void> renderer::_check_rows(point at, const view<const rgb888>& rows) const {
    if (!_state || _state->failed) {
        return error(idfxx::errc::invalid_state);
    }
    if (rows.width() != _state->source_width) {
        return error(idfxx::errc::invalid_size);
    }
    if (at.x != 0 || at.y != _state->next_source_row || rows.height() > _state->source_height - at.y) {
        return error(idfxx::errc::invalid_state);
    }
    return {};
}

void renderer::_accept_row(const rgb888* row) {
    state& s = *_state;
    s.yield_if_due();
    ++s.next_source_row;
    if (!s.resampling) {
        return;
    }
    s.resampling->push(row);
    if (s.buffered()) {
        while (s.resampling->pop(s.row.data())) {
            s.store_resampled_row();
        }
    }
}

std::optional<renderer::output_row> renderer::_next_row() {
    state& s = *_state;
    if (s.next_output_row >= _output.height) {
        return std::nullopt;
    }
    const rgb888* pixels = s.buffered() ? s.next_frame_row() : s.next_streamed_row();
    if (pixels == nullptr) {
        return std::nullopt;
    }
    s.yield_if_due();
    const point at{static_cast<size_t>(_output.x), static_cast<size_t>(_output.y) + s.next_output_row};
    ++s.next_output_row;
    return output_row{at, view<const rgb888>(pixels, _output.width, 1)};
}

void renderer::_abort() noexcept {
    _state->failed = true;
}

} // namespace idfxx::image
