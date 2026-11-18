# SD Card

Mounts the FAT filesystem on an SD card over SPI, saves a file safely and reads it back, prints the
free space, then unmounts it.

## What it demonstrates

- Setting up an SPI bus with DMA with `idfxx::spi::master_bus`, and an `idfxx::sdcard::card` on it
- Mounting the card's filesystem at `/sdcard` with `idfxx::fatfs::volume`, formatting the card if it
  has no FAT filesystem (`format_if_missing`)
- Finishing or discarding, at startup, a replace of `hello.txt` that a reset interrupted, with
  `recover_file()`
- Replacing `hello.txt` with `replace_file()`, given a callback that writes the new contents with
  `std::fputs()`, so that a reset at any point leaves either the old contents or the new once
  `recover_file()` has run. On the way, the new contents are written as `3F76F00D.~RT` and renamed
  to `3F76F00D.~RN` once complete. A reset in the middle of one of the renames is beyond this: see
  the component README
- Reading the file back with `std::ifstream`
- Reporting the volume's capacity and free space with `size()` and `free_size()`
- Unmounting explicitly with `unmount()` once every file is closed, so that a failure is reported
  rather than only logged by the destructor
- Catching the `std::system_error` thrown when the slot is empty or the card can't be mounted

The example uses the exception-based API (`CONFIG_COMPILER_CXX_EXCEPTIONS=y` in
`sdkconfig.defaults`). `sdkconfig.defaults` also enables long file names (`CONFIG_FATFS_LFN_HEAP=y`,
the default from ESP-IDF 6.0); the example's file (`hello.txt`) and the names `replace_file()` uses
for it have 8.3 names, which work without them.

## Hardware required

- An ESP32-S3 development board (see the note below for other targets)
- An SD card slot or module wired over SPI, and a card. A card without a FAT filesystem (including
  an exFAT card, as most cards over 32 GB are formatted) is reformatted, which erases it.

Wire the card to the board as follows, plus power and ground:

| Card signal | ESP32 GPIO |
| ----------- | ---------- |
| MOSI        | GPIO 11    |
| SCLK        | GPIO 12    |
| MISO        | GPIO 13    |
| CS          | GPIO 10    |

To use different pins, change the `PIN_*` constants at the top of `main/main.cpp`; keep the card on
an SPI bus with DMA. If your slot has a card-detect switch that pulls low when a card is in, wire it
to a free GPIO and set `PIN_DETECT` to it.

> [!NOTE]
> The default pins are the ESP32-S3's SPI2 IOMUX pins, and also suit the ESP32-S2 and ESP32-H2. On
> other targets, change them first:
> - ESP32: GPIO 10 and 11 are connected to the module's SPI flash, and GPIO 12 is a strapping pin.
> - ESP32-C3: GPIO 12 and 13 are usually connected to the SPI flash.
> - ESP32-C6: GPIO 12 and 13 are used by USB-JTAG, and GPIO 10 and 11 aren't led out on chips with
>   in-package flash.

## How to use

Create a project from this example:

```bash
idf.py create-project-from-example "cleishm/idfxx_fatfs:sd_card"
cd sd_card
```

Then build and flash it, then watch the serial output:

```bash
idf.py set-target esp32s3
idf.py -p PORT flash monitor
```

Replace `esp32s3` with your target and `PORT` with your board's serial port. Press `Ctrl-]` to exit
the monitor.
