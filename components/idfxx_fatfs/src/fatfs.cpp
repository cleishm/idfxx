// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#include <idfxx/fatfs>

#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <cerrno>
#include <cstdio>
#include <diskio.h>
#include <diskio_impl.h>
#include <diskio_rawflash.h>
#include <diskio_sdmmc.h>
#include <diskio_wl.h>
#include <esp_log.h>
#include <esp_vfs.h>
#include <esp_vfs_fat.h>
#include <memory>
#include <mutex>
#include <optional>
#include <sdmmc_cmd.h>
#include <span>
#include <string>
#include <utility>
#include <vector>
#include <wear_levelling.h>

namespace {
const char* TAG = "idfxx::fatfs";
}

namespace idfxx::fatfs {

// =============================================================================
// Error category
// =============================================================================

const error_category& fatfs_category() noexcept {
    static const error_category instance{};
    return instance;
}

const char* error_category::name() const noexcept {
    return "fatfs::Error";
}

std::string error_category::message(int ec) const {
    switch (errc(ec)) {
    case errc::disk_err:
        return "Low-level disk I/O error";
    case errc::int_err:
        return "Filesystem structure is inconsistent";
    case errc::not_ready:
        return "Medium is not ready";
    case errc::no_file:
        return "File not found";
    case errc::no_path:
        return "Path not found";
    case errc::invalid_name:
        return "Invalid path name";
    case errc::denied:
        return "Access denied, or directory or volume full";
    case errc::exist:
        return "Object already exists";
    case errc::invalid_object:
        return "Invalid file or directory object";
    case errc::write_protected:
        return "Medium is write-protected";
    case errc::invalid_drive:
        return "Invalid logical drive";
    case errc::not_enabled:
        return "Volume is not mounted";
    case errc::no_filesystem:
        return "No FAT filesystem";
    case errc::mkfs_aborted:
        return "Formatting aborted: medium unsuitable for the parameters";
    case errc::timeout:
        return "Timed out waiting for access to the volume";
    case errc::locked:
        return "File is locked";
    case errc::not_enough_core:
        return "Working buffer could not be allocated";
    case errc::too_many_open_files:
        return "Too many open files";
    case errc::invalid_parameter:
        return "Invalid parameter";
    default:
        return "Unknown FAT filesystem error";
    }
}

std::error_condition error_category::default_error_condition(int code) const noexcept {
    switch (errc(code)) {
    case errc::disk_err:
        return std::make_error_condition(std::errc::io_error);
    case errc::not_ready:
        return std::make_error_condition(std::errc::no_such_device);
    case errc::no_file:
    case errc::no_path:
        return std::make_error_condition(std::errc::no_such_file_or_directory);
    case errc::invalid_name:
    case errc::invalid_parameter:
        return std::make_error_condition(std::errc::invalid_argument);
    case errc::denied:
        return std::make_error_condition(std::errc::permission_denied);
    case errc::exist:
        return std::make_error_condition(std::errc::file_exists);
    case errc::invalid_object:
        return std::make_error_condition(std::errc::bad_file_descriptor);
    case errc::write_protected:
        return std::make_error_condition(std::errc::read_only_file_system);
    case errc::timeout:
        return std::make_error_condition(std::errc::timed_out);
    case errc::locked:
        return std::make_error_condition(std::errc::device_or_resource_busy);
    case errc::not_enough_core:
        return std::make_error_condition(std::errc::not_enough_memory);
    case errc::too_many_open_files:
        return std::make_error_condition(std::errc::too_many_files_open);
    default:
        return std::error_condition(code, *this);
    }
}

std::unexpected<std::error_code> fatfs_error(FRESULT fr) {
    assert(fr != FR_OK);
    if (fr == FR_NOT_ENOUGH_CORE) {
        raise_no_mem();
    }
    // Every errc value is its FRESULT.
    return error(static_cast<errc>(fr));
}

// =============================================================================
// Shared mount machinery
// =============================================================================

namespace {

// The VFS accepts prefixes of up to ESP_VFS_PATH_MAX characters, but the FAT VFS keeps its own copy
// in a char[ESP_VFS_PATH_MAX] filled with strlcpy(..., sizeof - 1). A longer path is truncated
// there, and can then never be unregistered (the lookup compares the truncated copy with the full
// path).
constexpr size_t max_mount_path_length = ESP_VFS_PATH_MAX - 2;

// f_mkfs and f_fdisk need a work buffer of at least one sector.
constexpr size_t work_buffer_size = 4096;
static_assert(work_buffer_size >= FF_MAX_SS);

// The largest allocation unit, in sectors, that FAT allows.
constexpr size_t max_sectors_per_cluster = 128;

// With fewer sectors than this, a flash filesystem gets a smaller root directory (as in ESP-IDF's
// esp_vfs_fat_spiflash_mount_rw_wl).
constexpr size_t small_flash_volume_sectors = 128;

// Serializes mounting and unmounting between all volumes and images: drive allocation, FatFs's
// drive table and the FAT VFS registrations are global and unlocked, and so is the mount ID that
// f_mount() increments (which a FatFs call that remounts a volume on its own also increments,
// outside this lock). Also guards `claimed`, and the writes to `card_image_drives` and
// `flash_probe_handles`.
//
// Lock order: a volume's own mutex (volume::state::mutex) first, then mount_mutex. close() takes
// mount_mutex itself, so it is never called with mount_mutex held: unmounting a volume (its
// destructor, move assignment and try_unmount()) calls it with the volume's own mutex held, and
// format() takes mount_mutex only around its f_mount() calls. make() takes only mount_mutex (the
// state it creates isn't shared yet), and releases it before a failed mount's state is destroyed,
// which closes it. A volume's make() also releases it while it formats a medium with no filesystem,
// as format() does, and takes it again before mounting the new one. Images have no mutex of their
// own.
std::mutex mount_mutex;

// What a mount uses: a flash region (its chip and start address) or an SD card. Mounting the same
// one twice would put two FatFs instances (and, on flash, two wear-leveling instances) on the same
// medium.
struct medium {
    const void* object; // esp_flash_t* for a partition, sdmmc_card_t* for a card
    uint32_t address;   // partition start; 0 for a card
    bool operator==(const medium&) const = default;
};

medium medium_of(const partition& part) {
    return {part.idf_handle()->flash_chip, part.address()};
}

medium medium_of(const sdmmc_card_t* card) {
    return {card, 0};
}

// The media mounted by volumes and images, with mount_mutex held.
std::vector<medium> claimed;

bool valid_mount_path(std::string_view path) {
    // The VFS also rejects "/" alone and a trailing '/'.
    return path.size() >= 2 && path.size() <= max_mount_path_length && path.front() == '/' &&
        path.back() != '/' && path.find('\0') == std::string_view::npos;
}

bool is_fat_partition(const partition& part) {
    return part.type() == partition::type::data && part.subtype() == partition::subtype::data_fat;
}

// Whether `opts` can format a medium with `sector_size`-byte sectors: an explicit allocation unit
// must be a power of two from one to 128 sectors, which f_mkfs would otherwise quietly replace with
// one of its own choosing. Checked before anything is mounted or unmounted.
bool valid_format_options(size_t sector_size, const format_options& opts) {
    if (!opts.allocation_unit_size) {
        return true;
    }
    const size_t au = *opts.allocation_unit_size;
    if (au < sector_size || au > max_sectors_per_cluster * sector_size || (au & (au - 1)) != 0) {
        ESP_LOGE(TAG, "Invalid allocation unit size %zu for %zu-byte sectors", au, sector_size);
        return false;
    }
    return true;
}

// The au_size for f_mkfs, from options already checked with valid_format_options(): 0 lets it
// choose from the volume size.
DWORD allocation_unit(const format_options& opts) {
    return static_cast<DWORD>(opts.allocation_unit_size.value_or(0));
}

BYTE fat_count(const format_options& opts) {
    return opts.use_one_fat ? 1 : 2;
}

uint64_t sector_size_of(const FATFS& fs) {
#if FF_MAX_SS != FF_MIN_SS
    return fs.ssize;
#else
    (void)fs;
    return FF_MIN_SS;
#endif
}

result<void> check(FRESULT fr, const char* op) {
    if (fr != FR_OK) {
        ESP_LOGE(TAG, "%s failed (%d)", op, static_cast<int>(fr));
        return fatfs_error(fr);
    }
    return {};
}

} // namespace

namespace detail {

// The medium's claim, a FatFs drive, its VFS registration and its FatFs mount: the parts shared by
// volumes and images. Each part is recorded as it is set up, so that closing undoes exactly what
// was done. All members are used with mount_mutex held, except by the read-only queries, by a
// volume's operations (with the volume's own mutex held, see volume::state::lock_open()), by its
// format() (with the volume's own mutex held, and mount_mutex too around mount_fs() and
// unmount_fs()) and by its make() while it formats (before the state is shared).
struct mount_base {
    BYTE pdrv = FF_DRV_NOT_USED;
    std::array<char, 3> drive{};
    std::string base_path;
    // Set while the VFS registration is.
    FATFS* fs = nullptr;
    bool diskio_registered = false;
    bool fs_mounted = false;
    // Set by each successful mount_fs(); read without waiting for a format().
    std::atomic<uint64_t> size = 0;
    // Set first by every mount, and reset only by close(): the mount is open while it is set.
    std::optional<medium> claim;

