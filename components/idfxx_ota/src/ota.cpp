// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#include <idfxx/error>
#include <idfxx/ota>

#include <esp_app_desc.h>
#include <esp_log.h>
#include <esp_ota_ops.h>
#include <utility>

// Verify error codes match ESP-IDF constants
static_assert(std::to_underlying(idfxx::ota::errc::partition_conflict) == ESP_ERR_OTA_PARTITION_CONFLICT);
static_assert(std::to_underlying(idfxx::ota::errc::select_info_invalid) == ESP_ERR_OTA_SELECT_INFO_INVALID);
static_assert(std::to_underlying(idfxx::ota::errc::validate_failed) == ESP_ERR_OTA_VALIDATE_FAILED);
static_assert(std::to_underlying(idfxx::ota::errc::small_sec_ver) == ESP_ERR_OTA_SMALL_SEC_VER);
static_assert(std::to_underlying(idfxx::ota::errc::rollback_failed) == ESP_ERR_OTA_ROLLBACK_FAILED);
static_assert(std::to_underlying(idfxx::ota::errc::rollback_invalid_state) == ESP_ERR_OTA_ROLLBACK_INVALID_STATE);

// Verify image_state values match ESP-IDF constants
static_assert(std::to_underlying(idfxx::ota::image_state::new_image) == ESP_OTA_IMG_NEW);
static_assert(std::to_underlying(idfxx::ota::image_state::pending_verify) == ESP_OTA_IMG_PENDING_VERIFY);
static_assert(std::to_underlying(idfxx::ota::image_state::valid) == ESP_OTA_IMG_VALID);
static_assert(std::to_underlying(idfxx::ota::image_state::invalid) == ESP_OTA_IMG_INVALID);
static_assert(std::to_underlying(idfxx::ota::image_state::aborted) == ESP_OTA_IMG_ABORTED);
static_assert(std::to_underlying(idfxx::ota::image_state::undefined) == ESP_OTA_IMG_UNDEFINED);

// Verify sequential erase constant matches ESP-IDF
static_assert(OTA_WITH_SEQUENTIAL_WRITES == 0xfffffffe);

namespace {
const char* TAG = "idfxx::ota";
}

namespace idfxx::ota {

const error_category& ota_category() noexcept {
    static const error_category instance{};
    return instance;
}

const char* error_category::name() const noexcept {
    return "ota::Error";
}

std::string error_category::message(int ec) const {
    switch (errc(ec)) {
    case errc::partition_conflict:
        return "Cannot update the currently running partition";
    case errc::select_info_invalid:
        return "OTA data partition contains invalid content";
    case errc::validate_failed:
        return "OTA app image is invalid";
    case errc::small_sec_ver:
        return "Firmware secure version is less than the running firmware";
    case errc::rollback_failed:
        return "No valid firmware in passive partition for rollback";
    case errc::rollback_invalid_state:
        return "Running app is still pending verification; confirm before updating";
    default:
        return esp_err_to_name(static_cast<esp_err_t>(ec));
    }
}

static bool is_ota_error(esp_err_t e) noexcept {
    return (e & 0xFF00) == ESP_ERR_OTA_BASE;
}

std::unexpected<std::error_code> ota_error(esp_err_t e) {
    if (e == ESP_ERR_NO_MEM) {
        raise_no_mem();
    }
    if (is_ota_error(e)) {
        return std::unexpected(std::error_code{e, ota_category()});
    }
    return error(e);
}

static result<void> checked(esp_err_t err, const char* op) {
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "%s failed: %s", op, esp_err_to_name(err));
        return ota_error(err);
    }
    return {};
}

// =============================================================================
// app_description
// =============================================================================

app_description::app_description(const esp_app_desc_t& desc)
    : _desc(desc) {}

std::string_view app_description::version() const {
    return _desc.version;
}

std::string_view app_description::project_name() const {
    return _desc.project_name;
}

std::string_view app_description::time() const {
    return _desc.time;
}

std::string_view app_description::date() const {
    return _desc.date;
}

std::string_view app_description::idf_ver() const {
    return _desc.idf_ver;
}

uint32_t app_description::secure_version() const {
    return _desc.secure_version;
}

std::span<const uint8_t, 32> app_description::app_elf_sha256() const {
    return std::span<const uint8_t, 32>(_desc.app_elf_sha256);
}

// =============================================================================
// update
// =============================================================================

update::update(esp_ota_handle_t handle)
    : _handle(handle) {}

update::update(update&& other) noexcept
    : _handle(other._handle) {
    other._handle = 0;
}

update& update::operator=(update&& other) noexcept {
    if (this != &other) {
        if (_handle != 0) {
            esp_ota_abort(_handle);
        }
        _handle = other._handle;
        other._handle = 0;
    }
    return *this;
}

update::~update() {
    if (_handle != 0) {
        esp_ota_abort(_handle);
    }
}

