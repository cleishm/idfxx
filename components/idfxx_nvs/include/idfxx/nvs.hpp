// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

/**
 * @headerfile <idfxx/nvs>
 * @file nvs.hpp
 * @brief Non-Volatile Storage class.
 *
 * @defgroup idfxx_nvs NVS Component
 * @brief Type-safe persistent key-value storage in flash memory.
 *
 * Provides NVS namespace lifecycle management with type-safe storage
 * for integers (8-64 bits), strings, and binary blobs. Features an
 * explicit commit model for atomic updates.
 *
 * Depends on @ref idfxx_core for error handling.
 * @{
 */

#include <idfxx/error>

#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <string_view>
#include <system_error>
#include <vector>

typedef uint32_t nvs_handle_t;
typedef int esp_err_t;

namespace idfxx {

/** @cond INTERNAL */
/**
 * @headerfile <idfxx/nvs>
 * @brief Concept for fixed-size integral types (8 to 64 bits).
 */
template<typename T>
concept sized_integral =
    std::same_as<T, uint8_t> || std::same_as<T, int8_t> || std::same_as<T, uint16_t> || std::same_as<T, int16_t> ||
    std::same_as<T, uint32_t> || std::same_as<T, int32_t> || std::same_as<T, uint64_t> || std::same_as<T, int64_t>;
/** @endcond */

/**
 * @headerfile <idfxx/nvs>
 * @brief Non-Volatile Storage handle.
 *
 * Provides persistent key-value storage in flash memory. Supports strings,
 * binary blobs, and integer types from 8 to 64 bits.
 *
 * Changes are not persisted until commit() is called.
 */
class nvs {
public:
    /**
     * @brief Error codes for NVS operations.
     */
    enum class errc : esp_err_t {
        // clang-format off
        not_found            = 0x1102,  /*!< A requested entry couldn't be found or namespace doesn’t exist yet and mode is NVS_READONLY */
        type_mismatch        = 0x1103,  /*!< The type of set or get operation doesn't match the type of value stored in NVS */
        read_only            = 0x1104,  /*!< Storage handle was opened as read only */
        not_enough_space     = 0x1105,  /*!< There is not enough space in the underlying storage to save the value */
        invalid_name         = 0x1106,  /*!< Namespace name doesn’t satisfy constraints */
        invalid_handle       = 0x1107,  /*!< Handle has been closed or is NULL */
        remove_failed        = 0x1108,  /*!< The value wasn’t updated because flash write operation has failed. The value was written however, and update will be finished after re-initialization of nvs, provided that flash operation doesn’t fail again. */
        key_too_long         = 0x1109,  /*!< Key name is too long */
        invalid_state        = 0x110b,  /*!< NVS is in an inconsistent state due to a previous error. Call nvs::flash_init() again and create a new nvs object. */
        invalid_length       = 0x110c,  /*!< String or blob length is not sufficient to store data */
        no_free_pages        = 0x110d,  /*!< NVS partition doesn't contain any empty pages. This may happen if NVS partition was truncated. Erase the whole partition and call nvs::flash_init() again. */
        value_too_long       = 0x110e,  /*!< Value doesn't fit into the entry or string or blob length is longer than supported by the implementation */
        part_not_found       = 0x110f,  /*!< Partition with specified name is not found in the partition table */
        new_version_found    = 0x1110,  /*!< NVS partition contains data in new format and cannot be recognized by this version of code */
        xts_encr_failed      = 0x1111,  /*!< XTS encryption failed while writing NVS entry */
        xts_decr_failed      = 0x1112,  /*!< XTS decryption failed while reading NVS entry */
        xts_cfg_failed       = 0x1113,  /*!< XTS configuration setting failed */
        xts_cfg_not_found    = 0x1114,  /*!< XTS configuration not found */
        encr_not_supported   = 0x1115,  /*!< NVS encryption is not supported in this version */
        keys_not_initialized = 0x1116,  /*!< NVS key partition is uninitialized */
        corrupt_key_part     = 0x1117,  /*!< NVS key partition is corrupt */
        wrong_encryption     = 0x1119,  /*!< NVS partition is marked as encrypted with generic flash encryption. This is forbidden since the NVS encryption works differently. */
        // clang-format on
    };