    // Fails once close() has run, so that an operation on an unmounted volume or image never
    // reaches FatFs (whose drive may by then belong to another mount).
    [[nodiscard]] result<void> check_open() const {
        if (!claim) {
            ESP_LOGE(TAG, "%s is not mounted", base_path.c_str());
            return error(idfxx::errc::invalid_state);
        }
        return {};
    }

    // Records that this mount uses `m`, which must not already be mounted. Done before any ESP-IDF
    // call.
    result<void> claim_medium(medium m) {
        if (std::find(claimed.begin(), claimed.end(), m) != claimed.end()) {
            ESP_LOGE(TAG, "Already mounted");
            return error(idfxx::errc::invalid_state);
        }
        claimed.push_back(m);
        claim = m;
        return {};
    }

    void release_medium() {
        if (claim) {
            std::erase(claimed, *claim);
            claim.reset();
        }
    }

    // Reserves a free FatFs drive for this mount. The drive is taken when a disk driver is
    // registered on it, which the caller does next (with mount_mutex still held).
    result<void> reserve_drive(std::string_view mount_path) {
        BYTE d = FF_DRV_NOT_USED;
        if (ff_diskio_get_drive(&d) != ESP_OK || d == FF_DRV_NOT_USED) {
            ESP_LOGE(
                TAG, "All %d FatFs volumes are in use (CONFIG_FATFS_VOLUME_COUNT)", FF_VOLUMES
            );
            return error(idfxx::errc::invalid_state);
        }
        pdrv = d;
        drive = {static_cast<char>('0' + d), ':', '\0'};
        base_path = mount_path;
        return {};
    }

    // The start of every mount: claims `m`, then reserves a drive for the mount at `mount_path`.
    result<void> begin_mount(medium m, std::string_view mount_path) {
        if (auto r = claim_medium(m); !r) {
            return r;
        }
        return reserve_drive(mount_path);
    }

    result<void> register_vfs(size_t max_files) {
        esp_vfs_fat_conf_t conf{};
        conf.base_path = base_path.c_str();
        conf.fat_drive = drive.data();
        conf.max_files = max_files;
        FATFS* out = nullptr;
        esp_err_t err = esp_vfs_fat_register_cfg(&conf, &out);
        if (err == ESP_ERR_INVALID_STATE) {
            // Another FAT filesystem is registered at this path; `out` is its FATFS, not ours.
            ESP_LOGE(TAG, "A FAT filesystem is already mounted at %s", base_path.c_str());
            return error(idfxx::errc::invalid_state);
        }
        if (err == ESP_ERR_NO_MEM) {
            // Also returned when the VFS table (CONFIG_VFS_MAX_COUNT entries) is full, which is no
            // reason to treat it as heap exhaustion and abort; a genuine allocation failure here is
            // reported the same way.
            ESP_LOGE(
                TAG,
                "Failed to register %s: VFS table full (CONFIG_VFS_MAX_COUNT)",
                base_path.c_str()
            );
            return error(idfxx::errc::invalid_state);
        }
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to register %s: %s", base_path.c_str(), esp_err_to_name(err));
            return error(err);
        }
        fs = out;
        return {};
    }

    // Mounts the filesystem in FatFs. Even when the mount fails, FatFs keeps the FATFS registered
    // for the drive, so unmount_fs() is needed either way. Callers pass the result through check(),
    // which raises for FR_NOT_ENOUGH_CORE (a failure to allocate the sector buffer, with
    // CONFIG_FATFS_USE_DYN_BUFFERS).
    FRESULT mount_fs() {
        fs_mounted = true;
        FRESULT fr = f_mount(fs, drive.data(), 1);
        if (fr == FR_INT_ERR) {
            // f_mount reports a failure to create the volume's mutex (an allocation) as FR_INT_ERR,
            // before it registers the FATFS; mounting the volume itself never returns that.
            unmount_fs();
            raise_no_mem();
        }
        if (fr == FR_OK) {
            size = static_cast<uint64_t>(fs->n_fatent - 2) * fs->csize * sector_size_of(*fs);
        }
        return fr;
    }

    void unmount_fs() {
        if (fs_mounted) {
            f_mount(nullptr, drive.data(), 0);
            fs_mounted = false;
        }
    }

    void unregister_diskio() {
        if (diskio_registered) {
            ff_diskio_unregister(pdrv);
            diskio_registered = false;
        }
    }

    esp_err_t unregister_vfs() {
        if (fs == nullptr) {
            return ESP_OK;
        }
        fs = nullptr;
        esp_err_t err = esp_vfs_fat_unregister_path(base_path.c_str());
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to unregister %s: %s", base_path.c_str(), esp_err_to_name(err));
        }
        return err;
    }

    // The free space, for an image (which has no lock of its own): fails once close() has run.
    [[nodiscard]] result<uint64_t> free_size() const {
        if (auto r = check_open(); !r) {
            return error(r.error());
        }
        return open_free_size();
    }

