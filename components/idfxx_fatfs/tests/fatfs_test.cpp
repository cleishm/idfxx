// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Unit tests for idfxx fatfs
// Uses ESP-IDF Unity test framework with compile-time static_asserts
//
// The flash tests use the test app's `storage` partition (data/fat, mounted with wear leveling) and `fatimg`
// partition (data/fat, holding the raw image built from tests/fatimg). On QEMU `storage` starts erased; on hardware
// it holds whatever the previous run left, so every test that needs a known state formats or erases it.

#include "idfxx/fatfs"
#include "sd_test_rig.hpp"
#include "unity.h"

#include <idfxx/partition>
#include <idfxx/sdcard>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cinttypes>
#include <concepts>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <diskio_impl.h>
#include <diskio_rawflash.h>
#include <dirent.h>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <new>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <system_error>
#include <type_traits>
#include <unistd.h>
#include <utility>
#include <vector>
#include <wear_levelling.h>

using namespace idfxx;
using namespace idfxx::fatfs;

// =============================================================================
// Compile-time tests (static_assert)
// These verify correctness at compile time - if this file compiles, they pass.
// =============================================================================

// volume and image are not default constructible (must use make() or a constructor)
static_assert(!std::is_default_constructible_v<volume>);
static_assert(!std::is_default_constructible_v<image>);

// volume and image are not copyable (unique ownership of the mount)
static_assert(!std::is_copy_constructible_v<volume>);
static_assert(!std::is_copy_assignable_v<volume>);
static_assert(!std::is_copy_constructible_v<image>);
static_assert(!std::is_copy_assignable_v<image>);

// volume and image are move-only, and moving never throws
static_assert(std::is_nothrow_move_constructible_v<volume>);
static_assert(std::is_nothrow_move_assignable_v<volume>);
static_assert(std::is_nothrow_move_constructible_v<image>);
static_assert(std::is_nothrow_move_assignable_v<image>);

// make() returns result<T>
static_assert(std::is_same_v<
              decltype(volume::make(std::declval<sdcard::card&>(), std::string_view{})),
              result<volume>>);
static_assert(std::is_same_v<
              decltype(volume::make(std::declval<const partition&>(), std::string_view{})),
              result<volume>>);
static_assert(std::is_same_v<decltype(volume::make(std::string_view{}, std::string_view{})), result<volume>>);
static_assert(std::is_same_v<
              decltype(image::make(std::declval<sdcard::card&>(), std::string_view{})),
              result<image>>);
static_assert(std::is_same_v<
              decltype(image::make(std::declval<const partition&>(), std::string_view{})),
              result<image>>);
static_assert(std::is_same_v<decltype(image::make(std::string_view{}, std::string_view{})), result<image>>);

// try_replace_file() takes bytes or text in memory, a byte source or a writer returning result<void>, and nothing else
template<typename T>
concept replaceable_with = requires(volume& v, T&& t) {
    { v.try_replace_file(std::string_view{}, std::forward<T>(t)) } -> std::same_as<result<void>>;
};
static_assert(replaceable_with<std::span<const uint8_t>>);
static_assert(replaceable_with<std::vector<uint8_t>&>);
static_assert(replaceable_with<memory_source&>);
static_assert(replaceable_with<decltype([](FILE*) -> result<void> { return {}; })>);
static_assert(replaceable_with<result<void> (&)(FILE*)>);
static_assert(replaceable_with<result<void> (*)(FILE*)>);
static_assert(!replaceable_with<decltype([](FILE*) {})>);
static_assert(!replaceable_with<decltype([](FILE*) { return true; })>);
static_assert(replaceable_with<std::string_view>);
static_assert(replaceable_with<const std::string&>);
static_assert(replaceable_with<const char (&)[4]>);
static_assert(!replaceable_with<int>);

// errc is an error code enum, and its values are FatFs's FRESULT codes
static_assert(std::is_error_code_enum_v<fatfs::errc>);
static_assert(std::to_underlying(fatfs::errc::disk_err) == FR_DISK_ERR);
static_assert(std::to_underlying(fatfs::errc::no_file) == FR_NO_FILE);
static_assert(std::to_underlying(fatfs::errc::no_filesystem) == FR_NO_FILESYSTEM);
static_assert(std::to_underlying(fatfs::errc::invalid_parameter) == FR_INVALID_PARAMETER);

// =============================================================================
// Error category
// =============================================================================

TEST_CASE("fatfs error category name and messages", "[idfxx][fatfs]") {
    TEST_ASSERT_EQUAL_STRING("fatfs::Error", fatfs_category().name());
    std::error_code ec = fatfs::errc::no_file;
    TEST_ASSERT_TRUE(&ec.category() == &fatfs_category());
    TEST_ASSERT_EQUAL_STRING("File not found", ec.message().c_str());
    TEST_ASSERT_EQUAL_STRING("No FAT filesystem", make_error_code(fatfs::errc::no_filesystem).message().c_str());
    TEST_ASSERT_EQUAL_STRING("Unknown FAT filesystem error", fatfs_category().message(999).c_str());
}

TEST_CASE("fatfs errc compares equal to its std::errc equivalent", "[idfxx][fatfs]") {
    const std::pair<fatfs::errc, std::errc> mapped[] = {
        {fatfs::errc::disk_err, std::errc::io_error},
        {fatfs::errc::not_ready, std::errc::no_such_device},
        {fatfs::errc::no_file, std::errc::no_such_file_or_directory},
        {fatfs::errc::no_path, std::errc::no_such_file_or_directory},
        {fatfs::errc::invalid_name, std::errc::invalid_argument},
        {fatfs::errc::denied, std::errc::permission_denied},
        {fatfs::errc::exist, std::errc::file_exists},
        {fatfs::errc::invalid_object, std::errc::bad_file_descriptor},
        {fatfs::errc::write_protected, std::errc::read_only_file_system},
        {fatfs::errc::timeout, std::errc::timed_out},
        {fatfs::errc::locked, std::errc::device_or_resource_busy},
        {fatfs::errc::not_enough_core, std::errc::not_enough_memory},
        {fatfs::errc::too_many_open_files, std::errc::too_many_files_open},
        {fatfs::errc::invalid_parameter, std::errc::invalid_argument},
    };
    for (auto [e, posix] : mapped) {
        std::error_code ec = e;
        TEST_ASSERT_TRUE(ec == posix);
        TEST_ASSERT_TRUE(ec == e);
    }

    // No portable equivalent: the condition stays in the fatfs category
    const fatfs::errc unmapped[] = {
        fatfs::errc::int_err,
        fatfs::errc::invalid_drive,
        fatfs::errc::not_enabled,
        fatfs::errc::no_filesystem,
        fatfs::errc::mkfs_aborted,
    };
    for (auto e : unmapped) {
        std::error_code ec = e;
        TEST_ASSERT_TRUE(&ec.default_error_condition().category() == &fatfs_category());
        TEST_ASSERT_EQUAL_INT(std::to_underlying(e), ec.default_error_condition().value());
    }

    // Distinct from the idfxx::errc values with the same numbers
    TEST_ASSERT_FALSE(std::error_code(fatfs::errc::no_file) == idfxx::errc::fail);
}

TEST_CASE("fatfs_error converts a FatFs result", "[idfxx][fatfs]") {
    auto u = fatfs_error(FR_NO_PATH);
    TEST_ASSERT_TRUE(u.error() == fatfs::errc::no_path);
    TEST_ASSERT_TRUE(&u.error().category() == &fatfs_category());

    result<int> r = fatfs_error(FR_EXIST);
    TEST_ASSERT_FALSE(r.has_value());
    TEST_ASSERT_TRUE(r.error() == std::errc::file_exists);
}

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
TEST_CASE("fatfs_error treats FR_NOT_ENOUGH_CORE as out of memory", "[idfxx][fatfs]") {
    bool threw = false;
    try {
        (void)fatfs_error(FR_NOT_ENOUGH_CORE);
    } catch (const std::bad_alloc&) {
        threw = true;
    }
    TEST_ASSERT_TRUE_MESSAGE(threw, "Expected std::bad_alloc to be thrown");
}
#endif

// =============================================================================
// Configuration defaults
// =============================================================================

TEST_CASE("fatfs config default initialization", "[idfxx][fatfs]") {
    volume::card_config card_cfg{};
    TEST_ASSERT_EQUAL_UINT32(5, card_cfg.max_files);
    TEST_ASSERT_FALSE(card_cfg.format_if_missing);
    TEST_ASSERT_FALSE(card_cfg.format.allocation_unit_size.has_value());
    TEST_ASSERT_FALSE(card_cfg.format.use_one_fat);
    TEST_ASSERT_FALSE(card_cfg.disk_status_check);

    volume::flash_config flash_cfg{};
    TEST_ASSERT_EQUAL_UINT32(5, flash_cfg.max_files);
    TEST_ASSERT_FALSE(flash_cfg.format_if_missing);
    TEST_ASSERT_FALSE(flash_cfg.format.allocation_unit_size.has_value());
    TEST_ASSERT_FALSE(flash_cfg.format.use_one_fat);

    image::card_config image_card_cfg{};
    TEST_ASSERT_EQUAL_UINT32(5, image_card_cfg.max_files);
    TEST_ASSERT_FALSE(image_card_cfg.disk_status_check);

    image::flash_config image_flash_cfg{};
    TEST_ASSERT_EQUAL_UINT32(5, image_flash_cfg.max_files);

    format_options opts{};
    TEST_ASSERT_FALSE(opts.allocation_unit_size.has_value());
    TEST_ASSERT_FALSE(opts.use_one_fat);
}

// =============================================================================
// Helpers for the flash tests
// =============================================================================

// Asserts that the result `r` holds a value, failing with its error's message if not. A macro, so that Unity reports
// the caller's line.
#define TEST_ASSERT_OK(r) TEST_ASSERT_TRUE_MESSAGE((r).has_value(), (r) ? "" : (r).error().message().c_str())

namespace {

constexpr const char* STORAGE = "storage"; // wear-leveled data/fat partition
constexpr const char* FATIMG = "fatimg";   // raw FAT image built from tests/fatimg

partition find_partition(const char* label) {
    auto r = partition::try_find(label);
    TEST_ASSERT_TRUE_MESSAGE(r.has_value(), label);
    return *r;
}

// Leaves `storage` erased, with no filesystem or wear-leveling state. Nothing may be mounted on it.
void erase_storage() {
    auto part = find_partition(STORAGE);
    TEST_ASSERT_TRUE(part.try_erase_range(0, part.size()).has_value());
}

// Writes the first `count` sectors of `storage`, as a volume sees them through the wear-leveling layer. `fill` is
// given them zeroed to fill in, the sector size and the number of sectors. Nothing may be mounted on `storage`.
template<typename Fill>
void write_storage_sectors(size_t count, Fill fill) {
    auto part = find_partition(STORAGE);
    wl_handle_t h = WL_INVALID_HANDLE;
    TEST_ASSERT_EQUAL_INT(ESP_OK, wl_mount(part.idf_handle(), &h));
    const size_t sector_size = wl_sector_size(h);
    std::vector<uint8_t> sectors(count * sector_size, 0);
    fill(sectors, sector_size, wl_size(h) / sector_size);
    const esp_err_t erase_err = wl_erase_range(h, 0, sectors.size());
    const esp_err_t write_err = wl_write(h, 0, sectors.data(), sectors.size());
    const esp_err_t unmount_err = wl_unmount(h);
    TEST_ASSERT_EQUAL_INT(ESP_OK, erase_err);
    TEST_ASSERT_EQUAL_INT(ESP_OK, write_err);
    TEST_ASSERT_EQUAL_INT(ESP_OK, unmount_err);
}

// Fills `bs`, a zeroed sector, with a damaged FAT16 or FAT32 boot sector (`fat_bits` is 16 or 32): it has the jump
// code, the type string and the signature, but its BPB is left zeroed (no bytes per sector, sectors per cluster or
// FATs). FatFs takes a FAT32 one for a volume boot record, so it looks for no partition table in it, then rejects its
// BPB.
void fill_damaged_boot_sector(uint8_t* bs, int fat_bits) {
    const bool fat32 = fat_bits == 32;
    bs[0] = 0xEB; // jump code, past the BPB
    bs[1] = fat32 ? 0x58 : 0x3C;
    bs[2] = 0x90;
    std::memcpy(bs + 3, "MSDOS5.0", 8);
    std::memcpy(bs + (fat32 ? 82 : 54), fat32 ? "FAT32   " : "FAT16   ", 8);
    bs[510] = 0x55;
    bs[511] = 0xAA;
}

// Fills `mbr`, a zeroed sector, with a partition table listing a one-sector FAT32 (LBA) partition at each of the
// sectors in `starts`.
void fill_partition_table(uint8_t* mbr, std::initializer_list<size_t> starts) {
    uint8_t* entry = mbr + 446;
    for (size_t start : starts) {
        entry[4] = 0x0C; // partition type: FAT32 (LBA)
        for (size_t b = 0; b < 4; ++b) {
            entry[8 + b] = static_cast<uint8_t>(start >> (8 * b)); // first sector
        }
        entry[12] = 1; // sector count
        entry += 16;
    }
    mbr[510] = 0x55;
    mbr[511] = 0xAA;
}

// Writes a partition table to sector 0 of `storage` listing two partitions: the first at sector 1, which holds a
// damaged FAT32 boot sector, and the second at sector 2, which holds no boot sector, or (with `second_past_end`) just
// past the end of the medium. Nothing may be mounted on `storage`.
void write_table_with_damaged_first_partition(bool second_past_end) {
    write_storage_sectors(3, [&](std::vector<uint8_t>& sectors, size_t sector_size, size_t sector_count) {
        fill_partition_table(sectors.data(), {1, second_past_end ? sector_count : 2});
        fill_damaged_boot_sector(sectors.data() + sector_size, 32);
        std::fill_n(sectors.data() + 2 * sector_size, sector_size, 0xA5);
    });
}

// The mounts a test makes, and the card it moves. They are held in static storage rather than in the test's stack
// frame: a failed Unity assertion longjmps out of the test, skipping destructors, which would leave them mounted and
// fail the tests after it. A test holding two volumes or two images at once uses slots 0 and 1.
std::optional<volume>& held_volume(size_t slot = 0) {
    static std::array<std::optional<volume>, 2> vols;
    return vols[slot];
}

std::optional<image>& held_image(size_t slot = 0) {
    static std::array<std::optional<image>, 2> imgs;
    return imgs[slot];
}

std::optional<sdcard::card>& moved_card() {
    static std::optional<sdcard::card> c;
    return c;
}

// Destroys whatever a test left behind, unmounting what is still mounted: the mounts, then the moved card.
void release_mounts() {
    for (size_t slot = 0; slot < 2; ++slot) {
        held_image(slot).reset();
        held_volume(slot).reset();
    }
    moved_card().reset();
}

// Releases a test's mounts when the test ends and, from tearDown, when it fails. In a [hw] test, constructed right
// after the card_fixture, so that the mounts are released before the card and bus.
class mounts_fixture {
public:
    mounts_fixture() { idfxx::test::run_after_each_test(&release_mounts); }

