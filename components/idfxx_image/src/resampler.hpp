// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Streaming separable resampler, private to idfxx_image. See resampler.cpp.

#pragma once

#include <idfxx/image>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace idfxx::image::detail {

// A rectangle of source pixels. Float, because the resampler's arithmetic is
// defined on float extents.
struct source_rect {
    float x;
    float y;
    float w;
    float h;
};

// Resampling a source rectangle to dst_w x dst_h, of which only a window is
// computed. Each output pixel depends only on its own coefficients, so the
// window changes nothing else.
struct resampler_config {
    uint32_t src_width;
    uint32_t src_height;
    source_rect source;
    filter kernel;
    uint32_t dst_w;
    uint32_t dst_h;
    uint32_t col0;  // first computed column of dst_w
    uint32_t vis_w; // computed columns; at least 1
    uint32_t row0;  // first computed row of dst_h
    uint32_t vis_h; // computed rows; at least 1
};

// One axis: its coefficients, one output index at a time.
struct resampler_axis {
    bool conv;        // convolution; otherwise a copy
    int32_t off;      // copy: source index of output index 0
    uint32_t in_size; // conv: source extent along the axis
    uint32_t max_n;   // conv: largest window bounds() returns
    double in0;
    double scale;
    double support;
    double inv_fs;
};

// Every size derived from a config, without allocating anything.
struct resampler_plan {
    resampler_axis h;
    resampler_axis v;
    bool vh; // tall path: vertical pass before horizontal
    filter kernel;
    uint32_t col0;
    uint32_t vis_w;
    uint32_t row0;
    uint32_t vis_h;
    uint32_t h_taps;    // largest horizontal window (0 if h copies)
    uint64_t hk_len;    // horizontal weights: the computed columns' windows, summed
    uint32_t v_taps;    // largest vertical window = ring rows (0: no row is ever stored)
    uint32_t ring_w;    // pixels per ring row
    uint32_t cbase;     // tall path: first source column held
    uint32_t ybox_last; // source rows the output reads
};

// Takes source rows, top to bottom, and gives computed rows, top to bottom.
class resampler {
public:
    // Derives every size for a config. Allocates nothing.
    [[nodiscard]] static resampler_plan plan(const resampler_config& cfg);

    // The bytes a resampler made from this plan allocates.
    [[nodiscard]] static uint64_t memory_bytes(const resampler_plan& plan);

    explicit resampler(const resampler_plan& plan);

    // Takes the next source row (src_width pixels). Call pop() until it returns
    // false after every push.
    void push(const rgb888* src_row);

    // Writes the next computed row (vis_w pixels), if the source rows it needs
    // have been pushed. Returns false if not, or when all rows have been given.
    [[nodiscard]] bool pop(rgb888* dst_row);

private:
    [[nodiscard]] rgb888* _ring_row(uint32_t src_row);
    void _h_pass(const rgb888* in, uint32_t in_base, rgb888* out) const;
    void _v_pass(uint32_t width, rgb888* out) const;
    void _next_window();

    resampler_plan _p;
    std::vector<uint16_t> _hlo;      // [vis_w] first source column of each computed column
    std::vector<uint16_t> _hn;       // [vis_w] window length
    std::vector<int32_t> _hk;        // [hk_len] horizontal weights, hn[j] for column j, packed
    std::vector<int32_t> _vk;        // [v_taps] weights of the current row
    std::vector<const rgb888*> _win; // [v_taps] ring rows of the current window
    std::vector<rgb888> _ring;       // [v_taps * ring_w]
    std::vector<rgb888> _tmp;        // [ring_w] tall path: vertical result
    std::vector<double> _wk;         // [max(h_taps, v_taps)] scratch for the coefficients
    uint32_t _pushed = 0;            // source rows accepted
    uint32_t _done = 0;              // computed rows given
    uint32_t _next_lo = 0;           // source window of the next computed row
    uint32_t _next_n = 0;
};

// Where a layout puts a source rectangle: resampled to dst_w x dst_h, at
// (dst_x, dst_y) inside the area it was laid out in.
struct picture_layout {
    source_rect source;
    uint32_t dst_x;
    uint32_t dst_y;
    uint32_t dst_w;
    uint32_t dst_h;
};

// Scales a source of src_w x src_h to the larger of the two ratios, so it fills
// the area, and center-crops it.
[[nodiscard]] picture_layout layout_cover(float src_w, float src_h, uint32_t area_w, uint32_t area_h);

// Scales a source of src_w x src_h to the smaller of the two ratios, so it fits
// inside the area, and centers it.
[[nodiscard]] picture_layout layout_fit(float src_w, float src_h, uint32_t area_w, uint32_t area_h);

} // namespace idfxx::image::detail
