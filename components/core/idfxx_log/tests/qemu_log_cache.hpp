// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Lets the tests set the flash cache state that ESP-IDF's log is told about. Under QEMU, a test can
// report the cache as enabled for the rest of the test, so that sinks receive the test's lines. A
// test can also report the cache as disabled for a while, as when code runs from IRAM with the
// flash cache disabled.

#pragma once

#include "sdkconfig.h"
#include "test_cleanup.hpp"

#include <esp_private/cache_utils.h>
#include <esp_private/log_util.h>

namespace sink_test {

namespace detail {

inline bool cache_enabled() {
    return true;
}

inline bool cache_disabled() {
    return false;
}

// The cache is reported as enabled until the test ends (under QEMU only).
inline bool cache_reported_enabled = false;

// The cache check that the test runs with. This is ESP-IDF's own check or, under QEMU after
// report_cache_enabled_until_test_end(), a check that reports the cache as enabled.
inline esp_log_cache_enabled_t test_cache_check() {
    return cache_reported_enabled ? cache_enabled : spi_flash_cache_enabled;
}

// Restores the check that ESP-IDF installs at startup, if the test reported the cache as enabled.
inline void restore_cache_check() {
    if (cache_reported_enabled) {
        esp_log_util_set_cache_enabled_cb(spi_flash_cache_enabled);
        cache_reported_enabled = false;
    }
}

} // namespace detail

// QEMU reports the flash cache as disabled (spi_flash_cache_enabled() is false), so ESP-IDF's log
// treats every line as logged in a constrained context, where no sink receives lines. This
// function reports the cache as enabled instead, until the test ends. That also changes how log v2
// prints to this app's console: it prints through the installed vprintf and applies each tag's
// level, instead of printing every line through esp_rom_vprintf. Does nothing outside QEMU.
//
// Call it before registering the test's cleanup that removes its sinks, so that the check is
// restored after the sinks are removed.
inline void report_cache_enabled_until_test_end() {
#ifdef CONFIG_IDF_TARGET_QEMU
    idfxx::test::run_after_each_test(detail::restore_cache_check);
    esp_log_util_set_cache_enabled_cb(detail::cache_enabled);
    detail::cache_reported_enabled = true;
#endif
}

// With @p disabled true, reports the flash cache to ESP-IDF's log as disabled, as when code runs
// from IRAM with the flash cache disabled. With @p disabled false, restores the check that the
// test ran with before. Make no assertion in between, because a failed assertion would leave the
// cache reported as disabled.
inline void report_cache_disabled(bool disabled) {
    esp_log_util_set_cache_enabled_cb(
        disabled ? detail::cache_disabled : detail::test_cache_check()
    );
}

} // namespace sink_test
