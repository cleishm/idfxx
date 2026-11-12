// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

/**
 * @headerfile <idfxx/fatfs>
 * @file fatfs.hpp
 * @brief FAT filesystems on SD cards and flash partitions.
 *
 * @defgroup idfxx_fatfs FAT Filesystem Component
 * @brief FAT filesystems on SD cards and flash partitions, used through standard file I/O.
 *
 * Mounts a FAT filesystem at a path in the virtual filesystem, after which
 * its files are used with the C and C++ standard libraries (`fopen()`,
 * `std::fstream`, `std::filesystem`, POSIX `open()`, `stat()`, `opendir()`,
 * ...) under that path.
 *
 * - fatfs::volume is a writable filesystem on an SD card (see
 *   @ref idfxx_sdcard) or on a wear-leveled flash partition.
 * - fatfs::image is a read-only FAT volume: a prebuilt image on a flash
 *   partition, typically generated at build time with
 *   `fatfs_create_rawflash_image()`, or the filesystem on an SD card
 *   (including a write-protected one).
 *
 * At most `CONFIG_FATFS_VOLUME_COUNT` (2 by default) volumes and images can
 * be mounted at once.
 *
 * Depends on @ref idfxx_core for error handling, @ref idfxx_partition for
 * flash partitions and @ref idfxx_sdcard for SD cards.
 * @{
 */

#include "sdkconfig.h"

#include <idfxx/byte_source>
#include <idfxx/error>
#include <idfxx/partition>
#include <idfxx/sdcard>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <expected>
#include <ff.h>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>

/**
 * @headerfile <idfxx/fatfs>
 * @brief FAT filesystem classes.
 */
namespace idfxx::fatfs {

/**
 * @headerfile <idfxx/fatfs>
 * @brief Error codes reported by the FAT filesystem.
 *
 * The values are FatFs's `FRESULT` codes. The disk drivers report every
 * error from the card or the wear-leveling layer to FatFs as a plain I/O
 * failure, so medium errors while mounting a filesystem or using its files
 * surface as `disk_err`. Only mounting and unmounting the wear-leveling
 * layer, registering with the virtual filesystem and the checks made on a
 * card before mounting report idfxx::errc values.
 *
 * Codes with a portable equivalent also compare equal to a `std::errc`
 * value; see error_category::default_error_condition().
 */
enum class errc : esp_err_t {
    // clang-format off
    disk_err            = FR_DISK_ERR,            /*!< A low-level I/O error occurred on the medium. */
    int_err             = FR_INT_ERR,             /*!< The filesystem structure is inconsistent. */
    not_ready           = FR_NOT_READY,           /*!< The medium is not ready (for example, a card was removed). */
    no_file             = FR_NO_FILE,             /*!< The file was not found. */
    no_path             = FR_NO_PATH,             /*!< The path was not found. */
    invalid_name        = FR_INVALID_NAME,        /*!< The path name is invalid. */
    denied              = FR_DENIED,              /*!< Access was denied, or the directory or volume is full. */
    exist               = FR_EXIST,               /*!< An object with that name already exists. */
    invalid_object      = FR_INVALID_OBJECT,      /*!< The file or directory object is invalid. */
    write_protected     = FR_WRITE_PROTECTED,     /*!< The medium is write-protected. */
    invalid_drive       = FR_INVALID_DRIVE,       /*!< The logical drive number is invalid. */
    not_enabled         = FR_NOT_ENABLED,         /*!< The volume is not mounted. */
    no_filesystem       = FR_NO_FILESYSTEM,       /*!< The medium holds no FAT filesystem. */
    mkfs_aborted        = FR_MKFS_ABORTED,        /*!< Formatting was aborted: the medium is unsuitable for the parameters. */
    timeout             = FR_TIMEOUT,             /*!< Timed out waiting for access to the volume. */
    locked              = FR_LOCKED,              /*!< The file is locked by the file-sharing control. */
    /// A working buffer could not be allocated. Never returned by this component, which treats it
    /// as out of memory (see fatfs_error()); present so that every FRESULT has a value.
    not_enough_core     = FR_NOT_ENOUGH_CORE,
    too_many_open_files = FR_TOO_MANY_OPEN_FILES, /*!< Too many files are open. */
    invalid_parameter   = FR_INVALID_PARAMETER,   /*!< A parameter is invalid. */
    // clang-format on
};

/**
 * @headerfile <idfxx/fatfs>
 * @brief Error category for FAT filesystem errors.
 */
class error_category : public std::error_category {
public:
    /**
     * @brief Returns the name of the error category.
     *
     * @return The category name.
     */
    [[nodiscard]] const char* name() const noexcept override final;

    /**
     * @brief Returns a human-readable message for the given error code.
     *
     * @param ec The error code value.
     *
     * @return A descriptive error message.
     */
    [[nodiscard]] std::string message(int ec) const override final;

