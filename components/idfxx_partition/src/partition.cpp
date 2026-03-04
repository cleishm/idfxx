// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#include <idfxx/error>
#include <idfxx/partition>

#include <algorithm>
#include <array>
#include <esp_log.h>
#include <esp_partition.h>
#include <utility>

// Verify type enum values match ESP-IDF constants
static_assert(std::to_underlying(idfxx::partition::type::app) == ESP_PARTITION_TYPE_APP);
static_assert(std::to_underlying(idfxx::partition::type::data) == ESP_PARTITION_TYPE_DATA);
static_assert(std::to_underlying(idfxx::partition::type::bootloader) == ESP_PARTITION_TYPE_BOOTLOADER);
static_assert(std::to_underlying(idfxx::partition::type::partition_table) == ESP_PARTITION_TYPE_PARTITION_TABLE);
static_assert(std::to_underlying(idfxx::partition::type::any) == ESP_PARTITION_TYPE_ANY);

// Verify subtype enum values match ESP-IDF constants
static_assert(
    std::to_underlying(idfxx::partition::subtype::bootloader_primary) == ESP_PARTITION_SUBTYPE_BOOTLOADER_PRIMARY
);
static_assert(std::to_underlying(idfxx::partition::subtype::bootloader_ota) == ESP_PARTITION_SUBTYPE_BOOTLOADER_OTA);
static_assert(
    std::to_underlying(idfxx::partition::subtype::bootloader_recovery) == ESP_PARTITION_SUBTYPE_BOOTLOADER_RECOVERY
);
static_assert(
    std::to_underlying(idfxx::partition::subtype::partition_table_primary) ==
    ESP_PARTITION_SUBTYPE_PARTITION_TABLE_PRIMARY
);
static_assert(
    std::to_underlying(idfxx::partition::subtype::partition_table_ota) == ESP_PARTITION_SUBTYPE_PARTITION_TABLE_OTA
);
static_assert(std::to_underlying(idfxx::partition::subtype::app_factory) == ESP_PARTITION_SUBTYPE_APP_FACTORY);
static_assert(std::to_underlying(idfxx::partition::subtype::app_ota_0) == ESP_PARTITION_SUBTYPE_APP_OTA_0);
static_assert(std::to_underlying(idfxx::partition::subtype::app_ota_1) == ESP_PARTITION_SUBTYPE_APP_OTA_1);
static_assert(std::to_underlying(idfxx::partition::subtype::app_ota_2) == ESP_PARTITION_SUBTYPE_APP_OTA_2);
static_assert(std::to_underlying(idfxx::partition::subtype::app_ota_3) == ESP_PARTITION_SUBTYPE_APP_OTA_3);
static_assert(std::to_underlying(idfxx::partition::subtype::app_ota_4) == ESP_PARTITION_SUBTYPE_APP_OTA_4);
static_assert(std::to_underlying(idfxx::partition::subtype::app_ota_5) == ESP_PARTITION_SUBTYPE_APP_OTA_5);
static_assert(std::to_underlying(idfxx::partition::subtype::app_ota_6) == ESP_PARTITION_SUBTYPE_APP_OTA_6);
static_assert(std::to_underlying(idfxx::partition::subtype::app_ota_7) == ESP_PARTITION_SUBTYPE_APP_OTA_7);
static_assert(std::to_underlying(idfxx::partition::subtype::app_ota_8) == ESP_PARTITION_SUBTYPE_APP_OTA_8);
static_assert(std::to_underlying(idfxx::partition::subtype::app_ota_9) == ESP_PARTITION_SUBTYPE_APP_OTA_9);
static_assert(std::to_underlying(idfxx::partition::subtype::app_ota_10) == ESP_PARTITION_SUBTYPE_APP_OTA_10);
static_assert(std::to_underlying(idfxx::partition::subtype::app_ota_11) == ESP_PARTITION_SUBTYPE_APP_OTA_11);
static_assert(std::to_underlying(idfxx::partition::subtype::app_ota_12) == ESP_PARTITION_SUBTYPE_APP_OTA_12);
static_assert(std::to_underlying(idfxx::partition::subtype::app_ota_13) == ESP_PARTITION_SUBTYPE_APP_OTA_13);
static_assert(std::to_underlying(idfxx::partition::subtype::app_ota_14) == ESP_PARTITION_SUBTYPE_APP_OTA_14);
static_assert(std::to_underlying(idfxx::partition::subtype::app_ota_15) == ESP_PARTITION_SUBTYPE_APP_OTA_15);
static_assert(std::to_underlying(idfxx::partition::subtype::app_test) == ESP_PARTITION_SUBTYPE_APP_TEST);
static_assert(std::to_underlying(idfxx::partition::subtype::data_ota) == ESP_PARTITION_SUBTYPE_DATA_OTA);
static_assert(std::to_underlying(idfxx::partition::subtype::data_phy) == ESP_PARTITION_SUBTYPE_DATA_PHY);
static_assert(std::to_underlying(idfxx::partition::subtype::data_nvs) == ESP_PARTITION_SUBTYPE_DATA_NVS);
static_assert(std::to_underlying(idfxx::partition::subtype::data_coredump) == ESP_PARTITION_SUBTYPE_DATA_COREDUMP);
static_assert(std::to_underlying(idfxx::partition::subtype::data_nvs_keys) == ESP_PARTITION_SUBTYPE_DATA_NVS_KEYS);
static_assert(std::to_underlying(idfxx::partition::subtype::data_efuse_em) == ESP_PARTITION_SUBTYPE_DATA_EFUSE_EM);
static_assert(std::to_underlying(idfxx::partition::subtype::data_undefined) == ESP_PARTITION_SUBTYPE_DATA_UNDEFINED);
static_assert(std::to_underlying(idfxx::partition::subtype::data_esphttpd) == ESP_PARTITION_SUBTYPE_DATA_ESPHTTPD);
static_assert(std::to_underlying(idfxx::partition::subtype::data_fat) == ESP_PARTITION_SUBTYPE_DATA_FAT);
static_assert(std::to_underlying(idfxx::partition::subtype::data_spiffs) == ESP_PARTITION_SUBTYPE_DATA_SPIFFS);
static_assert(std::to_underlying(idfxx::partition::subtype::data_littlefs) == ESP_PARTITION_SUBTYPE_DATA_LITTLEFS);
static_assert(std::to_underlying(idfxx::partition::subtype::any) == ESP_PARTITION_SUBTYPE_ANY);

