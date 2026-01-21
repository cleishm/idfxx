// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#define LOG_LOCAL_LEVEL ESP_LOG_VERBOSE

#include <idfxx/log>

#include <esp_log.h>
#include <esp_log_buffer.h>
#include <string_view>

namespace idfxx::log {

void log(level lvl, const char* tag, std::string_view msg) {
    ESP_LOG_LEVEL(static_cast<esp_log_level_t>(lvl), tag, "%.*s", static_cast<int>(msg.size()), msg.data());
}

void set_level(const char* tag, level lvl) {
    esp_log_level_set(tag, static_cast<esp_log_level_t>(lvl));
}

void set_default_level(level lvl) {
    esp_log_level_set("*", static_cast<esp_log_level_t>(lvl));
}

void buffer_hex(level lvl, const char* tag, const void* buffer, size_t length) {
    esp_log_buffer_hex_internal(tag, buffer, static_cast<uint16_t>(length), static_cast<esp_log_level_t>(lvl));
}

void buffer_char(level lvl, const char* tag, const void* buffer, size_t length) {
    esp_log_buffer_char_internal(tag, buffer, static_cast<uint16_t>(length), static_cast<esp_log_level_t>(lvl));
}

void buffer_hex_dump(level lvl, const char* tag, const void* buffer, size_t length) {
    esp_log_buffer_hexdump_internal(tag, buffer, static_cast<uint16_t>(length), static_cast<esp_log_level_t>(lvl));
}

void logger::set_level(level lvl) const {
    idfxx::log::set_level(_tag, lvl);
}

} // namespace idfxx::log