    /**
     * @brief Maps a FAT filesystem error code to a portable error condition.
     *
     * Codes with a portable equivalent map to it, so callers may compare
     * against either form:
     * @code
     * ec == idfxx::fatfs::errc::no_file
     * ec == std::errc::no_such_file_or_directory
     * @endcode
     *
     * | errc                  | std::errc                   |
     * |-----------------------|-----------------------------|
     * | `disk_err`            | `io_error`                  |
     * | `not_ready`           | `no_such_device`            |
     * | `no_file`, `no_path`  | `no_such_file_or_directory` |
     * | `invalid_name`        | `invalid_argument`          |
     * | `denied`              | `permission_denied`         |
     * | `exist`               | `file_exists`               |
     * | `invalid_object`      | `bad_file_descriptor`       |
     * | `write_protected`     | `read_only_file_system`     |
     * | `timeout`             | `timed_out`                 |
     * | `locked`              | `device_or_resource_busy`   |
     * | `not_enough_core`     | `not_enough_memory`         |
     * | `too_many_open_files` | `too_many_files_open`       |
     * | `invalid_parameter`   | `invalid_argument`          |
     *
     * Every other value (`int_err`, `invalid_drive`, `not_enabled`,
     * `no_filesystem` and `mkfs_aborted`) has no portable equivalent, and
     * stays in this category.
     *
     * @note This table is for code that handles fatfs::errc values. File
     *       operations through the C and POSIX APIs report their own `errno`
     *       values, which ESP-IDF chooses: a write-protected volume, for
     *       example, gives `EACCES` there, not `EROFS`.
     *
     * @param code The `errc` value to map.
     * @return The `std::error_condition` for `code`.
     */
    [[nodiscard]] std::error_condition
    default_error_condition(int code) const noexcept override final;
};

/**
 * @headerfile <idfxx/fatfs>
 * @brief Returns a reference to the FAT filesystem error category singleton.
 *
 * @return Reference to the singleton fatfs::error_category instance.
 */
[[nodiscard]] const error_category& fatfs_category() noexcept;

/**
 * @headerfile <idfxx/fatfs>
 * @brief Creates an error code from an idfxx::fatfs::errc value.
 *
 * @param e The FAT filesystem error code.
 *
 * @return The corresponding std::error_code in the FAT filesystem category.
 */
[[nodiscard]] inline std::error_code make_error_code(errc e) noexcept {
    return {std::to_underlying(e), fatfs_category()};
}

/**
 * @headerfile <idfxx/fatfs>
 * @brief Creates an unexpected error from a FatFs result code.
 *
 * For use with direct calls to FatFs `f_*` functions (on the FATFS object
 * from volume::idf_handle(), for example) from functions that return
 * `idfxx::result<T>`.
 *
 * `FR_NOT_ENOUGH_CORE` means a heap allocation failed, and follows the idfxx
 * out-of-memory policy: it throws std::bad_alloc (or aborts when exceptions
 * are disabled) rather than returning an error.
 *
 * @param fr The FatFs result code. Must not be `FR_OK`.
 *
 * @return An unexpected value holding the corresponding fatfs::errc.
 *
 * @code
 * idfxx::result<FSIZE_t> try_file_size(const idfxx::fatfs::volume& vol, std::string_view name) {
 *     std::string path = std::format("{}/{}", vol.drive(), name);
 *     FILINFO info;
 *     if (FRESULT fr = f_stat(path.c_str(), &info); fr != FR_OK) {
 *         return idfxx::fatfs::fatfs_error(fr);
 *     }
 *     return info.fsize;
 * }
 * @endcode
 */
[[nodiscard]] std::unexpected<std::error_code> fatfs_error(FRESULT fr);

/**
 * @headerfile <idfxx/fatfs>
 * @brief Settings used when creating a filesystem.
 *
 * Used by volume::format(), and, as a volume configuration's `format`, by the
 * format a mount does when it finds no filesystem and `format_if_missing`
 * is set.
 *
 * @code
 * vol.format({.allocation_unit_size = 16 * 1024});
 * @endcode
 */
struct format_options {
    /// Size of an allocation unit (cluster) in bytes, or std::nullopt to let FatFs choose one from
    /// the volume's size (one sector on small flash volumes, 32 KiB on cards of 2 GB or more). An
    /// explicit value must be a power of two from one sector to 128 sectors (512 bytes to 64 KiB on
    /// an SD card; `CONFIG_WL_SECTOR_SIZE` to 128 times that on a flash partition), and is
    /// otherwise rejected with idfxx::errc::invalid_arg before the medium is touched. Larger units
    /// make large files faster to read and write, at the cost of more space for small files.
    std::optional<size_t> allocation_unit_size = std::nullopt;
    /// Creates a single FAT instead of the usual two, which leaves more space for data but no copy
    /// to recover from if the FAT is damaged.
    bool use_one_fat = false;
};

/**
 * @headerfile <idfxx/fatfs>
 * @brief A mounted, writable FAT filesystem on an SD card or a wear-leveled flash partition.
 *
 * While the volume exists, its files are used through the C and C++
 * standard libraries under its mount path:
 *
 * @code
 * idfxx::fatfs::volume vol("storage", "/data", {.format_if_missing = true});
 *
 * if (FILE* f = std::fopen("/data/hello.txt", "w")) {
 *     std::fputs("Hello", f);
 *     std::fclose(f);
 * }
 * std::println("{} of {} bytes free", vol.free_size(), vol.size());
 * @endcode
 *
 * A mount path must start with '/', not end with '/', and be at most 13
 * characters long; a path that breaks these rules is rejected with
 * `idfxx::errc::invalid_arg` before anything else is checked. Each volume
 * and image uses one of FatFs's `CONFIG_FATFS_VOLUME_COUNT` drives (2 by
 * default) while it is mounted.
 *
 * A card or flash partition can have only one volume or image mounted on it
 * at a time: a second mount fails with `idfxx::errc::invalid_state` until the
 * first is unmounted.
 *
 * Mounting and unmounting are serialized between all volumes and images.
 * File operations may be used from multiple tasks; FatFs serializes them per
 * volume. free_size() may be called while another task formats the volume,
 * and waits for the format to finish; size() doesn't wait, and reports the
 * size from before the format until the new filesystem is mounted.
 *
 * On an SD card, the volume does not own the card: the card must outlive the
 * volume, and its probe(), raw sector and erase operations must not be used
 * while the volume is mounted (its accessors, is_present() and
 * is_write_protected() may be). The card object may be moved while the volume
 * is mounted. A card that is removed is recovered by unmounting the volume,
 * destroying the card object, creating a new one once a card is present, and
 * mounting the volume again.
 *
 * The destructor unmounts the volume if it is still mounted. All files on the
 * volume must be closed before it is unmounted or formatted, and no other
 * task may use the volume's files while it is formatted.
 *
 * This type is non-copyable and move-only. A moved-from object must not be
 * used: any operation other than destruction or assignment is undefined
 * behavior.
 */
class volume {
public:
    /**
     * @headerfile <idfxx/fatfs>
     * @brief Configuration for a volume on an SD card.
     *
     * @code
     * idfxx::fatfs::volume vol(card, "/sdcard", {
     *     .max_files = 8,
     *     .format_if_missing = true,
     * });
     * @endcode
     */
    struct card_config {
        /// Maximum number of files open at once on this volume. Must be at least 1. Memory for this
        /// many open files is allocated when the volume is mounted, including a sector buffer for
        /// each, unless `CONFIG_FATFS_USE_DYN_BUFFERS` is enabled (the default from ESP-IDF 6.0):
        /// each open file's buffer is then allocated when it is opened, and freed when it is
        /// closed.
        size_t max_files = 5;
        /// When set, a mount that finds no recognizable FAT filesystem on the card (including a
        /// damaged boot sector, and an exFAT filesystem on a partitioned card, which is not
        /// supported) formats the card with `format` and mounts it, losing the card's contents;
        /// otherwise such a mount fails with fatfs::errc::no_filesystem. The card is given a
        /// partition table with a single partition covering it. Read errors never cause a format;
        /// the mount fails with fatfs::errc::disk_err instead. Damage beyond the boot sector (in
        /// the FAT or a directory) isn't detected when mounting.
        bool format_if_missing = false;
        /// Settings for the format done when a mount finds no filesystem; unused unless
        /// `format_if_missing` is set. An invalid `allocation_unit_size` is rejected with
        /// idfxx::errc::invalid_arg even when it is not set.
        format_options format = {};
        /// Checks that the card responds before each access, so that a removed card is reported as
        /// not ready rather than as an I/O error: calls that take a path (`fopen()`, `stat()`,
        /// `remove()`, `opendir()`, ...) then fail with `ENODEV`, volume::try_free_size() with
        /// fatfs::errc::not_ready, and operations on a file or directory that is already open with
        /// `EBADF`. Without it they fail with `EIO` (fatfs::errc::disk_err). Adds a command to
        /// every access.
        bool disk_status_check = false;
    };

    /**
     * @headerfile <idfxx/fatfs>
     * @brief Configuration for a volume on a wear-leveled flash partition.
     *
     * @code
     * idfxx::fatfs::volume vol("storage", "/data", {.format_if_missing = true});
     * @endcode
     */
    struct flash_config {
        /// Maximum number of files open at once on this volume. Must be at least 1. Memory for this
        /// many open files is allocated when the volume is mounted, including a sector buffer for
        /// each, unless `CONFIG_FATFS_USE_DYN_BUFFERS` is enabled (the default from ESP-IDF 6.0):
        /// each open file's buffer is then allocated when it is opened, and freed when it is
        /// closed.
        size_t max_files = 5;
        /// When set, a mount that finds no recognizable FAT filesystem on the partition (for
        /// example, when it is erased, or its boot sector is damaged) formats the partition with
        /// `format` and mounts it, losing the partition's contents; otherwise such a mount fails
        /// with fatfs::errc::no_filesystem. Read errors never cause a format; the mount fails with
        /// fatfs::errc::disk_err instead. Damage beyond the boot sector (in the FAT or a directory)
        /// isn't detected when mounting.
        bool format_if_missing = false;
        /// Settings for the format done when a mount finds no filesystem; unused unless
        /// `format_if_missing` is set. An invalid `allocation_unit_size` is rejected with
        /// idfxx::errc::invalid_arg even when it is not set.
        format_options format = {};
    };

    // =========================================================================
    // On an SD card
    // =========================================================================

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Mounts the FAT filesystem on an SD card, with the default configuration.
     *
     * Does not take ownership of @p card: the caller must ensure that the card
     * outlives the volume. The card object may be moved while the volume is
     * mounted.
     *
     * @param card       An initialized card.
     * @param mount_path Where to mount the filesystem, for example "/sdcard".
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure, with the error codes listed for
     *         make(sdcard::card&, std::string_view, const card_config&).
     *
     * @code
     * idfxx::fatfs::volume vol(card, "/sdcard");
     * @endcode
     */
    [[nodiscard]] explicit volume(sdcard::card& card, std::string_view mount_path);

    /**
     * @brief Mounts the FAT filesystem on an SD card.
     *
     * Does not take ownership of @p card: the caller must ensure that the card
     * outlives the volume. The card object may be moved while the volume is
     * mounted.
     *
     * @param card       An initialized card.
     * @param mount_path Where to mount the filesystem, for example "/sdcard".
     * @param cfg        Volume configuration.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure, with the error codes listed for
     *         make(sdcard::card&, std::string_view, const card_config&).
     *
     * @code
     * idfxx::fatfs::volume vol(card, "/sdcard", {.format_if_missing = true});
     * @endcode
     */
    [[nodiscard]] explicit volume(
        sdcard::card& card,
        std::string_view mount_path,
        const card_config& cfg
    );
#endif

    /**
     * @brief Mounts the FAT filesystem on an SD card, with the default configuration.
     *
     * Does not take ownership of @p card: the caller must ensure that the card
     * outlives the volume. The card object may be moved while the volume is
     * mounted.
     *
     * @param card       An initialized card.
     * @param mount_path Where to mount the filesystem, for example "/sdcard".
     *
     * @return The mounted volume, or an error; see
     *         make(sdcard::card&, std::string_view, const card_config&).
     */
    [[nodiscard]] static result<volume> make(sdcard::card& card, std::string_view mount_path);

    /**
     * @brief Mounts the FAT filesystem on an SD card.
     *
     * Mounts the filesystem in the card's first FAT partition. When there is
     * no recognizable FAT filesystem (including a damaged boot sector) and
     * `cfg.format_if_missing` is set, the card is partitioned and
     * formatted first, erasing everything on it.
     *
     * A write-protected card (see sdcard::card::is_write_protected()) can't
     * be mounted as a writable volume, and is rejected; mount it read-only
     * with fatfs::image instead.
     *
     * Does not take ownership of @p card: the caller must ensure that the card
     * outlives the volume. The card object may be moved while the volume is
     * mounted.
     *
     * @param card       An initialized card.
     * @param mount_path Where to mount the filesystem, for example "/sdcard".
     * @param cfg        Volume configuration.
     *
     * @return The mounted volume, or an error.
     * @retval idfxx::errc::invalid_arg @p mount_path does not start with '/', ends with '/' or is
     *         longer than 13 characters, `cfg.max_files` is 0, or `cfg.format` has an invalid
     *         `allocation_unit_size`.
     * @retval idfxx::errc::invalid_state The card is write-protected (fatfs::image mounts it
     *         read-only), a volume or image is already mounted on @p card, another volume or image
     *         is mounted at @p mount_path, all of FatFs's drives are in use
     *         (`CONFIG_FATFS_VOLUME_COUNT`, 2 by default), or the virtual filesystem's table is
     *         full (`CONFIG_VFS_MAX_COUNT`, 8 by default).
     * @retval fatfs::errc::no_filesystem The card holds no recognizable FAT filesystem (an exFAT
     *         filesystem or a damaged boot sector counts as none), and `cfg.format_if_missing` is
     *         not set.
     * @retval fatfs::errc::not_ready The card did not respond (for example, it was removed).
     * @retval fatfs::errc::disk_err A read or write on the card failed. A read error never leads to
     *         a format.
     * @retval fatfs::errc::mkfs_aborted Formatting failed: the card is too small or too large for
     *         FAT with the given allocation unit size.
     */
    [[nodiscard]] static result<volume>
    make(sdcard::card& card, std::string_view mount_path, const card_config& cfg);