    ~mounts_fixture() { release_mounts(); }

    mounts_fixture(const mounts_fixture&) = delete;
    mounts_fixture& operator=(const mounts_fixture&) = delete;
};

volume::flash_config format_if_needed() {
    return {.format_if_missing = true};
}

// Checks that `storage`, holding no filesystem FatFs recognizes, fails to mount with fatfs::errc::no_filesystem
// without format_if_missing, and that with it the mount formats the partition, without a partition table, and
// the new filesystem can be used. The results are checked once nothing is mounted, so that a failure leaves no volume
// behind.
void assert_storage_formats_on_mount() {
    std::error_code unformatted;
    if (auto r = volume::make(STORAGE, "/fat"); !r) {
        unformatted = r.error();
    }
    std::error_code formatted;
    LBA_t volbase = 1;
    std::string text;
    if (auto vol = volume::make(STORAGE, "/fat", format_if_needed())) {
        volbase = vol->idf_handle()->volbase;
        if (FILE* f = std::fopen("/fat/new.txt", "w")) {
            std::fputs("formatted", f);
            std::fclose(f);
        }
        if (FILE* f = std::fopen("/fat/new.txt", "r")) {
            char buf[16];
            text.assign(buf, std::fread(buf, 1, sizeof(buf), f));
            std::fclose(f);
        }
    } else {
        formatted = vol.error();
    }

    TEST_ASSERT_TRUE_MESSAGE(unformatted == fatfs::errc::no_filesystem, unformatted.message().c_str());
    TEST_ASSERT_FALSE_MESSAGE(formatted, formatted.message().c_str());
    TEST_ASSERT_EQUAL_UINT32(0, volbase);
    TEST_ASSERT_EQUAL_STRING("formatted", text.c_str());
}

// Mounts `storage`, held in held_volume().
volume& mount_storage(std::string_view path = "/fat") {
    auto r = volume::make(STORAGE, path, format_if_needed());
    TEST_ASSERT_OK(r);
    return held_volume().emplace(std::move(*r));
}

// Mounts `storage` with an empty filesystem, held in held_volume().
volume& mount_empty_storage(std::string_view path = "/fat") {
    auto& vol = mount_storage(path);
    auto r = vol.try_format();
    TEST_ASSERT_OK(r);
    return vol;
}

// Leaves `storage` holding a raw copy of the `fatimg` image. Nothing may be mounted on either partition.
void copy_fatimg_to_storage() {
    auto src = find_partition(FATIMG);
    auto dst = find_partition(STORAGE);
    TEST_ASSERT_TRUE(dst.try_erase_range(0, dst.size()).has_value());
    std::vector<uint8_t> buf(4096);
    for (size_t offset = 0; offset < src.size(); offset += buf.size()) {
        TEST_ASSERT_TRUE(src.try_read(offset, buf).has_value());
        TEST_ASSERT_TRUE(dst.try_write(offset, buf).has_value());
    }
}

// Mounts the image on `storage`, held in held_image().
image& mount_image_on_storage(std::string_view path) {
    auto r = image::make(STORAGE, path);
    TEST_ASSERT_OK(r);
    return held_image().emplace(std::move(*r));
}

// Mounts `fatimg`, held in held_image(slot).
image& mount_fatimg(std::string_view path = "/img", size_t slot = 0) {
    auto r = image::make(FATIMG, path);
    TEST_ASSERT_OK(r);
    return held_image(slot).emplace(std::move(*r));
}

void write_file(const char* path, const char* text, const char* mode = "w") {
    FILE* f = std::fopen(path, mode);
    TEST_ASSERT_NOT_NULL_MESSAGE(f, path);
    TEST_ASSERT_EQUAL_INT(0, std::fputs(text, f) < 0 ? -1 : 0);
    TEST_ASSERT_EQUAL_INT(0, std::fclose(f));
}

std::string read_file(const char* path) {
    FILE* f = std::fopen(path, "r");
    TEST_ASSERT_NOT_NULL_MESSAGE(f, path);
    std::string text;
    char buf[64];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) {
        text.append(buf, n);
    }
    TEST_ASSERT_EQUAL_INT(0, std::fclose(f));
    return text;
}

bool exists(const char* path) {
    struct stat st;
    return stat(path, &st) == 0;
}

// Opens `path` with `mode`, expecting it to fail: returns errno, or 0 (having closed the file) if it opened.
int fopen_errno(const char* path, const char* mode) {
    errno = 0;
    if (FILE* f = std::fopen(path, mode)) {
        std::fclose(f);
        return 0;
    }
    return errno;
}

std::vector<std::string> list_dir(const char* path) {
    std::vector<std::string> names;
    DIR* dir = opendir(path);
    TEST_ASSERT_NOT_NULL_MESSAGE(dir, path);
    while (struct dirent* entry = readdir(dir)) {
        names.emplace_back(entry->d_name);
    }
    TEST_ASSERT_EQUAL_INT(0, closedir(dir));
    std::sort(names.begin(), names.end());
    return names;
}

bool contains(const std::vector<std::string>& names, const char* name) {
    return std::find(names.begin(), names.end(), name) != names.end();
}

template<typename T>
void assert_error(const result<T>& r, std::error_code expected) {
    TEST_ASSERT_FALSE(r.has_value());
    TEST_ASSERT_EQUAL_INT_MESSAGE(expected.value(), r.error().value(), r.error().message().c_str());
    TEST_ASSERT_TRUE(&r.error().category() == &expected.category());
}

// A disk driver whose medium never initializes, standing in for one that has stopped responding.
DSTATUS failing_disk_status(unsigned char) {
    return STA_NOINIT;
}

DRESULT failing_disk_read(unsigned char, unsigned char*, uint32_t, unsigned) {
    return RES_ERROR;
}

DRESULT failing_disk_write(unsigned char, const unsigned char*, uint32_t, unsigned) {
    return RES_ERROR;
}

DRESULT failing_disk_ioctl(unsigned char, unsigned char, void*) {
    return RES_ERROR;
}

constexpr ff_diskio_impl_t failing_disk{
    .init = failing_disk_status,
    .status = failing_disk_status,
    .read = failing_disk_read,
    .write = failing_disk_write,
    .ioctl = failing_disk_ioctl,
};

// The FatFs physical drive number behind a volume's drive ("0:" is drive 0).
BYTE pdrv_of(const volume& vol) {
    return static_cast<BYTE>(vol.drive()[0] - '0');
}

} // namespace

// =============================================================================
// Argument validation
// =============================================================================

TEST_CASE("fatfs rejects invalid mount paths", "[idfxx][fatfs]") {
    const char* invalid[] = {
        "",               // empty
        "fat",            // no leading '/'
        "/",              // root alone
        "/fat/",          // trailing '/'
        "/abcdefghijklm", // 14 characters
        "/abcdefghijklmnopqrstuvwxyz",
    };
    for (const char* path : invalid) {
        assert_error(volume::make(STORAGE, path), idfxx::errc::invalid_arg);
        assert_error(volume::make(find_partition(STORAGE), path, format_if_needed()), idfxx::errc::invalid_arg);
        assert_error(image::make(FATIMG, path), idfxx::errc::invalid_arg);
        assert_error(image::make(find_partition(FATIMG), path), idfxx::errc::invalid_arg);
    }
    // An embedded NUL
    assert_error(image::make(FATIMG, std::string_view("/img\0x", 6)), idfxx::errc::invalid_arg);
}

TEST_CASE("fatfs accepts a 13-character mount path and releases it", "[idfxx][fatfs]") {
    mounts_fixture mounts;
    const char* path = "/abcdefghijkl"; // 13 characters, the longest allowed
    TEST_ASSERT_EQUAL_UINT32(13, std::strlen(path));
    for (int i = 0; i < 2; ++i) {
        auto& img = mount_fatimg(path);
        TEST_ASSERT_EQUAL_STRING("Hello from the FAT image\n", read_file("/abcdefghijkl/hello.txt").c_str());
        TEST_ASSERT_TRUE(img.try_unmount().has_value());
        TEST_ASSERT_FALSE(exists("/abcdefghijkl/hello.txt"));
    }
}

TEST_CASE("fatfs swapped label and mount path arguments fail", "[idfxx][fatfs]") {
    assert_error(volume::make("/fat", STORAGE), idfxx::errc::invalid_arg);
    assert_error(volume::make("/fat", STORAGE, format_if_needed()), idfxx::errc::invalid_arg);
    assert_error(image::make("/img", FATIMG), idfxx::errc::invalid_arg);
}

TEST_CASE("fatfs unknown partition label is not_found", "[idfxx][fatfs]") {
    assert_error(volume::make("nosuchpart", "/fat"), idfxx::errc::not_found);
    assert_error(image::make("nosuchpart", "/img"), idfxx::errc::not_found);
    assert_error(volume::make("", "/fat"), idfxx::errc::not_found);
    assert_error(image::make("", "/img"), idfxx::errc::not_found);
}

TEST_CASE("fatfs rejects partitions that are not data/fat", "[idfxx][fatfs]") {
    assert_error(volume::make("nvs", "/fat", format_if_needed()), idfxx::errc::invalid_arg);
    assert_error(image::make("nvs", "/img"), idfxx::errc::invalid_arg);
    auto app = partition::try_find(partition::type::app);
    TEST_ASSERT_TRUE(app.has_value());
    assert_error(volume::make(*app, "/fat", format_if_needed()), idfxx::errc::invalid_arg);
    assert_error(image::make(*app, "/img"), idfxx::errc::invalid_arg);
}

TEST_CASE("fatfs rejects max_files of 0", "[idfxx][fatfs]") {
    assert_error(volume::make(STORAGE, "/fat", volume::flash_config{.max_files = 0}), idfxx::errc::invalid_arg);
    assert_error(image::make(FATIMG, "/img", image::flash_config{.max_files = 0}), idfxx::errc::invalid_arg);
    // Checked before the partition is looked up
    assert_error(volume::make("nosuchpart", "/fat", volume::flash_config{.max_files = 0}), idfxx::errc::invalid_arg);
    assert_error(image::make("nosuchpart", "/img", image::flash_config{.max_files = 0}), idfxx::errc::invalid_arg);
}

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
TEST_CASE("fatfs constructors throw on failure", "[idfxx][fatfs]") {
    bool threw = false;
    try {
        volume vol("nosuchpart", "/fat");
    } catch (const std::system_error& e) {
        threw = true;
        TEST_ASSERT_TRUE(e.code() == idfxx::errc::not_found);
    }
    TEST_ASSERT_TRUE_MESSAGE(threw, "Expected std::system_error to be thrown");

    threw = false;
    try {
        image img(FATIMG, "img");
    } catch (const std::system_error& e) {
        threw = true;
        TEST_ASSERT_TRUE(e.code() == idfxx::errc::invalid_arg);
    }
    TEST_ASSERT_TRUE_MESSAGE(threw, "Expected std::system_error to be thrown");
}
#endif