    // The free space, once check_open() has passed.
    [[nodiscard]] result<uint64_t> open_free_size() const {
        DWORD free_clusters = 0;
        FATFS* f = nullptr;
        if (auto r = check(f_getfree(drive.data(), &free_clusters, &f), "f_getfree"); !r) {
            return error(r.error());
        }
        return static_cast<uint64_t>(free_clusters) * f->csize * sector_size_of(*f);
    }
};

} // namespace detail

// =============================================================================
// Read-only disk drivers
// =============================================================================

namespace {

// A read-only disk driver for an image on an SD card: the card's sectors, read as ESP-IDF's SD card
// driver reads them, with the drive reported as write-protected and writes refused.

// What the driver, and the probe on an SD card, use on each FatFs drive. An entry is written with
// mount_mutex held, before the driver is registered on its drive, and read by the driver. The card
// is read through its sdmmc_card_t, which stays in place when the sdcard::card object is moved.
struct card_image_drive {
    sdmmc_card_t* card = nullptr;
    size_t sector_count = 0; // As sdcard::card reports it.
    size_t sector_size = 0;  // As sdcard::card reports it.
    bool disk_status_check = false;
};

card_image_drive card_image_drive_for(const sdcard::card& card, bool disk_status_check) {
    return {
        .card = card.idf_handle(),
        .sector_count = card.sector_count(),
        .sector_size = card.sector_size(),
        .disk_status_check = disk_status_check,
    };
}

std::array<card_image_drive, FF_VOLUMES> card_image_drives;

// STA_PROTECT, with STA_NOINIT when the card doesn't respond. Also the drivers' initialization,
// which checks the card whether or not disk_status_check is set, so that a mount on a card that
// doesn't respond fails with FR_NOT_READY.
DSTATUS card_image_availability(BYTE pdrv) {
    sdmmc_card_t* card = card_image_drives[pdrv].card;
    assert(card);
    if (esp_err_t err = sdmmc_get_status(card); err != ESP_OK) {
        ESP_LOGE(TAG, "Card status check failed: %s", esp_err_to_name(err));
        return STA_PROTECT | STA_NOINIT;
    }
    return STA_PROTECT;
}

DSTATUS card_image_status(BYTE pdrv) {
    if (card_image_drives[pdrv].disk_status_check) {
        return card_image_availability(pdrv);
    }
    return STA_PROTECT;
}

DRESULT card_image_read(BYTE pdrv, BYTE* buff, uint32_t sector, UINT count) {
    sdmmc_card_t* card = card_image_drives[pdrv].card;
    assert(card);
    if (esp_err_t err = sdmmc_read_sectors(card, buff, sector, count); err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read the card: %s", esp_err_to_name(err));
        return RES_ERROR;
    }
    return RES_OK;
}

DRESULT refuse_write(BYTE, const BYTE*, uint32_t, UINT) {
    return RES_WRPRT;
}

DRESULT card_image_ioctl(BYTE pdrv, BYTE cmd, void* buff) {
    const card_image_drive& d = card_image_drives[pdrv];
    assert(d.card);
    switch (cmd) {
    case CTRL_SYNC:
        return RES_OK;
    case GET_SECTOR_COUNT:
        *static_cast<LBA_t*>(buff) = static_cast<LBA_t>(d.sector_count);
        return RES_OK;
    case GET_SECTOR_SIZE:
        *static_cast<WORD*>(buff) = static_cast<WORD>(d.sector_size);
        return RES_OK;
    case GET_BLOCK_SIZE:
        // As ESP-IDF's SD card driver: the erase block size is unknown.
        return RES_ERROR;
    default:
        return RES_PARERR;
    }
}

constexpr ff_diskio_impl_t card_image_impl{
    .init = card_image_availability,
    .status = card_image_status,
    .read = card_image_read,
    .write = refuse_write,
    .ioctl = card_image_ioctl,
};

// Probe drivers, which a volume's mount uses in place of ESP-IDF's driver to mount again a medium
// on which FatFs found no filesystem (see volume::state::probe()). They read the medium as
// ESP-IDF's driver does, recording whether a read failed, and refuse writes.

// Whether a read that a probe made on each FatFs drive failed. Cleared with mount_mutex held,
// before a probe is registered on the drive, and read with mount_mutex held once it has been
// replaced. Atomic: a FatFs call through the volume's mount path, from another task, can also read
// through the probe.
std::array<std::atomic<bool>, FF_VOLUMES> probe_read_failed;

// Records a failed read of `count` sectors from `sector`, on a medium of `sector_count` sectors,
// unless the read reaches beyond its end. FatFs reads the first sector of each partition that a
// partition table lists, and a stale or foreign table can list one that starts beyond the end of
// the medium; reading it is no sign of a failing medium. Such reads are still passed to the medium,
// as ESP-IDF's driver passes them, so that FatFs mounts through the probe with the result it had
// through that driver: they fail on an SD card, but wrap around on flash (wl_read() doesn't check
// bounds). Failing them on flash would change the result: FatFs in ESP-IDF 6.x reports FR_DISK_ERR,
// not FR_NO_FILESYSTEM, when the last boot sector it tried couldn't be read, even if it then found
// an empty entry.
void record_probe_failure(BYTE pdrv, LBA_t sector, UINT count, LBA_t sector_count) {
    if (sector < sector_count && count <= sector_count - sector) {
        probe_read_failed[pdrv] = true;
    }
}

DRESULT card_probe_read(BYTE pdrv, BYTE* buff, uint32_t sector, UINT count) {
    const DRESULT res = card_image_read(pdrv, buff, sector, count);
    if (res != RES_OK) {
        record_probe_failure(pdrv, sector, count, card_image_drives[pdrv].sector_count);
    }
    return res;
}

// The probe on an SD card: the image driver, with its failed reads recorded.
constexpr ff_diskio_impl_t card_probe_impl{
    .init = card_image_availability,
    .status = card_image_status,
    .read = card_probe_read,
    .write = refuse_write,
    .ioctl = card_image_ioctl,
};

// The wear-leveling handle that the probe on a flash partition reads on each FatFs drive. An entry
// is written with mount_mutex held, before the probe is registered on its drive.
std::array<wl_handle_t, FF_VOLUMES> flash_probe_handles;

DSTATUS flash_probe_status(BYTE) {
    return STA_PROTECT;
}

DRESULT flash_probe_read(BYTE pdrv, BYTE* buff, uint32_t sector, UINT count) {
    const wl_handle_t wl = flash_probe_handles[pdrv];
    const size_t sector_size = wl_sector_size(wl);
    // Read even when it reaches beyond the end, as ESP-IDF's driver does (see
    // record_probe_failure()).
    if (esp_err_t err = wl_read(wl, sector * sector_size, buff, count * sector_size);
        err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read the partition: %s", esp_err_to_name(err));
        record_probe_failure(pdrv, sector, count, wl_size(wl) / sector_size);
        return RES_ERROR;
    }
    return RES_OK;
}

DRESULT flash_probe_ioctl(BYTE pdrv, BYTE cmd, void* buff) {
    const wl_handle_t wl = flash_probe_handles[pdrv];
    switch (cmd) {
    case CTRL_SYNC:
        return RES_OK;
    case GET_SECTOR_COUNT:
        *static_cast<LBA_t*>(buff) = wl_size(wl) / wl_sector_size(wl);
        return RES_OK;
    case GET_SECTOR_SIZE:
        *static_cast<WORD*>(buff) = static_cast<WORD>(wl_sector_size(wl));
        return RES_OK;
    case GET_BLOCK_SIZE:
        // As ESP-IDF's wear-leveling driver.
        return RES_ERROR;
    default:
        return RES_PARERR;
    }
}

// The probe on a wear-leveled flash partition.
constexpr ff_diskio_impl_t flash_probe_impl{
    .init = flash_probe_status,
    .status = flash_probe_status,
    .read = flash_probe_read,
    .write = refuse_write,
    .ioctl = flash_probe_ioctl,
};

} // namespace