    /**
     * @brief Error category for NVS errors.
     */
    class error_category : public std::error_category {
    public:
        /** @brief Returns the name of the error category. */
        [[nodiscard]] const char* name() const noexcept override final;

        /** @brief Returns a human-readable message for the given error code. */
        [[nodiscard]] std::string message(int ec) const override final;
    };

    class flash;

    /**
     * @brief Opens a NVS namespace.
     *
     * @param namespace_name Namespace name (max 15 characters).
     * @param read_only      If true, opens in read-only mode.
     *
     * @return The new nvs handle, or an error.
     */
    [[nodiscard]] static result<std::unique_ptr<nvs>> make(std::string_view namespace_name, bool read_only = false);

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Opens a NVS namespace.
     *
     * @param namespace_name Namespace name (max 15 characters).
     * @param read_only      If true, opens in read-only mode.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    [[nodiscard]] explicit nvs(std::string_view namespace_name, bool read_only = false);
#endif

    ~nvs();

    nvs(const nvs&) = delete;
    nvs& operator=(const nvs&) = delete;
    nvs(nvs&&) = delete;
    nvs& operator=(nvs&&) = delete;

    /** @brief Returns true if the handle allows writes. */
    [[nodiscard]] bool is_writeable() const { return !_read_only; }

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Commits pending changes to flash.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on error.
     */
    void commit() { unwrap(try_commit()); }

    /**
     * @brief Erases a key.
     * @param key The key to erase.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on error.
     */
    void erase(std::string_view key) { unwrap(try_erase(key)); }

    /**
     * @brief Erases all keys in the namespace.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on error.
     */
    void erase_all() { unwrap(try_erase_all()); }

    /**
     * @brief Stores a string.
     * @param key   Key name.
     * @param value String value.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on error.
     */
    void set_string(std::string_view key, std::string_view value) { unwrap(try_set_string(key, value)); }

    /**
     * @brief Retrieves a string.
     * @param key Key name.
     * @return The string value.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on error.
     */
    [[nodiscard]] std::string get_string(std::string_view key) { return unwrap(try_get_string(key)); }

    /**
     * @brief Stores binary data.
     * @param key    Key name.
     * @param data   Data pointer.
     * @param length Data size in bytes.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on error.
     */
    void set_blob(std::string_view key, const void* data, size_t length) { unwrap(try_set_blob(key, data, length)); }

    /**
     * @brief Stores binary data.
     * @param key  Key name.
     * @param data Data vector.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on error.
     */
    void set_blob(std::string_view key, const std::vector<uint8_t>& data) { unwrap(try_set_blob(key, data)); }

    /**
     * @brief Retrieves binary data.
     * @param key Key name.
     * @return The data.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on error.
     */
    [[nodiscard]] std::vector<uint8_t> get_blob(std::string_view key) { return unwrap(try_get_blob(key)); }

    /**
     * @brief Stores an integer value.
     * @tparam T Integer type (8 to 64 bits, signed or unsigned).
     * @param key   Key name.
     * @param value Value to store.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on error.
     */
    template<typename T>
        requires sized_integral<T>
    void set_value(std::string_view key, T value) {
        unwrap(try_set_value(key, value));
    }

    /**
     * @brief Retrieves an integer value.
     * @tparam T Integer type (8 to 64 bits, signed or unsigned).
     * @param key Key name.
     * @return The value.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on error.
     */
    template<typename T>
        requires sized_integral<T>
    [[nodiscard]] T get_value(std::string_view key) {
        return unwrap(try_get_value<T>(key));
    }
#endif

    /**
     * @brief Commits pending changes to flash.
     * @return Success, or an error.
     */
    [[nodiscard]] result<void> try_commit();

    /**
     * @brief Erases a key.
     * @param key The key to erase.
     * @return Success, or an error.
     */
    [[nodiscard]] result<void> try_erase(std::string_view key);

    /**
     * @brief Erases all keys in the namespace.
     * @return Success, or an error.
     */
    [[nodiscard]] result<void> try_erase_all();

    /**
     * @brief Stores a string.
     * @param key   Key name.
     * @param value String value.
     * @return Success, or an error.
     */
    [[nodiscard]] result<void> try_set_string(std::string_view key, std::string_view value);

