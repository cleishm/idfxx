# idfxx_sdcard

SD and MMC/eMMC memory cards over SPI or SDMMC, with raw sector access.

📚 **[Full API Documentation](https://cleishm.github.io/idfxx/group__idfxx__sdcard.html)**

## Features

- One `card` class for cards on an SPI bus (any target) or an SDMMC host slot (esp32, esp32s3,
  esp32p4)
- Card type (SDSC, SDHC, SDXC, MMC), identification (product name, manufacturer, serial number,
  manufacturing date), capacity, sector size, bus width and clock
- SDMMC bus speed modes: default speed and high speed, plus UHS-I SDR50, DDR50 and SDR104 on esp32p4
- Card-detect and write-protect pins
- Raw sector reads and writes, serialized per card, with buffers anywhere in memory (also for a
  filesystem mounted through `idf_handle()`)
- Erase, discard and whole-card erase; sanitize on MMC/eMMC
- Configuration checked up front: invalid pins, data-pin groups, bus widths and clock limits are
  rejected before the bus or host is touched
- The card's state stays at a fixed address when the object is moved, so a filesystem mounted on it
  (for example with idfxx_fatfs) stays valid

## Requirements

- ESP-IDF 5.5 or later
- C++23 compiler
- idfxx_core, idfxx_gpio, idfxx_spi and cleishm/quanta (installed automatically)

## Installation

### ESP-IDF Component Manager

Add to your project's `idf_component.yml`:

```yaml
dependencies:
  cleishm/idfxx_sdcard:
    version: "^1.0.0"
```

Or add `idfxx_sdcard` to the `REQUIRES` list in your component's `CMakeLists.txt`.

## Usage

### Basic Example

If `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled:

```cpp
#include <idfxx/log>
#include <idfxx/sdcard>
#include <idfxx/spi/master>

using namespace quanta::frequency_literals;

try {
    // The bus must use DMA
    idfxx::spi::bus_config bus_cfg{};
    bus_cfg.mosi = idfxx::gpio_11;
    bus_cfg.miso = idfxx::gpio_13;
    bus_cfg.sclk = idfxx::gpio_12;
    idfxx::spi::master_bus bus(idfxx::spi::host_device::spi2, idfxx::spi::dma_chan::ch_auto, bus_cfg);

    // Adds its own device to the bus and initializes the card
    idfxx::sdcard::card card(bus, {
        .cs = idfxx::gpio_10,
        .card_detect = idfxx::gpio_4,
        .max_frequency = 20_MHz,
    });

    idfxx::log::info("SD", "{} card, {} MiB, {} at {} kHz", card.type(), card.size() >> 20, card.width(),
                     card.frequency().count() / 1000);

    std::vector<uint8_t> sector(card.sector_size());
    card.read_sectors(0, sector);
} catch (const std::system_error& e) {
    idfxx::log::error("SD", "Card error: {}", e.what());
}
```

On an SDMMC host slot (esp32s3 shown; on esp32 the pins are fixed and there are no pin fields):

```cpp
idfxx::sdcard::card card({
    .slot = idfxx::sdcard::card::slot::slot_1,
    .clk = idfxx::gpio_14,
    .cmd = idfxx::gpio_15,
    .d0 = idfxx::gpio_2,
    .d1 = idfxx::gpio_4,
    .d2 = idfxx::gpio_12,
    .d3 = idfxx::gpio_13, // D0-D3: a 4-bit bus
    .speed = idfxx::sdcard::bus_speed::high_speed, // 40 MHz, if the card supports it
});
```

### Result-based API

If `CONFIG_COMPILER_CXX_EXCEPTIONS` is *not* enabled, the result-based API must be used:

```cpp
auto card_result = idfxx::sdcard::card::make(bus, {
    .cs = idfxx::gpio_10,
    .card_detect = idfxx::gpio_4,
});
if (!card_result) {
    if (card_result.error() == idfxx::errc::not_found) {
        idfxx::log::warn("SD", "No card in the slot");
    } else {
        idfxx::log::error("SD", "Card init failed: {}", card_result.error().message());
    }
    return;
}
auto card = std::move(*card_result);

std::vector<uint8_t> sector(card.sector_size());
if (auto r = card.try_read_sectors(0, sector); !r) {
    idfxx::log::error("SD", "Read failed: {}", r.error().message());
}
```

## API Overview

### `card`

**Creation:**
- `make(bus, spi_config)` / `card(bus, spi_config)` - Card on an SPI bus, clocked at up to
  `spi_config::max_frequency`
- `make(sdmmc_config)` / `card(sdmmc_config)` - Card on an SDMMC host slot (targets with an SDMMC
  host)

**SDMMC speed** (`sdmmc_config::speed`, a `bus_speed`):
- `default_speed` (the default) - 20 MHz
- `high_speed` - 40 MHz: SD high speed, or MMC high-speed timing (dual data rate on a 4-bit bus,
  when the device supports it)
- `sdr50`, `ddr50`, `sdr104` - UHS-I at 100, 50 (dual data rate) and 200 MHz, on esp32p4 with
  `io_power_ldo` set (`sdr104` needs ESP-IDF 6.0 or later)
- `sdmmc_config::max_frequency` - Optional lower clock for `default_speed` and `high_speed`

**Identity and geometry:**
- `type()` - `card_type::sdsc`, `sdhc`, `sdxc` or `mmc`
- `id()` - `card_id` with `product_name()`, `manufacturer_id()`, `oem_id()`, `revision()`,
  `serial_number()`, `manufacture_date()`
- `size()`, `sector_size()`, `sector_count()` - Capacity in bytes and sectors
- `frequency()`, `width()` - Bus clock and width in use

**Slot state:**
- `is_present()` - Card-detect pin (true when none is configured)
- `is_write_protected()` - Write-protect pin (false when none is configured)
- `probe()` / `try_probe()` - Check that the card responds

**Raw sector I/O** (sizes in bytes, whole sectors only):
- `read_sectors(first, dst)` / `try_read_sectors(...)`
- `write_sectors(first, src)` / `try_write_sectors(...)`

**Erase:**
- `erase_sectors(first, count)` - Erase (trim on MMC/eMMC)
- `erase_sectors(first, count, idfxx::sdcard::discard)` - Discard, when `supports_discard()`
- `erase_all()` - Erase the whole card (trim on MMC/eMMC)
- `sanitize(timeout)` - Purge discarded data, when `supports_sanitize()` (MMC/eMMC)

**ESP-IDF interop:**
- `idf_handle()` - The `sdmmc_card_t*`, at a fixed address for the card's lifetime

**Lifetime:**
- Non-copyable and move-only; the destructor removes the SPI device or releases the SDMMC slot

## Error Handling

The component uses `idfxx::errc` codes:

| Error | Meaning |
|-------|---------|
| `errc::invalid_arg` | Invalid configuration, including an SPI bus without DMA, or a UHS-I speed without `io_power_ldo` or with `max_frequency` (checked before the card is contacted), or a non-positive sanitize timeout |
| `errc::invalid_size` | A byte count that is not whole sectors, or a sector range past the end of the card |
| `errc::invalid_state` | The SDMMC slot is already in use by another card (or, with ESP-IDF 6.0 and later, by other code), the SPI bus has no free device slot for the card, or a write or erase on a write-protected card |
| `errc::not_found` | The card-detect pin reports an empty slot |
| `errc::timeout` | The card did not respond (how a missing card shows up when there is no card-detect pin) |
| `errc::invalid_response`, `errc::invalid_crc` | Communication errors |
| `errc::not_supported` | SDIO-only card, an erase mode the card doesn't support, `bus_speed::sdr104` with an SDR104 card on an ESP32-P4 before revision 3.0, or `bus_speed::ddr50` with a DDR50 card on eight data lines |

## Important Notes

- **SPI bus needs DMA.** Create the `spi::master_bus` with `dma_chan::ch_auto` (or a specific
  channel); `make()` rejects a bus without DMA with `errc::invalid_arg`, before contacting the card.
  The card adds its own device to the bus and drives its chip select itself, acquiring the bus for
  each command, so the bus can be shared with other devices. The bus must outlive the card. The
  card's `cs`, `card_detect` and `write_protect` pins must not be any of the bus's SCLK, MOSI or
  MISO pins (this is not checked).
- **SPI clock.** `spi_config::max_frequency` (20 MHz by default, at least 400 kHz) is an upper
  bound: the card runs at the highest clock the bus supports at or below it, and never above 50 MHz
  (the SD high-speed limit). On most targets the bus supports 40, 26.7, 20, 16 MHz and so on, so 26
  MHz gives 20 MHz, and 50 MHz gives 40 MHz. On esp32, a bus on GPIO matrix pins runs at 26.7 MHz or
  less; use the SPI host's IO MUX pins for 40 MHz. With ESP-IDF 5.5.0 on esp32p4, the bus's slowest
  clock is about 470 kHz, used for any lower limit. Above 20 MHz the card is switched to high speed;
  a card without high speed runs at 20 MHz, or the next supported clock below it. Check
  `frequency()` for the clock in use.
- **Shared MISO.** Before each command, the card waits for MISO to read high (the card not busy),
  for the driver's default timeout. If another device on the bus can hold MISO low, such as an
  unpowered device on the same lines, that wait times out before every command and slows all card
  access. With ESP-IDF 5.5.3 or later, set `spi_config::miso_wait_timeout` (at most 127 ms) to a
  shorter wait, such as 1 ms. Zero sends commands without waiting, but also skips the idle clocks
  sent to the card while waiting: on a bus shared with another active device, the first command
  after that device's transfers can then fail with `errc::timeout`.
- **Raw I/O and filesystems don't mix.** While a filesystem is mounted on the card, only the
  identity and geometry accessors (`type()`, `id()`, `size()`, `sector_size()`, `sector_count()`,
  `frequency()`, `width()`), `is_present()`, `is_write_protected()` and `idf_handle()` may be used.
  `probe()`, the raw sector reads and writes and the erase operations send commands that bypass the
  filesystem's locking and caches.
- **Card removal.** A removed card must be initialized again once it is back: unmount any filesystem
  on it, destroy the `card` object, create a new one once a card is present (`make()` fails with
  `errc::not_found` while the card-detect pin reports an empty slot), then mount the filesystem
  again.
- **The card must outlive anything mounted on it.** Moving the card object is fine (its state stays
  put), but destroying it while a volume is mounted leaves the volume with a dangling card.
- **Card detect is active low only.** The card is present when the pin is low. Over SPI the internal
  pull-up is enabled; on an SDMMC slot the pin needs an external pull-up.
- **SDMMC availability and pins.** SDMMC is available on esp32, esp32s3 and esp32p4 only
  (`SOC_SDMMC_HOST_SUPPORTED`). On esp32s3 every pin is routed through the GPIO matrix. On esp32p4,
  slot 1 uses the GPIO matrix but slot 0 only works on its dedicated pins (CLK 43, CMD 44, D0-D3
  39-42, D4-D7 45-48). On esp32 both slots use fixed pins, so `sdmmc_config` has no pin fields; slot
  0's pins (GPIO 6-11) are the SPI flash pins on most modules, which leaves slot 1 (CLK 14, CMD 15,
  D0 2, D1 4, D2 12, D3 13). On esp32, `card_detect` and `write_protect` must not be any of the
  slot's pins that the bus width uses (a 1-bit bus leaves D1-D3 free). The CMD and data lines need
  external pull-ups.
- **Sharing the SDMMC host.** The host is initialized on first use and shared between slots, also
  with other code: a card and, say, `esp_vfs_fat_sdmmc_mount` can use different slots at the same
  time, with any ESP-IDF version. A slot that other code has already set up must not be used for a
  card. ESP-IDF 6.0 and later reject it with `errc::invalid_state`; ESP-IDF 5.5 doesn't detect it,
  and the card takes the slot over from the other code.
- **SDMMC bus width** follows the data pins supplied: D0 alone, D0-D3 or D0-D7. A `width` wider than
  the pins allow is rejected; a narrower one leaves the extra pins unused.
- **SDMMC speed is a mode, not just a clock.** `speed` picks the timing the card is switched to, and
  with it the clock; `max_frequency` can only lower the clock of `default_speed` or `high_speed` (in
  high speed, 20 MHz or less keeps an SD card in default speed). A card that doesn't support the
  requested mode runs in a slower one, without an error (see `bus_speed`); check `frequency()`. The
  UHS-I modes need `io_power_ldo` (channel 4 on Espressif's ESP32-P4 boards).
- **SPI erase quirks.** SD cards on an SPI bus don't support discard. Some SPI-mode cards don't
  respond correctly after an erase until they are initialized again, by destroying the card object
  and creating a new one.
- **Shared buses.** If the card shares its bus with other devices, those devices must not hold their
  chip selects low while the card is idle.
- **Power and sleep.** The component doesn't switch the card's power; on boards with a power-enable
  pin, drive it with an `idfxx::gpio` and let the supply settle before creating the card. Light
  sleep can cause card reads to time out. Before deep sleep, unmount any filesystem, destroy the
  card and cut its power.
- **SDIO-only cards** are rejected with `errc::not_supported`. With `CONFIG_SD_ENABLE_SDIO_SUPPORT`
  disabled (ESP-IDF 6.0 and later), ESP-IDF skips SDIO detection and such a card fails
  initialization with another error.
- **SDXC** is reported for SD cards of 32 GiB (2^35 bytes) or more: these identify themselves as
  SDHC, and are told apart by size.

## License

Apache License 2.0 - see [LICENSE](LICENSE) for details.
