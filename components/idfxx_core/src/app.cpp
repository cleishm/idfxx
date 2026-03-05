// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#include <idfxx/app>

#include <esp_app_desc.h>

namespace idfxx::app {

namespace {

const esp_app_desc_t& desc() {
    return *esp_app_get_description();
}

} // anonymous namespace

std::string_view version() {
    return desc().version;
}

std::string_view project_name() {
    return desc().project_name;
}

std::string_view compile_time() {
    return desc().time;
}

std::string_view compile_date() {
    return desc().date;
}

std::string_view idf_version() {
    return desc().idf_ver;
}

uint32_t secure_version() {
    return desc().secure_version;
}

std::string elf_sha256_hex() {
    char buf[CONFIG_APP_RETRIEVE_LEN_ELF_SHA + 1] = {};
    esp_app_get_elf_sha256(buf, sizeof(buf));
    return buf;
}

} // namespace idfxx::app
