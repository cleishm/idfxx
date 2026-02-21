// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#include <idfxx/error>
#include <idfxx/nvs>

#include <esp_log.h>
#include <esp_system.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <utility>

// Verify error codes match ESP-IDF constants
static_assert(std::to_underlying(idfxx::nvs::errc::not_found) == ESP_ERR_NVS_NOT_FOUND);
static_assert(std::to_underlying(idfxx::nvs::errc::type_mismatch) == ESP_ERR_NVS_TYPE_MISMATCH);
static_assert(std::to_underlying(idfxx::nvs::errc::read_only) == ESP_ERR_NVS_READ_ONLY);
static_assert(std::to_underlying(idfxx::nvs::errc::not_enough_space) == ESP_ERR_NVS_NOT_ENOUGH_SPACE);
static_assert(std::to_underlying(idfxx::nvs::errc::invalid_name) == ESP_ERR_NVS_INVALID_NAME);
static_assert(std::to_underlying(idfxx::nvs::errc::invalid_handle) == ESP_ERR_NVS_INVALID_HANDLE);
static_assert(std::to_underlying(idfxx::nvs::errc::remove_failed) == ESP_ERR_NVS_REMOVE_FAILED);
static_assert(std::to_underlying(idfxx::nvs::errc::key_too_long) == ESP_ERR_NVS_KEY_TOO_LONG);
static_assert(std::to_underlying(idfxx::nvs::errc::invalid_state) == ESP_ERR_NVS_INVALID_STATE);
static_assert(std::to_underlying(idfxx::nvs::errc::invalid_length) == ESP_ERR_NVS_INVALID_LENGTH);
static_assert(std::to_underlying(idfxx::nvs::errc::no_free_pages) == ESP_ERR_NVS_NO_FREE_PAGES);
static_assert(std::to_underlying(idfxx::nvs::errc::value_too_long) == ESP_ERR_NVS_VALUE_TOO_LONG);
static_assert(std::to_underlying(idfxx::nvs::errc::part_not_found) == ESP_ERR_NVS_PART_NOT_FOUND);

// Verify key size matches ESP-IDF constant
static_assert(idfxx::nvs::flash::key_size == NVS_KEY_SIZE);

namespace {
const char* TAG = "idfxx::nvs";
}

