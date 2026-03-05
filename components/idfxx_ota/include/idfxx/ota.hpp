// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

/**
 * @headerfile <idfxx/ota>
 * @file ota.hpp
 * @brief OTA firmware update management.
 *
 * @defgroup idfxx_ota OTA Component
 * @brief OTA firmware update session management with rollback support.
 *
 * Provides OTA update sessions, boot partition control, image state management,
 * and rollback support for firmware updates.
 *
 * Depends on @ref idfxx_core for error handling and @ref idfxx_partition for partition access.
 * @{
 */

#include <idfxx/error>
#include <idfxx/partition>

#include <cstddef>
#include <cstdint>
#include <esp_app_desc.h>
#include <span>
#include <string_view>

typedef uint32_t esp_ota_handle_t;

namespace idfxx::ota {

/**
 * @brief Tag type for sequential (incremental) flash erase mode.
 *
 * When passed to the update constructor or make(), the flash is erased
 * incrementally as data is written, rather than all at once.
 */
struct sequential_erase_tag {};

/**
 * @brief Tag value for sequential (incremental) flash erase mode.
 *
 * @see sequential_erase_tag
 */
inline constexpr sequential_erase_tag sequential_erase{};

/**
 * @brief Error codes for OTA operations.
 */
enum class errc : esp_err_t {
    // clang-format off
    partition_conflict     = 0x1501, /*!< Cannot update the currently running partition. */
    select_info_invalid    = 0x1502, /*!< OTA data partition contains invalid content. */
    validate_failed        = 0x1503, /*!< OTA app image is invalid. */
    small_sec_ver          = 0x1504, /*!< Firmware secure version is less than the running firmware. */
    rollback_failed        = 0x1505, /*!< No valid firmware in passive partition for rollback. */
    rollback_invalid_state = 0x1506, /*!< Running app is still pending verification; confirm before updating. */
    // clang-format on
};

/**
 * @brief Error category for OTA errors.
 */
class error_category : public std::error_category {
public:
    /** @brief Returns the name of the error category. */
    [[nodiscard]] const char* name() const noexcept override final;

    /** @brief Returns a human-readable message for the given error code. */
    [[nodiscard]] std::string message(int ec) const override final;
};

/**
 * @brief OTA image state.
 *
 * Describes the lifecycle state of a firmware image in the OTA data partition.
 */
enum class image_state : uint32_t {
    // clang-format off
    new_image      = 0x0U,        /*!< Newly written image, not yet booted. */
    pending_verify = 0x1U,        /*!< First boot completed, awaiting confirmation. */
    valid          = 0x2U,        /*!< Confirmed as workable. */
    invalid        = 0x3U,        /*!< Confirmed as non-workable. */
    aborted        = 0x4U,        /*!< Failed to confirm workability during first boot. */
    undefined      = 0xFFFFFFFFU, /*!< No OTA state recorded for this partition. */
    // clang-format on
};

/**
 * @headerfile <idfxx/ota>
 * @brief Application description from a firmware image.
 *
 * Provides read-only access to metadata embedded in a firmware image,
 * including version, project name, build time, and ELF SHA-256 hash.
 */
class app_description {
public:
    /** @brief Returns the application version string. */
    [[nodiscard]] std::string_view version() const;

    /** @brief Returns the project name. */
    [[nodiscard]] std::string_view project_name() const;

    /** @brief Returns the compile time string. */
    [[nodiscard]] std::string_view time() const;

    /** @brief Returns the compile date string. */
    [[nodiscard]] std::string_view date() const;

    /** @brief Returns the ESP-IDF version string used to build the image. */
    [[nodiscard]] std::string_view idf_ver() const;

    /** @brief Returns the secure version counter. */
    [[nodiscard]] uint32_t secure_version() const;

    /** @brief Returns the SHA-256 hash of the application ELF file. */
    [[nodiscard]] std::span<const uint8_t, 32> app_elf_sha256() const;

    /** @brief Returns a reference to the underlying ESP-IDF app description structure. */
    [[nodiscard]] const esp_app_desc_t& idf_handle() const { return _desc; }

private:
    friend result<app_description> try_partition_description(const partition&);

    explicit app_description(const esp_app_desc_t& desc);

