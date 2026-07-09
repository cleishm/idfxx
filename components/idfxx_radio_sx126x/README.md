# idfxx_radio_sx126x

Semtech SX126x family LoRa radio driver — SX1261, SX1262, and SX1268.

📚 **[Full API Documentation](https://cleishm.github.io/idfxx/group__idfxx__radio.html)**

## Features

- One driver covers the SX1261, SX1262, and SX1268; the right power-amplifier
  configuration is selected via a `chip_variant` enum and output power is
  validated against the variant's supported range (including the SX1261's
  special +15 dBm PA setting).
- Implements the chip-agnostic `idfxx::radio::transceiver` interface from
  `idfxx_radio`, so application code, higher-layer protocols, and event
  listeners can target any LoRa chip.
- Three coordinated API styles: blocking (`transmit`/`receive`/`scan_channel`),
  future-based one-shot async (`start_transmit`, single-shot
  `start_receive(buffer)`, and `start_channel_scan`, each returning an
  `idfxx::future` — no event loop required), and event-loop dispatch for
  packet streams (continuous/duty-cycled `start_receive` + `read_received`).
- Low-power operation: duty-cycled receive (`start_receive(rx, sleep)`) and
  channel-activity detection for listen-before-talk.
- Optional TCXO control via DIO3, DIO2-as-RF-switch or external `rxen`/`txen`
  RF-switch GPIOs, configurable regulator (LDO / DC-DC), warm-start sleep, and
  CAD with user-tunable detection thresholds.
- Low-level opcode escape hatch (`write_command`, `read_command`,
  `write_register`, `read_register`, `write_buffer`, `read_buffer`) plus the
  chip's IRQ-register surface for advanced/diagnostic use.

## Requirements

- ESP-IDF 5.5 or later
- C++23 compiler
- `idfxx_radio` (provides the `radio::transceiver` base class)
- `idfxx_spi` (SPI master bus driver)
- `idfxx_gpio` (BUSY / DIO1 / NRESET / CS pins)

## Installation

### ESP-IDF Component Manager

Add to your project's `idf_component.yml`:

```yaml
dependencies:
  idfxx_radio_sx126x:
    version: "^1.0.0"
```

Or add `idfxx_radio_sx126x` to the `REQUIRES` list in your component's
`CMakeLists.txt`.

## Usage

### Basic Example (exception-based)

```cpp
#include <idfxx/gpio>
#include <idfxx/radio/sx126x>
#include <idfxx/spi/master>

using namespace frequency_literals;
using namespace std::chrono_literals;

idfxx::spi::master_bus bus(
    idfxx::spi::host_device::spi2,
    idfxx::spi::dma_chan::ch_auto,
    {
        .mosi = idfxx::gpio_11,
        .miso = idfxx::gpio_13,
        .sclk = idfxx::gpio_12,
    });

idfxx::radio::sx126x radio(bus, {
    .variant = idfxx::radio::sx126x::chip_variant::sx1262,
    .cs      = idfxx::gpio_10,
    .busy    = idfxx::gpio_18,
    .dio1    = idfxx::gpio_5,
    .nreset  = idfxx::gpio_7,
});

radio.set_frequency(915_MHz);
radio.set_output_power(14);
radio.set_lora_modulation({
    .sf = idfxx::radio::spreading_factor::sf9,
    .bw = idfxx::radio::bandwidth::bw_125,
    .cr = idfxx::radio::coding_rate::cr_4_5,
});
radio.set_lora_packet_params({});

std::array<uint8_t, 5> payload = {'h', 'e', 'l', 'l', 'o'};
radio.transmit(payload, 2s);
```

### Future-based async operations

One-shot operations return an `idfxx::future` that completes with the
operation's result — no event loop required:

```cpp
auto tx = radio.start_transmit(payload);
// ... other work while the packet is on the air ...
tx.wait_for(2s);                     // throws errc::timeout if not done

std::array<uint8_t, 256> buf;
auto rx = radio.start_receive(buf);  // single-shot: one packet into buf
auto info = rx.wait();               // rx_info once it arrives
```

The buffer passed to single-shot `start_receive` must stay valid until the
future completes (or the operation is cancelled with `standby()`/`sleep()`,
which completes outstanding futures with `errc::not_finished`). Dropping a
future without waiting is safe — the operation continues and still posts its
event when `config::loop` is set.

### Result-based API

