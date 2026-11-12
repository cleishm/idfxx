# idfxx_fatfs

FAT filesystems on SD cards and flash partitions, used through standard file I/O.

📚 **[Full API Documentation](https://cleishm.github.io/idfxx/group__idfxx__fatfs.html)**

## Features

- `volume`: a writable FAT filesystem on an `idfxx::sdcard::card` (over SPI or SDMMC) or on a
  wear-leveled flash partition, found by its label or as an `idfxx::partition`
- `image`: a read-only FAT volume, either a prebuilt image on a flash partition (such as one built
  from a directory with `fatfs_create_rawflash_image()`) or the filesystem on an SD card, including
  a write-protected one
- Files are used through the C and C++ standard libraries (`fopen()`, `std::fstream`, POSIX
  `open()`, `stat()`, `opendir()`, ...) under the mount path
- Optional formatting when a mount finds no filesystem, and `format()` to erase a mounted volume
- Capacity (`size()`) and free space (`free_size()`)
- Safe file replacement: `replace_file()` writes new contents (bytes or text in memory, an
  `idfxx::byte_source` or a callback writing to a `FILE*`) so that a reset leaves either the old
  contents or the new, and `recover_file()` finishes or discards at startup a replace that a reset
  interrupted
- Explicit `unmount()` that reports errors (such as a failure to save the wear-leveling state); the
  destructor unmounts otherwise
- Arguments checked up front: an invalid mount path, label or partition type fails before anything
  is mounted
- `fatfs::errc` for FatFs's result codes, comparable with `std::errc`, and `fatfs_error()` for
  direct FatFs calls

## Requirements

- ESP-IDF 5.5 or later
- C++23 compiler
- idfxx_core, idfxx_partition and idfxx_sdcard (installed automatically)

## Installation

### ESP-IDF Component Manager

Add to your project's `idf_component.yml`:

```yaml
dependencies:
  cleishm/idfxx_fatfs:
    version: "^1.0.0"
```

Or add `idfxx_fatfs` to the `REQUIRES` list in your component's `CMakeLists.txt`.

A flash volume needs a data partition of subtype `fat` in the partition table:

```csv
# Name,   Type, SubType, Offset, Size, Flags
storage,  data, fat,     ,       1M,
```

## Usage

### Basic Example

If `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled:

```cpp
#include <idfxx/fatfs>
#include <idfxx/log>

#include <cstdio>
#include <fstream>

try {
    // The `storage` partition, mounted at /data; formatted if it holds no filesystem
    idfxx::fatfs::volume vol("storage", "/data", {
        .format_if_missing = true,
    });

    {
        std::ofstream out("/data/hello.txt");
        out << "Hello\n";
    }
    idfxx::log::info("FS", "{} of {} bytes free", vol.free_size(), vol.size());

    // Replace a file so that a reset leaves the old contents or the new; at startup, recover_file() first
    vol.recover_file("/data/config.txt");
    vol.replace_file("/data/config.txt", [](FILE* f) -> idfxx::result<void> {
        std::fputs("brightness=7\n", f);
        return {};
    });

    vol.unmount(); // all files closed first
} catch (const std::system_error& e) {
    idfxx::log::error("FS", "Filesystem error: {}", e.what());
}
```

On an SD card (see idfxx_sdcard for setting up the card):

```cpp
idfxx::sdcard::card card(bus, {.cs = idfxx::gpio_10, .card_detect = idfxx::gpio_4});
idfxx::fatfs::volume vol(card, "/sdcard", {
    .format_if_missing = true,
});
```

A read-only image, built from a directory by the project
(`fatfs_create_rawflash_image(assets ../assets FLASH_IN_PROJECT)` in a component's
`CMakeLists.txt`):

```cpp
idfxx::fatfs::image assets("assets", "/assets");
std::ifstream in("/assets/index.html");
```

An SD card mounted read-only, which also works for a write-protected card:

```cpp
idfxx::fatfs::image photos(card, "/sdcard");
std::ifstream in("/sdcard/photos/list.txt");
```

### Result-based API

If `CONFIG_COMPILER_CXX_EXCEPTIONS` is *not* enabled, the result-based API must be used:

```cpp
auto vol_result = idfxx::fatfs::volume::make("storage", "/data", {
    .format_if_missing = true,
});
if (!vol_result) {
    idfxx::log::error("FS", "Mount failed: {}", vol_result.error().message());
    return;
}
auto vol = std::move(*vol_result);

if (auto free = vol.try_free_size()) {
    idfxx::log::info("FS", "{} bytes free", *free);
}

// At startup, before reading the file; then replace it
if (auto r = vol.try_recover_file("/data/config.txt"); !r) {
    idfxx::log::error("FS", "Recovery failed: {}", r.error().message());
}
static constexpr uint8_t config[] = {'o', 'n', '\n'};
if (auto r = vol.try_replace_file("/data/config.txt", config); !r) {
    idfxx::log::error("FS", "Replace failed: {}", r.error().message());
}

if (auto r = vol.try_unmount(); !r) {
    idfxx::log::error("FS", "Unmount failed: {}", r.error().message());
}
```

## API Overview

### `volume`

**Creation** (mount paths start with '/' and are at most 13 characters):
- `make(card, mount_path)` / `make(card, mount_path, card_config)` / `volume(card, ...)` - On an SD
  card
- `make(partition, mount_path)` / `make(partition, mount_path, flash_config)` /
  `volume(partition, ...)` - On a wear-leveled data/fat partition
- `make(label, mount_path)` / `make(label, mount_path, flash_config)` / `volume(label, ...)` - On
  the partition with that label

**Configuration:**
- `card_config`: `max_files`, `format_if_missing`, `format`, `disk_status_check`
- `flash_config`: `max_files`, `format_if_missing`, `format`
- `format_options`: `allocation_unit_size`, `use_one_fat`

**Accessors:**
- `mount_path()`, `drive()` - The mount path, and the FatFs drive ("0:") for direct `f_*` calls
- `idf_handle()` - The `FATFS*`
- `size()` - Capacity in bytes, determined at mount
- `free_size()` / `try_free_size()` - Free space in bytes

**Operations:**
- `format(opts)` / `try_format(opts)` - Erase the volume and create an empty filesystem; stays
  mounted
- `unmount()` / `try_unmount()` - Unmount now, reporting errors

**Replacing files** (paths are full paths under the mount path, such as "/sdcard/config.json"):
- `replace_file(path, contents)` / `try_replace_file(path, contents)` - Replace a file with bytes
  (an `idfxx::byte_view`: any contiguous `std::byte` or `uint8_t` buffer), so that a reset leaves
  either the old contents or the new
- `replace_file(path, text)` / `try_replace_file(path, text)` - The same, with the characters of a
  `std::string_view`
- `replace_file(path, source)` / `try_replace_file(path, source)` - The same, reading an
  `idfxx::byte_source` to its end through a 4 KiB buffer
- `replace_file(path, writer)` / `try_replace_file(path, writer)` - The same, with what `writer`
  (invocable as `idfxx::result<void>(FILE*)`) writes; an error it returns abandons the replace
- `recover_file(path)` / `try_recover_file(path)` - Finish or discard a replace that a reset
  interrupted; call at startup, before reading the file

**Lifetime:**
- Non-copyable and move-only; the destructor unmounts if still mounted

### `image`

**Creation** (read-only; mount paths as for `volume`):
- `make(card, mount_path)` / `make(card, mount_path, card_config)` / `image(card, ...)` - On an SD
  card
- `make(partition, mount_path)` / `make(partition, mount_path, flash_config)` /
  `image(partition, ...)` - On a data/fat partition holding a raw FAT image
- `make(label, mount_path)` / `make(label, mount_path, flash_config)` / `image(label, ...)` - On the
  partition with that label

**Configuration:**
- `card_config`: `max_files`, `disk_status_check`
- `flash_config`: `max_files`

**Accessors:**
- `mount_path()`, `drive()`, `idf_handle()`, `size()`, `free_size()` / `try_free_size()`

**Operations:**
- `unmount()` / `try_unmount()`

**Lifetime:**
- Non-copyable and move-only; the destructor unmounts if still mounted

### Errors

- `fatfs::errc` - FatFs's result codes
- `fatfs_category()`, `make_error_code(errc)`
- `fatfs_error(FRESULT)` - For the results of direct FatFs calls

## Error Handling

Errors from FatFs itself are `idfxx::fatfs::errc` values. The disk drivers report every card and
wear-leveling error to FatFs as a plain I/O failure, so a medium error while mounting or using files
shows up as `fatfs::errc::disk_err`. Only argument errors, the checks made on a card before
mounting, mounting and unmounting the wear-leveling layer and registering with the virtual
filesystem give `idfxx::errc` values.

| Error | Meaning |
|-------|---------|
| `idfxx::errc::invalid_arg` | Invalid mount path, `max_files` of 0, an invalid `allocation_unit_size`, a partition that isn't data/fat, (for a volume) a partition marked read-only, or a path given to `replace_file()` or `recover_file()` that doesn't name a file under the mount path |
| `idfxx::errc::not_found` | No partition with the given label |
| `idfxx::errc::invalid_state` | (For a volume) the card is write-protected, the card or partition already has a volume or image mounted on it, a volume or image is already mounted at the path, or a table is full: FatFs's drives (`CONFIG_FATFS_VOLUME_COUNT`, 2 by default), the virtual filesystem's entries (`CONFIG_VFS_MAX_COUNT`, 8 by default) or the wear-leveling layer's handles (8, flash volumes only) |
| `fatfs::errc::no_filesystem` | No recognizable FAT filesystem on the medium (including exFAT and a damaged boot sector), and (for a volume) `format_if_missing` not set |
| `fatfs::errc::disk_err` | An I/O error on the medium; also an image partition with no valid FAT boot sector (for example, an erased one) |
| `fatfs::errc::not_ready` | The card did not respond (for example, it was removed) |
| `fatfs::errc::not_enabled` | From `try_free_size()`, after a `format()` whose remount failed |
| `fatfs::errc::mkfs_aborted` | Formatting failed: the medium doesn't suit FAT with the given allocation unit size |
| `fatfs::errc::no_path` | From `replace_file()`: the file's directory doesn't exist |
| `fatfs::errc::denied` | From `replace_file()`: the path names a directory or a read-only file |

`replace_file()` also reports the `errno` from creating, writing or closing its temporary file, in
`std::generic_category()` (for example `ENOSPC` when the volume is full), and passes on unchanged an
error that a byte source or a writer callback returns.

FatFs codes with a POSIX equivalent compare equal to it
(`fatfs::errc::no_file == std::errc::no_such_file_or_directory`, `exist == file_exists`,
`denied == permission_denied`, `write_protected == read_only_file_system`, and so on; the full table
is in the API documentation). File operations through the C and POSIX APIs report `errno` instead.

## Important Notes

- **Volume count.** FatFs has `CONFIG_FATFS_VOLUME_COUNT` drives (2 by default), shared by every
  volume and image (and any other FatFs user). Mounting one more fails with
  `idfxx::errc::invalid_state`; raise the setting to mount more at once. Each mount also takes one
  of the virtual filesystem's `CONFIG_VFS_MAX_COUNT` entries (8 by default, shared with the console,
  sockets and other filesystems), and each flash volume one of the wear-leveling layer's 8 handles;
  when either runs out, mounting fails with `idfxx::errc::invalid_state` too.
- **Mount paths** must start with '/', must not end with '/', and are limited to 13 characters, two
  fewer than the virtual filesystem's `ESP_VFS_PATH_MAX`: ESP-IDF's FAT layer stores the path in a
  buffer that truncates longer paths, after which they can't be unmounted.
- **Long file names** are off by default before ESP-IDF 6.0 (8.3 names only: up to eight characters,
  a dot and up to three more, so `config.txt.tmp` is not valid). Enable them with
  `CONFIG_FATFS_LFN_HEAP=y` (or `CONFIG_FATFS_LFN_STACK`); ESP-IDF 6.0 and later enable them by
  default. The setting also controls whether `fatfs_create_rawflash_image()` keeps long names.
- **No exFAT.** ESP-IDF's FatFs is built without exFAT, so an exFAT card (most SDXC cards, 64 GB and
  up, come formatted that way) fails to mount with `fatfs::errc::no_filesystem`. With
  `format_if_missing` set, such a card is reformatted as FAT32, erasing it. That holds for a card
  with a partition table, the usual layout. On a card without one, FatFs takes part of the exFAT
  boot sector for partition entries, and depending on its contents the mount can fail with
  `fatfs::errc::disk_err` instead, leaving the card as it is; clearing the card's first sector (with
  `sdcard::card::write_sectors()`, before mounting) then lets `format_if_missing` reformat it.
- **Format allocation units.** By default (`format_options::allocation_unit_size` left as
  `std::nullopt`) FatFs chooses the cluster size from the volume's size. An explicit size must be a
  power of two from one sector to 128 sectors (512 bytes to 64 KiB on an SD card,
  `CONFIG_WL_SECTOR_SIZE` to 128 times that on flash); anything else fails with
  `idfxx::errc::invalid_arg` before the medium is touched, both in `format()` and when mounting a
  volume, whose configuration's `format` is checked whether or not `format_if_missing` is set. A
  very small size on a large card can exceed FAT32's cluster limit, and a large one on a small
  volume can leave too few clusters; either fails the format with `mkfs_aborted`.
- **Formatting when mounting.** With `format_if_missing` set, a mount that finds no recognizable FAT
  filesystem (including a damaged boot sector) formats the medium with the configuration's `format`
  settings, losing its contents. A medium that can't be read is never formatted: the mount is first
  tried again, and fails with `fatfs::errc::disk_err` if a read of the medium fails. (When the
  second try finds the filesystem, it is mounted.) Damage beyond the boot sector (in the FAT or a
  directory) isn't detected when mounting: the mount succeeds, and the file operations that meet it
  fail.
- **The card must outlive the volume or image.** A volume or image on a card uses the card but
  doesn't own it. The card object can be moved while mounted (its state stays put), but its
  `probe()`, raw sector and erase operations must not be used while it is mounted, as they bypass
  the filesystem; its accessors, `is_present()` and `is_write_protected()` are fine.
- **Write-protected cards** are rejected by `volume` with `idfxx::errc::invalid_state`: FatFs isn't
  told about the card's write-protect switch, so a write-protected card can't be mounted as a
  writable volume. Mount it read-only with `image` instead.
- **Card removal.** Without `card_config::disk_status_check` (in `volume::card_config` and
  `image::card_config`), file operations on a removed card fail with `EIO`. With it, calls that take
  a path (`fopen()`, `stat()`, `opendir()`, ...) fail with `ENODEV`, and operations on files that
  were already open with `EBADF`. Either way the card must be set up again: unmount the volume or
  image, destroy the `card` object, create a new one once a card is present, then mount again.
- **Close files first.** Close every file and directory on a volume or image before formatting or
  unmounting it (or letting it be destroyed). While `format()` runs, no other task may use the
  volume's files; `free_size()` waits for the format to finish, while `size()` returns at once, with
  the size from before the format until the new filesystem is mounted.
- **Memory per open file.** Each of a volume's `max_files` slots is allocated when it is mounted,
  whether or not a file is open in it. With `CONFIG_FATFS_PER_FILE_CACHE` (the default), each open
  file also has a sector buffer:
  - With `CONFIG_FATFS_USE_DYN_BUFFERS` (the default from ESP-IDF 6.0), the buffer is allocated when
    the file is opened and freed when it is closed, and is one sector of the volume: 512 bytes on an
    SD card, `CONFIG_WL_SECTOR_SIZE` on flash.
  - Without it (the default in ESP-IDF 5.5), every slot holds a buffer of `FF_MAX_SS` bytes from the
    mount on: the larger of 512 and `CONFIG_WL_SECTOR_SIZE`, so 4096 by default, even on an SD card.
    The default `max_files` of 5 then costs about 20 KiB per volume.

  Lower `max_files` to what the program needs.
- **Large sequential files.** C `FILE*` streams on a FAT volume are buffered 128 bytes at a time by
  default, which makes reading or writing files of hundreds of kilobytes slow. Give such a stream a
  larger buffer with `setvbuf()` (for example 4096 bytes or more), or raise the default for every
  stream with `CONFIG_FATFS_VFS_FSTAT_BLKSIZE`.
- **Replacing a file safely.** FatFs's rename fails with `EEXIST` when the destination exists, so a
  file can't be replaced by renaming another over it. `volume::replace_file()` replaces it in steps,
  so that a reset at any point leaves either the old contents or the new once `recover_file()` has
  run: it writes the new contents to a temporary file and closes it (which writes it to the medium),
  renames that to a second name, which marks it complete, removes the old file, then renames the
  complete file into its place. Call `recover_file()` at startup, before reading the file, for each
  file you replace this way: it finishes a replace whose new contents were complete, and discards an
  incomplete one. Each `replace_file()` also runs it first, so leftovers never get in the way. The
  directory must already exist, and the file must not be open while it is replaced. Replacing the
  same path from two tasks at once is not supported. No lock is held while the contents are written,
  so a writer callback may use the volume (calling `free_size()`, for example).
- **Names of a replace in progress.** The temporary file is `<HASH>.~RT` and the complete one
  `<HASH>.~RN`, in the same directory as the file, where `<HASH>` is the FNV-1a 32-bit hash of the
  file's name (its last path component, with ASCII letters folded to uppercase first) as 8 uppercase
  hex digits: `config.json` gives `189A8B29.~RT` and `189A8B29.~RN`. Both are 8.3 names, valid with
  or without long file names. Seen on a PC, such files are what a reset left of a replace: a `.~RN`
  file holds complete new contents, and a `.~RT` file may be incomplete. Two files in one directory
  whose names hash alike (a one-in-four-billion chance for any pair) would share these names, and
  recovering one could then put the other's new contents in its place.
- **A reset during a rename isn't covered.** A reset in the middle of one of the renames can corrupt
  the file when FatFs places the new directory entry in a different sector from the old one: it
  writes the new entry and then removes the old one, so a reset between the two writes leaves both
  names on the same data, and removing either one (as recovery does) frees data the other still
  uses. FAT can't detect or repair this short of a filesystem check.
- **Images are read-only.** Opening a file on an image for writing (`fopen()` with "w", "a" or
  "r+"), and creating, removing or renaming files and directories, fail with `EACCES`; `fopen()`
  returns NULL rather than failing at the first write. On an SD card, nothing is written to the
  card, and it is never formatted. An image on a flash partition must have a sector size FatFs
  supports, between 512 bytes and `CONFIG_WL_SECTOR_SIZE` (4096 by default);
  `fatfs_create_rawflash_image()` uses `CONFIG_FATFS_SECTOR_SIZE`, which matches by default.
- **Wear-leveled vs raw partitions.** A `volume` on a flash partition goes through wear leveling,
  whose state occupies a few sectors of the partition; an `image` is read directly. A partition
  written by `fatfs_create_rawflash_image()` is for `image`; `fatfs_create_spiflash_image()` makes
  one for `volume`.
- **One mount per card or partition.** A card or partition with a volume or image mounted on it
  can't be mounted again (as either) until that one is unmounted: the second mount fails with
  `idfxx::errc::invalid_state`.
- **Power and sleep.** Unmount the volume or image, then destroy the card, before cutting a card's
  power or entering deep sleep. On a flash volume, unmounting saves the wear-leveling state.
- **The SD card component is always linked.** `<idfxx/fatfs>` includes `<idfxx/sdcard>`, so
  idfxx_sdcard (and its dependencies) is part of every build, even one that only uses flash
  partitions; the linker drops what isn't used.

## License

Apache License 2.0 - see [LICENSE](LICENSE) for details.