static result<esp_ota_handle_t> begin_ota(const partition& part, size_t image_size) {
    esp_ota_handle_t handle;
    if (auto r = checked(esp_ota_begin(part.idf_handle(), image_size, &handle), "esp_ota_begin"); !r) {
        return std::unexpected(r.error());
    }
    return handle;
}

result<update> update::make(const partition& part) {
    return begin_ota(part, OTA_SIZE_UNKNOWN).transform([](auto handle) { return update{handle}; });
}

result<update> update::make(const partition& part, size_t image_size) {
    if (image_size == OTA_SIZE_UNKNOWN || image_size == OTA_WITH_SEQUENTIAL_WRITES) {
        return error(idfxx::errc::invalid_arg);
    }
    return begin_ota(part, image_size).transform([](auto handle) { return update{handle}; });
}

result<update> update::make(const partition& part, sequential_erase_tag) {
    return begin_ota(part, OTA_WITH_SEQUENTIAL_WRITES).transform([](auto handle) { return update{handle}; });
}

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
update::update(const partition& part)
    : update(unwrap(begin_ota(part, OTA_SIZE_UNKNOWN))) {}

update::update(const partition& part, size_t image_size)
    : update(unwrap(begin_ota(part, image_size))) {}

update::update(const partition& part, sequential_erase_tag)
    : update(unwrap(begin_ota(part, OTA_WITH_SEQUENTIAL_WRITES))) {}

update update::resume(const partition& part, size_t offset) {
    return unwrap(try_resume(part, offset));
}

update update::resume(const partition& part, size_t offset, size_t erase_size) {
    return unwrap(try_resume(part, offset, erase_size));
}

update update::resume(const partition& part, size_t offset, sequential_erase_tag) {
    return unwrap(try_resume(part, offset, sequential_erase));
}
#endif

static result<esp_ota_handle_t> resume_ota(const partition& part, size_t erase_size, size_t offset) {
    esp_ota_handle_t handle;
    if (auto r = checked(esp_ota_resume(part.idf_handle(), erase_size, offset, &handle), "esp_ota_resume"); !r) {
        return std::unexpected(r.error());
    }
    return handle;
}

result<update> update::try_resume(const partition& part, size_t offset) {
    return resume_ota(part, OTA_SIZE_UNKNOWN, offset).transform([](auto handle) { return update{handle}; });
}

result<update> update::try_resume(const partition& part, size_t offset, size_t erase_size) {
    if (erase_size == OTA_SIZE_UNKNOWN || erase_size == OTA_WITH_SEQUENTIAL_WRITES) {
        return error(idfxx::errc::invalid_arg);
    }
    return resume_ota(part, erase_size, offset).transform([](auto handle) { return update{handle}; });
}

result<update> update::try_resume(const partition& part, size_t offset, sequential_erase_tag) {
    return resume_ota(part, OTA_WITH_SEQUENTIAL_WRITES, offset).transform([](auto handle) { return update{handle}; });
}

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
void update::set_final_partition(const partition& final_part, bool copy) {
    unwrap(try_set_final_partition(final_part, copy));
}
#endif

result<void> update::try_set_final_partition(const partition& final_part, bool copy) {
    if (_handle == 0) {
        return error(idfxx::errc::invalid_state);
    }
    return checked(esp_ota_set_final_partition(_handle, final_part.idf_handle(), copy), "esp_ota_set_final_partition");
}

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
void update::write(const void* data, size_t size) {
    unwrap(try_write(data, size));
}

void update::write(std::span<const uint8_t> data) {
    unwrap(try_write(data));
}

void update::write_with_offset(uint32_t offset, const void* data, size_t size) {
    unwrap(try_write_with_offset(offset, data, size));
}

void update::write_with_offset(uint32_t offset, std::span<const uint8_t> data) {
    unwrap(try_write_with_offset(offset, data));
}
#endif

result<void> update::try_write(const void* data, size_t size) {
    if (_handle == 0) {
        return error(idfxx::errc::invalid_state);
    }
    return checked(esp_ota_write(_handle, data, size), "esp_ota_write");
}

result<void> update::try_write(std::span<const uint8_t> data) {
    return try_write(data.data(), data.size());
}

result<void> update::try_write_with_offset(uint32_t offset, const void* data, size_t size) {
    if (_handle == 0) {
        return error(idfxx::errc::invalid_state);
    }
    return checked(esp_ota_write_with_offset(_handle, data, size, offset), "esp_ota_write_with_offset");
}

result<void> update::try_write_with_offset(uint32_t offset, std::span<const uint8_t> data) {
    return try_write_with_offset(offset, data.data(), data.size());
}

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
void update::end() {
    unwrap(try_end());
}

void update::abort() {
    unwrap(try_abort());
}
#endif