// =============================================================================
// Volume on a wear-leveled flash partition
// =============================================================================

TEST_CASE("fatfs volume on an erased partition fails with no_filesystem without format", "[idfxx][fatfs]") {
    mounts_fixture mounts;
    erase_storage();
    assert_error(volume::make(STORAGE, "/fat"), fatfs::errc::no_filesystem);
    // Nothing is left mounted or registered: the same mount succeeds once formatting is allowed
    auto& vol = mount_storage();
    TEST_ASSERT_TRUE(vol.size() > 0);
}

TEST_CASE("fatfs volume formats a partition whose partition table lists no FAT filesystem", "[idfxx][fatfs]") {
    // Before formatting, a mount is tried again with its reads watched, so that a read error is never taken for a
    // missing filesystem. Here every read succeeds, and the format goes ahead.
    erase_storage();
    write_storage_sectors(1, [](std::vector<uint8_t>& mbr, size_t, size_t) {
        // A partition table listing two partitions, at sectors 1 and 2, which hold no boot sector
        fill_partition_table(mbr.data(), {1, 2});
    });
    // FatFs finds no filesystem in either partition. Formatted without a partition table, replacing the one written
    // above.
    assert_storage_formats_on_mount();
}

TEST_CASE("fatfs volume formats a partition whose partition table lists a partition past its end", "[idfxx][fatfs]") {
    // A stale or foreign partition table: a partition it lists beyond the end of the medium doesn't stop the format.
    erase_storage();
    write_storage_sectors(1, [](std::vector<uint8_t>& mbr, size_t, size_t sector_count) {
        fill_partition_table(mbr.data(), {sector_count}); // just past the end
    });
    assert_storage_formats_on_mount();
}

TEST_CASE("fatfs volume formats a partition whose first listed partition has a damaged boot sector", "[idfxx][fatfs]") {
    // FatFs takes the first partition's boot sector for a FAT32 volume boot record, so it reads no other partition
    // before rejecting its BPB.
    erase_storage();
    write_table_with_damaged_first_partition(false);
    assert_storage_formats_on_mount();
}

TEST_CASE("fatfs volume formats a partition listing a damaged partition, then one past its end", "[idfxx][fatfs]") {
    // As above, with the second partition listed just past the end of the medium.
    erase_storage();
    write_table_with_damaged_first_partition(true);
    assert_storage_formats_on_mount();
}

TEST_CASE("fatfs volume formats a partition with a damaged FAT32 boot sector", "[idfxx][fatfs]") {
    erase_storage();
    write_storage_sectors(1, [](std::vector<uint8_t>& bs, size_t, size_t) { fill_damaged_boot_sector(bs.data(), 32); });
    assert_storage_formats_on_mount();
}

TEST_CASE("fatfs volume formats a partition with a damaged FAT16 boot sector", "[idfxx][fatfs]") {
    // Without a proper BPB, FatFs doesn't recognize a FAT12 or FAT16 volume boot record by its type string, and looks
    // for a partition table in it; the boot code where that would be is zeroed here, so it lists none.
    erase_storage();
    write_storage_sectors(1, [](std::vector<uint8_t>& bs, size_t, size_t) { fill_damaged_boot_sector(bs.data(), 16); });
    assert_storage_formats_on_mount();
}

TEST_CASE("fatfs volume formats an erased partition when format_if_missing is set", "[idfxx][fatfs]") {
    mounts_fixture mounts;
    erase_storage();
    auto part = find_partition(STORAGE);
    auto& vol = mount_storage("/fat");

    TEST_ASSERT_EQUAL_STRING("/fat", std::string(vol.mount_path()).c_str());
    TEST_ASSERT_EQUAL_UINT32(2, vol.drive().size());
    TEST_ASSERT_TRUE(vol.drive()[0] >= '0' && vol.drive()[0] <= '9');
    TEST_ASSERT_TRUE(vol.drive()[1] == ':');
    TEST_ASSERT_NOT_NULL(vol.idf_handle());

    // The wear-leveling layer keeps some sectors for itself, and FAT some more
    TEST_ASSERT_GREATER_THAN_UINT32(part.size() / 2, static_cast<uint32_t>(vol.size()));
    TEST_ASSERT_LESS_THAN_UINT32(part.size(), static_cast<uint32_t>(vol.size()));
    auto free = vol.try_free_size();
    TEST_ASSERT_TRUE(free.has_value());
    TEST_ASSERT_GREATER_THAN_UINT32(0, static_cast<uint32_t>(*free));
    TEST_ASSERT_LESS_OR_EQUAL_UINT32(static_cast<uint32_t>(vol.size()), static_cast<uint32_t>(*free));
    TEST_ASSERT_FALSE(exists("/fat/hello.txt"));
}

TEST_CASE("fatfs volume write, read and append with stdio", "[idfxx][fatfs]") {
    mounts_fixture mounts;
    mount_empty_storage();
    write_file("/fat/hello.txt", "Hello");
    TEST_ASSERT_EQUAL_STRING("Hello", read_file("/fat/hello.txt").c_str());
    write_file("/fat/hello.txt", ", world", "a");
    TEST_ASSERT_EQUAL_STRING("Hello, world", read_file("/fat/hello.txt").c_str());
    write_file("/fat/hello.txt", "Replaced");
    TEST_ASSERT_EQUAL_STRING("Replaced", read_file("/fat/hello.txt").c_str());

    struct stat st;
    TEST_ASSERT_EQUAL_INT(0, stat("/fat/hello.txt", &st));
    TEST_ASSERT_TRUE(S_ISREG(st.st_mode));
    TEST_ASSERT_EQUAL_INT(8, static_cast<int>(st.st_size));

    TEST_ASSERT_NULL(std::fopen("/fat/missing.txt", "r"));
    TEST_ASSERT_EQUAL_INT(ENOENT, errno);
}

TEST_CASE("fatfs volume write, read and append with fstream", "[idfxx][fatfs]") {
    mounts_fixture mounts;
    mount_empty_storage();
    {
        std::ofstream out("/fat/stream.txt");
        TEST_ASSERT_TRUE(out.is_open());
        out << "line 1\n";
    }
    {
        std::ofstream out("/fat/stream.txt", std::ios::app);
        TEST_ASSERT_TRUE(out.is_open());
        out << "line 2\n";
    }
    std::ifstream in("/fat/stream.txt");
    TEST_ASSERT_TRUE(in.is_open());
    std::string first;
    std::string second;
    std::getline(in, first);
    std::getline(in, second);
    TEST_ASSERT_EQUAL_STRING("line 1", first.c_str());
    TEST_ASSERT_EQUAL_STRING("line 2", second.c_str());

    // Binary data round-trips exactly
    std::vector<char> data(1000);
    for (size_t i = 0; i < data.size(); ++i) {
        data[i] = static_cast<char>(i * 31);
    }
    {
        std::ofstream out("/fat/data.bin", std::ios::binary);
        out.write(data.data(), static_cast<std::streamsize>(data.size()));
        TEST_ASSERT_TRUE(out.good());
    }
    std::ifstream bin("/fat/data.bin", std::ios::binary);
    std::vector<char> back((std::istreambuf_iterator<char>(bin)), std::istreambuf_iterator<char>());
    TEST_ASSERT_EQUAL_UINT32(data.size(), back.size());
    TEST_ASSERT_EQUAL_MEMORY(data.data(), back.data(), data.size());
}

TEST_CASE("fatfs volume rename onto an existing file fails; remove then rename replaces it", "[idfxx][fatfs]") {
    mounts_fixture mounts;
    mount_empty_storage();
    write_file("/fat/config.txt", "old");
    write_file("/fat/config.tmp", "new");

    // FatFs does not replace an existing destination
    errno = 0;
    TEST_ASSERT_EQUAL_INT(-1, std::rename("/fat/config.tmp", "/fat/config.txt"));
    TEST_ASSERT_EQUAL_INT(EEXIST, errno);
    TEST_ASSERT_EQUAL_STRING("old", read_file("/fat/config.txt").c_str());
    TEST_ASSERT_EQUAL_STRING("new", read_file("/fat/config.tmp").c_str());

    // Remove the destination first
    TEST_ASSERT_EQUAL_INT(0, std::remove("/fat/config.txt"));
    TEST_ASSERT_EQUAL_INT(0, std::rename("/fat/config.tmp", "/fat/config.txt"));
    TEST_ASSERT_EQUAL_STRING("new", read_file("/fat/config.txt").c_str());
    TEST_ASSERT_FALSE(exists("/fat/config.tmp"));
}

TEST_CASE("fatfs volume mkdir, stat and opendir", "[idfxx][fatfs]") {
    mounts_fixture mounts;
    mount_empty_storage();
    TEST_ASSERT_EQUAL_INT(0, mkdir("/fat/dir", 0755));
    TEST_ASSERT_EQUAL_INT(0, mkdir("/fat/dir/sub", 0755));
    errno = 0;
    TEST_ASSERT_EQUAL_INT(-1, mkdir("/fat/dir", 0755));
    TEST_ASSERT_EQUAL_INT(EEXIST, errno);

    struct stat st;
    TEST_ASSERT_EQUAL_INT(0, stat("/fat/dir", &st));
    TEST_ASSERT_TRUE(S_ISDIR(st.st_mode));

    write_file("/fat/dir/a.txt", "a");
    write_file("/fat/dir/sub/b.txt", "b");
    auto names = list_dir("/fat/dir");
    TEST_ASSERT_EQUAL_UINT32(2, names.size());
    TEST_ASSERT_TRUE(contains(names, "a.txt"));
    TEST_ASSERT_TRUE(contains(names, "sub"));
    TEST_ASSERT_EQUAL_STRING("b", read_file("/fat/dir/sub/b.txt").c_str());

    // A directory must be empty to be removed
    TEST_ASSERT_EQUAL_INT(-1, rmdir("/fat/dir/sub"));
    TEST_ASSERT_EQUAL_INT(0, std::remove("/fat/dir/sub/b.txt"));
    TEST_ASSERT_EQUAL_INT(0, rmdir("/fat/dir/sub"));
    TEST_ASSERT_FALSE(exists("/fat/dir/sub"));
}

#if !CONFIG_FATFS_LFN_NONE
TEST_CASE("fatfs volume long file names", "[idfxx][fatfs]") {
    mounts_fixture mounts;
    mount_empty_storage();
    const char* path = "/fat/A rather long file name.text";
    write_file(path, "long");
    TEST_ASSERT_EQUAL_STRING("long", read_file(path).c_str());
    auto names = list_dir("/fat");
    TEST_ASSERT_TRUE(contains(names, "A rather long file name.text"));
}
#endif

TEST_CASE("fatfs volume free space decreases after writing", "[idfxx][fatfs]") {
    mounts_fixture mounts;
    auto& vol = mount_empty_storage();
    auto before = vol.try_free_size();
    TEST_ASSERT_TRUE(before.has_value());

    constexpr size_t bytes = 64 * 1024;
    std::vector<char> data(bytes, 'x');
    FILE* f = std::fopen("/fat/big.bin", "w");
    TEST_ASSERT_NOT_NULL(f);
    TEST_ASSERT_EQUAL_UINT32(bytes, std::fwrite(data.data(), 1, data.size(), f));
    TEST_ASSERT_EQUAL_INT(0, std::fclose(f));

    auto after = vol.try_free_size();
    TEST_ASSERT_TRUE(after.has_value());
    TEST_ASSERT_TRUE(*after < *before);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(bytes, static_cast<uint32_t>(*before - *after));

    TEST_ASSERT_EQUAL_INT(0, std::remove("/fat/big.bin"));
    auto removed = vol.try_free_size();
    TEST_ASSERT_TRUE(removed.has_value());
    TEST_ASSERT_TRUE(*removed == *before);
#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    TEST_ASSERT_TRUE(vol.free_size() == *removed);
#endif
}