// =============================================================================
// volume
// =============================================================================

namespace {

// The buffer that replacing a file from a byte source reads through (as idfxx_jpeg reads its
// sources).
constexpr size_t replace_buffer_size = 4096;

constexpr char ascii_upper(char c) {
    return c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c;
}

// The base of the two names that replacing a file called `name` uses: the FNV-1a 32-bit hash of
// `name`, with ASCII letters folded to uppercase first (FAT names are case-insensitive), as 8
// uppercase hex digits.
std::string replace_name_base(std::string_view name) {
    uint32_t hash = 2166136261u;
    for (char c : name) {
        hash = (hash ^ static_cast<uint8_t>(ascii_upper(c))) * 16777619u;
    }
    std::string hex(8, '0');
    for (size_t i = hex.size(); i-- > 0; hash >>= 4) {
        hex[i] = "0123456789ABCDEF"[hash & 0xF];
    }
    return hex;
}

// The error for a failed stdio call: its errno, or EIO if it set none.
std::unexpected<std::error_code> stdio_error() {
    return error(std::error_code(errno != 0 ? errno : EIO, std::generic_category()));
}

result<void> write_all(FILE* file, std::span<const uint8_t> data) {
    errno = 0;
    if (std::fwrite(data.data(), 1, data.size(), file) != data.size()) {
        return stdio_error();
    }
    return {};
}

struct file_closer {
    void operator()(FILE* file) const { std::fclose(file); }
};

// Removes `path`, a FatFs path, if there is a file there.
result<void> remove_if_present(const std::string& path) {
    FRESULT fr = f_unlink(path.c_str());
    if (fr == FR_NO_FILE) {
        return {};
    }
    return check(fr, "f_unlink");
}

} // namespace

struct volume::state : detail::mount_base {
    // For a volume on an SD card (sd.card is null otherwise): the card, as the probe reads it, and
    // card_config::disk_status_check.
    card_image_drive sd{};
    std::optional<wl_handle_t> wl; // For a volume on a flash partition.

    // Serializes format() with try_free_size(), unmounting, and the FatFs calls that replacing and
    // recovering a file make (but not the writing of its contents, which goes through the VFS).
    // Taken before mount_mutex when both are held (see mount_mutex); FatFs's own lock on the volume
    // is taken inside it.
    std::mutex mutex;

    // Takes `mutex` for an operation on the volume, and returns it held, or fails with
    // idfxx::errc::invalid_state (see check_open()) once the volume is unmounted.
    [[nodiscard]] result<std::unique_lock<std::mutex>> lock_open() {
        std::unique_lock lock(mutex);
        if (auto r = check_open(); !r) {
            return error(r.error());
        }
        return lock;
    }

    // The FatFs paths that replacing a file uses: the file's own, and those of its two other names.
    struct replace_paths {
        std::string target;   // "0:/dir/config.json"
        std::string temp;     // "0:/dir/<HASH>.~RT", being written
        std::string complete; // "0:/dir/<HASH>.~RN", complete
    };

    // The paths for replacing the file at `path`, a VFS path, which must name a file under
    // base_path.
    result<replace_paths> paths_for_replace(std::string_view path) const {
        if (path.size() <= base_path.size() + 1 || !path.starts_with(base_path) ||
            path[base_path.size()] != '/' || path.back() == '/' ||
            path.find('\0') != std::string_view::npos) {
            ESP_LOGE(
                TAG,
                "%.*s is not a file path under %s",
                static_cast<int>(path.size()),
                path.data(),
                base_path.c_str()
            );
            return error(idfxx::errc::invalid_arg);
        }
        const std::string_view in_volume = path.substr(base_path.size()); // "/dir/config.json"
        replace_paths p;
        p.target.reserve(2 + in_volume.size());
        p.target.append(drive.data()).append(in_volume);
        const size_t name_start = p.target.rfind('/') + 1;
        p.temp = p.target.substr(0, name_start) +
            replace_name_base(std::string_view(p.target).substr(name_start)) + ".~RT";
        p.complete = p.temp;
        p.complete.back() = 'N';
        return p;
    }

    // Puts the complete file in place of the target, with `mutex` held: removes the target, if
    // there is one, then renames the complete file to it.
    result<void> install(const replace_paths& p) {
        if (auto r = remove_if_present(p.target); !r) {
            return r;
        }
        return check(f_rename(p.complete.c_str(), p.target.c_str()), "f_rename");
    }

    // Finishes or discards a replace that a reset interrupted, with `mutex` held: if the complete
    // file exists, it replaces the target; then any temporary file is removed. Fails with
    // fatfs::errc::no_path, without logging it, if the directory doesn't exist.
    result<void> recover(const replace_paths& p) {
        FRESULT fr = f_stat(p.complete.c_str(), nullptr);
        if (fr == FR_NO_PATH) {
            return fatfs_error(fr);
        }
        if (fr == FR_OK) {
            ESP_LOGW(
                TAG,
                "Finishing an interrupted replace of %s%s",
                base_path.c_str(),
                p.target.c_str() + 2
            );
            if (auto r = install(p); !r) {
                return r;
            }
        } else if (fr != FR_NO_FILE) {
            return check(fr, "f_stat");
        }
        fr = f_unlink(p.temp.c_str());
        if (fr == FR_OK) {
            ESP_LOGW(
                TAG,
                "Discarded an unfinished replace of %s%s",
                base_path.c_str(),
                p.target.c_str() + 2
            );
            return {};
        }
        return fr == FR_NO_FILE ? result<void>{} : check(fr, "f_unlink");
    }

    // Checks, with `mutex` held and before anything is written, that the target can be replaced:
    // that its name is valid, and that it is not a directory or read-only. Otherwise the new
    // contents would be left complete but never put in place, since removing the target would fail
    // every time.
    result<void> check_target(const replace_paths& p) {
        FILINFO info;
        FRESULT fr = f_stat(p.target.c_str(), &info);
        if (fr == FR_NO_FILE) {
            return {};
        }
        if (fr == FR_OK && (info.fattrib & (AM_DIR | AM_RDO)) != 0) {
            ESP_LOGE(
                TAG,
                "Can't replace %s%s: a directory or read-only file",
                base_path.c_str(),
                p.target.c_str() + 2
            );
            return fatfs_error(FR_DENIED);
        }
        return check(fr, "f_stat");
    }

