// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#include <idfxx/lcd/detail/panel_factory.hpp>
#include <idfxx/lcd/ili9341>

#include <esp_lcd_ili9341.h>
#include <esp_lcd_panel_ops.h>
#include <utility>

namespace idfxx::lcd {

result<ili9341> ili9341::make(idfxx::lcd::panel_io& panel_io, panel::config config) {
    return detail::make_panel_handle(esp_lcd_new_panel_ili9341, panel_io.idf_handle(), config)
        .transform([](auto handle) { return ili9341{handle}; });
}

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
ili9341::ili9341(idfxx::lcd::panel_io& panel_io, panel::config config)
    : ili9341(unwrap(make(panel_io, std::move(config)))) {}
#endif

ili9341::ili9341(ili9341&& other) noexcept
    : panel(std::move(other))
    , _handle(std::exchange(other._handle, nullptr)) {}

ili9341& ili9341::operator=(ili9341&& other) noexcept {
    if (this != &other) {
        if (_handle != nullptr) {
            esp_lcd_panel_del(_handle);
        }

        panel::operator=(std::move(other));
        _handle = std::exchange(other._handle, nullptr);
    }
    return *this;
}

ili9341::~ili9341() {
    if (_handle != nullptr) {
        esp_lcd_panel_del(_handle);
    }
}

esp_lcd_panel_handle_t ili9341::do_idf_handle() const {
    return _handle;
}

} // namespace idfxx::lcd