// Verify mmap_memory enum values match ESP-IDF constants
static_assert(std::to_underlying(idfxx::partition::mmap_memory::data) == ESP_PARTITION_MMAP_DATA);
static_assert(std::to_underlying(idfxx::partition::mmap_memory::inst) == ESP_PARTITION_MMAP_INST);

// Verify app_ota helper
static_assert(std::to_underlying(idfxx::app_ota(0)) == ESP_PARTITION_SUBTYPE_APP_OTA_0);
static_assert(std::to_underlying(idfxx::app_ota(15)) == ESP_PARTITION_SUBTYPE_APP_OTA_15);

namespace {
const char* TAG = "idfxx::partition";
}

namespace idfxx {

// =========================================================================
// Discovery
// =========================================================================

struct label_buf {
    std::array<char, 17> buf; // ESP-IDF labels are max 16 chars + null
    const char* c_str;

    explicit label_buf(std::string_view label) {
        if (label.empty()) {
            c_str = nullptr;
        } else {
            auto n = std::min(label.size(), buf.size() - 1);
            std::copy_n(label.data(), n, buf.data());
            buf[n] = '\0';
            c_str = buf.data();
        }
    }
};

static result<void> checked(esp_err_t err, const char* op) {
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "partition %s failed: %s", op, esp_err_to_name(err));
        return error(err);
    }
    return {};
}

static const esp_partition_t* find_first(enum partition::type pt, enum partition::subtype ps, std::string_view label) {
    label_buf lb{label};
    return esp_partition_find_first(
        static_cast<esp_partition_type_t>(pt), static_cast<esp_partition_subtype_t>(ps), lb.c_str
    );
}

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
partition partition::find(enum type pt, enum subtype ps, std::string_view label) {
    return unwrap(try_find(pt, ps, label));
}

partition partition::find(std::string_view label) {
    return unwrap(try_find(label));
}
#endif

result<partition> partition::try_find(enum type pt, enum subtype ps, std::string_view label) {
    const esp_partition_t* part = find_first(pt, ps, label);
    if (part == nullptr) {
        return error(errc::not_found);
    }
    return partition{part};
}

result<partition> partition::try_find(std::string_view label) {
    return try_find(partition::type::any, partition::subtype::any, label);
}