    // Removes an abandoned replace's temporary file when destroyed, unless dismissed: on an error,
    // and when an exception unwinds. Never throws: a failure to remove it is only logged, and the
    // next recovery removes it.
    struct temp_remover {
        state& s;
        const replace_paths& paths;
        bool dismissed = false;

        ~temp_remover() {
            if (dismissed) {
                return;
            }
            auto lock = s.lock_open();
            if (!lock) {
                return;
            }
            // Not through check(), which raises for FR_NOT_ENOUGH_CORE.
            FRESULT fr = f_unlink(paths.temp.c_str());
            if (fr != FR_OK && fr != FR_NO_FILE && fr != FR_NO_PATH) {
                ESP_LOGW(
                    TAG,
                    "Failed to remove %s%s (%d)",
                    s.base_path.c_str(),
                    paths.temp.c_str() + 2,
                    static_cast<int>(fr)
                );
            }
        }
    };

    // Writes the temporary file through stdio, at its VFS path, with `write`, which is given it as
    // a FILE* (with `writer`), and closes it, with no lock held.
    result<void> write_temp(const replace_paths& p, void* writer, write_fn write) {
        // "/sdcard/dir/<HASH>.~RT"
        const std::string temp_file = std::string(base_path).append(p.temp, 2);
        errno = 0;
        FILE* f = std::fopen(temp_file.c_str(), "wb");
        if (f == nullptr) {
            return stdio_error();
        }
        // Destroyed in the reverse order: the file is closed before it is removed.
        temp_remover remover{*this, p};
        std::unique_ptr<FILE, file_closer> file(f);
        errno = 0;
        if (auto r = write(writer, file.get()); !r) {
            return r;
        }
        // A write that failed unreported.
        if (std::ferror(file.get())) {
            return stdio_error();
        }
        errno = 0;
        // Closing writes the file to the medium (f_close() syncs it) before it is renamed.
        if (std::fclose(file.release()) != 0) {
            return stdio_error();
        }
        remover.dismissed = true;
        return {};
    }

    // Replaces the target of `p` with the contents that `write` writes to the FILE* it is given,
    // with `writer` (see volume::try_replace_file()).
    result<void> replace(const replace_paths& p, void* writer, write_fn write) {
        {
            auto lock = lock_open();
            if (!lock) {
                return error(lock.error());
            }
            if (auto r = recover(p); !r) {
                return r;
            }
            if (auto r = check_target(p); !r) {
                return r;
            }
        }
        // No lock is held while the contents are written.
        if (auto r = write_temp(p, writer, write); !r) {
            return r;
        }
        auto lock = lock_open();
        if (!lock) {
            return error(lock.error());
        }
        if (auto r = check(f_rename(p.temp.c_str(), p.complete.c_str()), "f_rename"); !r) {
            return r;
        }
        return install(p);
    }

    // The size of the medium's sectors, which allocation units are measured in.
    [[nodiscard]] size_t sector_size() const {
        return sd.card ? sd.sector_size : CONFIG_WL_SECTOR_SIZE;
    }

    // Partitions the card with a single partition covering it, then creates a filesystem in it (as
    // ESP-IDF's esp_vfs_fat_sdcard_format_cfg()).
    result<void> make_card_filesystem(const format_options& opts, std::span<uint8_t> work) {
        ESP_LOGW(TAG, "Formatting the card mounted at %s", base_path.c_str());
        const LBA_t partitions[4] = {100, 0, 0, 0}; // percentages of the card
        if (auto r = check(f_fdisk(pdrv, partitions, work.data()), "f_fdisk"); !r) {
            return r;
        }
        const MKFS_PARM parm{
            .fmt = FM_ANY,
            .n_fat = fat_count(opts),
            .align = 0,
            .n_root = 0,
            .au_size = allocation_unit(opts),
        };
        return check(f_mkfs(drive.data(), &parm, work.data(), work.size()), "f_mkfs");
    }

    // Creates a filesystem without a partition table (as ESP-IDF's
    // esp_vfs_fat_spiflash_format_cfg_rw_wl()).
    result<void> make_flash_filesystem(const format_options& opts, std::span<uint8_t> work) {
        ESP_LOGW(TAG, "Formatting the partition mounted at %s", base_path.c_str());
        const size_t sectors = wl_size(*wl) / wl_sector_size(*wl);
        const UINT small_root_entries = CONFIG_WL_SECTOR_SIZE == 512 ? 16 : 128;
        const MKFS_PARM parm{
            .fmt = FM_ANY | FM_SFD,
            .n_fat = fat_count(opts),
            .align = 0,
            .n_root = sectors <= small_flash_volume_sectors ? small_root_entries : 0,
            .au_size = allocation_unit(opts),
        };
        return check(f_mkfs(drive.data(), &parm, work.data(), work.size()), "f_mkfs");
    }

    // Creates a filesystem with `opts`, already checked with valid_format_options(), using `work`,
    // a buffer of work_buffer_size bytes.
    result<void> make_filesystem(const format_options& opts, std::span<uint8_t> work) {
        return sd.card ? make_card_filesystem(opts, work) : make_flash_filesystem(opts, work);
    }

    // Registers ESP-IDF's disk driver for the medium on this mount's drive, in place of any driver
    // registered there.
    result<void> register_diskio() {
        if (sd.card) {
            ff_diskio_register_sdmmc(pdrv, sd.card);
            // After registering, which resets it.
            ff_sdmmc_set_disk_status_check(pdrv, sd.disk_status_check);
        } else if (auto err = ff_diskio_register_wl_partition(pdrv, *wl); err != ESP_OK) {
            return error(err);
        }
        diskio_registered = true;
        return {};
    }

    // After a mount that found no filesystem, with mount_mutex held, mounts the filesystem again
    // through a probe driver in place of ESP-IDF's: FatFs reads the medium as it did the first
    // time, and the probe records whether a read failed. Returns that mount's result, with
    // FR_DISK_ERR in place of FR_NO_FILESYSTEM if a read failed. The filesystem is left unmounted,
    // with ESP-IDF's driver registered again.
    //
    // FatFs reports a boot sector that can't be read as FR_DISK_ERR only when it is the last one it
    // reads: when reading a partition's boot sector fails, it moves on to the next entry in the
    // partition table, and reports FR_NO_FILESYSTEM if that one holds no FAT filesystem (or, in
    // ESP-IDF 5.5's FatFs, is empty). The probe keeps such a read error from being taken for a
    // missing filesystem.
    //
    // The drivers are swapped only while the filesystem is unmounted, when a FatFs call on the
    // drive (from another task, through the mount path) fails with FR_NOT_ENABLED rather than
    // reaching the driver being replaced. If mounting raises, the probe stays registered until
    // close() unregisters it.
    result<FRESULT> probe() {
        unmount_fs();
        probe_read_failed[pdrv] = false;
        if (sd.card) {
            card_image_drives[pdrv] = sd;
            ff_diskio_register(pdrv, &card_probe_impl);
        } else {
            flash_probe_handles[pdrv] = *wl;
            ff_diskio_register(pdrv, &flash_probe_impl);
        }
        const FRESULT fr = mount_fs();
        unmount_fs();
        if (auto r = register_diskio(); !r) {
            return error(r.error());
        }
        return fr == FR_NO_FILESYSTEM && probe_read_failed[pdrv] ? FR_DISK_ERR : fr;
    }