namespace idfxx {

const nvs::error_category& nvs_category() noexcept {
    static const nvs::error_category instance{};
    return instance;
}

const char* nvs::error_category::name() const noexcept {
    return "nvs::Error";
}

std::string nvs::error_category::message(int ec) const {
    switch (nvs::errc(ec)) {
    case nvs::errc::not_found:
        return "A requested entry couldn't be found or namespace doesn't exist yet and mode is NVS_READONLY";
    case nvs::errc::type_mismatch:
        return "The type of set or get operation doesn't match the type of value stored in NVS";
    case nvs::errc::read_only:
        return "Storage handle was opened as read only";
    case nvs::errc::not_enough_space:
        return "There is not enough space in the underlying storage to save the value";
    case nvs::errc::invalid_name:
        return "Namespace name doesn't satisfy constraints";
    case nvs::errc::invalid_handle:
        return "Handle has been closed or is NULL";
    case nvs::errc::remove_failed:
        return "The value wasn't updated because flash write operation has failed";
    case nvs::errc::key_too_long:
        return "Key name is too long";
    case nvs::errc::invalid_state:
        return "NVS is in an inconsistent state due to a previous error. Call nvs::flash_init() again and create a new nvs object";
    case nvs::errc::invalid_length:
        return "String or blob length is not sufficient to store data";
    case nvs::errc::no_free_pages:
        return "NVS partition doesn't contain any empty pages";
    case nvs::errc::value_too_long:
        return "Value doesn't fit into the entry or string or blob length is longer than supported";
    case nvs::errc::part_not_found:
        return "Partition with specified name is not found in the partition table";
    case nvs::errc::new_version_found:
        return "NVS partition contains data in an unrecognized new format";
    case nvs::errc::xts_encr_failed:
        return "XTS encryption failed while writing NVS entry";
    case nvs::errc::xts_decr_failed:
        return "XTS decryption failed while reading NVS entry";
    case nvs::errc::xts_cfg_failed:
        return "XTS configuration setting failed";
    case nvs::errc::xts_cfg_not_found:
        return "XTS configuration not found";
    case nvs::errc::encr_not_supported:
        return "NVS encryption is not supported in this version";
    case nvs::errc::keys_not_initialized:
        return "NVS key partition is uninitialized";
    case nvs::errc::corrupt_key_part:
        return "NVS key partition is corrupt";
    case nvs::errc::wrong_encryption:
        return "NVS partition is marked as encrypted with generic flash encryption";
    default:
        return esp_err_to_name(static_cast<esp_err_t>(ec));
    }
}

static std::optional<nvs::errc> make_errc(esp_err_t e) noexcept {
    switch (e) {
    case ESP_ERR_NVS_NOT_FOUND:
        return nvs::errc::not_found;
    case ESP_ERR_NVS_TYPE_MISMATCH:
        return nvs::errc::type_mismatch;
    case ESP_ERR_NVS_READ_ONLY:
        return nvs::errc::read_only;
    case ESP_ERR_NVS_NOT_ENOUGH_SPACE:
        return nvs::errc::not_enough_space;
    case ESP_ERR_NVS_INVALID_NAME:
        return nvs::errc::invalid_name;
    case ESP_ERR_NVS_INVALID_HANDLE:
        return nvs::errc::invalid_handle;
    case ESP_ERR_NVS_REMOVE_FAILED:
        return nvs::errc::remove_failed;
    case ESP_ERR_NVS_KEY_TOO_LONG:
        return nvs::errc::key_too_long;
    case ESP_ERR_NVS_INVALID_STATE:
        return nvs::errc::invalid_state;
    case ESP_ERR_NVS_INVALID_LENGTH:
        return nvs::errc::invalid_length;
    case ESP_ERR_NVS_NO_FREE_PAGES:
        return nvs::errc::no_free_pages;
    case ESP_ERR_NVS_VALUE_TOO_LONG:
        return nvs::errc::value_too_long;
    case ESP_ERR_NVS_PART_NOT_FOUND:
        return nvs::errc::part_not_found;
    case ESP_ERR_NVS_NEW_VERSION_FOUND:
        return nvs::errc::new_version_found;
    case ESP_ERR_NVS_XTS_ENCR_FAILED:
        return nvs::errc::xts_encr_failed;
    case ESP_ERR_NVS_XTS_DECR_FAILED:
        return nvs::errc::xts_decr_failed;
    case ESP_ERR_NVS_XTS_CFG_FAILED:
        return nvs::errc::xts_cfg_failed;
    case ESP_ERR_NVS_XTS_CFG_NOT_FOUND:
        return nvs::errc::xts_cfg_not_found;
    case ESP_ERR_NVS_ENCR_NOT_SUPPORTED:
        return nvs::errc::encr_not_supported;
    case ESP_ERR_NVS_KEYS_NOT_INITIALIZED:
        return nvs::errc::keys_not_initialized;
    case ESP_ERR_NVS_CORRUPT_KEY_PART:
        return nvs::errc::corrupt_key_part;
    case ESP_ERR_NVS_WRONG_ENCRYPTION:
        return nvs::errc::wrong_encryption;
    default:
        return std::nullopt;
    }
}

std::unexpected<std::error_code> nvs_error(esp_err_t e) {
    if (e == ESP_ERR_NO_MEM) {
        raise_no_mem();
    }
    return std::unexpected(
        make_errc(e).transform([](nvs::errc ec) { return std::error_code{std::to_underlying(ec), nvs_category()}; }
        ).value_or(make_error_code(e))
    );
}

static result<nvs_handle_t> open_nvs(std::string_view namespace_name, bool read_only) {
    // Validate namespace name: must not be empty and max 15 characters
    if (namespace_name.empty() || namespace_name.length() > 15) {
        return error(nvs::errc::invalid_name);
    }
    std::string ns_str{namespace_name};
    nvs_handle_t handle;
    esp_err_t err = nvs_open(ns_str.c_str(), read_only ? NVS_READONLY : NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "Failed to open nvs namespace '%s': %s", ns_str.c_str(), esp_err_to_name(err));
        return nvs_error(err);
    }
    return handle;
}

