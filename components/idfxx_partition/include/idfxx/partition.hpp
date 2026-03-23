// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

/**
 * @headerfile <idfxx/partition>
 * @file partition.hpp
 * @brief Flash partition management class.
 *
 * @defgroup idfxx_partition Partition Component
 * @brief Type-safe flash partition discovery, reading, writing, and memory mapping.
 *
 * Provides partition lookup by type, subtype, and label, with read/write/erase
 * operations and memory mapping.
 *
 * Depends on @ref idfxx_core for error handling.
 * @{
 */

#include <idfxx/error>

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <esp_partition.h>
#include <span>
#include <string_view>
#include <type_traits>
#include <vector>

namespace idfxx {

/**
 * @headerfile <idfxx/partition>
 * @brief A flash partition.
 *
 * Represents a region of flash storage defined in the partition table.
 * Provides discovery, reading, writing, erasing, and memory mapping.
 *
 * Use the static find methods to discover partitions from the partition table.
 * Partitions are copyable and remain valid for the entire application lifetime.
 *
 * @code
 * auto part = idfxx::partition::find("storage");
 * std::array<uint8_t, 256> buf{};
 * part.read(0, buf.data(), buf.size());
 * @endcode
 */
class partition {
public:
    /** @brief Partition type. */
    enum class type : int {
        app = 0x00,             ///< Application partition
        data = 0x01,            ///< Data partition
        bootloader = 0x02,      ///< Bootloader partition
        partition_table = 0x03, ///< Partition table
        any = 0xff,             ///< Match any partition type (for searching)
    };

    /** @brief Partition subtype. */
    enum class subtype : int {
        // clang-format off
        // Bootloader subtypes
        bootloader_primary  = 0x00, ///< Primary bootloader
        bootloader_ota      = 0x01, ///< OTA bootloader
        bootloader_recovery = 0x02, ///< Recovery bootloader

        // Partition table subtypes
        partition_table_primary = 0x00, ///< Primary partition table
        partition_table_ota     = 0x01, ///< OTA partition table

        // App subtypes
        app_factory = 0x00, ///< Factory application
        app_ota_0   = 0x10, ///< OTA application slot 0
        app_ota_1   = 0x11, ///< OTA application slot 1
        app_ota_2   = 0x12, ///< OTA application slot 2
        app_ota_3   = 0x13, ///< OTA application slot 3
        app_ota_4   = 0x14, ///< OTA application slot 4
        app_ota_5   = 0x15, ///< OTA application slot 5
        app_ota_6   = 0x16, ///< OTA application slot 6
        app_ota_7   = 0x17, ///< OTA application slot 7
        app_ota_8   = 0x18, ///< OTA application slot 8
        app_ota_9   = 0x19, ///< OTA application slot 9
        app_ota_10  = 0x1a, ///< OTA application slot 10
        app_ota_11  = 0x1b, ///< OTA application slot 11
        app_ota_12  = 0x1c, ///< OTA application slot 12
        app_ota_13  = 0x1d, ///< OTA application slot 13
        app_ota_14  = 0x1e, ///< OTA application slot 14
        app_ota_15  = 0x1f, ///< OTA application slot 15
        app_test    = 0x20, ///< Test application

        // Data subtypes
        data_ota       = 0x00, ///< OTA data
        data_phy       = 0x01, ///< PHY init data
        data_nvs       = 0x02, ///< NVS data
        data_coredump  = 0x03, ///< Core dump data
        data_nvs_keys  = 0x04, ///< NVS encryption keys
        data_efuse_em  = 0x05, ///< eFuse emulation data
        data_undefined = 0x06, ///< Undefined data
        data_esphttpd  = 0x80, ///< ESPHTTPD data
        data_fat       = 0x81, ///< FAT filesystem data
        data_spiffs    = 0x82, ///< SPIFFS data
        data_littlefs  = 0x83, ///< LittleFS data

        any = 0xff, ///< Match any subtype (for searching)
        // clang-format on
    };

    /** @brief Memory type for memory-mapped partition access. */
    enum class mmap_memory : int {
        data = 0, ///< Map to data memory, allows byte-aligned access
        inst,     ///< Map to instruction memory, allows only 4-byte-aligned access
    };

    class mmap_handle;

    // =========================================================================
    // Discovery
    // =========================================================================

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Finds the first partition matching the given criteria.
     *
     * @param type    Partition type to match, or type::any for all types.
     * @param subtype Partition subtype to match, or subtype::any for all subtypes.
     * @param label   Optional partition label to match. Empty matches all labels.
     *
     * @return The matching partition.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error with idfxx::errc::not_found if no partition matches.
     */
    [[nodiscard]] static partition
    find(enum type type, enum subtype subtype = subtype::any, std::string_view label = {});

