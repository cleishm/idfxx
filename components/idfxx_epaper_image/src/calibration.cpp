// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#include <idfxx/epaper/image_writer>

#include <array>
#include <cstddef>
#include <cstdint>
#include <epaper_image.h>
#include <optional>
#include <vector>

namespace idfxx::epaper {

namespace {

// The value of an ink in a framebuffer's pixels, if its code can reach one.
std::optional<uint8_t> ink_value(enum color_mode mode, size_t index, uint8_t code) {
    switch (mode) {
    case color_mode::color6:
        switch (static_cast<color6>(code)) {
        case color6::black:
        case color6::white:
        case color6::yellow:
        case color6::red:
        case color6::blue:
        case color6::green:
            return code;
        }
        return std::nullopt;
    case color_mode::gray4:
        // Code 0 is the darkest level, and gray4 counts from the lightest.
        return code == index ? std::optional<uint8_t>(3 - code) : std::nullopt;
    case color_mode::mono:
        return code == index ? std::optional<uint8_t>(code) : std::nullopt;
    }
    return std::nullopt;
}

image::rgb888 to_rgb888(epi_rgb_t c) {
    return {c.r, c.g, c.b};
}

} // namespace

const calibration& calibration::el133uf1() {
    static const calibration cal = *make(epi_panel_el133uf1);
    return cal;
}

const calibration& calibration::gdey075t7_mono() {
    static const calibration cal = *make(epi_panel_gdey075t7_mono);
    return cal;
}

const calibration& calibration::gdey075t7_gray4() {
    static const calibration cal = *make(epi_panel_gdey075t7_grey4);
    return cal;
}

result<calibration> calibration::make(const epi_panel_t& panel) {
    enum color_mode mode;
    switch (panel.ink_count) {
    case 6:
        mode = color_mode::color6;
        break;
    case 4:
        mode = color_mode::gray4;
        break;
    case 2:
        mode = color_mode::mono;
        break;
    default:
        return error(errc::invalid_arg);
    }
    if (panel.inks == nullptr) {
        return error(errc::invalid_arg);
    }
    std::array<ink, 6> inks{};
    for (size_t i = 0; i < panel.ink_count; ++i) {
        const epi_ink_t& from = panel.inks[i];
        const auto value = ink_value(mode, i, from.code);
        if (!value) {
            return error(errc::invalid_arg);
        }
        inks[i] = {.value = *value, .nominal = to_rgb888(from.nominal), .measured = to_rgb888(from.measured)};
    }
    return calibration(&panel, mode, inks, panel.ink_count);
}

const char* calibration::name() const noexcept {
    return _panel->name;
}

size_t calibration::width() const noexcept {
    return _panel->width;
}

size_t calibration::height() const noexcept {
    return _panel->height;
}

std::vector<preset> calibration::presets() const {
    std::vector<preset> out;
    out.reserve(_panel->preset_count);
    for (size_t i = 0; i < _panel->preset_count; ++i) {
        const epi_preset_t& from = _panel->presets[i];
        out.push_back({
            .name = from.name,
            .intent = {
                .tone = from.intent->tone,
                .detail = from.intent->detail,
                .adapt = from.intent->adapt,
                .vibrance = from.intent->vibrance,
                .veil = from.intent->veil,
            },
        });
    }
    return out;
}

} // namespace idfxx::epaper