    esp_app_desc_t _desc;
};

/**
 * @headerfile <idfxx/ota>
 * @brief RAII OTA update session.
 *
 * Manages an OTA update operation, including writing firmware data and
 * finalizing the update. The destructor aborts the update if end() has
 * not been called. Move-only.
 *
 * @code
 * auto part = idfxx::ota::next_update_partition();
 * idfxx::ota::update upd(part);
 * upd.write(data.data(), data.size());
 * upd.end();
 * idfxx::ota::set_boot_partition(part);
 * @endcode
 */
class update {
public:
#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Begins an OTA update, erasing the entire partition.
     *
     * @param part Target partition for the update.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    [[nodiscard]] explicit update(const partition& part);

    /**
     * @brief Begins an OTA update, erasing space for the specified image size.
     *
     * @param part       Target partition for the update.
     * @param image_size Expected image size in bytes.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    [[nodiscard]] explicit update(const partition& part, size_t image_size);

    /**
     * @brief Begins an OTA update with sequential (incremental) flash erase.
     *
     * Flash is erased incrementally as data is written, rather than all at once.
     *
     * @param part Target partition for the update.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    [[nodiscard]] explicit update(const partition& part, sequential_erase_tag);

    /**
     * @brief Resumes an interrupted OTA update, erasing all remaining flash.
     *
     * Continues writing to the partition from the specified offset without
     * re-erasing already-written data.
     *
     * @param part   Target partition for the update.
     * @param offset Byte offset from which to resume writing.
     *
     * @return The resumed update session.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    [[nodiscard]] static update resume(const partition& part, size_t offset);

    /**
     * @brief Resumes an interrupted OTA update, erasing the specified amount of flash.
     *
     * Continues writing to the partition from the specified offset without
     * re-erasing already-written data.
     *
     * @param part       Target partition for the update.
     * @param offset     Byte offset from which to resume writing.
     * @param erase_size Amount of flash to erase ahead in bytes.
     *
     * @return The resumed update session.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    [[nodiscard]] static update resume(const partition& part, size_t offset, size_t erase_size);

    /**
     * @brief Resumes an interrupted OTA update with sequential (incremental) flash erase.
     *
     * Continues writing to the partition from the specified offset without
     * re-erasing already-written data. Flash is erased incrementally as data is written.
     *
     * @param part   Target partition for the update.
     * @param offset Byte offset from which to resume writing.
     *
     * @return The resumed update session.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    [[nodiscard]] static update resume(const partition& part, size_t offset, sequential_erase_tag);
#endif

    /**
     * @brief Begins an OTA update, erasing the entire partition.
     *
     * @param part Target partition for the update.
     *
     * @return The update session, or an error.
     * @retval ota::errc::partition_conflict if the partition is the currently running partition.
     * @retval ota::errc::rollback_invalid_state if the running app has not confirmed state.
     */
    [[nodiscard]] static result<update> make(const partition& part);

    /**
     * @brief Begins an OTA update, erasing space for the specified image size.
     *
     * @param part       Target partition for the update.
     * @param image_size Expected image size in bytes.
     *
     * @return The update session, or an error.
     * @retval idfxx::errc::invalid_arg if image_size is a reserved sentinel value.
     * @retval ota::errc::partition_conflict if the partition is the currently running partition.
     * @retval ota::errc::rollback_invalid_state if the running app has not confirmed state.
     */
    [[nodiscard]] static result<update> make(const partition& part, size_t image_size);

    /**
     * @brief Begins an OTA update with sequential (incremental) flash erase.
     *
     * Flash is erased incrementally as data is written, rather than all at once.
     *
     * @param part Target partition for the update.
     *
     * @return The update session, or an error.
     * @retval ota::errc::partition_conflict if the partition is the currently running partition.
     * @retval ota::errc::rollback_invalid_state if the running app has not confirmed state.
     */
    [[nodiscard]] static result<update> make(const partition& part, sequential_erase_tag);

    /**
     * @brief Resumes an interrupted OTA update, erasing all remaining flash.
     *
     * Continues writing to the partition from the specified offset without
     * re-erasing already-written data.
     *
     * @param part   Target partition for the update.
     * @param offset Byte offset from which to resume writing.
     *
     * @return The resumed update session, or an error.
     * @retval ota::errc::partition_conflict if the partition is the currently running partition.
     */
    [[nodiscard]] static result<update> try_resume(const partition& part, size_t offset);

    /**
     * @brief Resumes an interrupted OTA update, erasing the specified amount of flash.
     *
     * Continues writing to the partition from the specified offset without
     * re-erasing already-written data.
     *
     * @param part       Target partition for the update.
     * @param offset     Byte offset from which to resume writing.
     * @param erase_size Amount of flash to erase ahead in bytes.
     *
     * @return The resumed update session, or an error.
     * @retval idfxx::errc::invalid_arg if erase_size is a reserved sentinel value.
     * @retval ota::errc::partition_conflict if the partition is the currently running partition.
     */
    [[nodiscard]] static result<update> try_resume(const partition& part, size_t offset, size_t erase_size);