    /**
     * @brief Finds the first partition with the given label.
     *
     * Searches across all partition types and subtypes.
     *
     * @param label Partition label to match.
     *
     * @return The matching partition.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error with idfxx::errc::not_found if no partition matches.
     */
    [[nodiscard]] static partition find(std::string_view label);
#endif

    /**
     * @brief Finds the first partition matching the given criteria.
     *
     * @param type    Partition type to match, or type::any for all types.
     * @param subtype Partition subtype to match, or subtype::any for all subtypes.
     * @param label   Optional partition label to match. Empty matches all labels.
     *
     * @return The matching partition, or an error.
     * @retval idfxx::errc::not_found if no partition matches the criteria.
     */
    [[nodiscard]] static result<partition>
    try_find(enum type type, enum subtype subtype = subtype::any, std::string_view label = {});

    /**
     * @brief Finds the first partition with the given label.
     *
     * Searches across all partition types and subtypes.
     *
     * @param label Partition label to match.
     *
     * @return The matching partition, or an error.
     * @retval idfxx::errc::not_found if no partition matches.
     */
    [[nodiscard]] static result<partition> try_find(std::string_view label);

    /**
     * @brief Finds all partitions matching the given criteria.
     *
     * @param type    Partition type to match, or type::any for all types.
     * @param subtype Partition subtype to match, or subtype::any for all subtypes.
     * @param label   Optional partition label to match. Empty matches all labels.
     *
     * @return A vector of matching partitions (may be empty).
     */
    [[nodiscard]] static std::vector<partition>
    find_all(enum type type = type::any, enum subtype subtype = subtype::any, std::string_view label = {});

    /**
     * @brief Constructs a partition from an ESP-IDF partition handle.
     *
     * @param handle Pointer to an ESP-IDF partition structure. Must not be null.
     *
     * @return A partition wrapping the given handle.
     */
    [[nodiscard]] static constexpr partition from_handle(const esp_partition_t* handle) { return partition{handle}; }

    // Copyable and movable
    partition(const partition&) = default;
    partition(partition&&) = default;
    partition& operator=(const partition&) = default;
    partition& operator=(partition&&) = default;

    // =========================================================================
    // Accessors
    // =========================================================================

    /** @brief Compares two partitions for equality. */
    [[nodiscard]] constexpr bool operator==(const partition&) const noexcept = default;

    /** @brief Returns the partition type. */
    [[nodiscard]] enum type type() const { return static_cast<enum partition::type>(_part->type); }

    /** @brief Returns the partition subtype. */
    [[nodiscard]] enum subtype subtype() const { return static_cast<enum partition::subtype>(_part->subtype); }

    /** @brief Returns the starting address in flash. */
    [[nodiscard]] uint32_t address() const { return _part->address; }

    /** @brief Returns the partition size in bytes. */
    [[nodiscard]] uint32_t size() const { return _part->size; }

    /** @brief Returns the erase block size. */
    [[nodiscard]] uint32_t erase_size() const { return _part->erase_size; }

    /** @brief Returns the partition label. */
    [[nodiscard]] std::string_view label() const { return _part->label; }

    /** @brief Returns true if the partition is encrypted. */
    [[nodiscard]] bool encrypted() const { return _part->encrypted; }

    /** @brief Returns true if the partition is read-only. */
    [[nodiscard]] bool readonly() const { return _part->readonly; }

    /** @brief Returns the underlying ESP-IDF partition handle. */
    [[nodiscard]] constexpr const esp_partition_t* idf_handle() const { return _part; }

    // =========================================================================
    // Read / Write / Erase
    // =========================================================================

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Reads data from the partition with decryption if applicable.
     *
     * @param offset Byte offset within the partition.
     * @param dst    Destination buffer.
     * @param size   Number of bytes to read.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    void read(size_t offset, void* dst, size_t size) const { unwrap(try_read(offset, dst, size)); }

