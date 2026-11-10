// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// A C++ port of the resampler of cleishm/epaper-image (src/epi_resampler.c and
// epi_sin_d from src/epi_math.c; MIT licensed, coefficient computation derived
// from Pillow's), with the same fixed-point arithmetic, kernels, window rules
// and order of passes. The tests compare its output byte for byte with the
// library's.
//
// Separable convolution of 8-bit RGB, one axis at a time:
//
//   Coefficients  resampler_axis: scale = (double)(float)(x1 - x0) / out; the
//                 filter's support is scaled by max(1, scale); output i is
//                 centerd at x0 + (i + 0.5) * scale and its window is
//                 [trunc(center - support + 0.5), trunc(center + support + 0.5))
//                 clamped to the image. Weights are normalized by their sum in
//                 double, then rounded to 22-bit fixed point.
//   Passes        h_pass / v_pass: int32 accumulation from 2^21 (round half up),
//                 then (sum >> 22) clamped to 0..255. The horizontal pass runs
//                 first and is clamped to 8 bits before the vertical pass reads
//                 it; it runs only on the rows the vertical pass reads.
//   Copies        an axis at scale 1 from offset 0 is copied, not convolved; so
//                 are both axes of a crop at integer offsets whose size is the
//                 output's.
//   Tall sources  when src_height > 100 * src_width and the height shrinks, the
//                 vertical pass runs first and the horizontal pass second; the
//                 ring then holds source rows.
//
// Determinism: the per-pixel loops are integer-only. Coefficients are computed
// in double with + - * / and the sine below, never libm. No expression may be
// fused: a fused center can move its last bit, and on a box filter with an
// integer rectangle that bit decides whether an edge pixel is in the window.
// The component compiles with -ffp-contract=off for this.

#include "resampler.hpp"

#include <algorithm>
#include <bit>
#include <cassert>
#include <cfloat>
#include <cstring>

#if !defined(FLT_EVAL_METHOD) || (FLT_EVAL_METHOD != 0 && FLT_EVAL_METHOD != 16)
#error                                                                                             \
    "floating-point expressions must be evaluated at their own precision, or the output is not deterministic"
#endif

