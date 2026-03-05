// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#include <idfxx/chip>

#include <esp_chip_info.h>
#include <utility>

// Verify chip_model values match ESP-IDF constants
static_assert(std::to_underlying(idfxx::chip_model::esp32) == CHIP_ESP32);
static_assert(std::to_underlying(idfxx::chip_model::esp32s2) == CHIP_ESP32S2);
static_assert(std::to_underlying(idfxx::chip_model::esp32s3) == CHIP_ESP32S3);
static_assert(std::to_underlying(idfxx::chip_model::esp32c3) == CHIP_ESP32C3);
static_assert(std::to_underlying(idfxx::chip_model::esp32c2) == CHIP_ESP32C2);
static_assert(std::to_underlying(idfxx::chip_model::esp32c6) == CHIP_ESP32C6);
static_assert(std::to_underlying(idfxx::chip_model::esp32h2) == CHIP_ESP32H2);
static_assert(std::to_underlying(idfxx::chip_model::esp32p4) == CHIP_ESP32P4);
static_assert(std::to_underlying(idfxx::chip_model::esp32c5) == CHIP_ESP32C5);
static_assert(std::to_underlying(idfxx::chip_model::esp32c61) == CHIP_ESP32C61);

// Verify chip_feature values match ESP-IDF constants
static_assert(std::to_underlying(idfxx::chip_feature::embedded_flash) == CHIP_FEATURE_EMB_FLASH);
static_assert(std::to_underlying(idfxx::chip_feature::wifi) == CHIP_FEATURE_WIFI_BGN);
static_assert(std::to_underlying(idfxx::chip_feature::ble) == CHIP_FEATURE_BLE);
static_assert(std::to_underlying(idfxx::chip_feature::bt_classic) == CHIP_FEATURE_BT);
static_assert(std::to_underlying(idfxx::chip_feature::ieee802154) == CHIP_FEATURE_IEEE802154);
static_assert(std::to_underlying(idfxx::chip_feature::embedded_psram) == CHIP_FEATURE_EMB_PSRAM);

namespace idfxx {

chip_info chip_info::get() noexcept {
    chip_info info;
    esp_chip_info(&info._info);
    return info;
}

std::string to_string(chip_model m) {
    switch (m) {
    case chip_model::esp32:
        return "ESP32";
    case chip_model::esp32s2:
        return "ESP32-S2";
    case chip_model::esp32s3:
        return "ESP32-S3";
    case chip_model::esp32c3:
        return "ESP32-C3";
    case chip_model::esp32c2:
        return "ESP32-C2";
    case chip_model::esp32c6:
        return "ESP32-C6";
    case chip_model::esp32h2:
        return "ESP32-H2";
    case chip_model::esp32p4:
        return "ESP32-P4";
    case chip_model::esp32c5:
        return "ESP32-C5";
    case chip_model::esp32c61:
        return "ESP32-C61";
    default:
        return "unknown(" + std::to_string(static_cast<int>(m)) + ")";
    }
}

} // namespace idfxx