result<std::unique_ptr<nvs>> nvs::make(std::string_view namespace_name, bool read_only) {
    return open_nvs(namespace_name, read_only).transform([&](auto handle) {
        return std::unique_ptr<nvs>(new nvs{handle, read_only});
    });
}

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
nvs::nvs(std::string_view namespace_name, bool read_only)
    : nvs(unwrap(open_nvs(namespace_name, read_only)), read_only) {}
#endif

nvs::nvs(nvs_handle_t handle, bool read_only)
    : _handle(handle)
    , _read_only(read_only) {}

nvs::~nvs() {
    nvs_close(_handle);
}

result<void> nvs::try_commit() {
    if (_read_only) {
        ESP_LOGD(TAG, "Cannot commit: handle is read-only");
        return error(nvs::errc::read_only);
    }
    esp_err_t err = nvs_commit(_handle);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "nvs commit failed: %s", esp_err_to_name(err));
        return nvs_error(err);
    }
    return {};
}

result<void> nvs::try_erase(std::string_view key) {
    if (_read_only) {
        ESP_LOGD(TAG, "Cannot erase: handle is read-only");
        return error(nvs::errc::read_only);
    }
    std::string key_str{key};
    esp_err_t err = nvs_erase_key(_handle, key_str.c_str());
    if (err != ESP_OK) {
        if (err != ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGD(TAG, "Failed to erase key '%s': %s", key_str.c_str(), esp_err_to_name(err));
        }
        return nvs_error(err);
    }
    return {};
}

result<void> nvs::try_erase_all() {
    if (_read_only) {
        ESP_LOGD(TAG, "Cannot erase_all: handle is read-only");
        return error(nvs::errc::read_only);
    }
    esp_err_t err = nvs_erase_all(_handle);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "Failed to erase all keys: %s", esp_err_to_name(err));
        return nvs_error(err);
    }
    return {};
}

result<void> nvs::try_set_string(std::string_view key, std::string_view value) {
    if (_read_only) {
        ESP_LOGD(TAG, "Cannot set_string: handle is read-only");
        return error(nvs::errc::read_only);
    }
    std::string key_str{key};
    std::string value_str{value};
    esp_err_t err = nvs_set_str(_handle, key_str.c_str(), value_str.c_str());
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "Failed to set string key '%s': %s", key_str.c_str(), esp_err_to_name(err));
        return nvs_error(err);
    }
    return {};
}

result<std::string> nvs::try_get_string(std::string_view key) {
    std::string key_str{key};

    // First, get the required size
    size_t required_size = 0;
    esp_err_t err = nvs_get_str(_handle, key_str.c_str(), nullptr, &required_size);
    if (err != ESP_OK) {
        if (err != ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGD(TAG, "Failed to get string key '%s': %s", key_str.c_str(), esp_err_to_name(err));
        }
        return nvs_error(err);
    }

    // Allocate buffer and get the string
    std::string value(required_size - 1, '\0'); // -1 for null terminator
    err = nvs_get_str(_handle, key_str.c_str(), value.data(), &required_size);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "Failed to get string key '%s': %s", key_str.c_str(), esp_err_to_name(err));
        return nvs_error(err);
    }

    return value;
}

result<void> nvs::try_set_blob(std::string_view key, const void* data, size_t length) {
    if (_read_only) {
        ESP_LOGD(TAG, "Cannot set_blob: handle is read-only");
        return error(nvs::errc::read_only);
    }
    std::string key_str{key};
    esp_err_t err = nvs_set_blob(_handle, key_str.c_str(), data, length);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "Failed to set blob key '%s': %s", key_str.c_str(), esp_err_to_name(err));
        return nvs_error(err);
    }
    return {};
}