namespace idfxx::image::detail {

namespace {

// ---- Sine (fdlibm k_sin.c, k_cos.c, e_rem_pio2.c medium path) ---------------

constexpr double S1 = -0x1.5555555555549p-3;
constexpr double S2 = 0x1.111111110f8a6p-7;
constexpr double S3 = -0x1.a01a019c161d5p-13;
constexpr double S4 = 0x1.71de357b1fe7dp-19;
constexpr double S5 = -0x1.ae5e68a2b9cebp-26;
constexpr double S6 = 0x1.5d93a5acfd57cp-33;
constexpr double C1 = 0x1.555555555554cp-5;
constexpr double C2 = -0x1.6c16c16c15177p-10;
constexpr double C3 = 0x1.a01a019cb1590p-16;
constexpr double C4 = -0x1.27e4f809c52adp-22;
constexpr double C5 = 0x1.1ee9ebdb4b1c4p-29;
constexpr double C6 = -0x1.8fae9be8838d4p-37;
constexpr double invpio2 = 0x1.45f306dc9c883p-1;
constexpr double pio2_1 = 0x1.921fb54400000p+0;   // first 33 bits of pi/2
constexpr double pio2_1t = 0x1.0b4611a626331p-34; // pi/2 - pio2_1
constexpr double pio2_2 = 0x1.0b4611a600000p-34;  // next 33 bits
constexpr double pio2_2t = 0x1.3198a2e037073p-69;
constexpr double pio2_3 = 0x1.3198a2e000000p-69; // next 33 bits
constexpr double pio2_3t = 0x1.b839a252049c1p-104;

uint32_t hi_word(double d) {
    return static_cast<uint32_t>(std::bit_cast<uint64_t>(d) >> 32);
}

// sin(x + y) for |x| <= pi/4, |y| <= ulp(x)/2; iy = 0 means y is 0.
double k_sin(double x, double y, int iy) {
    if ((hi_word(x) & 0x7FFFFFFFu) < 0x3E400000u) { // |x| < 2^-27
        return x;
    }
    double z = x * x;
    double v = z * x;
    double r = S2 + z * (S3 + z * (S4 + z * (S5 + z * S6)));
    if (iy == 0) {
        return x + v * (S1 + z * r);
    }
    return x - ((z * (0.5 * y - v * r) - y) - v * S1);
}

// cos(x + y) for |x| <= pi/4, |y| <= ulp(x)/2.
double k_cos(double x, double y) {
    double z = x * x;
    double w = z * z;
    double r = z * (C1 + z * (C2 + z * C3)) + w * w * (C4 + z * (C5 + z * C6));
    double hz = 0.5 * z;
    w = 1.0 - hz;
    return w + (((1.0 - w) - hz) + (z * r - x * y));
}

// x - n * pi/2 = y0 + y1 for pi/4 < |x| <= 2^19 * pi/2; returns n. All products
// fn * pio2_k are exact (fn < 2^20, constants of 33 bits).
int32_t rem_pio2(double x, double& y0, double& y1) {
    uint32_t hx = hi_word(x);
    uint32_t ix = hx & 0x7FFFFFFFu;
    double t = x < 0.0 ? -x : x;
    int32_t n = static_cast<int32_t>(t * invpio2 + 0.5);
    double fn = static_cast<double>(n);
    double r = t - fn * pio2_1;
    double w = fn * pio2_1t; // first round good to 85 bits
    int32_t j = static_cast<int32_t>(ix >> 20);
    double a = r - w;
    int32_t i = j - static_cast<int32_t>((hi_word(a) >> 20) & 0x7FFu);
    if (i > 16) { // second round, good to 118 bits
        double tt = r;
        w = fn * pio2_2;
        r = tt - w;
        w = fn * pio2_2t - ((tt - r) - w);
        a = r - w;
        i = j - static_cast<int32_t>((hi_word(a) >> 20) & 0x7FFu);
        if (i > 49) { // third round, 151 bits
            tt = r;
            w = fn * pio2_3;
            r = tt - w;
            w = fn * pio2_3t - ((tt - r) - w);
            a = r - w;
        }
    }
    double b = (r - a) - w;
    if (x < 0.0) {
        y0 = -a;
        y1 = -b;
        return -n;
    }
    y0 = a;
    y1 = b;
    return n;
}

double sin_d(double x) {
    uint32_t ix = hi_word(x) & 0x7FFFFFFFu;
    if (ix <= 0x3FE921FBu) { // |x| <~ pi/4
        return k_sin(x, 0.0, 0);
    }
    if (ix > 0x413921FBu) { // outside the domain, NaN or inf
        return std::bit_cast<double>(UINT64_C(0x7FF8000000000000));
    }
    double y0;
    double y1;
    uint32_t n = static_cast<uint32_t>(rem_pio2(x, y0, y1));
    switch (n & 3u) {
    case 0:
        return k_sin(y0, y1, 1);
    case 1:
        return k_cos(y0, y1);
    case 2:
        return -k_sin(y0, y1, 1);
    default:
        return -k_cos(y0, y1);
    }
}

// ---- Filters ----------------------------------------------------------------

double f_box(double x) {
    return (x > -0.5 && x <= 0.5) ? 1.0 : 0.0;
}

double f_bilinear(double x) {
    if (x < 0.0) {
        x = -x;
    }
    return x < 1.0 ? 1.0 - x : 0.0;
}

// a = -0.5, unfused:
// |x| < 1: ((a + 2) * x - (a + 3)) * x * x + 1; |x| < 2: (((x - 5) * x + 8) * x - 4) * a
double f_bicubic(double x) {
    if (x < 0.0) {
        x = -x;
    }
    if (x < 1.0) {
        return (1.5 * x - 2.5) * x * x + 1.0;
    }
    if (x < 2.0) {
        return (((x - 5.0) * x + 8.0) * x - 4.0) * -0.5;
    }
    return 0.0;
}

double sinc(double x) {
    if (x == 0.0) {
        return 1.0;
    }
    x = x * 0x1.921fb54442d18p+1; // pi
    return sin_d(x) / x;
}

double f_lanczos(double x) {
    if (-3.0 <= x && x < 3.0) {
        return sinc(x) * sinc(x / 3.0);
    }
    return 0.0;
}

using filter_fn = double (*)(double);

filter_fn filter_function(filter kernel) {
    switch (kernel) {
    case filter::box:
        return f_box;
    case filter::bilinear:
        return f_bilinear;
    case filter::bicubic:
        return f_bicubic;
    case filter::lanczos3:
        break;
    }
    return f_lanczos;
}

double filter_support(filter kernel) {
    switch (kernel) {
    case filter::box:
        return 0.5;
    case filter::bilinear:
        return 1.0;
    case filter::bicubic:
        return 2.0;
    case filter::lanczos3:
        break;
    }
    return 3.0;
}

// ---- One axis: coefficients, one output index at a time ---------------------

constexpr int rs_bits = 22; // fixed-point weight precision

resampler_axis axis_copy(int32_t off) {
    resampler_axis a{};
    a.off = off;
    return a;
}

resampler_axis axis_conv(uint32_t in_size, float x0, float x1, uint32_t out, double base_support) {
    float w = x1 - x0; // the extent, in float
    double scale = static_cast<double>(w) / static_cast<double>(out);
    double fs = scale < 1.0 ? 1.0 : scale;
    resampler_axis a{};
    a.conv = true;
    a.in_size = in_size;
    a.max_n = UINT32_MAX; // the plan measures the true maximum with the same function
    a.in0 = static_cast<double>(x0);
    a.scale = scale;
    a.support = base_support * fs;
    a.inv_fs = 1.0 / fs;
    return a;
}

// Source window [lo, lo + n) of output index i; returns the center.
double axis_bounds(const resampler_axis& a, uint32_t i, uint32_t& lo, uint32_t& n) {
    if (!a.conv) {
        lo = static_cast<uint32_t>(a.off) + i;
        n = 1;
        return 0.0;
    }
    double center = a.in0 + (static_cast<double>(i) + 0.5) * a.scale; // unfused
    int32_t xmin = static_cast<int32_t>(center - a.support + 0.5);
    if (xmin < 0) {
        xmin = 0;
    }
    int32_t xmax = static_cast<int32_t>(center + a.support + 0.5);
    if (xmax > static_cast<int32_t>(a.in_size)) {
        xmax = static_cast<int32_t>(a.in_size);
    }
    lo = static_cast<uint32_t>(xmin);
    n = static_cast<uint32_t>(xmax - xmin);
    if (n > a.max_n) {
        n = a.max_n;
    }
    return center;
}

// A normalized weight to fixed point, rounding half away from zero.
int32_t to_fixed(double p) {
    if (p < 0) {
        return static_cast<int32_t>(-0.5 + p * static_cast<double>(1 << rs_bits));
    }
    return static_cast<int32_t>(0.5 + p * static_cast<double>(1 << rs_bits));
}

// Fixed-point weights k[0..n) of output index i (conv axes only). Each tap is
// evaluated once, into the scratch w[0..n), summing in tap order; the
// normalization then reads w, so it is the same value the sum saw.
void axis_coeffs(
    const resampler_axis& a,
    filter_fn f,
    uint32_t i,
    double* w,
    int32_t* k,
    uint32_t& lo,
    uint32_t& n
) {
    double center = axis_bounds(a, i, lo, n);
    double ww = 0.0;
    for (uint32_t t = 0; t < n; t++) {
        w[t] = f((static_cast<double>(static_cast<int32_t>(t + lo)) - center + 0.5) * a.inv_fs);
        ww += w[t];
    }
    for (uint32_t t = 0; t < n; t++) {
        k[t] = to_fixed(ww != 0.0 ? w[t] / ww : w[t]);
    }
}

bool is_int_f(float v) { // v - (int)v == 0, v in [0, 65535]
    return v - static_cast<float>(static_cast<int32_t>(v)) == 0.0f;
}

// ---- Per-pixel helpers (integer only) ---------------------------------------

inline uint8_t clip8(int32_t ss) {
    if (ss < 0) {
        return 0; // (ss >> 22) < 0 -> 0
    }
    uint32_t v = static_cast<uint32_t>(ss) >> rs_bits;
    return v > 255u ? 255u : static_cast<uint8_t>(v);
}

constexpr int32_t rs_half = int32_t{1} << (rs_bits - 1);

// ---- Layout helpers ---------------------------------------------------------

// Round half away from zero for v >= 0 (exact: v - trunc(v) is exact).
uint32_t round_pos_f(float v) {
    uint32_t t = static_cast<uint32_t>(v);
    return (v - static_cast<float>(t) >= 0.5f) ? t + 1u : t;
}

// A source side cropped to extent len (at most the side), centerd.
void crop_axis(float side, float len, float& pos, float& size) {
    size = len > side ? side : len;
    pos = (side - size) * 0.5f;
}

} // namespace

// ---- Plan: every derived size -----------------------------------------------

resampler_plan resampler::plan(const resampler_config& cfg) {
    const float x0 = cfg.source.x;
    const float y0 = cfg.source.y;
    const float x1 = x0 + cfg.source.w; // the rectangle's far corner (x1, y1)
    const float y1 = y0 + cfg.source.h;

    resampler_plan p{};
    p.kernel = cfg.kernel;
    p.col0 = cfg.col0;
    p.vis_w = cfg.vis_w;
    p.row0 = cfg.row0;
    p.vis_h = cfg.vis_h;

    const uint32_t sw = cfg.src_width;
    const uint32_t sh = cfg.src_height;
    const uint32_t dw = cfg.dst_w;
    const uint32_t dh = cfg.dst_h;
    const double sup = filter_support(cfg.kernel);
    const bool crop_x = is_int_f(x0) && (x1 - x0) == static_cast<float>(dw);
    const bool crop_y = is_int_f(y0) && (y1 - y0) == static_cast<float>(dh);

    const bool tall = sh > 100u * sw && dh < sh;
    const bool id_x = dw == sw && x0 == 0.0f && x1 == static_cast<float>(dw);
    const bool id_y = dh == sh && y0 == 0.0f && y1 == static_cast<float>(dh);
    // A crop copies only if the other axis crops too, except on the tall path.
    const bool copy_x = (crop_x && (tall || crop_y)) || id_x;
    const bool copy_y = (crop_y && (tall || crop_x)) || id_y;

    p.h = copy_x ? axis_copy(static_cast<int32_t>(x0)) : axis_conv(sw, x0, x1, dw, sup);
    p.v = copy_y ? axis_copy(static_cast<int32_t>(y0)) : axis_conv(sh, y0, y1, dh, sup);
    // With either pass a copy the order does not matter.
    p.vh = tall && !copy_x && !copy_y;

    uint32_t lo = 0;
    uint32_t n = 0;
    uint32_t first_lo = 0;
    uint32_t last_end = 0;
    for (uint32_t j = 0; j < p.vis_w; j++) {
        axis_bounds(p.h, p.col0 + j, lo, n);
        if (p.h.conv) {
            p.h_taps = std::max(n, p.h_taps);
            p.hk_len += n;
        }
        if (j == 0) {
            first_lo = lo;
        }
        last_end = lo + n; // bounds are monotone in j
    }
    for (uint32_t i = 0; i < p.vis_h; i++) {
        axis_bounds(p.v, p.row0 + i, lo, n);
        p.v_taps = std::max(n, p.v_taps);
        p.ybox_last = lo + n;
    }
    p.h.max_n = p.h_taps;
    p.v.max_n = p.v_taps;
    if (p.vh) {
        p.cbase = first_lo;
        p.ring_w = last_end - first_lo;
    } else {
        p.ring_w = p.vis_w;
    }
    return p;
}

uint64_t resampler::memory_bytes(const resampler_plan& p) {
    const uint64_t vt = p.v_taps;
    const uint64_t ht = p.h_taps;
    const uint64_t vw = p.vis_w;
    const uint64_t rw = p.ring_w;
    return vt * sizeof(const rgb888*) + p.hk_len * sizeof(int32_t) +
        (p.v.conv ? vt : 0u) * sizeof(int32_t) + (p.h.conv ? vw : 0u) * 2 * sizeof(uint16_t) +
        vt * rw * sizeof(rgb888) + (p.vh ? rw : 0u) * sizeof(rgb888) +
        std::max(ht, vt) * sizeof(double);
}

// ---- Resampler --------------------------------------------------------------

resampler::resampler(const resampler_plan& plan)
    : _p(plan)
    , _hlo(_p.h.conv ? _p.vis_w : 0u)
    , _hn(_p.h.conv ? _p.vis_w : 0u)
    , _hk(_p.hk_len)
    , _vk(_p.v.conv ? _p.v_taps : 0u)
    , _win(_p.v_taps)
    , _ring(static_cast<size_t>(_p.v_taps) * _p.ring_w)
    , _tmp(_p.vh ? _p.ring_w : 0u)
    , _wk(std::max(_p.h_taps, _p.v_taps)) {
    // Every computed column gets bounds, even when all windows are empty
    // (h_taps = 0: a box collapsed to x0 = x1 gives black, the rounding offset
    // alone).
    if (_p.h.conv) {
        const filter_fn f = filter_function(_p.kernel);
        int32_t* k = _hk.data();
        [[maybe_unused]] uint64_t used = 0;
        for (uint32_t j = 0; j < _p.vis_w; j++) {
            uint32_t lo = 0;
            uint32_t n = 0;
            axis_coeffs(_p.h, f, _p.col0 + j, _wk.data(), k, lo, n);
            used += n;
            assert(used <= _p.hk_len);
            _hlo[j] = static_cast<uint16_t>(lo);
            _hn[j] = static_cast<uint16_t>(n);
            k += n;
        }
    }
    _next_window();
}

void resampler::_next_window() {
    if (_done < _p.vis_h) {
        axis_bounds(_p.v, _p.row0 + _done, _next_lo, _next_n);
    }
}

rgb888* resampler::_ring_row(uint32_t src_row) {
    return _ring.data() + static_cast<size_t>(src_row % _p.v_taps) * _p.ring_w;
}

// Horizontal pass of one row. in[c - in_base] is source column c. Sums are
// bounded by 255 * sum(|k|) + 2^21 < 1.4 * 2^30 for every filter.
void resampler::_h_pass(const rgb888* in, uint32_t in_base, rgb888* out) const {
    if (!_p.h.conv) {
        std::memcpy(
            out,
            in + (static_cast<uint32_t>(_p.h.off) + _p.col0 - in_base),
            static_cast<size_t>(_p.vis_w) * sizeof *out
        );
        return;
    }
    const int32_t* k = _hk.data();
    for (uint32_t j = 0; j < _p.vis_w; j++) {
        const rgb888* s = in + (_hlo[j] - in_base);
        const uint32_t n = _hn[j];
        int32_t s0 = rs_half;
        int32_t s1 = rs_half;
        int32_t s2 = rs_half;
        for (uint32_t t = 0; t < n; t++) {
            s0 += static_cast<int32_t>(s[t].r) * k[t];
            s1 += static_cast<int32_t>(s[t].g) * k[t];
            s2 += static_cast<int32_t>(s[t].b) * k[t];
        }
        out[j].r = clip8(s0);
        out[j].g = clip8(s1);
        out[j].b = clip8(s2);
        k += n;
    }
}

// Vertical pass over the current window into out[0, width).
void resampler::_v_pass(uint32_t width, rgb888* out) const {
    const uint32_t n = _next_n;
    const int32_t* k = _vk.data();
    for (uint32_t j = 0; j < width; j++) {
        int32_t s0 = rs_half;
        int32_t s1 = rs_half;
        int32_t s2 = rs_half;
        for (uint32_t t = 0; t < n; t++) {
            const rgb888* q = _win[t] + j;
            s0 += static_cast<int32_t>(q->r) * k[t];
            s1 += static_cast<int32_t>(q->g) * k[t];
            s2 += static_cast<int32_t>(q->b) * k[t];
        }
        out[j].r = clip8(s0);
        out[j].g = clip8(s1);
        out[j].b = clip8(s2);
    }
}

void resampler::push(const rgb888* src_row) {
    const uint32_t s = _pushed;
    if (_done < _p.vis_h && s >= _next_lo && s < _p.ybox_last) {
        // A row more than a window past the next pending row would overwrite a
        // ring row that row still needs.
        assert(s - _next_lo < _p.v_taps);
        rgb888* dst = _ring_row(s);
        if (_p.vh) {
            std::memcpy(dst, src_row + _p.cbase, static_cast<size_t>(_p.ring_w) * sizeof *dst);
        } else {
            _h_pass(src_row, 0, dst);
        }
    }
    _pushed = s + 1;
}

bool resampler::pop(rgb888* dst_row) {
    if (_done >= _p.vis_h || _pushed < _next_lo + _next_n) {
        return false;
    }
    if (!_p.v.conv) {
        std::memcpy(dst_row, _ring_row(_next_lo), static_cast<size_t>(_p.vis_w) * sizeof *dst_row);
    } else {
        uint32_t lo = 0;
        uint32_t n = 0;
        axis_coeffs(
            _p.v, filter_function(_p.kernel), _p.row0 + _done, _wk.data(), _vk.data(), lo, n
        );
        _next_n = n;
        for (uint32_t t = 0; t < n; t++) {
            _win[t] = _ring_row(lo + t);
        }
        if (_p.vh) {
            _v_pass(_p.ring_w, _tmp.data());
            _h_pass(_tmp.data(), _p.cbase, dst_row);
        } else {
            _v_pass(_p.vis_w, dst_row);
        }
    }
    _done++;
    _next_window();
    return true;
}

// ---- Layouts ----------------------------------------------------------------

picture_layout layout_cover(float src_w, float src_h, uint32_t area_w, uint32_t area_h) {
    const float dw = static_cast<float>(area_w);
    const float dh = static_cast<float>(area_h);
    const float sx = dw / src_w;
    const float sy = dh / src_h;
    picture_layout l{};
    if (sx >= sy) { // width fills: the whole source width, centerd rows
        l.source.x = 0.0f;
        l.source.w = src_w;
        crop_axis(src_h, dh / sx, l.source.y, l.source.h);
    } else {
        l.source.y = 0.0f;
        l.source.h = src_h;
        crop_axis(src_w, dw / sy, l.source.x, l.source.w);
    }
    l.dst_w = area_w;
    l.dst_h = area_h;
    return l;
}

picture_layout layout_fit(float src_w, float src_h, uint32_t area_w, uint32_t area_h) {
    const float sx = static_cast<float>(area_w) / src_w;
    const float sy = static_cast<float>(area_h) / src_h;
    const float scale = sx <= sy ? sx : sy;
    const uint32_t w = std::clamp<uint32_t>(round_pos_f(src_w * scale), 1u, area_w);
    const uint32_t h = std::clamp<uint32_t>(round_pos_f(src_h * scale), 1u, area_h);
    picture_layout l{};
    l.source = {0.0f, 0.0f, src_w, src_h};
    l.dst_w = w;
    l.dst_h = h;
    l.dst_x = (area_w - w) / 2u;
    l.dst_y = (area_h - h) / 2u;
    return l;
}

} // namespace idfxx::image::detail