    // =========================================================================
    // On a wear-leveled flash partition
    // =========================================================================

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Mounts the FAT filesystem on a wear-leveled flash partition, with the default
     *        configuration.
     *
     * @param part       A data partition of subtype fat.
     * @param mount_path Where to mount the filesystem, for example "/data".
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure, with the error codes listed for
     *         make(const partition&, std::string_view, const flash_config&).
     *
     * @code
     * idfxx::fatfs::volume vol(idfxx::partition::find("storage"), "/data");
     * @endcode
     */
    [[nodiscard]] explicit volume(const partition& part, std::string_view mount_path);

    /**
     * @brief Mounts the FAT filesystem on a wear-leveled flash partition.
     *
     * @param part       A data partition of subtype fat.
     * @param mount_path Where to mount the filesystem, for example "/data".
     * @param cfg        Volume configuration.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure, with the error codes listed for
     *         make(const partition&, std::string_view, const flash_config&).
     *
     * @code
     * auto part = idfxx::partition::find("storage");
     * idfxx::fatfs::volume vol(part, "/data", {.format_if_missing = true});
     * @endcode
     */
    [[nodiscard]] explicit volume(
        const partition& part,
        std::string_view mount_path,
        const flash_config& cfg
    );

    /**
     * @brief Mounts the FAT filesystem on the wear-leveled flash partition with a given label, with
     *        the default configuration.
     *
     * @param partition_label The label of a data partition of subtype fat.
     * @param mount_path      Where to mount the filesystem, for example "/data".
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure, with the error codes listed for
     *         make(std::string_view, std::string_view, const flash_config&).
     *
     * @code
     * idfxx::fatfs::volume vol("storage", "/data");
     * @endcode
     */
    [[nodiscard]] explicit volume(std::string_view partition_label, std::string_view mount_path);

    /**
     * @brief Mounts the FAT filesystem on the wear-leveled flash partition with a given label.
     *
     * @param partition_label The label of a data partition of subtype fat.
     * @param mount_path      Where to mount the filesystem, for example "/data".
     * @param cfg             Volume configuration.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure, with the error codes listed for
     *         make(std::string_view, std::string_view, const flash_config&).
     *
     * @code
     * idfxx::fatfs::volume vol("storage", "/data", {.format_if_missing = true});
     * @endcode
     */
    [[nodiscard]] explicit volume(
        std::string_view partition_label,
        std::string_view mount_path,
        const flash_config& cfg
    );
#endif

    /**
     * @brief Mounts the FAT filesystem on a wear-leveled flash partition, with the default
     *        configuration.
     *
     * @param part       A data partition of subtype fat.
     * @param mount_path Where to mount the filesystem, for example "/data".
     *
     * @return The mounted volume, or an error; see
     *         make(const partition&, std::string_view, const flash_config&).
     */
    [[nodiscard]] static result<volume> make(const partition& part, std::string_view mount_path);

    /**
     * @brief Mounts the FAT filesystem on a wear-leveled flash partition.
     *
     * The partition is accessed through ESP-IDF's wear-leveling layer, which
     * spreads writes over the partition's flash sectors and keeps its own
     * state in some of them. A partition written by `fatfs_create_spiflash_image()`
     * holds a wear-leveled filesystem; one written by
     * `fatfs_create_rawflash_image()` does not, and is mounted with
     * fatfs::image instead.
     *
     * When the partition holds no recognizable FAT filesystem (for example,
     * when it is erased, or its boot sector is damaged) and
     * `cfg.format_if_missing` is set, it is formatted first.
     *
     * @param part       A data partition of subtype fat.
     * @param mount_path Where to mount the filesystem, for example "/data".
     * @param cfg        Volume configuration.
     *
     * @return The mounted volume, or an error.
     * @retval idfxx::errc::invalid_arg @p mount_path does not start with '/', ends with '/' or is
     *         longer than 13 characters, `cfg.max_files` is 0, `cfg.format` has an invalid
     *         `allocation_unit_size`, the partition is not a data partition of subtype fat, or it
     *         is marked read-only in the partition table.
     * @retval idfxx::errc::invalid_state A volume or image is already mounted on the partition,
     *         another volume or image is mounted at @p mount_path, all of FatFs's drives are in use
     *         (`CONFIG_FATFS_VOLUME_COUNT`, 2 by default), the virtual filesystem's table is full
     *         (`CONFIG_VFS_MAX_COUNT`, 8 by default), or all of the wear-leveling layer's 8 handles
     *         are in use.
     * @retval fatfs::errc::no_filesystem The partition holds no recognizable FAT filesystem (a
     *         damaged boot sector counts as none), and `cfg.format_if_missing` is not set.
     * @retval fatfs::errc::disk_err A read or write on the partition failed. A read error never
     *         leads to a format.
     * @retval fatfs::errc::mkfs_aborted Formatting failed: the partition is too small for FAT with
     *         the given allocation unit size.
     * @retval Other idfxx::errc values for errors from the wear-leveling layer (for example
     *         idfxx::errc::invalid_arg for a partition too small to hold its state).
     */
    [[nodiscard]] static result<volume>
    make(const partition& part, std::string_view mount_path, const flash_config& cfg);

    /**
     * @brief Mounts the FAT filesystem on the wear-leveled flash partition with a given label, with
     *        the default configuration.
     *
     * @param partition_label The label of a data partition of subtype fat.
     * @param mount_path      Where to mount the filesystem, for example "/data".
     *
     * @return The mounted volume, or an error; see
     *         make(std::string_view, std::string_view, const flash_config&).
     */
    [[nodiscard]] static result<volume>
    make(std::string_view partition_label, std::string_view mount_path);

    /**
     * @brief Mounts the FAT filesystem on the wear-leveled flash partition with a given label.
     *
     * Equivalent to make(const partition&, std::string_view, const flash_config&)
     * on the partition with that label. The other arguments are checked before
     * the partition is looked up, so invalid ones (including swapping the
     * label and the mount path) fail with idfxx::errc::invalid_arg whether or
     * not a partition with that label exists.
     *
     * @param partition_label The label of a data partition of subtype fat.
     * @param mount_path      Where to mount the filesystem, for example "/data".
     * @param cfg             Volume configuration.
     *
     * @return The mounted volume, or an error.
     * @retval idfxx::errc::invalid_arg @p mount_path does not start with '/', ends with '/' or is
     *         longer than 13 characters, `cfg.max_files` is 0, `cfg.format` has an invalid
     *         `allocation_unit_size`, the partition is not a data partition of subtype fat, or it
     *         is marked read-only in the partition table.
     * @retval idfxx::errc::not_found There is no partition with that label.
     * @retval idfxx::errc::invalid_state A volume or image is already mounted on the partition,
     *         another volume or image is mounted at @p mount_path, all of FatFs's drives are in use
     *         (`CONFIG_FATFS_VOLUME_COUNT`, 2 by default), the virtual filesystem's table is full
     *         (`CONFIG_VFS_MAX_COUNT`, 8 by default), or all of the wear-leveling layer's 8 handles
     *         are in use.
     * @retval fatfs::errc::no_filesystem The partition holds no recognizable FAT filesystem (a
     *         damaged boot sector counts as none), and `cfg.format_if_missing` is not set.
     * @retval fatfs::errc::disk_err A read or write on the partition failed. A read error never
     *         leads to a format.
     * @retval fatfs::errc::mkfs_aborted Formatting failed: the partition is too small for FAT with
     *         the given allocation unit size.
     * @retval Other idfxx::errc values for errors from the wear-leveling layer.
     */
    [[nodiscard]] static result<volume>
    make(std::string_view partition_label, std::string_view mount_path, const flash_config& cfg);

    /**
     * @brief Unmounts the volume, if it is still mounted.
     *
     * Errors are logged; use unmount() or try_unmount() first to handle them.
     */
    ~volume();

    volume(const volume&) = delete;
    volume& operator=(const volume&) = delete;

    /** @brief Move constructor. Transfers the mount; the mount path and open files stay valid. */
    volume(volume&& other) noexcept;

    /**
     * @brief Move assignment. Unmounts this volume if it is still mounted, then takes over
     *        @p other's mount.
     */
    volume& operator=(volume&& other) noexcept;

    // =========================================================================
    // Accessors
    // =========================================================================