    /**
     * @brief Resumes an interrupted OTA update with sequential (incremental) flash erase.
     *
     * Continues writing to the partition from the specified offset without
     * re-erasing already-written data. Flash is erased incrementally as data is written.
     *
     * @param part   Target partition for the update.
     * @param offset Byte offset from which to resume writing.
     *
     * @return The resumed update session, or an error.
     * @retval ota::errc::partition_conflict if the partition is the currently running partition.
     */
    [[nodiscard]] static result<update> try_resume(const partition& part, size_t offset, sequential_erase_tag);

    ~update();

    update(const update&) = delete;
    update& operator=(const update&) = delete;

    /** @brief Move constructor. Transfers session ownership. */
    update(update&& other) noexcept;

    /** @brief Move assignment. Aborts any active session before transferring. */
    update& operator=(update&& other) noexcept;

    // =========================================================================
    // Final partition
    // =========================================================================

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Redirects the validated image to a different final partition.
     *
     * Must be called before the first write(). If copy is true, end() will
     * automatically copy the staging partition to the final partition.
     *
     * @param final_part The final destination partition.
     * @param copy       If true, end() copies staging to final automatically.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    void set_final_partition(const partition& final_part, bool copy = true);
#endif

    /**
     * @brief Redirects the validated image to a different final partition.
     *
     * Must be called before the first write(). If copy is true, end() will
     * automatically copy the staging partition to the final partition.
     *
     * @param final_part The final destination partition.
     * @param copy       If true, end() copies staging to final automatically.
     *
     * @return Success, or an error.
     */
    [[nodiscard]] result<void> try_set_final_partition(const partition& final_part, bool copy = true);

    // =========================================================================
    // Write
    // =========================================================================

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Writes data sequentially to the OTA partition.
     *
     * @param data Pointer to the data buffer.
     * @param size Number of bytes to write.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    void write(const void* data, size_t size);

    /**
     * @brief Writes data sequentially to the OTA partition.
     *
     * @param data Data to write.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    void write(std::span<const uint8_t> data);

    /**
     * @brief Writes data at a specific offset in the OTA partition.
     *
     * Supports non-contiguous writes. If flash encryption is enabled,
     * data should be 16-byte aligned.
     *
     * @param offset Offset within the partition.
     * @param data   Pointer to the data buffer.
     * @param size   Number of bytes to write.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    void write_with_offset(uint32_t offset, const void* data, size_t size);

    /**
     * @brief Writes data at a specific offset in the OTA partition.
     *
     * Supports non-contiguous writes. If flash encryption is enabled,
     * data should be 16-byte aligned.
     *
     * @param offset Offset within the partition.
     * @param data   Data to write.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    void write_with_offset(uint32_t offset, std::span<const uint8_t> data);
#endif

    /**
     * @brief Writes data sequentially to the OTA partition.
     *
     * @param data Pointer to the data buffer.
     * @param size Number of bytes to write.
     *
     * @return Success, or an error.
     * @retval ota::errc::validate_failed if the first byte contains an invalid image magic byte.
     */
    [[nodiscard]] result<void> try_write(const void* data, size_t size);

    /**
     * @brief Writes data sequentially to the OTA partition.
     *
     * @param data Data to write.
     *
     * @return Success, or an error.
     * @retval ota::errc::validate_failed if the first byte contains an invalid image magic byte.
     */
    [[nodiscard]] result<void> try_write(std::span<const uint8_t> data);

    /**
     * @brief Writes data at a specific offset in the OTA partition.
     *
     * Supports non-contiguous writes. If flash encryption is enabled,
     * data should be 16-byte aligned.
     *
     * @param offset Offset within the partition.
     * @param data   Pointer to the data buffer.
     * @param size   Number of bytes to write.
     *
     * @return Success, or an error.
     */
    [[nodiscard]] result<void> try_write_with_offset(uint32_t offset, const void* data, size_t size);

    /**
     * @brief Writes data at a specific offset in the OTA partition.
     *
     * Supports non-contiguous writes. If flash encryption is enabled,
     * data should be 16-byte aligned.
     *
     * @param offset Offset within the partition.
     * @param data   Data to write.
     *
     * @return Success, or an error.
     */
    [[nodiscard]] result<void> try_write_with_offset(uint32_t offset, std::span<const uint8_t> data);