    /**
     * @brief Retrieves a string.
     * @param key Key name.
     * @return The string value, or an error.
     */
    [[nodiscard]] result<std::string> try_get_string(std::string_view key);

    /**
     * @brief Stores binary data.
     * @param key    Key name.
     * @param data   Data pointer.
     * @param length Data size in bytes.
     * @return Success, or an error.
     */
    [[nodiscard]] result<void> try_set_blob(std::string_view key, const void* data, size_t length);

    /** @copydoc try_set_blob(std::string_view, const void*, size_t) */
    [[nodiscard]] result<void> try_set_blob(std::string_view key, const std::vector<uint8_t>& data);

    /**
     * @brief Retrieves binary data.
     * @param key Key name.
     * @return The data, or an error.
     */
    [[nodiscard]] result<std::vector<uint8_t>> try_get_blob(std::string_view key);

    /**
     * @brief Stores an integer value.
     * @tparam T Integer type (8 to 64 bits, signed or unsigned).
     * @param key   Key name.
     * @param value Value to store.
     * @return Success, or an error.
     */
    template<typename T>
        requires sized_integral<T>
    [[nodiscard]] result<void> try_set_value(std::string_view key, T value);

    /**
     * @brief Retrieves an integer value.
     * @tparam T Integer type (8 to 64 bits, signed or unsigned).
     * @param key Key name.
     * @return The value, or an error.
     */
    template<typename T>
        requires sized_integral<T>
    [[nodiscard]] result<T> try_get_value(std::string_view key);

private:
    nvs(nvs_handle_t handle, bool read_only);

    nvs_handle_t _handle;
    bool _read_only;
};

/**
 * @brief Returns a reference to the NVS error category singleton.
 *
 * @return Reference to the singleton nvs::error_category instance.
 */
[[nodiscard]] const nvs::error_category& nvs_category() noexcept;

/**
 * @headerfile <idfxx/nvs>
 * @brief Creates an error code from an idfxx::nvs::errc value.
 */
[[nodiscard]] inline std::error_code make_error_code(nvs::errc e) noexcept {
    return {std::to_underlying(e), nvs_category()};
}

/**
 * @headerfile <idfxx/nvs>
 * @brief Creates an unexpected error from an ESP-IDF error code, mapping to NVS error codes where possible.
 *
 * Converts the ESP-IDF error code to an NVS-specific error code if a mapping exists,
 * otherwise falls back to the default IDFXX error category.
 *
 * @param e The ESP-IDF error code.
 * @return An unexpected value suitable for returning from result-returning functions.
 */
[[nodiscard]] std::unexpected<std::error_code> nvs_error(esp_err_t e) noexcept;

class nvs::flash {
public:
    constexpr static size_t key_size = 32; /*!< Size of each NVS encryption key in bytes */

    /**
     * @brief Key for encryption and decryption
     */
    struct secure_config {
        std::array<uint8_t, key_size> eky; /*!< XTS encryption and decryption key*/
        std::array<uint8_t, key_size> tky; /*!< XTS tweak key */
    };

public:
    // Non-instantiable
    flash() = delete;

    /** @brief Tag type to indicate insecure (non-encrypted) initialization. */
    struct insecure_t {
        explicit insecure_t() = default;
    };

    /** @brief Insecure (non-encrypted) initialization tag. */
    constexpr static insecure_t insecure{};

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Initializes the default NVS partition.
     *
     * This API initialises the default NVS partition. The default NVS partition
     * is the one that is labeled "nvs" in the partition table.
     *
     * When "NVS_ENCRYPTION" is enabled in the menuconfig, this API enables
     * the NVS encryption for the default NVS partition as follows
     *      1. Read security configurations from the first NVS key
     *         partition listed in the partition table. (NVS key partition is
     *         any "data" type partition which has the subtype value set to "nvs_keys")
     *      2. If the NVS key partition obtained in the previous step is empty,
     *         generate and store new keys in that NVS key partition.
     *      3. Internally call "nvs::flash::secure_init()" with
     *         the security configurations obtained/generated in the previous steps.
     *
     * Post initialization NVS read/write APIs remain the same irrespective of NVS encryption.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    static void init() { unwrap(try_init()); }