result<void> update::try_end() {
    if (_handle == 0) {
        return error(idfxx::errc::invalid_state);
    }
    esp_ota_handle_t handle = _handle;
    _handle = 0;
    return checked(esp_ota_end(handle), "esp_ota_end");
}

result<void> update::try_abort() {
    if (_handle == 0) {
        return error(idfxx::errc::invalid_state);
    }
    esp_ota_handle_t handle = _handle;
    _handle = 0;
    return checked(esp_ota_abort(handle), "esp_ota_abort");
}

// =============================================================================
// Free functions - Boot partition management
// =============================================================================

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
void set_boot_partition(const partition& part) {
    unwrap(try_set_boot_partition(part));
}

partition boot_partition() {
    return unwrap(try_boot_partition());
}

partition running_partition() {
    return unwrap(try_running_partition());
}

partition next_update_partition(const partition* start_from) {
    return unwrap(try_next_update_partition(start_from));
}
#endif

result<void> try_set_boot_partition(const partition& part) {
    return checked(esp_ota_set_boot_partition(part.idf_handle()), "esp_ota_set_boot_partition");
}

result<partition> try_boot_partition() {
    const esp_partition_t* part = esp_ota_get_boot_partition();
    if (part == nullptr) {
        return error(idfxx::errc::not_found);
    }
    return partition::from_handle(part);
}

result<partition> try_running_partition() {
    const esp_partition_t* part = esp_ota_get_running_partition();
    if (part == nullptr) {
        return error(idfxx::errc::not_found);
    }
    return partition::from_handle(part);
}

result<partition> try_next_update_partition(const partition* start_from) {
    const esp_partition_t* sf = start_from ? start_from->idf_handle() : nullptr;
    const esp_partition_t* part = esp_ota_get_next_update_partition(sf);
    if (part == nullptr) {
        return error(idfxx::errc::not_found);
    }
    return partition::from_handle(part);
}

// =============================================================================
// Free functions - App description
// =============================================================================

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
app_description partition_description(const partition& part) {
    return unwrap(try_partition_description(part));
}
#endif

result<app_description> try_partition_description(const partition& part) {
    esp_app_desc_t desc{};
    if (auto r =
            checked(esp_ota_get_partition_description(part.idf_handle(), &desc), "esp_ota_get_partition_description");
        !r) {
        return std::unexpected(r.error());
    }
    return app_description{desc};
}

// =============================================================================
// Free functions - State management
// =============================================================================

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
void mark_valid() {
    unwrap(try_mark_valid());
}

void mark_invalid_and_rollback() {
    unwrap(try_mark_invalid_and_rollback());
    // Should not reach here - esp_ota_mark_app_invalid_rollback_and_reboot reboots on success
    __builtin_unreachable();
}

image_state partition_state(const partition& part) {
    return unwrap(try_partition_state(part));
}
#endif

result<void> try_mark_valid() {
    return checked(esp_ota_mark_app_valid_cancel_rollback(), "esp_ota_mark_app_valid_cancel_rollback");
}

result<void> try_mark_invalid_and_rollback() {
    // Only returns on failure - on success the device reboots
    return checked(esp_ota_mark_app_invalid_rollback_and_reboot(), "esp_ota_mark_app_invalid_rollback_and_reboot");
}

result<image_state> try_partition_state(const partition& part) {
    esp_ota_img_states_t state;
    if (auto r = checked(esp_ota_get_state_partition(part.idf_handle(), &state), "esp_ota_get_state_partition"); !r) {
        return std::unexpected(r.error());
    }
    return static_cast<image_state>(state);
}

// =============================================================================
// Free functions - Rollback checking
// =============================================================================

bool rollback_possible() {
    return esp_ota_check_rollback_is_possible();
}

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
partition last_invalid_partition() {
    return unwrap(try_last_invalid_partition());
}
#endif

result<partition> try_last_invalid_partition() {
    const esp_partition_t* part = esp_ota_get_last_invalid_partition();
    if (part == nullptr) {
        return error(idfxx::errc::not_found);
    }
    return partition::from_handle(part);
}

// =============================================================================
// Free functions - Partition maintenance
// =============================================================================

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
void erase_last_boot_app_partition() {
    unwrap(try_erase_last_boot_app_partition());
}

void invalidate_inactive_ota_data_slot() {
    unwrap(try_invalidate_inactive_ota_data_slot());
}
#endif

result<void> try_erase_last_boot_app_partition() {
    return checked(esp_ota_erase_last_boot_app_partition(), "esp_ota_erase_last_boot_app_partition");
}

result<void> try_invalidate_inactive_ota_data_slot() {
    return checked(esp_ota_invalidate_inactive_ota_data_slot(), "esp_ota_invalidate_inactive_ota_data_slot");
}

// =============================================================================
// Free functions - Partition count
// =============================================================================

size_t app_partition_count() {
    return esp_ota_get_app_partition_count();
}

} // namespace idfxx::ota