result<void> nvs::try_set_blob(std::string_view key, std::span<const uint8_t> data) {
    return try_set_blob(key, data.data(), data.size());
}

result<std::vector<uint8_t>> nvs::try_get_blob(std::string_view key) {
    std::string key_str{key};

    // First, get the required size
    size_t required_size = 0;
    esp_err_t err = nvs_get_blob(_handle, key_str.c_str(), nullptr, &required_size);
    if (err != ESP_OK) {
        if (err != ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGD(TAG, "Failed to get blob key '%s': %s", key_str.c_str(), esp_err_to_name(err));
        }
        return nvs_error(err);
    }

    // Allocate buffer and get the blob
    std::vector<uint8_t> value(required_size);
    err = nvs_get_blob(_handle, key_str.c_str(), value.data(), &required_size);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "Failed to get blob key '%s': %s", key_str.c_str(), esp_err_to_name(err));
        return nvs_error(err);
    }
    return value;
}

// Template specializations for numeric types

// uint8_t
template<>
result<void> nvs::try_set_value<uint8_t>(std::string_view key, uint8_t value) {
    if (_read_only) {
        ESP_LOGD(TAG, "Cannot set uint8_t: handle is read-only");
        return error(nvs::errc::read_only);
    }
    std::string key_str{key};
    esp_err_t err = nvs_set_u8(_handle, key_str.c_str(), value);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "Failed to set uint8_t key '%s': %s", key_str.c_str(), esp_err_to_name(err));
        return nvs_error(err);
    }
    return {};
}

template<>
result<uint8_t> nvs::try_get_value<uint8_t>(std::string_view key) {
    std::string key_str{key};
    uint8_t value;
    esp_err_t err = nvs_get_u8(_handle, key_str.c_str(), &value);
    if (err != ESP_OK) {
        if (err != ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGD(TAG, "Failed to get uint8_t key '%s': %s", key_str.c_str(), esp_err_to_name(err));
        }
        return nvs_error(err);
    }
    return value;
}

// int8_t
template<>
result<void> nvs::try_set_value<int8_t>(std::string_view key, int8_t value) {
    if (_read_only) {
        return error(nvs::errc::read_only);
    }
    std::string key_str{key};
    esp_err_t err = nvs_set_i8(_handle, key_str.c_str(), value);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "Failed to set int8_t key '%s': %s", key_str.c_str(), esp_err_to_name(err));
        return nvs_error(err);
    }
    return {};
}

template<>
result<int8_t> nvs::try_get_value<int8_t>(std::string_view key) {
    std::string key_str{key};
    int8_t value;
    esp_err_t err = nvs_get_i8(_handle, key_str.c_str(), &value);
    if (err != ESP_OK) {
        if (err != ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGD(TAG, "Failed to get int8_t key '%s': %s", key_str.c_str(), esp_err_to_name(err));
        }
        return nvs_error(err);
    }
    return value;
}

// uint16_t
template<>
result<void> nvs::try_set_value<uint16_t>(std::string_view key, uint16_t value) {
    if (_read_only) {
        return error(nvs::errc::read_only);
    }
    std::string key_str{key};
    esp_err_t err = nvs_set_u16(_handle, key_str.c_str(), value);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "Failed to set uint16_t key '%s': %s", key_str.c_str(), esp_err_to_name(err));
        return nvs_error(err);
    }
    return {};
}

template<>
result<uint16_t> nvs::try_get_value<uint16_t>(std::string_view key) {
    std::string key_str{key};
    uint16_t value;
    esp_err_t err = nvs_get_u16(_handle, key_str.c_str(), &value);
    if (err != ESP_OK) {
        if (err != ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGD(TAG, "Failed to get uint16_t key '%s': %s", key_str.c_str(), esp_err_to_name(err));
        }
        return nvs_error(err);
    }
    return value;
}

// int16_t
template<>
result<void> nvs::try_set_value<int16_t>(std::string_view key, int16_t value) {
    if (_read_only) {
        return error(nvs::errc::read_only);
    }
    std::string key_str{key};
    esp_err_t err = nvs_set_i16(_handle, key_str.c_str(), value);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "Failed to set int16_t key '%s': %s", key_str.c_str(), esp_err_to_name(err));
        return nvs_error(err);
    }
    return {};
}