    /**
     * @brief Initialize the default NVS partition with encryption.
     *
     * This API initialises the default NVS partition. The default NVS partition
     * is the one that is labeled "nvs" in the partition table.
     *
     * @param cfg Security configuration (keys) to be used for NVS encryption/decryption.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    static void init(const secure_config& cfg) { unwrap(try_init(cfg)); }

    /**
     * @brief Initialize the default NVS partition without encryption.
     *
     * This API initialises the default NVS partition. The default NVS partition
     * is the one that is labeled "nvs" in the partition table.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    static void init(insecure_t) { unwrap(try_init(insecure)); }

    /**
     * @brief Initialize NVS flash storage for the specified partition.
     *
     * @param partition_label Label of the partition. Must be no longer than 16 characters.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    static void init(std::string_view partition_label) { unwrap(try_init(partition_label)); }

    /**
     * @brief Initialize NVS flash storage for the specified partition.
     *
     * @param partition_label Label of the partition. Must be no longer than 16 characters.
     * @param cfg Security configuration (keys) to be used for NVS encryption/decryption
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    static void init(std::string_view partition_label, const secure_config& cfg) {
        unwrap(try_init(partition_label, cfg));
    }

    /**
     * @brief Initialize NVS flash storage for the specified partition without encryption.
     *
     * @param partition_label Label of the partition. Must be no longer than 16 characters.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    static void init(insecure_t, std::string_view partition_label) { unwrap(try_init(insecure, partition_label)); }
#endif

    /**
     * @brief Initializes the default NVS partition.
     *
     * This API initialises the default NVS partition. The default NVS partition
     * is the one that is labeled "nvs" in the partition table.
     *
     * When "NVS_ENCRYPTION" is enabled in the menuconfig, this API enables
     * the NVS encryption for the default NVS partition as follows
     *      1. Read security configurations from the first NVS key
     *         partition listed in the partition table. (NVS key partition is
     *         any "data" type partition which has the subtype value set to "nvs_keys")
     *      2. If the NVS key partition obtained in the previous step is empty,
     *         generate and store new keys in that NVS key partition.
     *      3. Internally call "nvs::flash::try_init()" with
     *         the security configurations obtained/generated in the previous steps.
     *
     * Post initialization NVS read/write APIs remain the same irrespective of NVS encryption.
     *
     * @return Success, or an error.
     * @retval nvs::errc::no_free_pages if the NVS storage contains no empty pages
     *         (which may happen if NVS partition was truncated).
     * @retval nvs::errc::part_not_found if no partition with label "nvs" is found in the partition table.
     * @retval nvs::errc::no_mem in case memory could not be allocated for the internal structures.
     * @retval Other error codes from the underlying flash storage driver.
     */
    [[nodiscard]] static result<void> try_init();

    /**
     * @brief Initialize the default NVS partition with encryption.
     *
     * This API initialises the default NVS partition. The default NVS partition
     * is the one that is labeled "nvs" in the partition table.
     *
     * @param cfg Security configuration (keys) to be used for NVS encryption/decryption.
     *
     * @return Success, or an error.
     * @retval nvs::errc::no_free_pages if the NVS storage contains no empty pages
     *         (which may happen if NVS partition was truncated).
     * @retval nvs::errc::part_not_found if no partition with label "nvs" is found in the partition table.
     * @retval nvs::errc::no_mem in case memory could not be allocated for the internal structures.
     * @retval Other error codes from the underlying flash storage driver.
     */
    [[nodiscard]] static result<void> try_init(const secure_config& cfg);

    /**
     * @brief Initialize the default NVS partition without encryption.
     *
     * This API initialises the default NVS partition. The default NVS partition
     * is the one that is labeled "nvs" in the partition table.
     *
     * @return Success, or an error.
     * @retval nvs::errc::no_free_pages if the NVS storage contains no empty pages
     *         (which may happen if NVS partition was truncated).
     * @retval nvs::errc::part_not_found if no partition with label "nvs" is found in the partition table.
     * @retval nvs::errc::no_mem in case memory could not be allocated for the internal structures.
     * @retval Other error codes from the underlying flash storage driver.
     */
    [[nodiscard]] static result<void> try_init(insecure_t);

