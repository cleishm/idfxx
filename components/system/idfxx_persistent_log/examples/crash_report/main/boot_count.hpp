// SPDX-License-Identifier: Apache-2.0

// Counts boots in NVS; you don't need to read this to follow the example.

#pragma once

#include <idfxx/nvs>

#include <cstdint>

namespace boot_count {

// Counts this boot, and returns its number: 1 on the first boot after the flash is erased.
inline uint32_t next() {
    idfxx::nvs::flash::init();
    idfxx::nvs store("crash_report");
    const uint32_t boot = store.find_value<uint32_t>("boot").value_or(0) + 1;
    store.set_value("boot", boot);
    store.commit();
    return boot;
}

} // namespace boot_count