template<>
result<int16_t> nvs::try_get_value<int16_t>(std::string_view key) {
    std::string key_str{key};
    int16_t value;
    esp_err_t err = nvs_get_i16(_handle, key_str.c_str(), &value);
    if (err != ESP_OK) {
        if (err != ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGD(TAG, "Failed to get int16_t key '%s': %s", key_str.c_str(), esp_err_to_name(err));
        }
        return nvs_error(err);
    }
    return value;
}

// uint32_t
template<>
result<void> nvs::try_set_value<uint32_t>(std::string_view key, uint32_t value) {
    if (_read_only) {
        return error(nvs::errc::read_only);
    }
    std::string key_str{key};
    esp_err_t err = nvs_set_u32(_handle, key_str.c_str(), value);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "Failed to set uint32_t key '%s': %s", key_str.c_str(), esp_err_to_name(err));
        return nvs_error(err);
    }
    return {};
}

template<>
result<uint32_t> nvs::try_get_value<uint32_t>(std::string_view key) {
    std::string key_str{key};
    uint32_t value;
    esp_err_t err = nvs_get_u32(_handle, key_str.c_str(), &value);
    if (err != ESP_OK) {
        if (err != ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGD(TAG, "Failed to get uint32_t key '%s': %s", key_str.c_str(), esp_err_to_name(err));
        }
        return nvs_error(err);
    }
    return value;
}

// int32_t
template<>
result<void> nvs::try_set_value<int32_t>(std::string_view key, int32_t value) {
    if (_read_only) {
        return error(nvs::errc::read_only);
    }
    std::string key_str{key};
    esp_err_t err = nvs_set_i32(_handle, key_str.c_str(), value);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "Failed to set int32_t key '%s': %s", key_str.c_str(), esp_err_to_name(err));
        return nvs_error(err);
    }
    return {};
}

template<>
result<int32_t> nvs::try_get_value<int32_t>(std::string_view key) {
    std::string key_str{key};
    int32_t value;
    esp_err_t err = nvs_get_i32(_handle, key_str.c_str(), &value);
    if (err != ESP_OK) {
        if (err != ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGD(TAG, "Failed to get int32_t key '%s': %s", key_str.c_str(), esp_err_to_name(err));
        }
        return nvs_error(err);
    }
    return value;
}

// uint64_t
template<>
result<void> nvs::try_set_value<uint64_t>(std::string_view key, uint64_t value) {
    if (_read_only) {
        return error(nvs::errc::read_only);
    }
    std::string key_str{key};
    esp_err_t err = nvs_set_u64(_handle, key_str.c_str(), value);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "Failed to set uint64_t key '%s': %s", key_str.c_str(), esp_err_to_name(err));
        return nvs_error(err);
    }
    return {};
}

template<>
result<uint64_t> nvs::try_get_value<uint64_t>(std::string_view key) {
    std::string key_str{key};
    uint64_t value;
    esp_err_t err = nvs_get_u64(_handle, key_str.c_str(), &value);
    if (err != ESP_OK) {
        if (err != ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGD(TAG, "Failed to get uint64_t key '%s': %s", key_str.c_str(), esp_err_to_name(err));
        }
        return nvs_error(err);
    }
    return value;
}

// int64_t
template<>
result<void> nvs::try_set_value<int64_t>(std::string_view key, int64_t value) {
    if (_read_only) {
        return error(nvs::errc::read_only);
    }
    std::string key_str{key};
    esp_err_t err = nvs_set_i64(_handle, key_str.c_str(), value);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "Failed to set int64_t key '%s': %s", key_str.c_str(), esp_err_to_name(err));
        return nvs_error(err);
    }
    return {};
}