    /**
     * @brief Reads data from the partition with decryption if applicable.
     *
     * @param offset Byte offset within the partition.
     * @param dst    Destination span.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    void read(size_t offset, std::span<uint8_t> dst) const { unwrap(try_read(offset, dst)); }

    /**
     * @brief Writes data to the partition with encryption if applicable.
     *
     * @param offset Byte offset within the partition.
     * @param src    Source buffer.
     * @param size   Number of bytes to write.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    void write(size_t offset, const void* src, size_t size) { unwrap(try_write(offset, src, size)); }

    /**
     * @brief Writes data to the partition with encryption if applicable.
     *
     * @param offset Byte offset within the partition.
     * @param src    Source span.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    void write(size_t offset, std::span<const uint8_t> src) { unwrap(try_write(offset, src)); }

    /**
     * @brief Reads data from the partition without decryption.
     *
     * @param offset Byte offset within the partition.
     * @param dst    Destination buffer.
     * @param size   Number of bytes to read.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    void read_raw(size_t offset, void* dst, size_t size) const { unwrap(try_read_raw(offset, dst, size)); }

    /**
     * @brief Reads data from the partition without decryption.
     *
     * @param offset Byte offset within the partition.
     * @param dst    Destination span.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    void read_raw(size_t offset, std::span<uint8_t> dst) const { unwrap(try_read_raw(offset, dst)); }

    /**
     * @brief Writes data to the partition without encryption.
     *
     * @param offset Byte offset within the partition.
     * @param src    Source buffer.
     * @param size   Number of bytes to write.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    void write_raw(size_t offset, const void* src, size_t size) { unwrap(try_write_raw(offset, src, size)); }

    /**
     * @brief Writes data to the partition without encryption.
     *
     * @param offset Byte offset within the partition.
     * @param src    Source span.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    void write_raw(size_t offset, std::span<const uint8_t> src) { unwrap(try_write_raw(offset, src)); }

    /**
     * @brief Erases a range of the partition.
     *
     * Both offset and size must be aligned to erase_size().
     *
     * @param offset Byte offset within the partition (must be erase-aligned).
     * @param size   Number of bytes to erase (must be erase-aligned).
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    void erase_range(size_t offset, size_t size) { unwrap(try_erase_range(offset, size)); }

    /**
     * @brief Computes the SHA-256 hash of the partition contents.
     *
     * @return A 32-byte SHA-256 hash.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    [[nodiscard]] std::array<uint8_t, 32> sha256() const { return unwrap(try_sha256()); }

    /**
     * @brief Memory-maps a region of the partition.
     *
     * @param offset Byte offset within the partition (must be aligned to 0x10000 for data,
     *               or SPI_FLASH_MMU_PAGE_SIZE for instruction memory).
     * @param size   Number of bytes to map.
     * @param memory Memory type to map to.
     *
     * @return A handle to the mapped memory region.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    [[nodiscard]] mmap_handle mmap(size_t offset, size_t size, enum mmap_memory memory = mmap_memory::data) const;
#endif

    /**
     * @brief Reads data from the partition with decryption if applicable.
     *
     * @param offset Byte offset within the partition.
     * @param dst    Destination buffer.
     * @param size   Number of bytes to read.
     *
     * @return Success, or an error.
     */
    [[nodiscard]] result<void> try_read(size_t offset, void* dst, size_t size) const;

    /**
     * @brief Reads data from the partition with decryption if applicable.
     *
     * @param offset Byte offset within the partition.
     * @param dst    Destination span.
     *
     * @return Success, or an error.
     */
    [[nodiscard]] result<void> try_read(size_t offset, std::span<uint8_t> dst) const;

    /**
     * @brief Writes data to the partition with encryption if applicable.
     *
     * @param offset Byte offset within the partition.
     * @param src    Source buffer.
     * @param size   Number of bytes to write.
     *
     * @return Success, or an error.
     */
    [[nodiscard]] result<void> try_write(size_t offset, const void* src, size_t size);

    /**
     * @brief Writes data to the partition with encryption if applicable.
     *
     * @param offset Byte offset within the partition.
     * @param src    Source span.
     *
     * @return Success, or an error.
     */
    [[nodiscard]] result<void> try_write(size_t offset, std::span<const uint8_t> src);

    /**
     * @brief Reads data from the partition without decryption.
     *
     * @param offset Byte offset within the partition.
     * @param dst    Destination buffer.
     * @param size   Number of bytes to read.
     *
     * @return Success, or an error.
     */
    [[nodiscard]] result<void> try_read_raw(size_t offset, void* dst, size_t size) const;

    /**
     * @brief Reads data from the partition without decryption.
     *
     * @param offset Byte offset within the partition.
     * @param dst    Destination span.
     *
     * @return Success, or an error.
     */
    [[nodiscard]] result<void> try_read_raw(size_t offset, std::span<uint8_t> dst) const;

    /**
     * @brief Writes data to the partition without encryption.
     *
     * @param offset Byte offset within the partition.
     * @param src    Source buffer.
     * @param size   Number of bytes to write.
     *
     * @return Success, or an error.
     */
    [[nodiscard]] result<void> try_write_raw(size_t offset, const void* src, size_t size);

