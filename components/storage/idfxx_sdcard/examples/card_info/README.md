# Card Info

Initializes an SD card over SPI and prints its type, identity, capacity and clock, then the start of
its first sector.

## What it demonstrates

- Setting up an SPI bus with DMA with `idfxx::spi::master_bus`
- Initializing the card on the bus with `idfxx::sdcard::card`, with its chip-select pin, an optional
  card-detect pin and a maximum clock
- Reading the card type with `type()`, and its identification (product name and revision,
  manufacturer and OEM IDs, serial number and manufacturing date) with `id()`
- Reading the capacity with `sector_count()`, `sector_size()` and `size()`, and the bus width and
  clock in use with `width()` and `frequency()`
- Reading sector 0 with `read_sectors()` and printing the start of it with the logger's
  `buffer_hex_dump()`, and its boot signature
- Catching the `std::system_error` thrown when the slot is empty or the card doesn't respond

The example uses the exception-based API (`CONFIG_COMPILER_CXX_EXCEPTIONS=y` in
`sdkconfig.defaults`).

## Hardware required

- An ESP32-S3 development board (see the note below for other targets)
- An SD card slot or module wired over SPI, and a card

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
idf.py create-project-from-example "cleishm/idfxx_sdcard:card_info"
cd card_info
```

Then build and flash it, then watch the serial output:

```bash
idf.py set-target esp32s3
idf.py -p PORT flash monitor
```

Replace `esp32s3` with your target and `PORT` with your board's serial port. Press `Ctrl-]` to exit
the monitor.
