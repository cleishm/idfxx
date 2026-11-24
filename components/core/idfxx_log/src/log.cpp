// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#define LOG_LOCAL_LEVEL ESP_LOG_VERBOSE

#include "line_text.hpp"
#include "sinks.hpp"

#include <idfxx/log>

#include <esp_log.h>
#include <esp_log_buffer.h>
#include <string_view>

namespace idfxx::log {

void log(level lvl, const char* tag, std::string_view msg) {
    // The sink entry is held while ESP-IDF's log prints the line, so that the wrapper doesn't also
    // capture the line for the sinks added with all_lines. Instead, this function passes the
    // complete line to every sink after it is printed. When no sink is added, the only check is the
    // count of added sinks.
    const detail::sink_entry entry =
        detail::any_sink_added() ? detail::enter_sinks() : detail::sink_entry::none();
    ESP_LOG_LEVEL(
        static_cast<esp_log_level_t>(lvl), tag, "%.*s", static_cast<int>(msg.size()), msg.data()
    );
    if (entry) {
        detail::deliver(detail::offered_to::every_sink, lvl, detail::tag_view(tag), msg);
    }
}

void set_level(const char* tag, level lvl) {
    esp_log_level_set(tag, static_cast<esp_log_level_t>(lvl));
}

void set_default_level(level lvl) {
    esp_log_level_set("*", static_cast<esp_log_level_t>(lvl));
}

void buffer_hex(level lvl, const char* tag, const void* buffer, uint16_t length) {
    esp_log_buffer_hex_internal(tag, buffer, length, static_cast<esp_log_level_t>(lvl));
}

void buffer_char(level lvl, const char* tag, const void* buffer, uint16_t length) {
    esp_log_buffer_char_internal(tag, buffer, length, static_cast<esp_log_level_t>(lvl));
}

void buffer_hex_dump(level lvl, const char* tag, const void* buffer, uint16_t length) {
    esp_log_buffer_hexdump_internal(tag, buffer, length, static_cast<esp_log_level_t>(lvl));
}

void logger::set_level(level lvl) const {
    idfxx::log::set_level(_tag, lvl);
}

} // namespace idfxx::log