TEST_CASE("fatfs volume format clears files and stays mounted", "[idfxx][fatfs]") {
    mounts_fixture mounts;
    auto& vol = mount_empty_storage();
    write_file("/fat/keep.txt", "gone after format");
    TEST_ASSERT_EQUAL_INT(0, mkdir("/fat/dir", 0755));

    TEST_ASSERT_TRUE(vol.try_format().has_value());
    // FatFs chooses the cluster size: one sector on a volume this small
    TEST_ASSERT_EQUAL_UINT32(1, vol.idf_handle()->csize);
    TEST_ASSERT_FALSE(exists("/fat/keep.txt"));
    TEST_ASSERT_FALSE(exists("/fat/dir"));
    TEST_ASSERT_EQUAL_UINT32(0, list_dir("/fat").size());
    write_file("/fat/new.txt", "after");
    TEST_ASSERT_EQUAL_STRING("after", read_file("/fat/new.txt").c_str());

    // Format options take effect
    TEST_ASSERT_TRUE(vol.try_format({.allocation_unit_size = 8192, .use_one_fat = true}).has_value());
    TEST_ASSERT_EQUAL_INT(1, vol.idf_handle()->n_fats);
    TEST_ASSERT_EQUAL_UINT32(8192 / CONFIG_WL_SECTOR_SIZE, vol.idf_handle()->csize);
    TEST_ASSERT_FALSE(exists("/fat/new.txt"));
    TEST_ASSERT_TRUE(vol.size() > 0);
    auto free = vol.try_free_size();
    TEST_ASSERT_TRUE(free.has_value() && *free <= vol.size());

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    write_file("/fat/again.txt", "x");
    vol.format();
    TEST_ASSERT_FALSE(exists("/fat/again.txt"));
    TEST_ASSERT_EQUAL_INT(2, vol.idf_handle()->n_fats);
#endif
}

TEST_CASE("fatfs volume format rejects an invalid allocation unit size and leaves the volume alone", "[idfxx][fatfs]") {
    mounts_fixture mounts;
    auto& vol = mount_empty_storage();
    write_file("/fat/keep.txt", "kept");

    const size_t invalid[] = {
        0,
        CONFIG_WL_SECTOR_SIZE / 2,   // less than a sector
        CONFIG_WL_SECTOR_SIZE + 1,   // not a power of two
        3 * CONFIG_WL_SECTOR_SIZE,   // not a power of two
        256 * CONFIG_WL_SECTOR_SIZE, // more than 128 sectors
    };
    for (size_t au : invalid) {
        assert_error(vol.try_format({.allocation_unit_size = au}), idfxx::errc::invalid_arg);
    }
#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    bool threw = false;
    try {
        vol.format({.allocation_unit_size = 3 * CONFIG_WL_SECTOR_SIZE});
    } catch (const std::system_error& e) {
        threw = true;
        TEST_ASSERT_TRUE(e.code() == idfxx::errc::invalid_arg);
    }
    TEST_ASSERT_TRUE_MESSAGE(threw, "Expected std::system_error to be thrown");
#endif

    // Rejected before anything was unmounted or written
    TEST_ASSERT_EQUAL_STRING("kept", read_file("/fat/keep.txt").c_str());
    TEST_ASSERT_TRUE(vol.try_free_size().has_value());

    // One sector is the smallest valid size
    TEST_ASSERT_TRUE(vol.try_format({.allocation_unit_size = CONFIG_WL_SECTOR_SIZE}).has_value());
    TEST_ASSERT_EQUAL_UINT32(1, vol.idf_handle()->csize);
    TEST_ASSERT_FALSE(exists("/fat/keep.txt"));
}

TEST_CASE("fatfs volume mount rejects an invalid allocation unit size before mounting", "[idfxx][fatfs]") {
    mounts_fixture mounts;
    {
        auto& vol = mount_empty_storage();
        write_file("/fat/keep.txt", "kept");
        TEST_ASSERT_TRUE(vol.try_unmount().has_value());
    }
    const volume::flash_config bad{
        .format_if_missing = true,
        .format = {.allocation_unit_size = 3 * CONFIG_WL_SECTOR_SIZE},
    };

    // Rejected even though the partition holds a filesystem that needs no formatting
    assert_error(volume::make(STORAGE, "/fat", bad), idfxx::errc::invalid_arg);
    assert_error(volume::make(find_partition(STORAGE), "/fat", bad), idfxx::errc::invalid_arg);
    // ...and before the partition is looked up
    assert_error(volume::make("nosuchpart", "/fat", bad), idfxx::errc::invalid_arg);
    TEST_ASSERT_FALSE(exists("/fat/keep.txt"));
    {
        auto r = volume::make(STORAGE, "/fat");
        TEST_ASSERT_OK(r);
        held_volume().emplace(std::move(*r));
        TEST_ASSERT_EQUAL_STRING("kept", read_file("/fat/keep.txt").c_str());
        held_volume().reset();
    }

    // ...and on an erased partition, which is left unformatted
    erase_storage();
    assert_error(volume::make(STORAGE, "/fat", bad), idfxx::errc::invalid_arg);
    assert_error(volume::make(STORAGE, "/fat"), fatfs::errc::no_filesystem);
}

TEST_CASE("fatfs volume mount rejects invalid format settings without format_if_missing", "[idfxx][fatfs]") {
    mounts_fixture mounts;
    {
        auto& vol = mount_empty_storage();
        TEST_ASSERT_TRUE(vol.try_unmount().has_value());
    }
    // The partition holds a filesystem, and the settings would never be used
    const volume::flash_config bad{.format = {.allocation_unit_size = 3 * CONFIG_WL_SECTOR_SIZE}};
    assert_error(volume::make(STORAGE, "/fat", bad), idfxx::errc::invalid_arg);
    assert_error(volume::make(find_partition(STORAGE), "/fat", bad), idfxx::errc::invalid_arg);
}

TEST_CASE("fatfs volume format that aborts remounts the filesystem already there", "[idfxx][fatfs]") {
    mounts_fixture mounts;
    auto& vol = mount_empty_storage();
    write_file("/fat/keep.txt", "kept");
    const uint64_t size = vol.size();

    // A valid size, but 128 sectors is more than this small partition can hold
    assert_error(vol.try_format({.allocation_unit_size = 128 * CONFIG_WL_SECTOR_SIZE}), fatfs::errc::mkfs_aborted);

    TEST_ASSERT_EQUAL_STRING("kept", read_file("/fat/keep.txt").c_str());
    TEST_ASSERT_TRUE(vol.size() == size);
    TEST_ASSERT_TRUE(vol.try_free_size().has_value());
}

TEST_CASE("fatfs volume format whose remount fails leaves the volume unusable until unmounted", "[idfxx][fatfs]") {
    mounts_fixture mounts;
    {
        auto& vol = mount_empty_storage();
        write_file("/fat/keep.txt", "kept");
        const uint64_t size = vol.size();

        // The medium stops responding: formatting fails, and so does mounting it again afterward. The results are
        // only checked once the volume is unmounted, so that a failure leaves no volume on the failing driver.
        ff_diskio_register(pdrv_of(vol), &failing_disk);
        auto formatted = vol.try_format();

        // Every access now fails, without FatFs trying to mount the volume again
        auto free1 = vol.try_free_size();
        auto free2 = vol.try_free_size();
        const int open_errno = fopen_errno("/fat/keep.txt", "r");
        const uint64_t size_after = vol.size();

        // Unmounting still undoes everything (and unregisters the failing driver)
        auto unmounted = vol.try_unmount();

        assert_error(formatted, fatfs::errc::not_ready);
        assert_error(free1, fatfs::errc::not_enabled);
        assert_error(free2, fatfs::errc::not_enabled);
        TEST_ASSERT_EQUAL_INT(ENODEV, open_errno);
        TEST_ASSERT_TRUE(size_after == size);
        TEST_ASSERT_TRUE(unmounted.has_value());
    }
    // Nothing reached the partition
    auto r = volume::make(STORAGE, "/fat");
    TEST_ASSERT_OK(r);
    held_volume().emplace(std::move(*r));
    TEST_ASSERT_EQUAL_STRING("kept", read_file("/fat/keep.txt").c_str());
}

TEST_CASE("fatfs volume files persist across unmount and remount", "[idfxx][fatfs]") {
    mounts_fixture mounts;
    {
        auto& vol = mount_empty_storage();
        TEST_ASSERT_EQUAL_INT(0, mkdir("/fat/saved", 0755));
        write_file("/fat/saved/state.txt", "persisted");
        TEST_ASSERT_TRUE(vol.try_unmount().has_value());
        TEST_ASSERT_FALSE(exists("/fat/saved/state.txt"));
    }
    // Without format_if_missing: the filesystem must be there
    auto r = volume::make(STORAGE, "/fat");
    TEST_ASSERT_OK(r);
    held_volume().emplace(std::move(*r));
    TEST_ASSERT_EQUAL_STRING("persisted", read_file("/fat/saved/state.txt").c_str());

    // Also after destruction rather than an explicit unmount
    write_file("/fat/saved/second.txt", "also persisted");
    held_volume().reset();
    mount_storage("/data");
    TEST_ASSERT_EQUAL_STRING("also persisted", read_file("/data/saved/second.txt").c_str());
}

TEST_CASE("fatfs volume explicit unmount, then destruction", "[idfxx][fatfs]") {
    mounts_fixture mounts;
    {
        auto& vol = mount_empty_storage();
        TEST_ASSERT_TRUE(vol.try_unmount().has_value());
        held_volume().reset(); // destroying an unmounted volume does nothing
    }
#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    {
        auto& vol = held_volume().emplace(STORAGE, "/fat");
        vol.unmount();
        held_volume().reset();
    }
#endif
    // The drive and the path are free again
    mount_storage("/fat");
    write_file("/fat/after.txt", "ok");
    TEST_ASSERT_EQUAL_STRING("ok", read_file("/fat/after.txt").c_str());
}

TEST_CASE("fatfs volume move construction and assignment keep the mount working", "[idfxx][fatfs]") {
    mounts_fixture mounts;
    auto& vol = mount_empty_storage();
    FATFS* fs = vol.idf_handle();
    const uint64_t size = vol.size();
    write_file("/fat/moved.txt", "before move");

    // A file left open across the move stays usable
    FILE* f = std::fopen("/fat/open.txt", "w");
    TEST_ASSERT_NOT_NULL(f);

    auto& moved = held_volume(1).emplace(std::move(vol));
    TEST_ASSERT_TRUE(moved.idf_handle() == fs);
    TEST_ASSERT_TRUE(moved.size() == size);
    TEST_ASSERT_EQUAL_STRING("/fat", std::string(moved.mount_path()).c_str());
    TEST_ASSERT_EQUAL_STRING("before move", read_file("/fat/moved.txt").c_str());
    TEST_ASSERT_TRUE(std::fputs("still open", f) >= 0);
    TEST_ASSERT_EQUAL_INT(0, std::fclose(f));
    TEST_ASSERT_EQUAL_STRING("still open", read_file("/fat/open.txt").c_str());

    // Move assignment onto a moved-from volume
    vol = std::move(moved);
    TEST_ASSERT_TRUE(vol.idf_handle() == fs);
    write_file("/fat/assigned.txt", "after assignment");
    TEST_ASSERT_EQUAL_STRING("after assignment", read_file("/fat/assigned.txt").c_str());
    TEST_ASSERT_TRUE(vol.try_unmount().has_value());
}

TEST_CASE("fatfs image move construction and assignment", "[idfxx][fatfs]") {
    mounts_fixture mounts;
    // A second raw image, on `storage`: a copy of `fatimg`
    copy_fatimg_to_storage();
    auto& first = mount_image_on_storage("/img1");
    auto& second = mount_fatimg("/img2", 1);
    FATFS* fs = second.idf_handle();

    first = std::move(second); // unmounts /img1
    TEST_ASSERT_FALSE(exists("/img1/hello.txt"));
    TEST_ASSERT_TRUE(first.idf_handle() == fs);
    TEST_ASSERT_EQUAL_STRING("/img2", std::string(first.mount_path()).c_str());
    TEST_ASSERT_EQUAL_STRING("Hello from the FAT image\n", read_file("/img2/hello.txt").c_str());

    auto& moved = held_image(1).emplace(std::move(first)); // in place of the moved-from second
    TEST_ASSERT_EQUAL_STRING("Hello from the FAT image\n", read_file("/img2/hello.txt").c_str());
    TEST_ASSERT_TRUE(moved.try_unmount().has_value());
    TEST_ASSERT_FALSE(exists("/img2/hello.txt"));
}