    // Mounts the filesystem, formatting it with `format` first if FatFs recognizes none and
    // `format_if_missing` is set. `format` has already been checked with valid_format_options().
    // `lock` holds mount_mutex, and is released while formatting, as in format(): the claim, the
    // disk driver on this mount's drive and the VFS registration keep the medium, the drive and the
    // path reserved meanwhile. It is held again on return, but not when an exception propagates
    // from formatting.
    result<void> mount_or_format(
        bool format_if_missing,
        const format_options& format,
        std::unique_lock<std::mutex>& lock
    ) {
        const FRESULT fr = mount_fs();
        // FR_NO_FILESYSTEM covers an erased medium, exFAT and a damaged boot sector. Damage to the
        // FAT or the directories isn't seen until files are used. A read error never leads to a
        // format: FatFs reports most as FR_DISK_ERR (or FR_NOT_READY), and probe() catches those it
        // reports as FR_NO_FILESYSTEM.
        if (fr != FR_NO_FILESYSTEM || !format_if_missing) {
            return check(fr, "f_mount");
        }
        ESP_LOGW(TAG, "No filesystem to mount at %s", base_path.c_str());
        auto probed = probe();
        if (!probed) {
            return error(probed.error());
        }
        if (*probed == FR_OK) {
            // A read that failed the first time succeeded the second.
            return check(mount_fs(), "f_mount");
        }
        if (*probed != FR_NO_FILESYSTEM) {
            ESP_LOGE(TAG, "Failed to read the medium for %s; not formatting it", base_path.c_str());
            return check(*probed, "f_mount");
        }
        lock.unlock();
        auto work = std::make_unique_for_overwrite<uint8_t[]>(work_buffer_size);
        auto made = make_filesystem(format, {work.get(), work_buffer_size});
        lock.lock();
        if (!made) {
            return made;
        }
        return check(mount_fs(), "f_mount");
    }

    result<void> mount_card(
        const card_image_drive& card,
        std::string_view mount_path,
        const card_config& cfg,
        std::unique_lock<std::mutex>& lock
    ) {
        if (auto r = begin_mount(medium_of(card.card), mount_path); !r) {
            return r;
        }
        sd = card;
        if (auto r = register_diskio(); !r) {
            return r;
        }
        if (auto r = register_vfs(cfg.max_files); !r) {
            return r;
        }
        return mount_or_format(cfg.format_if_missing, cfg.format, lock);
    }

    result<void> mount_flash(
        const partition& part,
        std::string_view mount_path,
        const flash_config& cfg,
        std::unique_lock<std::mutex>& lock
    ) {
        // The drive is reserved before the wear-leveling layer is mounted, so that a full drive
        // table needs no undoing there.
        if (auto r = begin_mount(medium_of(part), mount_path); !r) {
            return r;
        }
        wl_handle_t handle = WL_INVALID_HANDLE;
        if (auto err = wl_mount(part.idf_handle(), &handle); err != ESP_OK) {
            ESP_LOGE(
                TAG,
                "Failed to mount wear leveling on %s: %s",
                part.idf_handle()->label,
                esp_err_to_name(err)
            );
            if (err == ESP_ERR_NO_MEM) {
                // Also returned when all of the wear-leveling layer's handles (MAX_WL_HANDLES, 8)
                // are in use, which is no reason to treat it as heap exhaustion and abort; a
                // genuine allocation failure here is reported the same way.
                return error(idfxx::errc::invalid_state);
            }
            return error(err);
        }
        wl = handle;
        if (auto r = register_diskio(); !r) {
            return r;
        }
        if (auto r = register_vfs(cfg.max_files); !r) {
            return r;
        }
        return mount_or_format(cfg.format_if_missing, cfg.format, lock);
    }

    // With `mutex` held (see lock_open()), and not mount_mutex. Takes mount_mutex only around
    // f_mount(), which updates FatFs's global drive table and mount ID; f_fdisk() and f_mkfs() use
    // only this volume's own drive, which no other mount can take, so formatting doesn't hold up
    // other mounts. The claim, the disk driver and the VFS registration are left alone.
    result<void> format(const format_options& opts) {
        // Before the filesystem is unmounted, so that invalid options, and a failure to allocate,
        // leave the volume untouched.
        if (!valid_format_options(sector_size(), opts)) {
            return error(idfxx::errc::invalid_arg);
        }
        auto work = std::make_unique_for_overwrite<uint8_t[]>(work_buffer_size);
        {
            std::lock_guard lock(mount_mutex);
            unmount_fs();
        }
        auto made = make_filesystem(opts, {work.get(), work_buffer_size});
        FRESULT fr;
        {
            std::lock_guard lock(mount_mutex);
            // Mounted again whether or not formatting succeeded, so that the volume stays usable if
            // the medium still holds a filesystem.
            fr = mount_fs();
            if (fr != FR_OK) {
                // FatFs keeps a FATFS whose mount failed registered, and would try mounting it
                // again on the next access. Unregistered, every access fails with FR_NOT_ENABLED
                // until the volume is unmounted (or formatted again).
                unmount_fs();
            }
        }
        // Checked even when formatting failed, and after unmounting, so that a failure to allocate
        // in the mount raises with nothing left registered.
        auto remounted = check(fr, "f_mount");
        if (!made) {
            return made;
        }
        return remounted;
    }

    // Undoes the mount, in the reverse order of mount_card() and mount_flash(), with mount_mutex
    // taken here. Always completes, and returns the first error.
    esp_err_t close() {
        std::lock_guard lock(mount_mutex);
        esp_err_t first = ESP_OK;
        unmount_fs();
        unregister_diskio();
        if (wl) {
            ff_diskio_clear_pdrv_wl(*wl);
            // Saves the wear-leveling state.
            if (auto err = wl_unmount(*wl); err != ESP_OK) {
                ESP_LOGE(
                    TAG,
                    "Failed to unmount wear leveling for %s: %s",
                    base_path.c_str(),
                    esp_err_to_name(err)
                );
                first = err;
            }
            wl.reset();
        }
        if (auto err = unregister_vfs(); err != ESP_OK && first == ESP_OK) {
            first = err;
        }
        release_medium();
        return first;
    }

    // Undoes anything still set up: when a mount fails, or an out-of-memory exception leaves make()
    // part-way through one, make() destroys the state after releasing mount_mutex, which close()
    // takes. Every other state is destroyed after close() has run (in volume's destructor, move
    // assignment or try_unmount()), and has nothing left to undo.
    ~state() { (void)close(); }
};

namespace {

template<typename Config>
bool valid_args(std::string_view mount_path, const Config& cfg) {
    return valid_mount_path(mount_path) && cfg.max_files > 0;
}

// The checks on a card volume's arguments.
bool valid_card_args(
    const sdcard::card& card,
    std::string_view mount_path,
    const volume::card_config& cfg
) {
    return valid_args(mount_path, cfg) && valid_format_options(card.sector_size(), cfg.format);
}

// The checks on a flash volume's arguments, none of which need the partition: the wear-leveling
// layer presents every partition in sectors of CONFIG_WL_SECTOR_SIZE.
bool valid_flash_args(std::string_view mount_path, const volume::flash_config& cfg) {
    return valid_args(mount_path, cfg) && valid_format_options(CONFIG_WL_SECTOR_SIZE, cfg.format);
}

result<partition> find_by_label(std::string_view label) {
    if (label.empty()) {
        // An empty label would match any partition.
        return error(idfxx::errc::not_found);
    }
    return partition::try_find(label);
}

} // namespace