template<>
result<int64_t> nvs::try_get_value<int64_t>(std::string_view key) {
    std::string key_str{key};
    int64_t value;
    esp_err_t err = nvs_get_i64(_handle, key_str.c_str(), &value);
    if (err != ESP_OK) {
        if (err != ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGD(TAG, "Failed to get int64_t key '%s': %s", key_str.c_str(), esp_err_to_name(err));
        }
        return nvs_error(err);
    }
    return value;
}

result<void> nvs::flash::try_init() {
    esp_err_t err = nvs_flash_init();
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "nvs_flash_init failed: %s", esp_err_to_name(err));
        return nvs_error(err);
    }
    return {};
}

result<void> nvs::flash::try_init(const secure_config& cfg) {
    nvs_sec_cfg_t nvs_cfg{};
    std::copy(cfg.eky.begin(), cfg.eky.end(), nvs_cfg.eky);
    std::copy(cfg.tky.begin(), cfg.tky.end(), nvs_cfg.tky);
    esp_err_t err = nvs_flash_secure_init(&nvs_cfg);
    memset(&nvs_cfg, 0, sizeof(nvs_cfg)); // Clear sensitive data
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "nvs_flash_secure_init failed: %s", esp_err_to_name(err));
        return nvs_error(err);
    }
    return {};
}

result<void> nvs::flash::try_init(insecure_t) {
    esp_err_t err = nvs_flash_secure_init(nullptr);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "nvs_flash_init failed: %s", esp_err_to_name(err));
        return nvs_error(err);
    }
    return {};
}

result<void> nvs::flash::try_init(std::string_view partition_label) {
    std::string label_str{partition_label};
    esp_err_t err = nvs_flash_init_partition(label_str.c_str());
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "nvs_flash_init_partition('%s') failed: %s", label_str.c_str(), esp_err_to_name(err));
        return nvs_error(err);
    }
    return {};
}

result<void> nvs::flash::try_init(std::string_view partition_label, const secure_config& cfg) {
    nvs_sec_cfg_t nvs_cfg{};
    std::copy(cfg.eky.begin(), cfg.eky.end(), nvs_cfg.eky);
    std::copy(cfg.tky.begin(), cfg.tky.end(), nvs_cfg.tky);
    std::string label_str{partition_label};
    esp_err_t err = nvs_flash_secure_init_partition(label_str.c_str(), &nvs_cfg);
    memset(&nvs_cfg, 0, sizeof(nvs_cfg)); // Clear sensitive data
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "nvs_flash_secure_init_partition('%s') failed: %s", label_str.c_str(), esp_err_to_name(err));
        return nvs_error(err);
    }
    return {};
}

result<void> nvs::flash::try_init(insecure_t, std::string_view partition_label) {
    std::string label_str{partition_label};
    esp_err_t err = nvs_flash_secure_init_partition(label_str.c_str(), nullptr);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "nvs_flash_init_partition('%s') failed: %s", label_str.c_str(), esp_err_to_name(err));
        return nvs_error(err);
    }
    return {};
}

result<void> nvs::flash::try_deinit() {
    esp_err_t err = nvs_flash_deinit();
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "nvs_flash_deinit failed: %s", esp_err_to_name(err));
        return nvs_error(err);
    }
    return {};
}

result<void> nvs::flash::try_deinit(std::string_view partition_label) {
    std::string label_str{partition_label};
    esp_err_t err = nvs_flash_deinit_partition(label_str.c_str());
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "nvs_flash_deinit_partition('%s') failed: %s", label_str.c_str(), esp_err_to_name(err));
        return nvs_error(err);
    }
    return {};
}

result<void> nvs::flash::try_erase() {
    esp_err_t err = nvs_flash_erase();
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "nvs_flash_erase failed: %s", esp_err_to_name(err));
        return nvs_error(err);
    }
    return {};
}

result<void> nvs::flash::try_erase(std::string_view partition_label) {
    std::string label_str{partition_label};
    esp_err_t err = nvs_flash_erase_partition(label_str.c_str());
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "nvs_flash_erase_partition('%s') failed: %s", label_str.c_str(), esp_err_to_name(err));
        return nvs_error(err);
    }
    return {};
}

} // namespace idfxx