TEST_CASE("fatfs volume honors max_files", "[idfxx][fatfs]") {
    mounts_fixture mounts;
    auto r = volume::make(STORAGE, "/fat", volume::flash_config{.max_files = 2, .format_if_missing = true});
    TEST_ASSERT_TRUE(r.has_value());
    held_volume().emplace(std::move(*r));
    FILE* a = std::fopen("/fat/a.txt", "w");
    FILE* b = std::fopen("/fat/b.txt", "w");
    TEST_ASSERT_NOT_NULL(a);
    TEST_ASSERT_NOT_NULL(b);
    errno = 0;
    TEST_ASSERT_NULL(std::fopen("/fat/c.txt", "w"));
    TEST_ASSERT_EQUAL_INT(ENFILE, errno);
    TEST_ASSERT_EQUAL_INT(0, std::fclose(a));
    FILE* c = std::fopen("/fat/c.txt", "w");
    TEST_ASSERT_NOT_NULL(c);
    TEST_ASSERT_EQUAL_INT(0, std::fclose(b));
    TEST_ASSERT_EQUAL_INT(0, std::fclose(c));
}

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
TEST_CASE("fatfs volume exception-based API", "[idfxx][fatfs]") {
    mounts_fixture mounts;
    auto part = find_partition(STORAGE);
    auto& vol = held_volume().emplace(part, "/fat", format_if_needed());
    vol.format();
    write_file("/fat/x.txt", "x");
    TEST_ASSERT_TRUE(vol.free_size() < vol.size());
    vol.unmount();

    held_volume(1).emplace(STORAGE, "/fat"); // by label
    TEST_ASSERT_EQUAL_STRING("x", read_file("/fat/x.txt").c_str());
}
#endif

// =============================================================================
// Replacing files
// =============================================================================

namespace {

// The path of one of the names replace_file() uses for `dir`/`name`, worked out here from the documented scheme: the
// FNV-1a 32-bit hash of `name` with its ASCII letters in uppercase, as 8 uppercase hex digits, then `extension`.
std::string replace_name(const char* dir, const char* name, const char* extension) {
    uint32_t hash = 0x811c9dc5;
    for (const char* c = name; *c != '\0'; ++c) {
        const char upper = (*c >= 'a' && *c <= 'z') ? static_cast<char>(*c - 'a' + 'A') : *c;
        hash = (hash ^ static_cast<uint8_t>(upper)) * 0x01000193;
    }
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%s/%08" PRIX32 "%s", dir, hash, extension);
    return buf;
}

// The temporary file, which may be incomplete.
std::string temp_name(const char* dir, const char* name) {
    return replace_name(dir, name, ".~RT");
}

// The complete file, waiting to replace the target.
std::string complete_name(const char* dir, const char* name) {
    return replace_name(dir, name, ".~RN");
}

std::span<const uint8_t> bytes_of(const char* text) {
    return {reinterpret_cast<const uint8_t*>(text), std::strlen(text)};
}

// A byte source that reads `data` at most `chunk` bytes at a time. If `failure` is set, it is returned instead of the
// end of the data.
class chunked_source {
public:
    chunked_source(std::span<const uint8_t> data, size_t chunk, std::error_code failure = {})
        : _src(data)
        , _chunk(chunk)
        , _failure(failure) {}

    result<size_t> try_read(std::span<uint8_t> buf) {
        if (_src.remaining() == 0 && _failure) {
            return error(_failure);
        }
        return _src.try_read(buf.first(std::min(buf.size(), _chunk)));
    }

private:
    memory_source _src;
    size_t _chunk;
    std::error_code _failure;
};
static_assert(byte_source<chunked_source>);

// A byte source whose try_read() is const, so that it can be passed as a const object.
struct const_readable_source {
    mutable memory_source source;

    result<size_t> try_read(std::span<uint8_t> buf) const { return source.try_read(buf); }
};
static_assert(replaceable_with<const const_readable_source&>);

// A writer that is a plain function.
result<void> write_from_function(FILE* f) {
    std::fputs("from a function", f);
    return {};
}

// Data that differs from byte to byte, larger than replace_file()'s 4 KiB buffer.
std::vector<uint8_t> pattern(size_t size = 3 * 4096 + 123) {
    std::vector<uint8_t> data(size);
    for (size_t i = 0; i < data.size(); ++i) {
        data[i] = static_cast<uint8_t>(i * 31 + i / 256);
    }
    return data;
}

std::vector<uint8_t> read_bytes(const char* path) {
    std::string text = read_file(path);
    return {text.begin(), text.end()};
}

void assert_no_leftovers(const char* dir, const char* name) {
    TEST_ASSERT_FALSE_MESSAGE(exists(temp_name(dir, name).c_str()), temp_name(dir, name).c_str());
    TEST_ASSERT_FALSE_MESSAGE(exists(complete_name(dir, name).c_str()), complete_name(dir, name).c_str());
}

} // namespace

TEST_CASE("fatfs replace_file uses the documented names", "[idfxx][fatfs]") {
    mounts_fixture mounts;
    // The hash of "CONFIG.JSON", worked out independently of both the test helper and the implementation
    TEST_ASSERT_EQUAL_STRING("/fat/189A8B29.~RT", temp_name("/fat", "config.json").c_str());
    TEST_ASSERT_EQUAL_STRING("/fat/189A8B29.~RN", complete_name("/fat", "Config.Json").c_str());

    // The names replace_file() leaves when the writer stops it are those
    auto& vol = mount_empty_storage();
    bool temp_seen = false;
    auto r = vol.try_replace_file("/fat/Config.Json", [&](FILE* f) -> result<void> {
        std::fputs("partial", f);
        std::fflush(f);
        temp_seen = exists("/fat/189A8B29.~RT");
        return error(idfxx::errc::timeout);
    });
    assert_error(r, make_error_code(idfxx::errc::timeout));
    TEST_ASSERT_TRUE(temp_seen);
    assert_no_leftovers("/fat", "config.json");
}

TEST_CASE("fatfs replace_file with contents in memory replaces a file and creates one", "[idfxx][fatfs]") {
    mounts_fixture mounts;
    auto& vol = mount_empty_storage();
    write_file("/fat/cfg.txt", "old contents");

    auto r = vol.try_replace_file("/fat/cfg.txt", bytes_of("new"));
    TEST_ASSERT_OK(r);
    TEST_ASSERT_EQUAL_STRING("new", read_file("/fat/cfg.txt").c_str());

    TEST_ASSERT_EQUAL_INT(0, mkdir("/fat/dir", 0755));
    const auto data = pattern();
    r = vol.try_replace_file("/fat/dir/data.bin", data);
    TEST_ASSERT_OK(r);
    auto back = read_bytes("/fat/dir/data.bin");
    TEST_ASSERT_EQUAL_UINT32(data.size(), back.size());
    TEST_ASSERT_EQUAL_MEMORY(data.data(), back.data(), data.size());

    // Text is written as it is, with no terminating null
    r = vol.try_replace_file("/fat/cfg.txt", std::string("text\n"));
    TEST_ASSERT_OK(r);
    TEST_ASSERT_EQUAL_UINT32(5, read_bytes("/fat/cfg.txt").size());
    TEST_ASSERT_EQUAL_STRING("text\n", read_file("/fat/cfg.txt").c_str());

    // Empty contents give an empty file
    r = vol.try_replace_file("/fat/cfg.txt", std::span<const uint8_t>{});
    TEST_ASSERT_TRUE(r.has_value());
    TEST_ASSERT_EQUAL_STRING("", read_file("/fat/cfg.txt").c_str());

    // Nothing else is left in either directory
    auto names = list_dir("/fat");
    TEST_ASSERT_EQUAL_UINT32(2, names.size());
    TEST_ASSERT_TRUE(contains(names, "cfg.txt"));
    TEST_ASSERT_TRUE(contains(names, "dir"));
    names = list_dir("/fat/dir");
    TEST_ASSERT_EQUAL_UINT32(1, names.size());
    TEST_ASSERT_TRUE(contains(names, "data.bin"));
}

TEST_CASE("fatfs replace_file from a byte source replaces a file and creates one", "[idfxx][fatfs]") {
    mounts_fixture mounts;
    auto& vol = mount_empty_storage();
    write_file("/fat/cfg.txt", "old contents");

    memory_source text{bytes_of("from a source")};
    auto r = vol.try_replace_file("/fat/cfg.txt", text);
    TEST_ASSERT_OK(r);
    TEST_ASSERT_EQUAL_STRING("from a source", read_file("/fat/cfg.txt").c_str());

    // Larger than the buffer, in short reads
    const auto data = pattern();
    chunked_source src{data, 1000};
    r = vol.try_replace_file("/fat/data.bin", src);
    TEST_ASSERT_OK(r);
    auto back = read_bytes("/fat/data.bin");
    TEST_ASSERT_EQUAL_UINT32(data.size(), back.size());
    TEST_ASSERT_EQUAL_MEMORY(data.data(), back.data(), data.size());

    // Larger than the buffer, in reads that fill it
    const auto more = pattern(5 * 4096);
    chunked_source full{more, SIZE_MAX};
    r = vol.try_replace_file("/fat/data.bin", full);
    TEST_ASSERT_TRUE(r.has_value());
    back = read_bytes("/fat/data.bin");
    TEST_ASSERT_EQUAL_UINT32(more.size(), back.size());
    TEST_ASSERT_EQUAL_MEMORY(more.data(), back.data(), more.size());

    assert_no_leftovers("/fat", "cfg.txt");
    assert_no_leftovers("/fat", "data.bin");
}

TEST_CASE("fatfs replace_file from a failing byte source keeps the old file", "[idfxx][fatfs]") {
    mounts_fixture mounts;
    auto& vol = mount_empty_storage();
    write_file("/fat/cfg.txt", "old contents");

    const auto data = pattern();
    chunked_source src{data, 4096, make_error_code(idfxx::errc::invalid_response)};
    auto r = vol.try_replace_file("/fat/cfg.txt", src);
    assert_error(r, make_error_code(idfxx::errc::invalid_response));
    TEST_ASSERT_EQUAL_STRING("old contents", read_file("/fat/cfg.txt").c_str());
    assert_no_leftovers("/fat", "cfg.txt");
}

TEST_CASE("fatfs replace_file with a writer replaces a file and creates one", "[idfxx][fatfs]") {
    mounts_fixture mounts;
    auto& vol = mount_empty_storage();
    write_file("/fat/cfg.txt", "old contents");

    // The writer runs with no lock held, so it may use the volume
    std::optional<uint64_t> free_in_writer;
    auto r = vol.try_replace_file("/fat/cfg.txt", [&](FILE* f) -> result<void> {
        auto free = vol.try_free_size();
        if (!free) {
            return error(free.error());
        }
        free_in_writer = *free;
        std::fprintf(f, "brightness=%d\n", 7);
        return {};
    });
    TEST_ASSERT_OK(r);
    TEST_ASSERT_TRUE(free_in_writer.has_value());
    TEST_ASSERT_EQUAL_STRING("brightness=7\n", read_file("/fat/cfg.txt").c_str());

    // A const writer, creating a file
    const auto writer = [](FILE* f) -> result<void> {
        std::fputs("created", f);
        return {};
    };
    r = vol.try_replace_file("/fat/new.txt", writer);
    TEST_ASSERT_TRUE(r.has_value());
    TEST_ASSERT_EQUAL_STRING("created", read_file("/fat/new.txt").c_str());

    assert_no_leftovers("/fat", "cfg.txt");
    assert_no_leftovers("/fat", "new.txt");
}

TEST_CASE("fatfs replace_file with a plain function writer and a const byte source", "[idfxx][fatfs]") {
    mounts_fixture mounts;
    auto& vol = mount_empty_storage();

    auto r = vol.try_replace_file("/fat/fn.txt", write_from_function);
    TEST_ASSERT_OK(r);
    TEST_ASSERT_EQUAL_STRING("from a function", read_file("/fat/fn.txt").c_str());
    write_file("/fat/fn.txt", "old contents");
    r = vol.try_replace_file("/fat/fn.txt", &write_from_function);
    TEST_ASSERT_OK(r);
    TEST_ASSERT_EQUAL_STRING("from a function", read_file("/fat/fn.txt").c_str());

    const const_readable_source src{memory_source{bytes_of("from a const source")}};
    r = vol.try_replace_file("/fat/src.txt", src);
    TEST_ASSERT_OK(r);
    TEST_ASSERT_EQUAL_STRING("from a const source", read_file("/fat/src.txt").c_str());

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    write_file("/fat/fn.txt", "old contents");
    vol.replace_file("/fat/fn.txt", write_from_function);
    TEST_ASSERT_EQUAL_STRING("from a function", read_file("/fat/fn.txt").c_str());
    const const_readable_source again{memory_source{bytes_of("again")}};
    vol.replace_file("/fat/src.txt", again);
    TEST_ASSERT_EQUAL_STRING("again", read_file("/fat/src.txt").c_str());
#endif

    assert_no_leftovers("/fat", "fn.txt");
    assert_no_leftovers("/fat", "src.txt");
}