// --- On an SD card -----------------------------------------------------------

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
volume::volume(sdcard::card& card, std::string_view mount_path)
    : volume(unwrap(make(card, mount_path))) {}

volume::volume(sdcard::card& card, std::string_view mount_path, const card_config& cfg)
    : volume(unwrap(make(card, mount_path, cfg))) {}
#endif

result<volume> volume::make(sdcard::card& card, std::string_view mount_path) {
    return make(card, mount_path, card_config{});
}

result<volume>
volume::make(sdcard::card& card, std::string_view mount_path, const card_config& cfg) {
    if (!valid_card_args(card, mount_path, cfg)) {
        return error(idfxx::errc::invalid_arg);
    }
    // FatFs is not told about the write-protect switch, so it would mount the card as writable.
    if (card.is_write_protected()) {
        ESP_LOGE(TAG, "The card is write-protected");
        return error(idfxx::errc::invalid_state);
    }
    // Declared before the lock, so that a failed mount is undone (by ~state, whose close() takes
    // mount_mutex) after the lock is released, on return and when an exception propagates.
    auto s = std::make_unique<state>();
    std::unique_lock lock(mount_mutex);
    if (auto r =
            s->mount_card(card_image_drive_for(card, cfg.disk_status_check), mount_path, cfg, lock);
        !r) {
        return error(r.error());
    }
    return volume{std::move(s)};
}

// --- On a wear-leveled flash partition ---------------------------------------

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
volume::volume(const partition& part, std::string_view mount_path)
    : volume(unwrap(make(part, mount_path))) {}

volume::volume(const partition& part, std::string_view mount_path, const flash_config& cfg)
    : volume(unwrap(make(part, mount_path, cfg))) {}

volume::volume(std::string_view partition_label, std::string_view mount_path)
    : volume(unwrap(make(partition_label, mount_path))) {}

volume::volume(
    std::string_view partition_label,
    std::string_view mount_path,
    const flash_config& cfg
)
    : volume(unwrap(make(partition_label, mount_path, cfg))) {}
#endif

result<volume> volume::make(const partition& part, std::string_view mount_path) {
    return make(part, mount_path, flash_config{});
}

result<volume>
volume::make(const partition& part, std::string_view mount_path, const flash_config& cfg) {
    if (!valid_flash_args(mount_path, cfg)) {
        return error(idfxx::errc::invalid_arg);
    }
    if (!is_fat_partition(part) || part.readonly()) {
        ESP_LOGE(
            TAG, "Partition %s is not a writable data/fat partition", part.idf_handle()->label
        );
        return error(idfxx::errc::invalid_arg);
    }
    // Declared before the lock, so that a failed mount is undone (by ~state, whose close() takes
    // mount_mutex) after the lock is released, on return and when an exception propagates.
    auto s = std::make_unique<state>();
    std::unique_lock lock(mount_mutex);
    if (auto r = s->mount_flash(part, mount_path, cfg, lock); !r) {
        return error(r.error());
    }
    return volume{std::move(s)};
}

result<volume> volume::make(std::string_view partition_label, std::string_view mount_path) {
    return make(partition_label, mount_path, flash_config{});
}

result<volume> volume::make(
    std::string_view partition_label,
    std::string_view mount_path,
    const flash_config& cfg
) {
    // Before the partition is looked up, so that invalid arguments are reported whether or not it
    // exists.
    if (!valid_flash_args(mount_path, cfg)) {
        return error(idfxx::errc::invalid_arg);
    }
    auto part = find_by_label(partition_label);
    if (!part) {
        return error(part.error());
    }
    return make(*part, mount_path, cfg);
}

// --- Lifetime ----------------------------------------------------------------

volume::volume(std::unique_ptr<state> s) noexcept
    : _state(std::move(s)) {}

volume::~volume() {
    if (_state) {
        std::lock_guard lock(_state->mutex);
        _state->close();
    }
}

volume::volume(volume&& other) noexcept = default;

volume& volume::operator=(volume&& other) noexcept {
    if (this != &other) {
        if (_state) {
            // Released before the state that holds it is destroyed.
            std::lock_guard lock(_state->mutex);
            _state->close();
        }
        _state = std::move(other._state);
    }
    return *this;
}

// --- Accessors ---------------------------------------------------------------

std::string_view volume::mount_path() const noexcept {
    return _state->base_path;
}

std::string_view volume::drive() const noexcept {
    return {_state->drive.data(), 2};
}

FATFS* volume::idf_handle() const noexcept {
    return _state->fs;
}

uint64_t volume::size() const noexcept {
    // Doesn't take the volume's mutex, so as not to wait for a format().
    return _state->size.load();
}

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
uint64_t volume::free_size() const {
    return unwrap(try_free_size());
}
#endif

result<uint64_t> volume::try_free_size() const {
    // Not while format() has the filesystem unmounted.
    auto lock = _state->lock_open();
    if (!lock) {
        return error(lock.error());
    }
    return _state->open_free_size();
}

// --- Operations --------------------------------------------------------------

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
void volume::format(const format_options& opts) {
    unwrap(try_format(opts));
}
#endif

result<void> volume::try_format(const format_options& opts) {
    auto lock = _state->lock_open();
    if (!lock) {
        return error(lock.error());
    }
    return _state->format(opts);
}

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
void volume::unmount() {
    unwrap(try_unmount());
}
#endif

result<void> volume::try_unmount() {
    std::lock_guard lock(_state->mutex);
    return wrap(_state->close());
}

// --- Replacing files ---------------------------------------------------------

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
void volume::replace_file(std::string_view path, std::span<const uint8_t> contents) {
    unwrap(try_replace_file(path, contents));
}

void volume::replace_file(std::string_view path, std::string_view text) {
    unwrap(try_replace_file(path, text));
}
#endif

result<void> volume::try_replace_file(std::string_view path, std::span<const uint8_t> contents) {
    auto write = [contents](FILE* file) { return write_all(file, contents); };
    return _try_replace_file_with(path, &write, &_write<decltype(write)>);
}

result<void> volume::try_replace_file(std::string_view path, std::string_view text) {
    return try_replace_file(
        path, std::span(reinterpret_cast<const uint8_t*>(text.data()), text.size())
    );
}

result<void> volume::_try_replace_file_from(std::string_view path, void* source, read_fn read) {
    auto p = _state->paths_for_replace(path);
    if (!p) {
        return error(p.error());
    }
    // Allocated once the path is checked.
    auto storage = std::make_unique_for_overwrite<uint8_t[]>(replace_buffer_size);
    const std::span<uint8_t> buf(storage.get(), replace_buffer_size);
    auto write = [&](FILE* file) -> result<void> {
        while (true) {
            auto n = read(source, buf);
            if (!n) {
                return error(n.error());
            }
            if (*n == 0) {
                return {};
            }
            if (auto r = write_all(file, buf.first(std::min(*n, buf.size()))); !r) {
                return r;
            }
        }
    };
    return _state->replace(*p, &write, &_write<decltype(write)>);
}

