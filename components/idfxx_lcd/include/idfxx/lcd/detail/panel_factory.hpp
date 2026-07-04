// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

/**
 * @headerfile <idfxx/lcd/detail/panel_factory.hpp>
 * @file panel_factory.hpp
 * @brief Shared panel-handle creation for esp_lcd-based panel drivers.
 * @ingroup idfxx_lcd
 */

#include <idfxx/error>
#include <idfxx/lcd/panel>

#include <esp_lcd_panel_dev.h>
#include <esp_lcd_panel_io.h>

/// @cond INTERNAL

namespace idfxx::lcd::detail {

/**
 * @brief Signature of esp_lcd vendor panel factories (`esp_lcd_new_panel_*`).
 */
using panel_factory =
    esp_err_t (*)(esp_lcd_panel_io_handle_t io, const esp_lcd_panel_dev_config_t* config, esp_lcd_panel_handle_t* ret);

/**
 * @brief Creates, resets, and initializes an esp_lcd panel.
 *
 * Translates @p config into the ESP-IDF device configuration, invokes the
 * vendor @p factory, then resets and initializes the resulting panel. On any
 * failure the handle is deleted before the error is returned.
 *
 * For use by panel driver components implementing @ref idfxx::lcd::panel.
 *
 * @param factory   The vendor panel factory (e.g. `esp_lcd_new_panel_ili9341`).
 * @param io_handle The panel I/O handle the panel communicates through.
 * @param config    The panel configuration.
 *
 * @return The initialized panel handle, or an error.
 */
[[nodiscard]] result<esp_lcd_panel_handle_t>
make_panel_handle(panel_factory factory, esp_lcd_panel_io_handle_t io_handle, const panel::config& config);

} // namespace idfxx::lcd::detail

/// @endcond