    /**
     * @brief Initialize NVS flash storage for the specified partition.
     *
     * @param partition_label Label of the partition. Must be no longer than 16 characters.
     *
     * @return Success, or an error.
     * @retval nvs::errc::no_free_pages if the NVS storage contains no empty pages
     *         (which may happen if NVS partition was truncated).
     * @retval nvs::errc::not_found if specified partition is not found in the partition table.
     * @retval nvs::errc::no_mem in case memory could not be allocated for the internal structures.
     * @retval Other error codes from the underlying flash storage driver.
     */
    [[nodiscard]] static result<void> try_init(std::string_view partition_label);

    /**
     * @brief Initialize NVS flash storage for the specified partition.
     *
     * @param partition_label Label of the partition. Must be no longer than 16 characters.
     * @param cfg Security configuration (keys) to be used for NVS encryption/decryption.
     *
     * @return Success, or an error.
     * @retval nvs::errc::no_free_pages if the NVS storage contains no empty pages
     *         (which may happen if NVS partition was truncated).
     * @retval nvs::errc::not_found if specified partition is not found in the partition table.
     * @retval nvs::errc::no_mem in case memory could not be allocated for the internal structures.
     * @retval Other error codes from the underlying flash storage driver.
     */
    [[nodiscard]] static result<void> try_init(std::string_view partition_label, const secure_config& cfg);

    /**
     * @brief Initialize NVS flash storage for the specified partition without encryption.
     *
     * @param partition_label Label of the partition. Must be no longer than 16 characters.
     *
     * @return Success, or an error.
     * @retval nvs::errc::no_free_pages if the NVS storage contains no empty pages
     *         (which may happen if NVS partition was truncated).
     * @retval nvs::errc::not_found if specified partition is not found in the partition table.
     * @retval nvs::errc::no_mem in case memory could not be allocated for the internal structures.
     * @retval Other error codes from the underlying flash storage driver.
     */
    [[nodiscard]] static result<void> try_init(insecure_t, std::string_view partition_label);

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Deinitialize NVS storage for the default NVS partition.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    static void deinit() { unwrap(try_deinit()); }

    /**
     * @brief Deinitialize NVS storage for the specified partition.
     *
     * @param partition_label Label of the partition. Must be no longer than 16 characters.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    static void deinit(std::string_view partition_label) { unwrap(try_deinit(partition_label)); }
#endif

    /**
     * @brief Deinitialize NVS storage for the default NVS partition.
     * @return Success, or an error.
     * @retval nvs::errc::not_initialized if the storage was not initialized prior to this call.
     */
    [[nodiscard]] static result<void> try_deinit();

    /**
     * @brief Deinitialize NVS storage for the specified partition.
     *
     * @param partition_label Label of the partition. Must be no longer than 16 characters.
     *
     * @return Success, or an error.
     * @retval nvs::errc::not_initialized if the storage was not initialized prior to this call.
     */
    [[nodiscard]] static result<void> try_deinit(std::string_view partition_label);

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Erases the default NVS partition.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    static void erase() { unwrap(try_erase()); }

    /**
     * @brief Erase specified NVS partition
     *
     * @param partition_label Label of the partition which should be erased
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    static void erase(std::string_view partition_label) { unwrap(try_erase(partition_label)); }
#endif

    /**
     * @brief Erases the default NVS partition.
     * @return Success, or an error.
     * @retval nvs::errc::not_found if there is no NVS partition labeled "nvs" in the partition table.
     */
    [[nodiscard]] static result<void> try_erase();

    /**
     * @brief Erases the specified NVS partition.
     *
     * @param partition_label Label of the partition. Must be no longer than 16 characters.
     *
     * @return Success, or an error.
     * @retval nvs::errc::not_found if specified partition is not found in the partition table.
     */
    [[nodiscard]] static result<void> try_erase(std::string_view partition_label);
};

} // namespace idfxx

/** @cond INTERNAL */
namespace std {
template<>
struct is_error_code_enum<idfxx::nvs::errc> : true_type {};
} // namespace std
/** @endcond */

/** @} */ // end of idfxx_nvs
