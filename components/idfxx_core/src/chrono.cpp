// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#include <idfxx/chrono>

#include <cerrno>
#include <sys/time.h>
#include <system_error>

namespace idfxx::chrono::detail {

result<void> try_set_system_time_us(std::chrono::sys_time<std::chrono::microseconds> us) {
    const auto s = std::chrono::floor<std::chrono::seconds>(us);
    if (s.time_since_epoch().count() < 0) {
        // The system clock can't hold times before the epoch.
        return error(errc::invalid_arg);
    }
    const timeval tv{
        .tv_sec = static_cast<time_t>(s.time_since_epoch().count()),
        .tv_usec = static_cast<suseconds_t>((us - s).count()),
    };
    // Clear errno so a stale value from an earlier call is not reported as
    // the cause of a failure that set none.
    errno = 0;
    if (settimeofday(&tv, nullptr) != 0) {
        const int err = errno;
        if (err == 0) {
            return error(errc::fail);
        }
        return error(std::error_code(err, std::generic_category()));
    }
    return {};
}

} // namespace idfxx::chrono::detail