    /**
     * @brief Returns the path the volume is mounted at.
     *
     * @return The mount path, for example "/sdcard".
     */
    [[nodiscard]] std::string_view mount_path() const noexcept;

    /**
     * @brief Returns the volume's FatFs logical drive.
     *
     * For direct calls to FatFs `f_*` functions, whose paths start with the
     * drive (for example "0:/hello.txt").
     *
     * @return The drive, for example "0:".
     */
    [[nodiscard]] std::string_view drive() const noexcept;

    /**
     * @brief Returns the volume's FatFs filesystem object.
     *
     * For direct calls to FatFs functions that take a `FATFS*`. Valid until
     * the volume is unmounted; after a format() whose remount failed, it
     * holds no mounted filesystem.
     *
     * @return The filesystem object.
     */
    [[nodiscard]] FATFS* idf_handle() const noexcept;

    /**
     * @brief Returns the volume's capacity for data.
     *
     * The size of the data area: the total size of all clusters, used and
     * free. Determined each time the filesystem is mounted: when the volume
     * is created, and when format() mounts it again. After a format() whose
     * remount fails, it still reports the size from before the format. Called
     * while format() runs in another task, it doesn't wait for the format to
     * finish: it returns the size from before the format until the new
     * filesystem is mounted.
     *
     * @return The capacity in bytes.
     */
    [[nodiscard]] uint64_t size() const noexcept;

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Returns the free space on the volume.
     *
     * The first call after mounting may scan the whole FAT, which can take a
     * while on a large card; FatFs then keeps the free count up to date.
     * Called while format() runs in another task, it waits for the format to
     * finish.
     *
     * @return The free space in bytes.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure, with the error codes listed for try_free_size().
     *
     * @code
     * std::println("{} of {} bytes free", vol.free_size(), vol.size());
     * @endcode
     */
    [[nodiscard]] uint64_t free_size() const;
#endif

    /**
     * @brief Returns the free space on the volume.
     *
     * The first call after mounting may scan the whole FAT, which can take a
     * while on a large card; FatFs then keeps the free count up to date.
     * Called while format() runs in another task, it waits for the format to
     * finish.
     *
     * @return The free space in bytes, or an error.
     * @retval fatfs::errc::disk_err A read on the medium failed.
     * @retval fatfs::errc::not_ready The card did not respond (for example, it was removed);
     *         reported this way only with `card_config::disk_status_check`.
     * @retval fatfs::errc::not_enabled A format() whose remount failed left the volume without a
     *         mounted filesystem.
     */
    [[nodiscard]] result<uint64_t> try_free_size() const;

    // =========================================================================
    // Operations
    // =========================================================================

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Erases the volume and creates an empty filesystem on it.
     *
     * The volume stays mounted at the same path, empty. On an SD card, the
     * card is first given a partition table with a single partition covering
     * it. All files and directories on the volume must be closed first, and
     * no other task may use the volume's files while it is formatted;
     * free_size() waits for it to finish, while size() returns at once.
     *
     * If formatting fails, the volume is mounted again with whatever the
     * medium then holds; if that fails too, file operations on the volume
     * fail (and try_free_size() reports fatfs::errc::not_enabled) until it is
     * unmounted or formatted successfully, and size() still reports the size
     * from before the format.
     *
     * @param opts Settings for the new filesystem.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure, with the error codes listed for try_format().
     *
     * @code
     * vol.format();
     * @endcode
     */
    void format(const format_options& opts = {});
#endif

    /**
     * @brief Erases the volume and creates an empty filesystem on it.
     *
     * The volume stays mounted at the same path, empty. On an SD card, the
     * card is first given a partition table with a single partition covering
     * it. All files and directories on the volume must be closed first, and
     * no other task may use the volume's files while it is formatted;
     * free_size() waits for it to finish, while size() returns at once.
     *
     * If formatting fails, the volume is mounted again with whatever the
     * medium then holds; if that fails too, file operations on the volume
     * fail (and try_free_size() reports fatfs::errc::not_enabled) until it is
     * unmounted or formatted successfully, and size() still reports the size
     * from before the format.
     *
     * @param opts Settings for the new filesystem.
     *
     * @return Success, or an error: the formatting error if formatting failed, otherwise the error
     *         from mounting the new filesystem.
     * @retval idfxx::errc::invalid_arg `opts.allocation_unit_size` is invalid (see format_options).
     *         Checked first: the volume is left mounted and unchanged.
     * @retval fatfs::errc::disk_err A read or write on the medium failed.
     * @retval fatfs::errc::not_ready The card did not respond (for example, it was removed).
     * @retval fatfs::errc::mkfs_aborted The medium is unsuitable for FAT with the given allocation
     *         unit size.
     * @retval fatfs::errc::no_filesystem Mounting the new filesystem found none.
     */
    [[nodiscard]] result<void> try_format(const format_options& opts = {});

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Unmounts the volume.
     *
     * The destructor unmounts the volume otherwise; unmounting explicitly
     * reports errors that the destructor can only log, notably a failure to
     * save the wear-leveling state of a flash partition. The volume is
     * unmounted even when an error is reported. All files on the volume must
     * be closed first.
     *
     * Afterward the object may only be destroyed or assigned to.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure, with the error codes listed for try_unmount().
     *
     * @code
     * vol.unmount();
     * @endcode
     */
    void unmount();
#endif

    /**
     * @brief Unmounts the volume.
     *
     * The destructor unmounts the volume otherwise; unmounting explicitly
     * reports errors that the destructor can only log, notably a failure to
     * save the wear-leveling state of a flash partition. The volume is
     * unmounted even when an error is reported. All files on the volume must
     * be closed first.
     *
     * Afterward the object may only be destroyed or assigned to.
     *
     * @return Success, or the first error encountered.
     * @retval Other idfxx::errc values for errors from the wear-leveling layer, which writes its
     *         state to flash.
     */
    result<void> try_unmount();

    // =========================================================================
    // Replacing files
    // =========================================================================

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Replaces a file with new contents, so that a reset leaves either the old contents or
     *        the new.
     *
     * Writes @p contents to a temporary file in the same directory as
     * @p path, then puts that file in place of the one at @p path, creating
     * it if there is none. Once recover_file() has run for @p path, a reset
     * at any point leaves either the old contents or the new. Call
     * recover_file() at startup, before reading the file, for each file
     * replaced this way.
     *
     * FatFs can't rename a file onto an existing one, so the file is replaced
     * in steps, using two names in the same directory:
     * 1. Finishes or discards an earlier replace of @p path that a reset
     *    interrupted, as recover_file() does.
     * 2. Writes the new contents to `<HASH>.~RT`, and closes it, which writes
     *    it to the medium.
     * 3. Renames `<HASH>.~RT` to `<HASH>.~RN`, which marks the new contents
     *    complete.
     * 4. Removes the file at @p path, if there is one, then renames
     *    `<HASH>.~RN` to @p path.
     *
     * `<HASH>` is the FNV-1a 32-bit hash of the last component of @p path,
     * with ASCII letters folded to uppercase first, as 8 uppercase hex
     * digits: 8.3 names, valid with or without long file names. If writing
     * the new contents fails, the temporary file is removed and the file at
     * @p path is left as it was. If a rename or removal fails, the files are
     * left as a reset at that point would leave them, for recover_file() to
     * finish or discard the replace.
     *
     * The directory must already exist, and @p path must not name a
     * directory. The file must not be open while it is replaced, and
     * replacing the same path from two tasks at once is not supported.
     *
     * @warning A reset in the middle of one of the renames can corrupt the file when FatFs places
     *          the new directory entry in a different sector from the old one: it writes the new
     *          entry and then removes the old one, so a reset between the two writes leaves both
     *          names on the same data, and removing either one (as recovery does) frees data the
     *          other still uses. FAT can't detect or repair this short of a filesystem check.
     *
     * @param path     The file's path, under mount_path(): for example "/sdcard/config.json".
     * @param contents The new contents.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure, with the error codes listed for
     *         try_replace_file(std::string_view, std::span<const uint8_t>).
     *
     * @code
     * std::vector<uint8_t> settings = encode_settings();
     * vol.replace_file("/sdcard/settings.bin", settings);
     * @endcode
     */
    void replace_file(std::string_view path, std::span<const uint8_t> contents);

    /**
     * @brief Replaces a file with new text, so that a reset leaves either the old contents or the
     *        new.
     *
     * Writes the characters of @p text, as they are and with no terminating
     * null, exactly as replace_file(std::string_view, std::span<const uint8_t>)
     * writes its contents. Once recover_file() has run for @p path, a reset at
     * any point leaves either the old contents or the new. Call
     * recover_file() at startup, before reading the file, for each file
     * replaced this way.
     *
     * The directory must already exist, and @p path must not name a
     * directory. The file must not be open while it is replaced, and
     * replacing the same path from two tasks at once is not supported.
     *
     * @warning A reset in the middle of one of the renames can corrupt the file, as described for
     *          replace_file(std::string_view, std::span<const uint8_t>).
     *
     * @param path The file's path, under mount_path(): for example "/sdcard/config.json".
     * @param text The new contents.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure, with the error codes listed for
     *         try_replace_file(std::string_view, std::span<const uint8_t>).
     *
     * @code
     * vol.replace_file("/sdcard/config.json", std::format(R"({{"brightness": {}}})", brightness));
     * @endcode
     */
    void replace_file(std::string_view path, std::string_view text);

