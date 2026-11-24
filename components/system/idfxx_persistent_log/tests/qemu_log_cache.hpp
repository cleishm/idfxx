// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Lets the tests set the flash cache state that ESP-IDF's log is told about. Under QEMU, a test can
// report the cache as enabled for the rest of the test, so that capture receives the test's lines.

#pragma once

#include "sdkconfig.h"
#include "test_cleanup.hpp"

#if defined(CONFIG_IDFXX_STD_FORMAT) && defined(CONFIG_IDF_TARGET_QEMU)
#include <esp_private/cache_utils.h>
#include <esp_private/log_util.h>
#endif

namespace idfxx::persistent_log::testing {

#if defined(CONFIG_IDFXX_STD_FORMAT) && defined(CONFIG_IDF_TARGET_QEMU)
namespace detail {

inline bool cache_enabled() {
    return true;
}

inline bool cache_reported = false;

// Restores the check that ESP-IDF installs at startup, if the test reported the cache as enabled.
inline void restore_cache_check() {
    if (cache_reported) {
        esp_log_util_set_cache_enabled_cb(spi_flash_cache_enabled);
        cache_reported = false;
    }
}

} // namespace detail
#endif

// QEMU reports the flash cache as disabled (spi_flash_cache_enabled() is false), so ESP-IDF's log
// treats every line as logged in a constrained context, where capture skips every line. This
// function reports the cache as enabled instead, until the test ends. That also changes how log v2
// prints to this app's console: it prints through the installed vprintf and applies each tag's
// level, instead of printing every line through esp_rom_vprintf. Does nothing without capture or
// outside QEMU.
//
// Call it before registering the test's cleanup that stops the log, so that the check is restored
// after the log is stopped.
inline void report_cache_enabled_until_test_end() {
#if defined(CONFIG_IDFXX_STD_FORMAT) && defined(CONFIG_IDF_TARGET_QEMU)
    idfxx::test::run_after_each_test(detail::restore_cache_check);
    esp_log_util_set_cache_enabled_cb(detail::cache_enabled);
    detail::cache_reported = true;
#endif
}

} // namespace idfxx::persistent_log::testing