TEST_CASE("fatfs replace_file whose writer fails keeps the old file", "[idfxx][fatfs]") {
    mounts_fixture mounts;
    auto& vol = mount_empty_storage();
    write_file("/fat/cfg.txt", "old contents");

    auto r = vol.try_replace_file("/fat/cfg.txt", [](FILE* f) -> result<void> {
        std::fputs("half-written", f);
        return error(idfxx::errc::invalid_crc);
    });
    assert_error(r, make_error_code(idfxx::errc::invalid_crc));
    TEST_ASSERT_EQUAL_STRING("old contents", read_file("/fat/cfg.txt").c_str());
    assert_no_leftovers("/fat", "cfg.txt");

    // Nor is a file created when there was none
    r = vol.try_replace_file("/fat/new.txt", [](FILE*) -> result<void> { return error(idfxx::errc::invalid_crc); });
    assert_error(r, make_error_code(idfxx::errc::invalid_crc));
    TEST_ASSERT_FALSE(exists("/fat/new.txt"));
    assert_no_leftovers("/fat", "new.txt");
}

TEST_CASE("fatfs replace_file that fills the volume keeps the old file", "[idfxx][fatfs]") {
    mounts_fixture mounts;
    auto& vol = mount_empty_storage();
    write_file("/fat/cfg.txt", "old contents");
    auto before = vol.try_free_size();
    TEST_ASSERT_TRUE(before.has_value());

    // The writer ignores the failed writes; the stream's error indicator still abandons the replace
    std::vector<char> chunk(4096, 'x');
    auto r = vol.try_replace_file("/fat/cfg.txt", [&](FILE* f) -> result<void> {
        for (uint64_t written = 0; written <= *before; written += chunk.size()) {
            std::fwrite(chunk.data(), 1, chunk.size(), f);
        }
        return {};
    });
    assert_error(r, std::make_error_code(std::errc::no_space_on_device));
    TEST_ASSERT_EQUAL_STRING("old contents", read_file("/fat/cfg.txt").c_str());
    assert_no_leftovers("/fat", "cfg.txt");
    auto after = vol.try_free_size();
    TEST_ASSERT_TRUE(after.has_value());
    TEST_ASSERT_TRUE(*after == *before);
}

TEST_CASE("fatfs replace_file and recover_file reject paths that don't name a file on the volume", "[idfxx][fatfs]") {
    mounts_fixture mounts;
    auto& vol = mount_empty_storage();
    TEST_ASSERT_EQUAL_INT(0, mkdir("/fat/dir", 0755));
    const auto contents = bytes_of("x");
    const std::string with_nul("/fat/a\0b.txt", 12);
    for (std::string_view path : {
             std::string_view("/other/a.txt"),
             std::string_view("/fatx/a.txt"),
             std::string_view("fat/a.txt"),
             std::string_view("a.txt"),
             std::string_view(""),
             std::string_view("/fat"),
             std::string_view("/fat/"),
             std::string_view("/fat/dir/"),
             std::string_view(with_nul),
         }) {
        assert_error(vol.try_replace_file(path, contents), make_error_code(idfxx::errc::invalid_arg));
        assert_error(vol.try_recover_file(path), make_error_code(idfxx::errc::invalid_arg));
    }
    // Nothing was written
    auto names = list_dir("/fat");
    TEST_ASSERT_EQUAL_UINT32(1, names.size());
    TEST_ASSERT_TRUE(list_dir("/fat/dir").empty());
}

TEST_CASE("fatfs replace_file in a missing directory fails with no_path; recover_file has nothing to do", "[idfxx][fatfs]") {
    mounts_fixture mounts;
    auto& vol = mount_empty_storage();
    assert_error(vol.try_replace_file("/fat/missing/cfg.txt", bytes_of("x")), make_error_code(fatfs::errc::no_path));
    TEST_ASSERT_TRUE(vol.try_recover_file("/fat/missing/cfg.txt").has_value());
    TEST_ASSERT_TRUE(list_dir("/fat").empty());
}

TEST_CASE("fatfs replace_file refuses to replace a directory", "[idfxx][fatfs]") {
    mounts_fixture mounts;
    auto& vol = mount_empty_storage();
    TEST_ASSERT_EQUAL_INT(0, mkdir("/fat/dir", 0755));
    assert_error(vol.try_replace_file("/fat/dir", bytes_of("x")), make_error_code(fatfs::errc::denied));
    struct stat st;
    TEST_ASSERT_EQUAL_INT(0, stat("/fat/dir", &st));
    TEST_ASSERT_TRUE(S_ISDIR(st.st_mode));
    assert_no_leftovers("/fat", "dir");
}

TEST_CASE("fatfs recover_file with nothing to recover leaves the file alone", "[idfxx][fatfs]") {
    mounts_fixture mounts;
    auto& vol = mount_empty_storage();
    TEST_ASSERT_TRUE(vol.try_recover_file("/fat/cfg.txt").has_value());
    TEST_ASSERT_TRUE(list_dir("/fat").empty());

    write_file("/fat/cfg.txt", "contents");
    TEST_ASSERT_TRUE(vol.try_recover_file("/fat/cfg.txt").has_value());
    TEST_ASSERT_EQUAL_STRING("contents", read_file("/fat/cfg.txt").c_str());
    TEST_ASSERT_EQUAL_UINT32(1, list_dir("/fat").size());
}

TEST_CASE("fatfs recover_file discards a temporary file", "[idfxx][fatfs]") {
    mounts_fixture mounts;
    // A reset while the new contents were written
    auto& vol = mount_empty_storage();
    write_file("/fat/cfg.txt", "old contents");
    write_file(temp_name("/fat", "cfg.txt").c_str(), "incomplete");

    TEST_ASSERT_TRUE(vol.try_recover_file("/fat/cfg.txt").has_value());
    TEST_ASSERT_EQUAL_STRING("old contents", read_file("/fat/cfg.txt").c_str());
    assert_no_leftovers("/fat", "cfg.txt");
}

TEST_CASE("fatfs recover_file puts a complete file in place of the old one", "[idfxx][fatfs]") {
    mounts_fixture mounts;
    // A reset after the new contents were complete, before the old file was removed
    auto& vol = mount_empty_storage();
    TEST_ASSERT_EQUAL_INT(0, mkdir("/fat/dir", 0755));
    write_file("/fat/dir/cfg.txt", "old contents");
    write_file(complete_name("/fat/dir", "cfg.txt").c_str(), "new contents");

    TEST_ASSERT_TRUE(vol.try_recover_file("/fat/dir/cfg.txt").has_value());
    TEST_ASSERT_EQUAL_STRING("new contents", read_file("/fat/dir/cfg.txt").c_str());
    assert_no_leftovers("/fat/dir", "cfg.txt");
    TEST_ASSERT_EQUAL_UINT32(1, list_dir("/fat/dir").size());
}

TEST_CASE("fatfs recover_file puts a complete file in place when the old one was removed", "[idfxx][fatfs]") {
    mounts_fixture mounts;
    // A reset after the old file was removed, before the final rename
    auto& vol = mount_empty_storage();
    write_file(complete_name("/fat", "cfg.txt").c_str(), "new contents");

    TEST_ASSERT_TRUE(vol.try_recover_file("/fat/cfg.txt").has_value());
    TEST_ASSERT_EQUAL_STRING("new contents", read_file("/fat/cfg.txt").c_str());
    assert_no_leftovers("/fat", "cfg.txt");
    TEST_ASSERT_EQUAL_UINT32(1, list_dir("/fat").size());
}

TEST_CASE("fatfs replace_file succeeds over the leftovers of an interrupted replace", "[idfxx][fatfs]") {
    mounts_fixture mounts;
    auto& vol = mount_empty_storage();
    write_file("/fat/cfg.txt", "old contents");
    write_file(temp_name("/fat", "cfg.txt").c_str(), "incomplete");
    write_file(complete_name("/fat", "cfg.txt").c_str(), "complete");

    auto r = vol.try_replace_file("/fat/cfg.txt", bytes_of("newest"));
    TEST_ASSERT_OK(r);
    TEST_ASSERT_EQUAL_STRING("newest", read_file("/fat/cfg.txt").c_str());
    assert_no_leftovers("/fat", "cfg.txt");
    TEST_ASSERT_EQUAL_UINT32(1, list_dir("/fat").size());
}

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
TEST_CASE("fatfs replace_file and recover_file exception-based API", "[idfxx][fatfs]") {
    mounts_fixture mounts;
    auto& vol = mount_empty_storage();
    write_file(complete_name("/fat", "cfg.txt").c_str(), "recovered");
    vol.recover_file("/fat/cfg.txt");
    TEST_ASSERT_EQUAL_STRING("recovered", read_file("/fat/cfg.txt").c_str());

    vol.replace_file("/fat/cfg.txt", bytes_of("from memory"));
    TEST_ASSERT_EQUAL_STRING("from memory", read_file("/fat/cfg.txt").c_str());

    memory_source src{bytes_of("from a source")};
    vol.replace_file("/fat/cfg.txt", src);
    TEST_ASSERT_EQUAL_STRING("from a source", read_file("/fat/cfg.txt").c_str());

    vol.replace_file("/fat/cfg.txt", [](FILE* f) -> result<void> {
        std::fputs("from a writer", f);
        return {};
    });
    TEST_ASSERT_EQUAL_STRING("from a writer", read_file("/fat/cfg.txt").c_str());

    // Errors throw
    bool threw = false;
    try {
        vol.replace_file("/fat/missing/cfg.txt", bytes_of("x"));
    } catch (const std::system_error& e) {
        threw = e.code() == fatfs::errc::no_path;
    }
    TEST_ASSERT_TRUE(threw);

    // An exception from the writer propagates, and the temporary file is removed
    threw = false;
    try {
        vol.replace_file("/fat/cfg.txt", [](FILE* f) -> result<void> {
            std::fputs("half-written", f);
            throw std::runtime_error("writer failed");
        });
    } catch (const std::runtime_error& e) {
        threw = std::strcmp(e.what(), "writer failed") == 0;
    }
    TEST_ASSERT_TRUE(threw);
    TEST_ASSERT_EQUAL_STRING("from a writer", read_file("/fat/cfg.txt").c_str());
    assert_no_leftovers("/fat", "cfg.txt");
}
#endif

// =============================================================================
// Mount table
// =============================================================================

TEST_CASE("fatfs the same mount path twice is invalid_state", "[idfxx][fatfs]") {
    mounts_fixture mounts;
    auto& vol = mount_empty_storage("/fat");
    write_file("/fat/mine.txt", "volume");

    assert_error(image::make(FATIMG, "/fat"), idfxx::errc::invalid_state);

    // The failed mount left the volume's registration alone
    TEST_ASSERT_EQUAL_STRING("volume", read_file("/fat/mine.txt").c_str());
    write_file("/fat/still.txt", "writable");
    TEST_ASSERT_TRUE(vol.try_free_size().has_value());

    // ...and released its own drive: the image mounts elsewhere, and at /fat once the volume is gone
    mount_fatimg("/img");
    TEST_ASSERT_TRUE(exists("/img/hello.txt"));
    held_image().reset();
    TEST_ASSERT_TRUE(vol.try_unmount().has_value());
    mount_fatimg("/fat");
    TEST_ASSERT_TRUE(exists("/fat/hello.txt"));
    TEST_ASSERT_FALSE(exists("/fat/mine.txt"));
}

TEST_CASE("fatfs a volume and an image together", "[idfxx][fatfs]") {
    mounts_fixture mounts;
    auto& vol = mount_empty_storage("/fat");
    auto& img = mount_fatimg("/img");
    TEST_ASSERT_TRUE(vol.drive() != img.drive());
    write_file("/fat/both.txt", "both mounted");
    TEST_ASSERT_EQUAL_STRING("Hello from the FAT image\n", read_file("/img/hello.txt").c_str());
    TEST_ASSERT_EQUAL_STRING("both mounted", read_file("/fat/both.txt").c_str());
}

TEST_CASE("fatfs a mount fails with invalid_state when all FatFs drives are in use", "[idfxx][fatfs]") {
    mounts_fixture mounts;
    mount_empty_storage("/fat");

    // Take every remaining drive outside the component, so that the next mount finds none free. The results are only
    // checked once the drives are released again, so that a failure leaves none taken.
    auto fatimg = find_partition(FATIMG);
    std::vector<BYTE> taken;
    esp_err_t register_err = ESP_OK;
    BYTE pdrv = FF_DRV_NOT_USED;
    while (ff_diskio_get_drive(&pdrv) == ESP_OK && pdrv != FF_DRV_NOT_USED) {
        register_err = ff_diskio_register_raw_partition(pdrv, fatimg.idf_handle());
        if (register_err != ESP_OK) {
            break;
        }
        taken.push_back(pdrv);
    }
    auto made = image::make(FATIMG, "/img");
    const bool img_exists = exists("/img/hello.txt");
    for (BYTE d : taken) {
        ff_diskio_unregister(d);
    }

    TEST_ASSERT_EQUAL_INT(ESP_OK, register_err);
    assert_error(made, idfxx::errc::invalid_state);
    TEST_ASSERT_FALSE(img_exists);

    // The failure left nothing behind (no claim on the partition, no registration at /img)
    mount_fatimg("/img");
    TEST_ASSERT_EQUAL_STRING("Hello from the FAT image\n", read_file("/img/hello.txt").c_str());
    write_file("/fat/still.txt", "volume still works");
    TEST_ASSERT_EQUAL_STRING("volume still works", read_file("/fat/still.txt").c_str());
}