    /**
     * @brief Replaces a file with the contents of a byte source, so that a reset leaves either the
     *        old contents or the new.
     *
     * Reads @p source to its end, through a 4 KiB buffer allocated for the
     * call, and writes what it reads to a temporary file in the same
     * directory as @p path, then puts that file in place of the one at
     * @p path, creating it if there is none. Once recover_file() has run for
     * @p path, a reset at any point leaves either the old contents or the
     * new. Call recover_file() at startup, before reading the file, for each
     * file replaced this way.
     *
     * The file is replaced in the steps described for
     * replace_file(std::string_view, std::span<const uint8_t>), through
     * `<HASH>.~RT` and `<HASH>.~RN` in the same directory. If reading
     * @p source or writing the new contents fails, the temporary file is
     * removed and the file at @p path is left as it was; so it is when the
     * source's `try_read()` throws (for example, std::bad_alloc), and the
     * exception then propagates. If a rename or removal fails, the files are
     * left as a reset at that point would leave them, for recover_file() to
     * finish or discard the replace.
     *
     * The directory must already exist, and @p path must not name a
     * directory. The file must not be open while it is replaced, and
     * replacing the same path from two tasks at once is not supported.
     *
     * @warning A reset in the middle of one of the renames can corrupt the file when FatFs places
     *          the new directory entry in a different sector from the old one: it writes the new
     *          entry and then removes the old one, so a reset between the two writes leaves both
     *          names on the same data, and removing either one (as recovery does) frees data the
     *          other still uses. FAT can't detect or repair this short of a filesystem check.
     *
     * @tparam Source The byte source type.
     * @param path   The file's path, under mount_path(): for example "/sdcard/config.json".
     * @param source The source of the new contents, read until it reports its end.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure, with the error codes listed for
     *         try_replace_file(std::string_view, Source&).
     *
     * @code
     * // Saves a copy of the `assets` partition on the card.
     * auto assets = idfxx::partition::find("assets");
     * idfxx::partition_source src{assets};
     * vol.replace_file("/sdcard/assets.bin", src);
     * @endcode
     */
    template<byte_source Source>
    void replace_file(std::string_view path, Source& source) {
        unwrap(try_replace_file(path, source));
    }

    /**
     * @brief Replaces a file with contents written by a callback, so that a reset leaves either the
     *        old contents or the new.
     *
     * Calls @p writer with a temporary file in the same directory as @p path,
     * opened with `fopen()` in mode "wb", to write the new contents with
     * stdio (`fwrite()`, `fprintf()`, ...), then puts that file in place of
     * the one at @p path, creating it if there is none. Once recover_file()
     * has run for @p path, a reset at any point leaves either the old
     * contents or the new. Call recover_file() at startup, before reading the
     * file, for each file replaced this way.
     *
     * @p writer may call `setvbuf()` before writing, but must not close the
     * file. It returns success, or an error to abandon the replace: the
     * temporary file is then removed, the file at @p path is left as it was,
     * and the error is returned. A write that fails without @p writer
     * reporting it is caught by the stream's error indicator (`ferror()`),
     * and also abandons the replace. If @p writer throws, the temporary file
     * is removed and the exception propagates. No lock is held while
     * @p writer runs, so it may call free_size(), for example.
     *
     * The file is replaced in the steps described for
     * replace_file(std::string_view, std::span<const uint8_t>), through
     * `<HASH>.~RT` and `<HASH>.~RN` in the same directory. If writing the new
     * contents fails, the temporary file is removed and the file at @p path
     * is left as it was. If a rename or removal fails, the files are left as
     * a reset at that point would leave them, for recover_file() to finish or
     * discard the replace.
     *
     * The directory must already exist, and @p path must not name a
     * directory. The file must not be open while it is replaced, and
     * replacing the same path from two tasks at once is not supported.
     *
     * @warning A reset in the middle of one of the renames can corrupt the file when FatFs places
     *          the new directory entry in a different sector from the old one: it writes the new
     *          entry and then removes the old one, so a reset between the two writes leaves both
     *          names on the same data, and removing either one (as recovery does) frees data the
     *          other still uses. FAT can't detect or repair this short of a filesystem check.
     *
     * @tparam Writer A callable type, invocable with a `FILE*` and returning idfxx::result<void>.
     * @param path   The file's path, under mount_path(): for example "/sdcard/config.json".
     * @param writer Writes the new contents to the `FILE*` it is given.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure, with the error codes listed for
     *         try_replace_file(std::string_view, Writer&&).
     *
     * @code
     * vol.replace_file("/sdcard/config.txt", [&](FILE* f) -> idfxx::result<void> {
     *     std::fprintf(f, "brightness=%d\n", brightness);
     *     return {};
     * });
     * @endcode
     */
    template<typename Writer>
        requires std::is_invocable_r_v<result<void>, Writer&, FILE*>
    void replace_file(std::string_view path, Writer&& writer) {
        unwrap(try_replace_file(path, std::forward<Writer>(writer)));
    }
#endif

    /**
     * @brief Replaces a file with new contents, so that a reset leaves either the old contents or
     *        the new.
     *
     * Writes @p contents to a temporary file in the same directory as
     * @p path, then puts that file in place of the one at @p path, creating
     * it if there is none. Once recover_file() has run for @p path, a reset
     * at any point leaves either the old contents or the new. Call
     * recover_file() at startup, before reading the file, for each file
     * replaced this way.
     *
     * FatFs can't rename a file onto an existing one, so the file is replaced
     * in steps, using two names in the same directory:
     * 1. Finishes or discards an earlier replace of @p path that a reset
     *    interrupted, as recover_file() does.
     * 2. Writes the new contents to `<HASH>.~RT`, and closes it, which writes
     *    it to the medium.
     * 3. Renames `<HASH>.~RT` to `<HASH>.~RN`, which marks the new contents
     *    complete.
     * 4. Removes the file at @p path, if there is one, then renames
     *    `<HASH>.~RN` to @p path.
     *
     * `<HASH>` is the FNV-1a 32-bit hash of the last component of @p path,
     * with ASCII letters folded to uppercase first, as 8 uppercase hex
     * digits: 8.3 names, valid with or without long file names. If writing
     * the new contents fails, the temporary file is removed and the file at
     * @p path is left as it was. If a rename or removal fails, the files are
     * left as a reset at that point would leave them, for recover_file() to
     * finish or discard the replace.
     *
     * The directory must already exist, and @p path must not name a
     * directory. The file must not be open while it is replaced, and
     * replacing the same path from two tasks at once is not supported.
     *
     * @warning A reset in the middle of one of the renames can corrupt the file when FatFs places
     *          the new directory entry in a different sector from the old one: it writes the new
     *          entry and then removes the old one, so a reset between the two writes leaves both
     *          names on the same data, and removing either one (as recovery does) frees data the
     *          other still uses. FAT can't detect or repair this short of a filesystem check.
     *
     * @param path     The file's path, under mount_path(): for example "/sdcard/config.json".
     * @param contents The new contents.
     *
     * @return Success, or an error.
     * @retval idfxx::errc::invalid_arg @p path is not under mount_path() (a path that only starts
     *         with the same characters, such as "/sdcard2/x" for a volume at "/sdcard", is not),
     *         names the mount path itself, ends with '/' or contains a null character.
     * @retval fatfs::errc::no_path The directory doesn't exist.
     * @retval fatfs::errc::invalid_name The path isn't a valid FAT name (for example, a long file
     *         name when long file names are disabled). Checked before anything is written.
     * @retval fatfs::errc::denied @p path names a directory or a read-only file (checked before
     *         anything is written), or the directory is full.
     * @retval errno An error in `std::generic_category()` holding the `errno` from creating,
     *         writing or closing the temporary file: for example `ENOSPC` when the volume is full.
     * @retval fatfs::errc::disk_err A read or write on the medium failed.
     * @retval fatfs::errc::not_ready The card did not respond (for example, it was removed);
     *         reported this way only with `card_config::disk_status_check`.
     * @retval fatfs::errc::not_enabled A format() whose remount failed left the volume without a
     *         mounted filesystem.
     */
    [[nodiscard]] result<void>
    try_replace_file(std::string_view path, std::span<const uint8_t> contents);

    /**
     * @brief Replaces a file with new text, so that a reset leaves either the old contents or the
     *        new.
     *
     * Writes the characters of @p text, as they are and with no terminating
     * null, exactly as try_replace_file(std::string_view, std::span<const uint8_t>)
     * writes its contents. Once recover_file() has run for @p path, a reset at
     * any point leaves either the old contents or the new. Call
     * recover_file() at startup, before reading the file, for each file
     * replaced this way.
     *
     * The directory must already exist, and @p path must not name a
     * directory. The file must not be open while it is replaced, and
     * replacing the same path from two tasks at once is not supported.
     *
     * @warning A reset in the middle of one of the renames can corrupt the file, as described for
     *          try_replace_file(std::string_view, std::span<const uint8_t>).
     *
     * @param path The file's path, under mount_path(): for example "/sdcard/config.json".
     * @param text The new contents.
     *
     * @return Success, or an error; see
     *         try_replace_file(std::string_view, std::span<const uint8_t>).
     */
    [[nodiscard]] result<void> try_replace_file(std::string_view path, std::string_view text);