```cpp
auto bus = idfxx::spi::master_bus::make(
    idfxx::spi::host_device::spi2,
    idfxx::spi::dma_chan::ch_auto,
    { /* ... */ });
if (!bus) { /* handle */ }

auto radio = idfxx::radio::sx126x::make(*bus, {
    .variant = idfxx::radio::sx126x::chip_variant::sx1262,
    .cs      = idfxx::gpio_10,
    .busy    = idfxx::gpio_18,
    .dio1    = idfxx::gpio_5,
    .nreset  = idfxx::gpio_7,
});
if (!radio) { /* handle */ }
```

### Event-loop receive

Supply an event loop via `config::loop` (your own, or `event_loop::system()`),
then drive continuous receive and read each packet from the `rx_done` handler:

```cpp
loop.listener_add(idfxx::radio::rx_done,
    [&radio](const idfxx::radio::rx_info& info) {
        std::array<uint8_t, 256> buf;
        auto rx = radio.read_received(buf);
        // ...
    });

radio.start_receive();
```

If `config::loop` is left null the radio still works in blocking mode; it simply
drops events instead of posting them.

## Examples

The `examples/` directory contains four standalone ESP-IDF projects you can
build and flash directly:

- `transmit_beacon/` — periodically transmits `beacon NNNN`. Pair with
  `receive_log` on a second board.
- `receive_log/` — blocking single-shot receive loop, logs every packet with
  RSSI / SNR.
- `ping_pong/` — bounces a counter between two boards. Both boards flash the
  same firmware; whichever times out first elects itself as the initiator.
- `duty_cycle_listener/` — low-power duty-cycled receive driven from an event
  loop, with listen-before-talk beaconing (`scan_channel`, then a non-blocking
  `start_transmit` whose future is awaited under a `time_on_air` watchdog).

Each example assumes Heltec WiFi LoRa 32 V3 / LilyGo T3 pin layout and a TCXO
on DIO3 (`config::tcxo`); edit the `PIN_*` constants and the TCXO voltage at the
top of `main/main.cpp` to match your board, or drop `config::tcxo` entirely if
your module uses a plain crystal.

## API Overview

### `idfxx::radio::sx126x` (inherits `idfxx::radio::transceiver`)

**Construction:**
- `make(bus, config)` — result-based.
- `sx126x(bus, config)` — exception-based.

**Variant:**
- `chip_variant::sx1261` (max +15 dBm), `chip_variant::sx1262` (max +22 dBm),
  `chip_variant::sx1268` (max +22 dBm, PA tuned for 410–810 MHz).
- `variant()` returns the configured variant.

**Inherited radio interface:** see `idfxx_radio`.

**SX126x-specific surface:**
- `standby(standby_clock::xosc)` — standby with the crystal oscillator running
  (faster TX/RX transitions than the inherited RC-oscillator `standby()`).
- `sleep(sleep_mode::cold)` — lowest-power sleep, losing all configuration
  (the inherited `sleep()` performs a warm, configuration-retaining sleep).
- `set_fs()` — frequency-synthesis mode (PLL-lock diagnostic).
- `set_cad_params(p)` — tune CAD detection thresholds.
- `irq_flag`, `get_irq_status()`, `clear_irq_status(mask)` — chip-register
  IRQ surface for advanced/diagnostic use.
- `write_command` / `read_command` / `write_register` / `read_register` /
  `write_buffer` / `read_buffer` — six low-level opcode primitives.
- `spi()` returns the underlying `spi::master_device&`.

## Configuration

`sx126x::config` fields:

| Field                  | Default                          | Notes                                                                                  |
| ---------------------- | -------------------------------- | -------------------------------------------------------------------------------------- |
| `variant`              | `sx1262`                         | Picks the PA table and max-power validation.                                           |
| `cs`, `busy`, `dio1`   | `gpio::nc()`                     | Required pins; SX126x has no DIO0.                                                     |
| `nreset`               | `gpio::nc()`                     | Active-low reset; strongly recommended. `nc()` skips the hardware reset at construction. |
| `clock_speed`          | `10 MHz`                         | Well under the chip's 18 MHz limit.                                                    |
| `dio2_as_rf_switch`    | `true`                           | Matches most modules (Heltec, LilyGo, RAK).                                            |
| `rxen`, `txen`         | `gpio::nc()`                     | Optional external RF-switch control pins, driven to match TX / RX / idle; `nc()` if unused. |
| `tcxo`                 | `std::nullopt`                   | When set, DIO3 powers the TCXO (default 1.7 V, 5 ms startup) and the chip is calibrated. |
| `regulator`            | `regulator::ldo`                 | Universal safe choice; DC-DC requires board support.                                   |
| `rx_boost`             | `false`                          | Boosted-RX gain: better sensitivity at the cost of higher receive current.             |
| `busy_timeout`         | `100 ms`                         | Chip worst-case post-sleep wake is ~3.5 ms.                                            |
| `dio1_mask`            | `std::nullopt`                   | IRQ flags latched and routed to DIO1; unset = driver default (tx/rx done, preamble, CRC, CAD). |
| `warm_start`           | `false`                          | Attach to an already configured chip: no reset pulse, no configuration commands.       |
| `loop`                 | `nullptr`                        | Event loop to post radio events on; `nullptr` disables event posting.                  |
| `worker_priority`      | `18`                             | High enough to keep up with PHY events.                                                |
| `worker_stack_size`    | `4096`                           |                                                                                        |
| `worker_core`          | `std::nullopt`                   | Either core.                                                                           |

## Error Handling

Uses `idfxx::result<T>` / `idfxx::errc` from `idfxx_core`:

- `errc::timeout` — BUSY did not go low in time, or a blocking call timed out.
- `errc::invalid_arg` — requested output power is outside the variant's
  supported range, or a configuration value is out of range.
- `errc::invalid_state` — operation called in an incompatible state.
- `errc::invalid_crc` — received packet failed its CRC check.
- `errc::not_finished` — the future's operation was cancelled by
  `standby()`/`sleep()` (or driver teardown) before it completed.

## Important Notes

- **GPIO ISR service must already be installed** before constructing the
  driver — call `idfxx::gpio::install_isr_service()` once at startup.
- The constructor pulses NRESET, waits for BUSY low, applies a default LoRa
  configuration (sync word `0x3444`, buffer base 0/0), the variant's
  `SetPaConfig` table, and — when `config::tcxo` is set — a full chip
  calibration. `set_frequency` additionally runs an image calibration for the
  target band.
- Events are only posted if you supply `config::loop` (your own loop or
  `event_loop::system()`). With no loop, blocking `transmit`/`receive` still
  work; events are simply dropped.
- `transmit`/`receive`/`start_transmit`/`start_receive`/`scan_channel`/
  `start_channel_scan` form the single data path of an instance and must not
  be called concurrently; a second data-path call before the first completes
  returns `errc::invalid_state`.
- `start_transmit` returns once the transmission has started; await the
  returned future for completion (a `tx_done` event also posts when a loop is
  configured). The transmit has no built-in timeout — if a hardware fault
  means it never completes, the driver stays "busy". Wait on the future with a
  `time_on_air`-sized `try_wait_for` and call `standby()` to recover (the
  `duty_cycle_listener` example shows the pattern).
- The buffer passed to single-shot `start_receive(buffer)` must remain valid
  until its future completes or the operation is cancelled
  (`standby()`/`sleep()`); transmit payloads are staged on-chip before
  `start_transmit` returns.
- Pin allocation for ESP32-S3 modules with embedded flash/PSRAM must avoid
  GPIO 26–32 (flash/PSRAM lines) and GPIO 19/20 (USB).
- The driver creates a worker task (default priority 18, 4 KB stack) that
  consumes DIO1 notifications. Increase stack only if you observe overflow.
- For the SX1268 (high-power PA, sub-GHz, 410–810 MHz band), set
  `variant = chip_variant::sx1268` and a band-appropriate `set_frequency`.
- **Wake-on-radio across host deep sleep**: leave the chip duty-cycle
  listening (`start_receive(rx_duty_cycle)`), route DIO1 to an RTC wake pin,
  and deep-sleep the host *without destroying the driver* (its destructor
  puts the chip to sleep). After the wake, reconstruct with
  `.nreset = gpio::nc(), .warm_start = true` and call `adopt_pending()` to
  pull the packet the chip caught while the host slept into the receive
  cache (`read_received`). Set `config::dio1_mask` without
  `preamble_detected` in this design, or the host wakes on every preamble
  symbol instead of once per completed packet. Call `standby()` (after
  `adopt_pending` — standby clears latched IRQs) before reconfiguring or
  transmitting.

## License

Apache License 2.0 - see [LICENSE](LICENSE) for details.