result<void> volume::_try_replace_file_with(std::string_view path, void* writer, write_fn write) {
    auto p = _state->paths_for_replace(path);
    if (!p) {
        return error(p.error());
    }
    return _state->replace(*p, writer, write);
}

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
void volume::recover_file(std::string_view path) {
    unwrap(try_recover_file(path));
}
#endif

result<void> volume::try_recover_file(std::string_view path) {
    auto p = _state->paths_for_replace(path);
    if (!p) {
        return error(p.error());
    }
    auto lock = _state->lock_open();
    if (!lock) {
        return error(lock.error());
    }
    auto r = _state->recover(*p);
    if (!r && r.error() == errc::no_path) {
        // Nothing to recover in a directory that doesn't exist.
        return {};
    }
    return r;
}

// =============================================================================
// image
// =============================================================================

struct image::state : detail::mount_base {
    result<void>
    mount_card(const card_image_drive& card, std::string_view mount_path, const card_config& cfg) {
        if (auto r = begin_mount(medium_of(card.card), mount_path); !r) {
            return r;
        }
        card_image_drives[pdrv] = card;
        ff_diskio_register(pdrv, &card_image_impl);
        diskio_registered = true;
        if (auto r = register_vfs(cfg.max_files); !r) {
            return r;
        }
        return check(mount_fs(), "f_mount");
    }

    result<void>
    mount_flash(const partition& part, std::string_view mount_path, const flash_config& cfg) {
        if (auto r = begin_mount(medium_of(part), mount_path); !r) {
            return r;
        }
        if (auto err = ff_diskio_register_raw_partition(pdrv, part.idf_handle()); err != ESP_OK) {
            return error(err);
        }
        diskio_registered = true;
        if (auto r = register_vfs(cfg.max_files); !r) {
            return r;
        }
        return check(mount_fs(), "f_mount");
    }

    // Undoes the mount, in the reverse order of mount_card() and mount_flash(), with mount_mutex
    // taken here. Always completes, and returns the first error.
    esp_err_t close() {
        std::lock_guard lock(mount_mutex);
        unmount_fs();
        unregister_diskio();
        esp_err_t err = unregister_vfs();
        release_medium();
        return err;
    }

    // Undoes anything still set up: when the mount fails, or an out-of-memory exception leaves
    // make() part-way through it, make() destroys the state after releasing mount_mutex, which
    // close() takes. Every other state is destroyed after close() has run (in image's destructor,
    // move assignment or try_unmount()), and has nothing left to undo.
    ~state() { (void)close(); }
};

// --- On an SD card -----------------------------------------------------------

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
image::image(sdcard::card& card, std::string_view mount_path)
    : image(unwrap(make(card, mount_path))) {}

image::image(sdcard::card& card, std::string_view mount_path, const card_config& cfg)
    : image(unwrap(make(card, mount_path, cfg))) {}
#endif

result<image> image::make(sdcard::card& card, std::string_view mount_path) {
    return make(card, mount_path, card_config{});
}

result<image> image::make(sdcard::card& card, std::string_view mount_path, const card_config& cfg) {
    if (!valid_args(mount_path, cfg)) {
        return error(idfxx::errc::invalid_arg);
    }
    // Declared before the lock, so that a failed mount is undone (by ~state, whose close() takes
    // mount_mutex) after the lock is released, on return and when an exception propagates.
    auto s = std::make_unique<state>();
    std::lock_guard lock(mount_mutex);
    if (auto r = s->mount_card(card_image_drive_for(card, cfg.disk_status_check), mount_path, cfg);
        !r) {
        return error(r.error());
    }
    return image{std::move(s)};
}

// --- On a flash partition ----------------------------------------------------

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
image::image(const partition& part, std::string_view mount_path)
    : image(unwrap(make(part, mount_path))) {}

image::image(const partition& part, std::string_view mount_path, const flash_config& cfg)
    : image(unwrap(make(part, mount_path, cfg))) {}

image::image(std::string_view partition_label, std::string_view mount_path)
    : image(unwrap(make(partition_label, mount_path))) {}

image::image(std::string_view partition_label, std::string_view mount_path, const flash_config& cfg)
    : image(unwrap(make(partition_label, mount_path, cfg))) {}
#endif

result<image> image::make(const partition& part, std::string_view mount_path) {
    return make(part, mount_path, flash_config{});
}

result<image>
image::make(const partition& part, std::string_view mount_path, const flash_config& cfg) {
    if (!valid_args(mount_path, cfg)) {
        return error(idfxx::errc::invalid_arg);
    }
    if (!is_fat_partition(part)) {
        ESP_LOGE(TAG, "Partition %s is not a data/fat partition", part.idf_handle()->label);
        return error(idfxx::errc::invalid_arg);
    }
    // Declared before the lock, so that a failed mount is undone (by ~state, whose close() takes
    // mount_mutex) after the lock is released, on return and when an exception propagates.
    auto s = std::make_unique<state>();
    std::lock_guard lock(mount_mutex);
    if (auto r = s->mount_flash(part, mount_path, cfg); !r) {
        return error(r.error());
    }
    return image{std::move(s)};
}

result<image> image::make(std::string_view partition_label, std::string_view mount_path) {
    return make(partition_label, mount_path, flash_config{});
}

result<image> image::make(
    std::string_view partition_label,
    std::string_view mount_path,
    const flash_config& cfg
) {
    // Before the partition is looked up, so that invalid arguments are reported whether or not it
    // exists.
    if (!valid_args(mount_path, cfg)) {
        return error(idfxx::errc::invalid_arg);
    }
    auto part = find_by_label(partition_label);
    if (!part) {
        return error(part.error());
    }
    return make(*part, mount_path, cfg);
}

// --- Lifetime ----------------------------------------------------------------

image::image(std::unique_ptr<state> s) noexcept
    : _state(std::move(s)) {}

image::~image() {
    if (_state) {
        _state->close();
    }
}

image::image(image&& other) noexcept = default;

image& image::operator=(image&& other) noexcept {
    if (this != &other) {
        if (_state) {
            _state->close();
        }
        _state = std::move(other._state);
    }
    return *this;
}

// --- Accessors ---------------------------------------------------------------

std::string_view image::mount_path() const noexcept {
    return _state->base_path;
}

std::string_view image::drive() const noexcept {
    return {_state->drive.data(), 2};
}

FATFS* image::idf_handle() const noexcept {
    return _state->fs;
}

uint64_t image::size() const noexcept {
    return _state->size.load();
}

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
uint64_t image::free_size() const {
    return unwrap(try_free_size());
}
#endif

result<uint64_t> image::try_free_size() const {
    return _state->free_size();
}

// --- Operations --------------------------------------------------------------

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
void image::unmount() {
    unwrap(try_unmount());
}
#endif

result<void> image::try_unmount() {
    return wrap(_state->close());
}

} // namespace idfxx::fatfs