    /**
     * @brief Replaces a file with the contents of a byte source, so that a reset leaves either the
     *        old contents or the new.
     *
     * Reads @p source to its end, through a 4 KiB buffer allocated for the
     * call, and writes what it reads to a temporary file in the same
     * directory as @p path, then puts that file in place of the one at
     * @p path, creating it if there is none. Once recover_file() has run for
     * @p path, a reset at any point leaves either the old contents or the
     * new. Call recover_file() at startup, before reading the file, for each
     * file replaced this way.
     *
     * The file is replaced in the steps described for
     * try_replace_file(std::string_view, std::span<const uint8_t>), through
     * `<HASH>.~RT` and `<HASH>.~RN` in the same directory. If reading
     * @p source or writing the new contents fails, the temporary file is
     * removed and the file at @p path is left as it was; so it is when the
     * source's `try_read()` throws (for example, std::bad_alloc), and the
     * exception then propagates. If a rename or removal fails, the files are
     * left as a reset at that point would leave them, for recover_file() to
     * finish or discard the replace.
     *
     * The directory must already exist, and @p path must not name a
     * directory. The file must not be open while it is replaced, and
     * replacing the same path from two tasks at once is not supported.
     *
     * @warning A reset in the middle of one of the renames can corrupt the file when FatFs places
     *          the new directory entry in a different sector from the old one: it writes the new
     *          entry and then removes the old one, so a reset between the two writes leaves both
     *          names on the same data, and removing either one (as recovery does) frees data the
     *          other still uses. FAT can't detect or repair this short of a filesystem check.
     *
     * @tparam Source The byte source type.
     * @param path   The file's path, under mount_path(): for example "/sdcard/config.json".
     * @param source The source of the new contents, read until it reports its end.
     *
     * @return Success, or an error: the source's own error, unchanged, if reading it fails,
     *         otherwise one of those listed for
     *         try_replace_file(std::string_view, std::span<const uint8_t>).
     */
    template<byte_source Source>
    [[nodiscard]] result<void> try_replace_file(std::string_view path, Source& source) {
        auto read = [&source](std::span<uint8_t> buf) -> result<size_t> {
            return source.try_read(byte_span{buf});
        };
        return _try_replace_file_from(path, &read, &_read<decltype(read)>);
    }

    /**
     * @brief Replaces a file with contents written by a callback, so that a reset leaves either the
     *        old contents or the new.
     *
     * Calls @p writer with a temporary file in the same directory as @p path,
     * opened with `fopen()` in mode "wb", to write the new contents with
     * stdio (`fwrite()`, `fprintf()`, ...), then puts that file in place of
     * the one at @p path, creating it if there is none. Once recover_file()
     * has run for @p path, a reset at any point leaves either the old
     * contents or the new. Call recover_file() at startup, before reading the
     * file, for each file replaced this way.
     *
     * @p writer may call `setvbuf()` before writing, but must not close the
     * file. It returns success, or an error to abandon the replace: the
     * temporary file is then removed, the file at @p path is left as it was,
     * and the error is returned. A write that fails without @p writer
     * reporting it is caught by the stream's error indicator (`ferror()`),
     * and also abandons the replace. If @p writer throws, the temporary file
     * is removed and the exception propagates. No lock is held while
     * @p writer runs, so it may call try_free_size(), for example.
     *
     * The file is replaced in the steps described for
     * try_replace_file(std::string_view, std::span<const uint8_t>), through
     * `<HASH>.~RT` and `<HASH>.~RN` in the same directory. If writing the new
     * contents fails, the temporary file is removed and the file at @p path
     * is left as it was. If a rename or removal fails, the files are left as
     * a reset at that point would leave them, for recover_file() to finish or
     * discard the replace.
     *
     * The directory must already exist, and @p path must not name a
     * directory. The file must not be open while it is replaced, and
     * replacing the same path from two tasks at once is not supported.
     *
     * @warning A reset in the middle of one of the renames can corrupt the file when FatFs places
     *          the new directory entry in a different sector from the old one: it writes the new
     *          entry and then removes the old one, so a reset between the two writes leaves both
     *          names on the same data, and removing either one (as recovery does) frees data the
     *          other still uses. FAT can't detect or repair this short of a filesystem check.
     *
     * @tparam Writer A callable type, invocable with a `FILE*` and returning idfxx::result<void>.
     * @param path   The file's path, under mount_path(): for example "/sdcard/config.json".
     * @param writer Writes the new contents to the `FILE*` it is given.
     *
     * @return Success, or an error: the error @p writer returned, an error in
     *         `std::generic_category()` holding the `errno` for a write it didn't report, or one of
     *         those listed for try_replace_file(std::string_view, std::span<const uint8_t>).
     */
    template<typename Writer>
        requires std::is_invocable_r_v<result<void>, Writer&, FILE*>
    [[nodiscard]] result<void> try_replace_file(std::string_view path, Writer&& writer) {
        auto write = [&writer](FILE* file) -> result<void> { return std::invoke(writer, file); };
        return _try_replace_file_with(path, &write, &_write<decltype(write)>);
    }

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Finishes or discards a replace of a file that a reset interrupted.
     *
     * Completes or undoes a replace_file() of @p path that a reset stopped
     * part-way, so that the file holds either its old contents or the new:
     * if the new contents were complete (`<HASH>.~RN` exists), removes the
     * file at @p path, if there is one, and renames `<HASH>.~RN` to @p path;
     * then removes `<HASH>.~RT`, if present, which may be incomplete. The
     * names are those described for
     * replace_file(std::string_view, std::span<const uint8_t>). Does nothing
     * when there is nothing to recover, including when the directory doesn't
     * exist. Running it again after a reset part-way through it is safe.
     *
     * Call it at startup, before reading the file, for each file replaced
     * with replace_file(). Each replace_file() also runs it for its path
     * first. Recovering the same path from two tasks at once, or while it is
     * replaced, is not supported.
     *
     * @warning A reset in the middle of one of the renames can corrupt the file when FatFs places
     *          the new directory entry in a different sector from the old one: it writes the new
     *          entry and then removes the old one, so a reset between the two writes leaves both
     *          names on the same data, and removing either one (as recovery does) frees data the
     *          other still uses. FAT can't detect or repair this short of a filesystem check.
     *
     * @param path The file's path, under mount_path(): for example "/sdcard/config.json".
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure, with the error codes listed for try_recover_file().
     *
     * @code
     * idfxx::fatfs::volume vol(card, "/sdcard");
     * vol.recover_file("/sdcard/config.json");
     * std::ifstream in("/sdcard/config.json");
     * @endcode
     */
    void recover_file(std::string_view path);
#endif

    /**
     * @brief Finishes or discards a replace of a file that a reset interrupted.
     *
     * Completes or undoes a try_replace_file() of @p path that a reset
     * stopped part-way, so that the file holds either its old contents or the
     * new: if the new contents were complete (`<HASH>.~RN` exists), removes
     * the file at @p path, if there is one, and renames `<HASH>.~RN` to
     * @p path; then removes `<HASH>.~RT`, if present, which may be
     * incomplete. The names are those described for
     * try_replace_file(std::string_view, std::span<const uint8_t>). Does
     * nothing when there is nothing to recover, including when the directory
     * doesn't exist. Running it again after a reset part-way through it is
     * safe.
     *
     * Call it at startup, before reading the file, for each file replaced
     * with try_replace_file(). Each try_replace_file() also runs it for its
     * path first. Recovering the same path from two tasks at once, or while
     * it is replaced, is not supported.
     *
     * @warning A reset in the middle of one of the renames can corrupt the file when FatFs places
     *          the new directory entry in a different sector from the old one: it writes the new
     *          entry and then removes the old one, so a reset between the two writes leaves both
     *          names on the same data, and removing either one (as recovery does) frees data the
     *          other still uses. FAT can't detect or repair this short of a filesystem check.
     *
     * @param path The file's path, under mount_path(): for example "/sdcard/config.json".
     *
     * @return Success, including when there was nothing to recover, or an error.
     * @retval idfxx::errc::invalid_arg @p path is not under mount_path(), names the mount path
     *         itself, ends with '/' or contains a null character.
     * @retval fatfs::errc::invalid_name The path isn't a valid FAT name.
     * @retval fatfs::errc::denied The new contents are complete, but the file at @p path can't be
     *         removed (it is read-only, or a directory that isn't empty).
     * @retval fatfs::errc::disk_err A read or write on the medium failed.
     * @retval fatfs::errc::not_ready The card did not respond (for example, it was removed);
     *         reported this way only with `card_config::disk_status_check`.
     * @retval fatfs::errc::not_enabled A format() whose remount failed left the volume without a
     *         mounted filesystem.
     */
    [[nodiscard]] result<void> try_recover_file(std::string_view path);

private:
    /// @cond INTERNAL
    struct state;
    explicit volume(std::unique_ptr<state> s) noexcept;