    /**
     * @brief Writes data to the partition without encryption.
     *
     * @param offset Byte offset within the partition.
     * @param src    Source span.
     *
     * @return Success, or an error.
     */
    [[nodiscard]] result<void> try_write_raw(size_t offset, std::span<const uint8_t> src);

    /**
     * @brief Erases a range of the partition.
     *
     * Both offset and size must be aligned to erase_size().
     *
     * @param offset Byte offset within the partition (must be erase-aligned).
     * @param size   Number of bytes to erase (must be erase-aligned).
     *
     * @return Success, or an error.
     */
    [[nodiscard]] result<void> try_erase_range(size_t offset, size_t size);

    /**
     * @brief Computes the SHA-256 hash of the partition contents.
     *
     * @return A 32-byte SHA-256 hash, or an error.
     */
    [[nodiscard]] result<std::array<uint8_t, 32>> try_sha256() const;

    /**
     * @brief Memory-maps a region of the partition.
     *
     * @param offset Byte offset within the partition (must be aligned to 0x10000 for data,
     *               or SPI_FLASH_MMU_PAGE_SIZE for instruction memory).
     * @param size   Number of bytes to map.
     * @param memory Memory type to map to.
     *
     * @return A handle to the mapped memory region, or an error.
     */
    [[nodiscard]] result<mmap_handle>
    try_mmap(size_t offset, size_t size, enum mmap_memory memory = mmap_memory::data) const;

    /**
     * @brief Checks if two partitions have identical contents by comparing SHA-256 hashes.
     *
     * @param other The other partition to compare with.
     *
     * @return true if the partitions have identical content.
     */
    [[nodiscard]] bool check_identity(const partition& other) const;

private:
    explicit constexpr partition(const esp_partition_t* part)
        : _part(part) {}

    const esp_partition_t* _part;
};

/**
 * @headerfile <idfxx/partition>
 * @brief Computes an OTA app subtype from a slot index.
 *
 * @param i OTA slot index (0-15).
 *
 * @return The corresponding app OTA subtype.
 */
[[nodiscard]] constexpr enum partition::subtype app_ota(unsigned i) {
    assert(i <= 15);
    return static_cast<enum partition::subtype>(0x10 + i);
}

/**
 * @headerfile <idfxx/partition>
 * @brief A memory-mapped partition region.
 *
 * Provides direct memory access to partition contents. The mapped region
 * is automatically unmapped on destruction. Move-only.
 *
 * @code
 * auto part = idfxx::partition::find(idfxx::partition::type::data, idfxx::partition::subtype::data_fat);
 * auto mapped = part.mmap(0, part.size());
 * auto bytes = mapped.as_span<const uint8_t>();
 * @endcode
 */
class partition::mmap_handle {
public:
    /** @brief Constructs an invalid (unmapped) handle. */
    mmap_handle() = default;

    ~mmap_handle();

    mmap_handle(const mmap_handle&) = delete;
    mmap_handle& operator=(const mmap_handle&) = delete;

    /** @brief Move constructor. */
    mmap_handle(mmap_handle&& other) noexcept;

    /** @brief Move assignment. Unmaps any previously mapped region. */
    mmap_handle& operator=(mmap_handle&& other) noexcept;

    /** @brief Returns true if this handle has a mapped region. */
    [[nodiscard]] explicit operator bool() const { return _ptr != nullptr; }

    /** @brief Returns a pointer to the mapped memory region. */
    [[nodiscard]] const void* data() const { return _ptr; }

    /** @brief Returns the size of the mapped region in bytes. */
    [[nodiscard]] size_t size() const { return _size; }

    /**
     * @brief Returns a typed span over the mapped memory region.
     *
     * @tparam T Element type (must be const-qualified).
     *
     * @return A span covering the mapped region.
     */
    template<typename T>
    [[nodiscard]] std::span<T> as_span() const {
        static_assert(std::is_const_v<T>, "T must be const-qualified; mapped partition memory is read-only");
        return {static_cast<T*>(_ptr), _size / sizeof(T)};
    }

    /**
     * @brief Releases ownership without unmapping.
     *
     * After calling this, the caller is responsible for unmapping the region.
     *
     * @return The underlying mmap handle for manual lifecycle management.
     */
    esp_partition_mmap_handle_t release() noexcept;

private:
    friend class partition;

    mmap_handle(const void* ptr, size_t size, esp_partition_mmap_handle_t handle);

    const void* _ptr = nullptr;
    size_t _size = 0;
    esp_partition_mmap_handle_t _handle = 0;
};

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
inline partition::mmap_handle partition::mmap(size_t offset, size_t size, enum mmap_memory memory) const {
    return unwrap(try_mmap(offset, size, memory));
}
#endif

} // namespace idfxx

/** @} */ // end of idfxx_partition