    // =========================================================================
    // End / Abort
    // =========================================================================

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Finalizes the OTA update and validates the written image.
     *
     * After calling end(), the handle is invalidated and the session is complete.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    void end();

    /**
     * @brief Aborts the OTA update and frees associated resources.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    void abort();
#endif

    /**
     * @brief Finalizes the OTA update and validates the written image.
     *
     * After calling try_end(), the handle is invalidated and the session is complete.
     *
     * @return Success, or an error.
     * @retval ota::errc::validate_failed if the image is invalid.
     */
    [[nodiscard]] result<void> try_end();

    /**
     * @brief Aborts the OTA update and frees associated resources.
     *
     * @return Success, or an error.
     */
    [[nodiscard]] result<void> try_abort();

private:
    explicit update(esp_ota_handle_t handle);

    esp_ota_handle_t _handle = 0;
};

// =============================================================================
// Free functions - Boot partition management
// =============================================================================

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
/**
 * @brief Sets the boot partition for the next reboot.
 *
 * @param part Partition containing the app image to boot.
 *
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
 * @throws std::system_error on failure.
 */
void set_boot_partition(const partition& part);

/**
 * @brief Returns the currently configured boot partition.
 *
 * @return The boot partition.
 *
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
 * @throws std::system_error with idfxx::errc::not_found if not available.
 */
[[nodiscard]] partition boot_partition();

/**
 * @brief Returns the partition of the currently running application.
 *
 * @return The running partition.
 *
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
 * @throws std::system_error with idfxx::errc::not_found if not available.
 */
[[nodiscard]] partition running_partition();

/**
 * @brief Returns the next OTA app partition to write an update to.
 *
 * Finds the next partition round-robin, starting from the current running partition.
 *
 * @param start_from If not null, use this partition as the starting point instead
 *                   of the running partition.
 *
 * @return The next update partition.
 *
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
 * @throws std::system_error with idfxx::errc::not_found if no eligible partition exists.
 */
[[nodiscard]] partition next_update_partition(const partition* start_from = nullptr);
#endif

/**
 * @brief Sets the boot partition for the next reboot.
 *
 * @param part Partition containing the app image to boot.
 *
 * @return Success, or an error.
 * @retval ota::errc::validate_failed if the partition contains an invalid app image.
 */
[[nodiscard]] result<void> try_set_boot_partition(const partition& part);

/**
 * @brief Returns the currently configured boot partition.
 *
 * @return The boot partition, or an error.
 * @retval idfxx::errc::not_found if the partition table is invalid or a flash read failed.
 */
[[nodiscard]] result<partition> try_boot_partition();

/**
 * @brief Returns the partition of the currently running application.
 *
 * @return The running partition, or an error.
 * @retval idfxx::errc::not_found if no partition is found or a flash read failed.
 */
[[nodiscard]] result<partition> try_running_partition();

/**
 * @brief Returns the next OTA app partition to write an update to.
 *
 * Finds the next partition round-robin, starting from the current running partition.
 *
 * @param start_from If not null, use this partition as the starting point instead
 *                   of the running partition.
 *
 * @return The next update partition, or an error.
 * @retval idfxx::errc::not_found if no eligible partition exists.
 */
[[nodiscard]] result<partition> try_next_update_partition(const partition* start_from = nullptr);

// =============================================================================
// Free functions - App description
// =============================================================================

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
/**
 * @brief Returns the application description from a partition.
 *
 * @param part An application partition to read from.
 *
 * @return The app description.
 *
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
 * @throws std::system_error on failure.
 */
[[nodiscard]] app_description partition_description(const partition& part);
#endif

/**
 * @brief Returns the application description from a partition.
 *
 * @param part An application partition to read from.
 *
 * @return The app description, or an error.
 * @retval idfxx::errc::not_found if the partition does not contain a valid app descriptor.
 */
[[nodiscard]] result<app_description> try_partition_description(const partition& part);

// =============================================================================
// Free functions - State management
// =============================================================================

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
/**
 * @brief Marks the running app as valid, cancelling any pending rollback.
 *
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
 * @throws std::system_error on failure.
 */
void mark_valid();

/**
 * @brief Marks the running app as invalid and triggers a rollback with reboot.
 *
 * On success this function reboots the device and does not return.
 *
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
 * @throws std::system_error on failure.
 */
[[noreturn]] void mark_invalid_and_rollback();

/**
 * @brief Returns the OTA image state for a partition.
 *
 * @param part The partition to query.
 *
 * @return The image state.
 *
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
 * @throws std::system_error on failure.
 */
[[nodiscard]] image_state partition_state(const partition& part);
#endif

/**
 * @brief Marks the running app as valid, cancelling any pending rollback.
 *
 * @return Success, or an error.
 */
[[nodiscard]] result<void> try_mark_valid();

/**
 * @brief Marks the running app as invalid and triggers a rollback with reboot.
 *
 * On success this function reboots the device and does not return.
 *
 * @return An error (only returns on failure).
 * @retval ota::errc::rollback_failed if no valid firmware exists for rollback.
 */
[[nodiscard]] result<void> try_mark_invalid_and_rollback();

/**
 * @brief Returns the OTA image state for a partition.
 *
 * @param part The partition to query.
 *
 * @return The image state, or an error.
 */
[[nodiscard]] result<image_state> try_partition_state(const partition& part);

// =============================================================================
// Free functions - Rollback checking
// =============================================================================

/**
 * @brief Checks whether a rollback to a previous firmware is possible.
 *
 * @return true if at least one valid app exists (besides the running one).
 */
[[nodiscard]] bool rollback_possible();

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
/**
 * @brief Returns the last partition with invalid state.
 *
 * Returns the last partition with state image_state::invalid or image_state::aborted.
 *
 * @return The last invalid partition.
 *
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
 * @throws std::system_error with idfxx::errc::not_found if no invalid partition exists.
 */
[[nodiscard]] partition last_invalid_partition();
#endif

/**
 * @brief Returns the last partition with invalid state.
 *
 * Returns the last partition with state image_state::invalid or image_state::aborted.
 *
 * @return The last invalid partition, or an error.
 * @retval idfxx::errc::not_found if no invalid partition exists.
 */
[[nodiscard]] result<partition> try_last_invalid_partition();

// =============================================================================
// Free functions - Partition maintenance
// =============================================================================

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
/**
 * @brief Erases the previous boot application partition and its OTA data.
 *
 * Call after the current app has been marked valid.
 *
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
 * @throws std::system_error on failure.
 */
void erase_last_boot_app_partition();

/**
 * @brief Invalidates the OTA data slot for the last boot application partition.
 *
 * Erases the OTA data slot without erasing the partition contents.
 *
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
 * @throws std::system_error on failure.
 */
void invalidate_inactive_ota_data_slot();
#endif

/**
 * @brief Erases the previous boot application partition and its OTA data.
 *
 * Call after the current app has been marked valid.
 *
 * @return Success, or an error.
 */
[[nodiscard]] result<void> try_erase_last_boot_app_partition();

/**
 * @brief Invalidates the OTA data slot for the last boot application partition.
 *
 * Erases the OTA data slot without erasing the partition contents.
 *
 * @return Success, or an error.
 */
[[nodiscard]] result<void> try_invalidate_inactive_ota_data_slot();

// =============================================================================
// Free functions - Partition count
// =============================================================================

/**
 * @brief Returns the number of OTA app partitions in the partition table.
 *
 * @return The number of OTA app partitions.
 */
[[nodiscard]] size_t app_partition_count();

/**
 * @brief Returns a reference to the OTA error category singleton.
 *
 * @return Reference to the singleton ota::error_category instance.
 */
[[nodiscard]] const error_category& ota_category() noexcept;

/**
 * @headerfile <idfxx/ota>
 * @brief Creates an error code from an idfxx::ota::errc value.
 */
[[nodiscard]] inline std::error_code make_error_code(errc e) noexcept {
    return {std::to_underlying(e), ota_category()};
}

/**
 * @headerfile <idfxx/ota>
 * @brief Creates an unexpected error from an ESP-IDF error code, mapping to OTA error codes where possible.
 *
 * Converts the ESP-IDF error code to an OTA-specific error code if a mapping exists,
 * otherwise falls back to the default IDFXX error category.
 *
 * @param e The ESP-IDF error code.
 * @return An unexpected value suitable for returning from result-returning functions.
 */
[[nodiscard]] std::unexpected<std::error_code> ota_error(esp_err_t e);

} // namespace idfxx::ota

/** @cond INTERNAL */
namespace std {
template<>
struct is_error_code_enum<idfxx::ota::errc> : true_type {};
} // namespace std
/** @endcond */

/** @} */ // end of idfxx_ota