    // Type-erased byte sources and writers for the replace_file() templates, which pass them to the
    // implementation: each is a callable of type F, passed as a void* with a function that calls
    // it.
    using read_fn = result<size_t> (*)(void* source, std::span<uint8_t> buf);
    using write_fn = result<void> (*)(void* writer, FILE* file);

    template<typename F>
    static result<size_t> _read(void* source, std::span<uint8_t> buf) {
        return (*static_cast<F*>(source))(buf);
    }

    template<typename F>
    static result<void> _write(void* writer, FILE* file) {
        return (*static_cast<F*>(writer))(file);
    }

    [[nodiscard]] result<void>
    _try_replace_file_from(std::string_view path, void* source, read_fn read);
    [[nodiscard]] result<void>
    _try_replace_file_with(std::string_view path, void* writer, write_fn write);
    /// @endcond

    std::unique_ptr<state> _state;
};

/**
 * @headerfile <idfxx/fatfs>
 * @brief A mounted, read-only FAT volume on a flash partition or an SD card.
 *
 * On a flash partition, the image is a prebuilt one, read directly from the
 * partition without wear leveling: typically generated at build time from a
 * directory of files with `fatfs_create_rawflash_image()` and flashed with
 * the app.
 *
 * @code
 * // CMakeLists.txt: fatfs_create_rawflash_image(assets ../assets FLASH_IN_PROJECT)
 * idfxx::fatfs::image assets("assets", "/assets");
 * std::ifstream in("/assets/index.html");
 * @endcode
 *
 * On an SD card, the image is the card's own FAT filesystem, and nothing is
 * written to the card. A write-protected card (see
 * sdcard::card::is_write_protected()), which volume refuses, mounts as an
 * image like any other.
 *
 * @code
 * idfxx::fatfs::image photos(card, "/sdcard");
 * std::ifstream in("/sdcard/photos/list.txt");
 * @endcode
 *
 * Opening a file for writing, and creating, removing or renaming files or
 * directories, fail with `EACCES` (FatFs reports the image as
 * write-protected): `fopen()` with a writing mode returns NULL, and the POSIX
 * calls return -1.
 *
 * Mount paths follow the same rules as for volume, and an image uses one of
 * FatFs's `CONFIG_FATFS_VOLUME_COUNT` drives (2 by default) while it is
 * mounted. A card or flash partition can have only one volume or image
 * mounted on it at a time: a second mount fails with
 * `idfxx::errc::invalid_state` until the first is unmounted.
 *
 * On an SD card, the image does not own the card: the card must outlive the
 * image, and its probe(), raw sector and erase operations must not be used
 * while the image is mounted (its accessors, is_present() and
 * is_write_protected() may be). The card object may be moved while the image
 * is mounted. A card that is removed is recovered by unmounting the image,
 * destroying the card object, creating a new one once a card is present, and
 * mounting the image again.
 *
 * The destructor unmounts the image if it is still mounted. All files on the
 * image must be closed before it is unmounted.
 *
 * This type is non-copyable and move-only. A moved-from object must not be
 * used: any operation other than destruction or assignment is undefined
 * behavior.
 */
class image {
public:
    /**
     * @headerfile <idfxx/fatfs>
     * @brief Configuration for an image on an SD card.
     *
     * @code
     * idfxx::fatfs::image photos(card, "/sdcard", {.max_files = 2, .disk_status_check = true});
     * @endcode
     */
    struct card_config {
        /// Maximum number of files open at once on this image. Must be at least 1. Memory for this
        /// many open files is allocated when the image is mounted, including a sector buffer for
        /// each, unless `CONFIG_FATFS_USE_DYN_BUFFERS` is enabled (the default from ESP-IDF 6.0):
        /// each open file's buffer is then allocated when it is opened, and freed when it is
        /// closed.
        size_t max_files = 5;
        /// Checks that the card responds before each access, so that a removed card is reported as
        /// not ready rather than as an I/O error: calls that take a path (`fopen()`, `stat()`,
        /// `opendir()`, ...) then fail with `ENODEV`, image::try_free_size() with
        /// fatfs::errc::not_ready, and operations on a file or directory that is already open with
        /// `EBADF`. Without it they fail with `EIO` (fatfs::errc::disk_err). Adds a command to
        /// every access.
        bool disk_status_check = false;
    };

    /**
     * @headerfile <idfxx/fatfs>
     * @brief Configuration for an image on a flash partition.
     *
     * @code
     * idfxx::fatfs::image assets("assets", "/assets", {.max_files = 2});
     * @endcode
     */
    struct flash_config {
        /// Maximum number of files open at once on this image. Must be at least 1. Memory for this
        /// many open files is allocated when the image is mounted, including a sector buffer for
        /// each, unless `CONFIG_FATFS_USE_DYN_BUFFERS` is enabled (the default from ESP-IDF 6.0):
        /// each open file's buffer is then allocated when it is opened, and freed when it is
        /// closed.
        size_t max_files = 5;
    };

    // =========================================================================
    // On an SD card
    // =========================================================================

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Mounts the FAT filesystem on an SD card read-only, with the default configuration.
     *
     * Does not take ownership of @p card: the caller must ensure that the card
     * outlives the image. The card object may be moved while the image is
     * mounted.
     *
     * @param card       An initialized card.
     * @param mount_path Where to mount the filesystem, for example "/sdcard".
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure, with the error codes listed for
     *         make(sdcard::card&, std::string_view, const card_config&).
     *
     * @code
     * idfxx::fatfs::image img(card, "/sdcard");
     * @endcode
     */
    [[nodiscard]] explicit image(sdcard::card& card, std::string_view mount_path);

    /**
     * @brief Mounts the FAT filesystem on an SD card read-only.
     *
     * Does not take ownership of @p card: the caller must ensure that the card
     * outlives the image. The card object may be moved while the image is
     * mounted.
     *
     * @param card       An initialized card.
     * @param mount_path Where to mount the filesystem, for example "/sdcard".
     * @param cfg        Image configuration.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure, with the error codes listed for
     *         make(sdcard::card&, std::string_view, const card_config&).
     *
     * @code
     * idfxx::fatfs::image img(card, "/sdcard", {.disk_status_check = true});
     * @endcode
     */
    [[nodiscard]] explicit image(
        sdcard::card& card,
        std::string_view mount_path,
        const card_config& cfg
    );
#endif

    /**
     * @brief Mounts the FAT filesystem on an SD card read-only, with the default configuration.
     *
     * Does not take ownership of @p card: the caller must ensure that the card
     * outlives the image. The card object may be moved while the image is
     * mounted.
     *
     * @param card       An initialized card.
     * @param mount_path Where to mount the filesystem, for example "/sdcard".
     *
     * @return The mounted image, or an error; see
     *         make(sdcard::card&, std::string_view, const card_config&).
     */
    [[nodiscard]] static result<image> make(sdcard::card& card, std::string_view mount_path);

    /**
     * @brief Mounts the FAT filesystem on an SD card read-only.
     *
     * Mounts the filesystem in the card's first FAT partition, as volume
     * does, but never writes to the card or formats it. A write-protected
     * card (see sdcard::card::is_write_protected()) mounts like any other.
     *
     * Does not take ownership of @p card: the caller must ensure that the card
     * outlives the image. The card object may be moved while the image is
     * mounted.
     *
     * @param card       An initialized card.
     * @param mount_path Where to mount the filesystem, for example "/sdcard".
     * @param cfg        Image configuration.
     *
     * @return The mounted image, or an error.
     * @retval idfxx::errc::invalid_arg @p mount_path does not start with '/', ends with '/' or is
     *         longer than 13 characters, or `cfg.max_files` is 0.
     * @retval idfxx::errc::invalid_state A volume or image is already mounted on @p card, another
     *         volume or image is mounted at @p mount_path, all of FatFs's drives are in use
     *         (`CONFIG_FATFS_VOLUME_COUNT`, 2 by default), or the virtual filesystem's table is
     *         full (`CONFIG_VFS_MAX_COUNT`, 8 by default).
     * @retval fatfs::errc::no_filesystem The card holds no recognizable FAT filesystem (an exFAT
     *         filesystem or a damaged boot sector counts as none).
     * @retval fatfs::errc::not_ready The card did not respond (for example, it was removed).
     * @retval fatfs::errc::disk_err A read on the card failed.
     */
    [[nodiscard]] static result<image>
    make(sdcard::card& card, std::string_view mount_path, const card_config& cfg);

    // =========================================================================
    // On a flash partition
    // =========================================================================

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Mounts a FAT image on a flash partition, with the default configuration.
     *
     * @param part       A data partition of subtype fat, holding a FAT image.
     * @param mount_path Where to mount the image, for example "/assets".
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure, with the error codes listed for
     *         make(const partition&, std::string_view, const flash_config&).
     *
     * @code
     * idfxx::fatfs::image assets(idfxx::partition::find("assets"), "/assets");
     * @endcode
     */
    [[nodiscard]] explicit image(const partition& part, std::string_view mount_path);

