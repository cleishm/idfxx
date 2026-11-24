// SPDX-License-Identifier: Apache-2.0

// Simulates a sensor that fails for a while; you don't need to read this to follow the example.

#pragma once

#include <cstdint>

// On the first boot, the sensor stops answering at this read (counted from 0).
inline constexpr int first_failed_read = 2;

// On the second boot, the sensor answers again from this read (counted from 0).
inline constexpr int first_read_answered_again = 3;

// Returns true if the sensor answers this read of this boot. The sensor answers every read from
// the third boot on.
inline bool sensor_answers(uint32_t boot, int read) {
    if (boot == 1) {
        return read < first_failed_read;
    }
    if (boot == 2) {
        return read >= first_read_answered_again;
    }
    return true;
}