std::vector<partition> partition::find_all(enum type pt, enum subtype ps, std::string_view label) {
    label_buf lb{label};

    struct iterator_guard {
        esp_partition_iterator_t it;
        ~iterator_guard() {
            if (it) {
                esp_partition_iterator_release(it);
            }
        }
    };

    std::vector<partition> results;
    iterator_guard guard{
        esp_partition_find(static_cast<esp_partition_type_t>(pt), static_cast<esp_partition_subtype_t>(ps), lb.c_str)
    };
    while (guard.it != nullptr) {
        const esp_partition_t* part = esp_partition_get(guard.it);
        if (part != nullptr) {
            results.emplace_back(partition{part});
        }
        guard.it = esp_partition_next(guard.it);
    }
    return results;
}

// =========================================================================
// Read / Write / Erase
// =========================================================================

result<void> partition::try_read(size_t offset, void* dst, size_t size) const {
    return checked(esp_partition_read(_part, offset, dst, size), "read");
}

result<void> partition::try_read(size_t offset, std::span<uint8_t> dst) const {
    return try_read(offset, dst.data(), dst.size());
}

result<void> partition::try_write(size_t offset, const void* src, size_t size) {
    return checked(esp_partition_write(_part, offset, src, size), "write");
}

result<void> partition::try_write(size_t offset, std::span<const uint8_t> src) {
    return try_write(offset, src.data(), src.size());
}

result<void> partition::try_read_raw(size_t offset, void* dst, size_t size) const {
    return checked(esp_partition_read_raw(_part, offset, dst, size), "read_raw");
}

result<void> partition::try_read_raw(size_t offset, std::span<uint8_t> dst) const {
    return try_read_raw(offset, dst.data(), dst.size());
}

result<void> partition::try_write_raw(size_t offset, const void* src, size_t size) {
    return checked(esp_partition_write_raw(_part, offset, src, size), "write_raw");
}

result<void> partition::try_write_raw(size_t offset, std::span<const uint8_t> src) {
    return try_write_raw(offset, src.data(), src.size());
}

result<void> partition::try_erase_range(size_t offset, size_t size) {
    return checked(esp_partition_erase_range(_part, offset, size), "erase_range");
}

result<std::array<uint8_t, 32>> partition::try_sha256() const {
    std::array<uint8_t, 32> hash{};
    esp_err_t err = esp_partition_get_sha256(_part, hash.data());
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "partition sha256 failed: %s", esp_err_to_name(err));
        return error(err);
    }
    return hash;
}

result<partition::mmap_handle> partition::try_mmap(size_t offset, size_t size, enum mmap_memory memory) const {
    const void* ptr = nullptr;
    esp_partition_mmap_handle_t handle = 0;
    esp_err_t err =
        esp_partition_mmap(_part, offset, size, static_cast<esp_partition_mmap_memory_t>(memory), &ptr, &handle);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "partition mmap failed: %s", esp_err_to_name(err));
        return error(err);
    }
    return mmap_handle{ptr, size, handle};
}

bool partition::check_identity(const partition& other) const {
    return esp_partition_check_identity(_part, other._part);
}

// =========================================================================
// mmap_handle
// =========================================================================

partition::mmap_handle::mmap_handle(const void* ptr, size_t size, esp_partition_mmap_handle_t handle)
    : _ptr(ptr)
    , _size(size)
    , _handle(handle) {}

partition::mmap_handle::mmap_handle(mmap_handle&& other) noexcept
    : _ptr(std::exchange(other._ptr, nullptr))
    , _size(std::exchange(other._size, 0))
    , _handle(std::exchange(other._handle, 0)) {}

partition::mmap_handle& partition::mmap_handle::operator=(mmap_handle&& other) noexcept {
    if (this != &other) {
        if (_ptr != nullptr) {
            esp_partition_munmap(_handle);
        }
        _ptr = std::exchange(other._ptr, nullptr);
        _size = std::exchange(other._size, 0);
        _handle = std::exchange(other._handle, 0);
    }
    return *this;
}

partition::mmap_handle::~mmap_handle() {
    if (_ptr != nullptr) {
        esp_partition_munmap(_handle);
    }
}

esp_partition_mmap_handle_t partition::mmap_handle::release() noexcept {
    auto handle = _handle;
    _ptr = nullptr;
    _size = 0;
    _handle = 0;
    return handle;
}

} // namespace idfxx