    /**
     * @brief Mounts a FAT image on a flash partition.
     *
     * @param part       A data partition of subtype fat, holding a FAT image.
     * @param mount_path Where to mount the image, for example "/assets".
     * @param cfg        Image configuration.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure, with the error codes listed for
     *         make(const partition&, std::string_view, const flash_config&).
     *
     * @code
     * idfxx::fatfs::image assets(idfxx::partition::find("assets"), "/assets", {.max_files = 2});
     * @endcode
     */
    [[nodiscard]] explicit image(
        const partition& part,
        std::string_view mount_path,
        const flash_config& cfg
    );

    /**
     * @brief Mounts the FAT image on the flash partition with a given label, with the default
     *        configuration.
     *
     * @param partition_label The label of a data partition of subtype fat, holding a FAT image.
     * @param mount_path      Where to mount the image, for example "/assets".
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure, with the error codes listed for
     *         make(std::string_view, std::string_view, const flash_config&).
     *
     * @code
     * idfxx::fatfs::image assets("assets", "/assets");
     * @endcode
     */
    [[nodiscard]] explicit image(std::string_view partition_label, std::string_view mount_path);

    /**
     * @brief Mounts the FAT image on the flash partition with a given label.
     *
     * @param partition_label The label of a data partition of subtype fat, holding a FAT image.
     * @param mount_path      Where to mount the image, for example "/assets".
     * @param cfg             Image configuration.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure, with the error codes listed for
     *         make(std::string_view, std::string_view, const flash_config&).
     *
     * @code
     * idfxx::fatfs::image assets("assets", "/assets", {.max_files = 2});
     * @endcode
     */
    [[nodiscard]] explicit image(
        std::string_view partition_label,
        std::string_view mount_path,
        const flash_config& cfg
    );
#endif

    /**
     * @brief Mounts a FAT image on a flash partition, with the default configuration.
     *
     * @param part       A data partition of subtype fat, holding a FAT image.
     * @param mount_path Where to mount the image, for example "/assets".
     *
     * @return The mounted image, or an error; see
     *         make(const partition&, std::string_view, const flash_config&).
     */
    [[nodiscard]] static result<image> make(const partition& part, std::string_view mount_path);

    /**
     * @brief Mounts a FAT image on a flash partition.
     *
     * The image's sector size must be one FatFs is configured for: between
     * 512 bytes and `CONFIG_WL_SECTOR_SIZE` (4096 by default).
     * `fatfs_create_rawflash_image()` uses `CONFIG_FATFS_SECTOR_SIZE` (4096 by
     * default).
     *
     * @param part       A data partition of subtype fat, holding a FAT image.
     * @param mount_path Where to mount the image, for example "/assets".
     * @param cfg        Image configuration.
     *
     * @return The mounted image, or an error.
     * @retval idfxx::errc::invalid_arg @p mount_path does not start with '/', ends with '/' or is
     *         longer than 13 characters, `cfg.max_files` is 0, or the partition is not a data
     *         partition of subtype fat.
     * @retval idfxx::errc::invalid_state A volume or image is already mounted on the partition,
     *         another volume or image is mounted at @p mount_path, all of FatFs's drives are in use
     *         (`CONFIG_FATFS_VOLUME_COUNT`, 2 by default), or the virtual filesystem's table is
     *         full (`CONFIG_VFS_MAX_COUNT`, 8 by default).
     * @retval fatfs::errc::no_filesystem The partition holds no FAT image.
     * @retval fatfs::errc::disk_err A read on the partition failed, or the partition does not start
     *         with a sector size FatFs supports: an erased partition, or an image with an
     *         unsupported sector size.
     *
     * @warning A partition holding a wear-leveled filesystem (one used by volume) is not reliably
     *          detected. On a freshly formatted one, the mount fails with fatfs::errc::disk_err;
     *          once wear leveling has moved its dummy sector, the mount can succeed and read
     *          corrupt data.
     */
    [[nodiscard]] static result<image>
    make(const partition& part, std::string_view mount_path, const flash_config& cfg);

    /**
     * @brief Mounts the FAT image on the flash partition with a given label, with the default
     *        configuration.
     *
     * @param partition_label The label of a data partition of subtype fat, holding a FAT image.
     * @param mount_path      Where to mount the image, for example "/assets".
     *
     * @return The mounted image, or an error; see
     *         make(std::string_view, std::string_view, const flash_config&).
     */
    [[nodiscard]] static result<image>
    make(std::string_view partition_label, std::string_view mount_path);

    /**
     * @brief Mounts the FAT image on the flash partition with a given label.
     *
     * Equivalent to make(const partition&, std::string_view, const flash_config&) on
     * the partition with that label. The other arguments are checked before
     * the partition is looked up, so invalid ones (including swapping the
     * label and the mount path) fail with idfxx::errc::invalid_arg whether or
     * not a partition with that label exists.
     *
     * @param partition_label The label of a data partition of subtype fat, holding a FAT image.
     * @param mount_path      Where to mount the image, for example "/assets".
     * @param cfg             Image configuration.
     *
     * @return The mounted image, or an error.
     * @retval idfxx::errc::invalid_arg @p mount_path does not start with '/', ends with '/' or is
     *         longer than 13 characters, `cfg.max_files` is 0, or the partition is not a data
     *         partition of subtype fat.
     * @retval idfxx::errc::not_found There is no partition with that label.
     * @retval idfxx::errc::invalid_state A volume or image is already mounted on the partition,
     *         another volume or image is mounted at @p mount_path, all of FatFs's drives are in use
     *         (`CONFIG_FATFS_VOLUME_COUNT`, 2 by default), or the virtual filesystem's table is
     *         full (`CONFIG_VFS_MAX_COUNT`, 8 by default).
     * @retval fatfs::errc::no_filesystem The partition holds no FAT image.
     * @retval fatfs::errc::disk_err A read on the partition failed, or the partition does not start
     *         with a sector size FatFs supports: an erased partition, or an image with an
     *         unsupported sector size.
     */
    [[nodiscard]] static result<image>
    make(std::string_view partition_label, std::string_view mount_path, const flash_config& cfg);

    /**
     * @brief Unmounts the image, if it is still mounted.
     */
    ~image();

    image(const image&) = delete;
    image& operator=(const image&) = delete;

    /** @brief Move constructor. Transfers the mount; the mount path and open files stay valid. */
    image(image&& other) noexcept;

    /**
     * @brief Move assignment. Unmounts this image if it is still mounted, then takes over
     *        @p other's mount.
     */
    image& operator=(image&& other) noexcept;

    // =========================================================================
    // Accessors
    // =========================================================================

    /**
     * @brief Returns the path the image is mounted at.
     *
     * @return The mount path, for example "/assets".
     */
    [[nodiscard]] std::string_view mount_path() const noexcept;

    /**
     * @brief Returns the image's FatFs logical drive.
     *
     * For direct calls to FatFs `f_*` functions, whose paths start with the
     * drive (for example "1:/index.html").
     *
     * @return The drive, for example "1:".
     */
    [[nodiscard]] std::string_view drive() const noexcept;

    /**
     * @brief Returns the image's FatFs filesystem object.
     *
     * For direct calls to FatFs functions that take a `FATFS*`. Valid until
     * the image is unmounted.
     *
     * @return The filesystem object.
     */
    [[nodiscard]] FATFS* idf_handle() const noexcept;

    /**
     * @brief Returns the image's capacity for data.
     *
     * The size of the data area: the total size of all clusters, used and
     * free. Determined when the image is mounted.
     *
     * @return The capacity in bytes.
     */
    [[nodiscard]] uint64_t size() const noexcept;

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Returns the unused space in the image.
     *
     * The first call after mounting scans the FAT, which can take a while on a
     * large card.
     *
     * @return The unused space in bytes.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure, with the error codes listed for try_free_size().
     */
    [[nodiscard]] uint64_t free_size() const;
#endif

    /**
     * @brief Returns the unused space in the image.
     *
     * The first call after mounting scans the FAT, which can take a while on a
     * large card.
     *
     * @return The unused space in bytes, or an error.
     * @retval fatfs::errc::disk_err A read on the medium failed.
     * @retval fatfs::errc::not_ready The card did not respond (for example, it was removed);
     *         reported this way only with `card_config::disk_status_check`.
     */
    [[nodiscard]] result<uint64_t> try_free_size() const;

    // =========================================================================
    // Operations
    // =========================================================================

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Unmounts the image.
     *
     * The destructor unmounts the image otherwise. The image is unmounted even
     * when an error is reported. All files on the image must be closed first.
     *
     * Afterward the object may only be destroyed or assigned to.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure, with the error codes listed for try_unmount().
     */
    void unmount();
#endif

    /**
     * @brief Unmounts the image.
     *
     * The destructor unmounts the image otherwise. The image is unmounted even
     * when an error is reported. All files on the image must be closed first.
     *
     * Afterward the object may only be destroyed or assigned to.
     *
     * @return Success, or the first error encountered.
     */
    result<void> try_unmount();

private:
    /// @cond INTERNAL
    struct state;
    explicit image(std::unique_ptr<state> s) noexcept;
    /// @endcond

    std::unique_ptr<state> _state;
};

} // namespace idfxx::fatfs

/** @} */ // end of idfxx_fatfs

/// @cond INTERNAL
namespace std {
template<>
struct is_error_code_enum<idfxx::fatfs::errc> : true_type {};
} // namespace std
/// @endcond
