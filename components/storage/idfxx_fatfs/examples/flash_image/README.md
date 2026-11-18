# Flash Image

Builds a read-only FAT image of a directory into the firmware, then mounts it, lists it and prints
one of its files.

## What it demonstrates

- Generating a FAT image of the `data/` directory at build time with `fatfs_create_rawflash_image()`
  in `main/CMakeLists.txt`, and flashing it to the `assets` partition with the app
  (`FLASH_IN_PROJECT`)
- A partition table with an `assets` partition (type `data`, subtype `fat`) for the image, in
  `partitions.csv`
- Mounting the image by its partition label at `/assets` with `idfxx::fatfs::image`
- Listing a directory with `opendir()` and `readdir()`, and reading a file with `std::ifstream`
- Opening a file for writing failing, as the image is read-only
- Catching the `std::system_error` thrown when the image can't be mounted

The example uses the exception-based API (`CONFIG_COMPILER_CXX_EXCEPTIONS=y` in
`sdkconfig.defaults`).

## Hardware required

- Any ESP32-series development board

The image holds the files in `data/`; change them and rebuild to change the image. The `assets`
partition is 128 KB, set in `partitions.csv`. `sdkconfig.defaults` selects the partition table and
enables long file names.

## How to use

Create a project from this example:

```bash
idf.py create-project-from-example "cleishm/idfxx_fatfs:flash_image"
cd flash_image
```

Then build and flash it, then watch the serial output:

```bash
idf.py set-target esp32
idf.py -p PORT flash monitor
```

Replace `esp32` with your target and `PORT` with your board's serial port. Press `Ctrl-]` to exit
the monitor.
