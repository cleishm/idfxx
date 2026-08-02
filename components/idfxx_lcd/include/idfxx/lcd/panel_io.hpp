// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

/**
 * @headerfile <idfxx/lcd/panel_io>
 * @file panel_io.hpp
 * @brief Deprecated alias for idfxx::panel_io.
 * @ingroup idfxx_lcd
 */

#include <idfxx/panel_io.hpp>

namespace idfxx::lcd {

/**
 * @headerfile <idfxx/lcd/panel_io>
 * @brief Deprecated alias for @ref idfxx::panel_io.
 *
 * The panel I/O class moved to the @ref idfxx_panel_io component as
 * `idfxx::panel_io`; this alias keeps existing code building.
 *
 * @deprecated Use `idfxx::panel_io` from the `idfxx_panel_io` component.
 */
using panel_io [[deprecated("use idfxx::panel_io from the idfxx_panel_io component")]] = idfxx::panel_io;

} // namespace idfxx::lcd