TEST_CASE("fatfs a volume on a partition twice is invalid_state", "[idfxx][fatfs]") {
    mounts_fixture mounts;
    auto& vol = mount_empty_storage("/fat");
    write_file("/fat/first.txt", "first mount");

    assert_error(volume::make(STORAGE, "/fat2", format_if_needed()), idfxx::errc::invalid_state);
    assert_error(volume::make(find_partition(STORAGE), "/fat2"), idfxx::errc::invalid_state);
    TEST_ASSERT_FALSE(exists("/fat2/first.txt"));
    TEST_ASSERT_EQUAL_STRING("first mount", read_file("/fat/first.txt").c_str());

    // Once unmounted, the partition can be mounted again
    TEST_ASSERT_TRUE(vol.try_unmount().has_value());
    mount_storage("/fat2");
    TEST_ASSERT_EQUAL_STRING("first mount", read_file("/fat2/first.txt").c_str());
}

TEST_CASE("fatfs a volume and an image on the same partition is invalid_state", "[idfxx][fatfs]") {
    mounts_fixture mounts;
    {
        auto& vol = mount_empty_storage("/fat");
        assert_error(image::make(STORAGE, "/img"), idfxx::errc::invalid_state);
        TEST_ASSERT_TRUE(vol.try_free_size().has_value());
        held_volume().reset();
    }
    {
        // Refused before the wear-leveling layer touches the image's partition
        mount_fatimg("/img");
        assert_error(volume::make(FATIMG, "/fat", format_if_needed()), idfxx::errc::invalid_state);
        assert_error(image::make(FATIMG, "/img2"), idfxx::errc::invalid_state);
        TEST_ASSERT_EQUAL_STRING("Hello from the FAT image\n", read_file("/img/hello.txt").c_str());
        held_image().reset();
    }
    // The image is intact, and both partitions can be mounted again
    mount_fatimg("/img");
    TEST_ASSERT_EQUAL_STRING("Hello from the FAT image\n", read_file("/img/hello.txt").c_str());
    auto& vol = mount_storage("/fat");
    TEST_ASSERT_TRUE(vol.try_free_size().has_value());
}

TEST_CASE("fatfs a moved-to volume releases the partition when destroyed", "[idfxx][fatfs]") {
    mounts_fixture mounts;
    {
        auto& vol = mount_storage("/fat");
        held_volume(1).emplace(std::move(vol));
        assert_error(volume::make(STORAGE, "/fat2"), idfxx::errc::invalid_state);
        // Destroys the moved-to volume, then the moved-from one
        held_volume(1).reset();
        held_volume().reset();
    }
    auto& vol = mount_storage("/fat");
    TEST_ASSERT_TRUE(vol.try_free_size().has_value());
}

TEST_CASE("fatfs a flash volume fails with invalid_state when wear leveling has no free handle", "[idfxx][fatfs]") {
    mounts_fixture mounts;
    // Leaves valid wear-leveling state on the partition, so the mounts below only read it
    TEST_ASSERT_TRUE(mount_empty_storage().try_unmount().has_value());

    auto part = find_partition(STORAGE);
    std::vector<wl_handle_t> handles;
    esp_err_t err = ESP_OK;
    while (handles.size() < 32) {
        wl_handle_t h = WL_INVALID_HANDLE;
        err = wl_mount(part.idf_handle(), &h);
        if (err != ESP_OK) {
            break;
        }
        handles.push_back(h);
    }
    // The results are only checked once the handles are released again, so that a failure leaves none in use
    auto made = volume::make(STORAGE, "/fat", format_if_needed());
    const bool fat_exists = exists("/fat");
    esp_err_t unmount_err = ESP_OK;
    for (wl_handle_t h : handles) {
        if (esp_err_t e = wl_unmount(h); e != ESP_OK && unmount_err == ESP_OK) {
            unmount_err = e;
        }
    }

    TEST_ASSERT_EQUAL_INT(ESP_ERR_NO_MEM, err);
    TEST_ASSERT_GREATER_THAN_UINT32(0, handles.size());
    assert_error(made, idfxx::errc::invalid_state);
    TEST_ASSERT_FALSE(fat_exists);
    TEST_ASSERT_EQUAL_INT(ESP_OK, unmount_err);

    // Nothing was left mounted or claimed
    auto& vol = mount_storage("/fat");
    TEST_ASSERT_TRUE(vol.try_free_size().has_value());
}

// =============================================================================
// Read-only image
// =============================================================================

TEST_CASE("fatfs image reads the files it was built from", "[idfxx][fatfs]") {
    mounts_fixture mounts;
    auto& img = mount_fatimg("/img");
    TEST_ASSERT_EQUAL_STRING("/img", std::string(img.mount_path()).c_str());
    TEST_ASSERT_EQUAL_UINT32(2, img.drive().size());
    TEST_ASSERT_NOT_NULL(img.idf_handle());

    TEST_ASSERT_EQUAL_STRING("Hello from the FAT image\n", read_file("/img/hello.txt").c_str());
    TEST_ASSERT_EQUAL_STRING("Nested file\n", read_file("/img/docs/nested/info.txt").c_str());
#if !CONFIG_FATFS_LFN_NONE
    TEST_ASSERT_EQUAL_STRING("A file with a long name\n", read_file("/img/a file with a long name.txt").c_str());
    auto names = list_dir("/img");
    TEST_ASSERT_TRUE(contains(names, "a file with a long name.txt"));
    TEST_ASSERT_TRUE(contains(names, "hello.txt"));
    TEST_ASSERT_TRUE(contains(names, "docs"));
#endif
    struct stat st;
    TEST_ASSERT_EQUAL_INT(0, stat("/img/docs", &st));
    TEST_ASSERT_TRUE(S_ISDIR(st.st_mode));

    std::ifstream in("/img/hello.txt");
    std::string line;
    std::getline(in, line);
    TEST_ASSERT_EQUAL_STRING("Hello from the FAT image", line.c_str());

    auto part = find_partition(FATIMG);
    TEST_ASSERT_TRUE(img.size() > 0);
    TEST_ASSERT_LESS_THAN_UINT32(part.size(), static_cast<uint32_t>(img.size()));
    auto free = img.try_free_size();
    TEST_ASSERT_TRUE(free.has_value());
    TEST_ASSERT_TRUE(*free > 0 && *free < img.size());
#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    TEST_ASSERT_TRUE(img.free_size() == *free);
#endif
}

TEST_CASE("fatfs image rejects writes with EACCES", "[idfxx][fatfs]") {
    mounts_fixture mounts;
    mount_fatimg("/img");

    // Opening for writing fails at fopen(), not at the first write
    TEST_ASSERT_EQUAL_INT(EACCES, fopen_errno("/img/new.txt", "w"));
    TEST_ASSERT_EQUAL_INT(EACCES, fopen_errno("/img/hello.txt", "a"));
    TEST_ASSERT_EQUAL_INT(EACCES, fopen_errno("/img/hello.txt", "r+"));

    errno = 0;
    TEST_ASSERT_EQUAL_INT(-1, mkdir("/img/dir", 0755));
    TEST_ASSERT_EQUAL_INT(EACCES, errno);
    errno = 0;
    TEST_ASSERT_EQUAL_INT(-1, std::remove("/img/hello.txt"));
    TEST_ASSERT_EQUAL_INT(EACCES, errno);
    errno = 0;
    TEST_ASSERT_EQUAL_INT(-1, std::rename("/img/hello.txt", "/img/renamed.txt"));
    TEST_ASSERT_EQUAL_INT(EACCES, errno);

    std::ofstream out("/img/stream.txt");
    TEST_ASSERT_FALSE(out.is_open());

    // Nothing changed
    TEST_ASSERT_EQUAL_STRING("Hello from the FAT image\n", read_file("/img/hello.txt").c_str());
    TEST_ASSERT_FALSE(exists("/img/new.txt"));
}

TEST_CASE("fatfs image on a partition without an image fails", "[idfxx][fatfs]") {
    mounts_fixture mounts;
    erase_storage();
    auto r = image::make(STORAGE, "/img");
    TEST_ASSERT_FALSE(r.has_value());
    // An erased partition has no valid sector size, which FatFs reports as a disk error
    TEST_ASSERT_TRUE_MESSAGE(r.error() == fatfs::errc::disk_err || r.error() == fatfs::errc::no_filesystem,
                             r.error().message().c_str());
    // Nothing was left mounted
    mount_fatimg("/img");
    TEST_ASSERT_TRUE(exists("/img/hello.txt"));
}

TEST_CASE("fatfs image by partition object and with a config", "[idfxx][fatfs]") {
    mounts_fixture mounts;
    auto part = find_partition(FATIMG);
    auto r = image::make(part, "/img", image::flash_config{.max_files = 1});
    TEST_ASSERT_TRUE(r.has_value());
    held_image().emplace(std::move(*r));
    FILE* a = std::fopen("/img/hello.txt", "r");
    TEST_ASSERT_NOT_NULL(a);
    errno = 0;
    TEST_ASSERT_NULL(std::fopen("/img/docs/nested/info.txt", "r"));
    TEST_ASSERT_EQUAL_INT(ENFILE, errno);
    TEST_ASSERT_EQUAL_INT(0, std::fclose(a));
#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    TEST_ASSERT_TRUE(held_image()->try_unmount().has_value());
    auto& img = held_image(1).emplace(part, "/img");
    TEST_ASSERT_TRUE(exists("/img/hello.txt"));
    img.unmount();
#endif
}

// =============================================================================
// Hardware-dependent tests
// These need a card in the SD slot of the board chosen in tests/Kconfig.projbuild
// (see tests/sd_test_rig.hpp), and are ignored when its card-detect line
// reports an empty slot.
// =============================================================================

namespace {

namespace rig = idfxx::sdcard::test;

// A file of the tests' own, so that the card's other contents are left alone.
constexpr const char* SD_TEST_FILE = "/sdcard/idfxx_fatfs_test.txt";

constexpr const char* SD_TEST_TEXT = "written by the idfxx_fatfs tests";

} // namespace

TEST_CASE("fatfs volume on an SD card writes, reads and remounts", "[idfxx][fatfs][hw]") {
    IDFXX_REQUIRE_SD_CARD();
    rig::card_fixture fixture;
    mounts_fixture mounts;
    auto& card = fixture.get();

    // Never formats: the card's own filesystem is used, and only the test's file is touched
    auto r = volume::make(card, "/sdcard");
    if (!r && r.error() == fatfs::errc::no_filesystem) {
        TEST_IGNORE_MESSAGE("The card holds no FAT filesystem (the test does not format it)");
    }
    TEST_ASSERT_OK(r);
    auto& vol = held_volume().emplace(std::move(*r));
    TEST_ASSERT_TRUE(vol.size() > 0);
    TEST_ASSERT_TRUE(vol.size() <= card.size());
    auto free = vol.try_free_size();
    TEST_ASSERT_TRUE(free.has_value() && *free <= vol.size());

    write_file(SD_TEST_FILE, SD_TEST_TEXT);
    TEST_ASSERT_EQUAL_STRING(SD_TEST_TEXT, read_file(SD_TEST_FILE).c_str());

    // The card object may move while the volume is mounted
    auto& moved = moved_card().emplace(std::move(card));
    write_file(SD_TEST_FILE, " after the card moved", "a");
    TEST_ASSERT_TRUE(vol.try_unmount().has_value());

    auto again_r = volume::make(moved, "/sdcard", volume::card_config{.disk_status_check = true});
    TEST_ASSERT_OK(again_r);
    auto& again = held_volume().emplace(std::move(*again_r));
    TEST_ASSERT_EQUAL_STRING(
        (std::string(SD_TEST_TEXT) + " after the card moved").c_str(), read_file(SD_TEST_FILE).c_str()
    );
    TEST_ASSERT_EQUAL_INT(0, std::remove(SD_TEST_FILE));
    TEST_ASSERT_TRUE(again.try_unmount().has_value());
}

TEST_CASE("fatfs volume on an SD card replaces a file", "[idfxx][fatfs][hw]") {
    IDFXX_REQUIRE_SD_CARD();
    rig::card_fixture fixture;
    mounts_fixture mounts;
    auto& card = fixture.get();

    // Never formats: the card's own filesystem is used, and only the test's file is touched
    auto r = volume::make(card, "/sdcard");
    if (!r && r.error() == fatfs::errc::no_filesystem) {
        TEST_IGNORE_MESSAGE("The card holds no FAT filesystem (the test does not format it)");
    }
    TEST_ASSERT_OK(r);
    auto& vol = held_volume().emplace(std::move(*r));
    TEST_ASSERT_TRUE(vol.try_recover_file(SD_TEST_FILE).has_value());

    // Created, then replaced, with data larger than the buffer
    auto created = vol.try_replace_file(SD_TEST_FILE, bytes_of("first version"));
    TEST_ASSERT_OK(created);
    TEST_ASSERT_EQUAL_STRING("first version", read_file(SD_TEST_FILE).c_str());
    const auto data = pattern();
    chunked_source src{data, 1000};
    auto replaced = vol.try_replace_file(SD_TEST_FILE, src);
    TEST_ASSERT_OK(replaced);
    auto back = read_bytes(SD_TEST_FILE);
    TEST_ASSERT_EQUAL_UINT32(data.size(), back.size());
    TEST_ASSERT_EQUAL_MEMORY(data.data(), back.data(), data.size());
    assert_no_leftovers("/sdcard", "idfxx_fatfs_test.txt");

    TEST_ASSERT_EQUAL_INT(0, std::remove(SD_TEST_FILE));
    TEST_ASSERT_TRUE(vol.try_unmount().has_value());
}

namespace {

// Writes SD_TEST_FILE through a volume, which is unmounted again, and returns the volume's size. Returns
// std::nullopt, having written nothing, if the card holds no FAT filesystem; never formats.
std::optional<uint64_t> write_sd_test_file(sdcard::card& card) {
    auto r = volume::make(card, "/sdcard");
    if (!r && r.error() == fatfs::errc::no_filesystem) {
        return std::nullopt;
    }
    TEST_ASSERT_OK(r);
    auto& vol = held_volume().emplace(std::move(*r));
    write_file(SD_TEST_FILE, SD_TEST_TEXT);
    const uint64_t size = vol.size();
    TEST_ASSERT_TRUE(vol.try_unmount().has_value());
    held_volume().reset();
    return size;
}

// Removes SD_TEST_FILE through a volume. Nothing may be mounted on the card.
void remove_sd_test_file(sdcard::card& card) {
    auto r = volume::make(card, "/sdcard");
    TEST_ASSERT_OK(r);
    auto& vol = held_volume().emplace(std::move(*r));
    TEST_ASSERT_EQUAL_INT(0, std::remove(SD_TEST_FILE));
    TEST_ASSERT_TRUE(vol.try_unmount().has_value());
    held_volume().reset();
}

} // namespace

TEST_CASE("fatfs image on an SD card reads a file and rejects writes, without formatting", "[idfxx][fatfs][hw]") {
    IDFXX_REQUIRE_SD_CARD();
    rig::card_fixture fixture;
    mounts_fixture mounts;
    auto& card = fixture.get();
    auto vol_size = write_sd_test_file(card);
    if (!vol_size) {
        TEST_IGNORE_MESSAGE("The card holds no FAT filesystem (the test does not format it)");
    }

    // The results are only checked once the image is unmounted and the file removed, so that a failure leaves the
    // card as it was
    std::error_code mount_err;
    std::string text;
    int stat_res = -1;
    struct stat st{};
    int rewrite_errno = 0;
    int create_errno = 0;
    int mkdir_res = 0;
    int mkdir_errno = 0;
    bool created = true;
    uint64_t size = 0;
    result<uint64_t> free = 0;
    bool unmounted = false;
    if (auto r = image::make(card, "/sdcard")) {
        auto& img = held_image().emplace(std::move(*r));
        text = read_file(SD_TEST_FILE);
        stat_res = stat(SD_TEST_FILE, &st);
        rewrite_errno = fopen_errno(SD_TEST_FILE, "w");
        create_errno = fopen_errno("/sdcard/idfxx_fatfs_new.txt", "w");
        errno = 0;
        mkdir_res = mkdir("/sdcard/idfxx_fatfs_dir", 0755);
        mkdir_errno = errno;
        created = exists("/sdcard/idfxx_fatfs_new.txt") || exists("/sdcard/idfxx_fatfs_dir");
        size = img.size();
        free = img.try_free_size();
        unmounted = img.try_unmount().has_value();
    } else {
        mount_err = r.error();
    }
    held_image().reset();
    remove_sd_test_file(card);

    TEST_ASSERT_FALSE_MESSAGE(mount_err, mount_err.message().c_str());
    TEST_ASSERT_TRUE(unmounted);
    TEST_ASSERT_EQUAL_STRING(SD_TEST_TEXT, text.c_str());
    TEST_ASSERT_EQUAL_INT(0, stat_res);
    TEST_ASSERT_TRUE(S_ISREG(st.st_mode));
    TEST_ASSERT_EQUAL_INT(std::strlen(SD_TEST_TEXT), st.st_size);
    TEST_ASSERT_EQUAL_INT(EACCES, rewrite_errno);
    TEST_ASSERT_EQUAL_INT(EACCES, create_errno);
    TEST_ASSERT_EQUAL_INT(-1, mkdir_res);
    TEST_ASSERT_EQUAL_INT(EACCES, mkdir_errno);
    TEST_ASSERT_FALSE(created);
    // The same filesystem as the volume saw
    TEST_ASSERT_TRUE(size == *vol_size);
    TEST_ASSERT_TRUE(size > 0 && size <= card.size());
    TEST_ASSERT_OK(free);
    TEST_ASSERT_TRUE(*free <= size);
}

TEST_CASE("fatfs image on an SD card with disk_status_check mounts and reads", "[idfxx][fatfs][hw]") {
    IDFXX_REQUIRE_SD_CARD();
    rig::card_fixture fixture;
    mounts_fixture mounts;
    auto& card = fixture.get();
    if (!write_sd_test_file(card)) {
        TEST_IGNORE_MESSAGE("The card holds no FAT filesystem (the test does not format it)");
    }

    std::error_code mount_err;
    std::string text;
    result<uint64_t> free = 0;
    bool unmounted = false;
    const image::card_config cfg{.max_files = 1, .disk_status_check = true};
    if (auto r = image::make(card, "/sdcard", cfg)) {
        auto& img = held_image().emplace(std::move(*r));
        text = read_file(SD_TEST_FILE);
        free = img.try_free_size();
        unmounted = img.try_unmount().has_value();
    } else {
        mount_err = r.error();
    }
    held_image().reset();
    remove_sd_test_file(card);

    TEST_ASSERT_FALSE_MESSAGE(mount_err, mount_err.message().c_str());
    TEST_ASSERT_TRUE(unmounted);
    TEST_ASSERT_EQUAL_STRING(SD_TEST_TEXT, text.c_str());
    TEST_ASSERT_OK(free);
}

TEST_CASE("fatfs a second mount on an SD card with an image is invalid_state", "[idfxx][fatfs][hw]") {
    IDFXX_REQUIRE_SD_CARD();
    rig::card_fixture fixture;
    mounts_fixture mounts;
    auto& card = fixture.get();
    auto first_r = image::make(card, "/sdcard");
    if (!first_r && first_r.error() == fatfs::errc::no_filesystem) {
        TEST_IGNORE_MESSAGE("The card holds no FAT filesystem (the test does not format it)");
    }
    TEST_ASSERT_OK(first_r);
    auto& first = held_image().emplace(std::move(*first_r));
    // At another path, so that the card's claim is what refuses them
    assert_error(image::make(card, "/sd2"), idfxx::errc::invalid_state);
    assert_error(volume::make(card, "/sd2"), idfxx::errc::invalid_state);
    TEST_ASSERT_FALSE(exists("/sd2"));
    TEST_ASSERT_TRUE(first.try_free_size().has_value());
    TEST_ASSERT_TRUE(first.try_unmount().has_value());

    auto vol_r = volume::make(card, "/sdcard");
    TEST_ASSERT_OK(vol_r);
    auto& vol = held_volume().emplace(std::move(*vol_r));
    assert_error(image::make(card, "/sd2"), idfxx::errc::invalid_state);
    TEST_ASSERT_TRUE(vol.try_free_size().has_value());
    TEST_ASSERT_TRUE(vol.try_unmount().has_value());

    // Nothing was left claimed: the card mounts again
    auto again_r = image::make(card, "/sd2");
    TEST_ASSERT_OK(again_r);
    auto& again = held_image().emplace(std::move(*again_r));
    TEST_ASSERT_TRUE(again.try_free_size().has_value());
    TEST_ASSERT_TRUE(again.try_unmount().has_value());
}

TEST_CASE("fatfs image on an SD card rejects invalid arguments", "[idfxx][fatfs][hw]") {
    IDFXX_REQUIRE_SD_CARD();
    rig::card_fixture fixture;
    mounts_fixture mounts;
    auto& card = fixture.get();
    const char* invalid[] = {"", "sdcard", "/", "/sdcard/", "/abcdefghijklm"};
    for (const char* path : invalid) {
        assert_error(image::make(card, path), idfxx::errc::invalid_arg);
    }
    assert_error(image::make(card, "/sdcard", image::card_config{.max_files = 0}), idfxx::errc::invalid_arg);
#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    bool threw = false;
    try {
        image img(card, "sdcard");
    } catch (const std::system_error& e) {
        threw = true;
        TEST_ASSERT_TRUE(e.code() == idfxx::errc::invalid_arg);
    }
    TEST_ASSERT_TRUE_MESSAGE(threw, "Expected std::system_error to be thrown");
#endif
}

// DESTRUCTIVE: erases everything on the card in the test board's slot.
TEST_CASE("fatfs volume format on an SD card ERASES THE CARD", "[idfxx][fatfs][hw]") {
    IDFXX_REQUIRE_SD_CARD();
    rig::card_fixture fixture;
    mounts_fixture mounts;
    auto& card = fixture.get();
    const format_options opts{.allocation_unit_size = 16 * 1024};
    auto r = volume::make(card, "/sdcard", {.format_if_missing = true, .format = opts});
    TEST_ASSERT_OK(r);
    auto& vol = held_volume().emplace(std::move(*r));
    write_file(SD_TEST_FILE, "gone after format");

    TEST_ASSERT_TRUE(vol.try_format(opts).has_value());
    TEST_ASSERT_FALSE(exists(SD_TEST_FILE));
    TEST_ASSERT_EQUAL_UINT32(16 * 1024 / 512, vol.idf_handle()->csize);
    TEST_ASSERT_TRUE(vol.size() > card.size() / 2 && vol.size() <= card.size());

    write_file(SD_TEST_FILE, "after format");
    TEST_ASSERT_TRUE(vol.try_unmount().has_value());
    auto again = volume::make(card, "/sdcard");
    TEST_ASSERT_OK(again);
    held_volume().emplace(std::move(*again));
    TEST_ASSERT_EQUAL_STRING("after format", read_file(SD_TEST_FILE).c_str());
    TEST_ASSERT_EQUAL_INT(0, std::remove(SD_TEST_FILE));
}

// DESTRUCTIVE: erases everything on the card in the test board's slot.
TEST_CASE("fatfs volume on an SD card with no filesystem formats on mount ERASES THE CARD", "[idfxx][fatfs][hw]") {
    IDFXX_REQUIRE_SD_CARD();
    rig::card_fixture fixture;
    mounts_fixture mounts;
    auto& card = fixture.get();
    // A blank sector 0: no partition table and no boot sector.
    const std::vector<uint8_t> blank(card.sector_size(), 0);
    TEST_ASSERT_TRUE(card.try_write_sectors(0, blank).has_value());
    assert_error(volume::make(card, "/sdcard"), fatfs::errc::no_filesystem);

    // Formats: FatFs finds no filesystem, and mounting again through the probe finds the card readable. The volume is
    // then used through ESP-IDF's driver again, with the status check it was configured with.
    auto r = volume::make(card, "/sdcard", {.format_if_missing = true, .disk_status_check = true});
    TEST_ASSERT_OK(r);
    auto& vol = held_volume().emplace(std::move(*r));
    TEST_ASSERT_TRUE(vol.size() > card.size() / 2 && vol.size() <= card.size());
    write_file(SD_TEST_FILE, "formatted on mount");
    TEST_ASSERT_EQUAL_STRING("formatted on mount", read_file(SD_TEST_FILE).c_str());

    TEST_ASSERT_TRUE(vol.try_unmount().has_value());
    auto again = volume::make(card, "/sdcard");
    TEST_ASSERT_OK(again);
    held_volume().emplace(std::move(*again));
    TEST_ASSERT_EQUAL_STRING("formatted on mount", read_file(SD_TEST_FILE).c_str());
    TEST_ASSERT_EQUAL_INT(0, std::remove(SD_TEST_FILE));
}
