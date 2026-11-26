# idfxx_radio_sx126x

Semtech SX126x family LoRa radio driver — SX1261, SX1262, and SX1268.

📚 **[Full API Documentation](https://cleishm.github.io/idfxx/group__idfxx__radio.html)**

## Features

- One driver covers the SX1261, SX1262, and SX1268. A `chip_variant` enum selects the right
  power-amplifier configuration. Output power is validated against the variant's supported range
  (including the SX1261's special +15 dBm PA setting).
- Implements the chip-agnostic `idfxx::radio::lora_transceiver` interface from `idfxx_radio`, so
  application code, higher-layer protocols, and event listeners can target any LoRa chip.
- Three coordinated API styles:
  - Blocking: `transmit`/`receive`/`scan_channel`.
  - Future-based one-shot async: `start_transmit`, single-shot `start_receive(buffer)`, and
    `start_channel_scan`. Each returns an `idfxx::future`, and no event loop is required.
  - Event-loop dispatch for packet streams: continuous/duty-cycled `start_listening` with
    `read_received`.
- Low-power operation: duty-cycled listening (`start_listening(rx, sleep)`) and channel-activity
  detection for listen-before-talk.
- Optional TCXO control via DIO3, DIO2-as-RF-switch or external `rxen`/`txen` RF-switch GPIOs,
  configurable regulator (LDO / DC-DC), warm-start sleep, and CAD with user-tunable detection
  thresholds.
- Low-level opcode access (`write_command`, `read_command`, `write_register`, `read_register`,
  `write_buffer`, `read_buffer`) and direct access to the chip's IRQ register, for
  advanced/diagnostic use.

## Requirements

- ESP-IDF 5.5 or later
- C++23 compiler
- `idfxx_radio` (provides the `radio::lora_transceiver` base class)
- `idfxx_spi` (SPI master bus driver)
- `idfxx_gpio` (BUSY / DIO1 / NRESET / CS pins)

## Installation

### ESP-IDF Component Manager

Add to your project's `idf_component.yml`:

```yaml
dependencies:
  idfxx_radio_sx126x:
    version: "^3.0.0"
```

Or add `idfxx_radio_sx126x` to the `REQUIRES` list in your component's `CMakeLists.txt`.

## Usage

### Basic Example (exception-based)

```cpp
#include <idfxx/gpio>
#include <idfxx/radio/sx126x>
#include <idfxx/spi/master>

using namespace quanta::frequency_literals;
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

radio.configure({
    .frequency = 915_MHz,
    .output_power = 14_dBm,
    .modulation = {
        .sf = idfxx::radio::spreading_factor::sf9,
        .bw = idfxx::radio::bandwidth::bw_125,
        .cr = idfxx::radio::coding_rate::cr_4_5,
    },
});

std::array<uint8_t, 5> payload = {'h', 'e', 'l', 'l', 'o'};
radio.transmit(payload);  // timeout computed from the packet's air-time
```

### Future-based async operations

One-shot operations return an `idfxx::future` that completes with the operation's result. No event
loop is required:

```cpp
auto tx = radio.start_transmit(payload);
// ... other work while the packet is on the air ...
tx.wait_for(2s);                     // throws errc::timeout if not done

std::array<uint8_t, 256> buf;
auto rx = radio.start_receive(buf);  // single-shot: one packet into buf
auto info = rx.wait();               // rx_info once it arrives
```

The buffer passed to single-shot `start_receive` must stay valid until the future completes (or the
operation is canceled with `standby()`/`sleep()`, which completes outstanding futures with
`errc::not_finished`). Dropping a future without waiting is safe. The operation continues and still
posts its event when `config::loop` is set.

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

Supply an event loop via `config::loop` (your own, or `event_loop::system()`). Then start continuous
receive and read each packet from the `rx_done` handler:

```cpp
loop.listener_add(idfxx::radio::rx_done,
    [&radio](const idfxx::radio::rx_info& info) {
        std::array<uint8_t, 256> buf;
        auto rx = radio.read_received(buf);
        // ...
    });

radio.start_listening();
```

If `config::loop` is left null, the radio still works in blocking mode. It drops events instead of
posting them.

## Examples

The `examples/` directory contains four standalone ESP-IDF projects that you can build and flash
directly:

- `transmit_beacon/` — periodically transmits `beacon NNNN`. Pair with `receive_log` on a second
  board.
- `receive_log/` — blocking single-shot receive loop, logs every packet with RSSI / SNR.
- `ping_pong/` — sends a counter back and forth between two boards. Both boards run the same
  firmware. The board that times out first becomes the initiator.
- `duty_cycle_listener/` — low-power duty-cycled receive driven from an event loop, with
  listen-before-talk beaconing. It calls `scan_channel`, then a non-blocking `start_transmit`, and
  waits on the transmit's future with a watchdog timeout based on `time_on_air`.

Each example assumes Heltec WiFi LoRa 32 V3 / LilyGo T3 pin layout and a TCXO on DIO3
(`config::tcxo`). To match your board, edit the `PIN_*` constants and the TCXO voltage at the top of
`main/main.cpp`. If your module uses a plain crystal, remove `config::tcxo` entirely.

## API Overview

### `idfxx::radio::sx126x` (inherits `idfxx::radio::lora_transceiver`)

**Construction:**
- `make(bus, config)` — result-based.
- `sx126x(bus, config)` — exception-based.
- A cold start fails with `errc::not_found` if no chip answers on the bus. The inherited `probe()`
  repeats the check at any later point (e.g. after a warm start).

**Variant:**
- `chip_variant::sx1261` (max +15 dBm), `chip_variant::sx1262` (max +22 dBm), `chip_variant::sx1268`
  (max +22 dBm, PA tuned for 410–810 MHz).
- `variant()` returns the configured variant.

**Inherited radio interface:** see `idfxx_radio`.

**SX126x-specific API:**
- `standby(standby_clock::xosc)` — standby with the crystal oscillator running (faster TX/RX
  transitions than the inherited RC-oscillator `standby()`).
- `sleep(sleep_mode::cold)` — lowest-power sleep, losing all configuration (the inherited `sleep()`
  performs a warm, configuration-retaining sleep).
- `set_cad_params(p)` — tune CAD detection thresholds.
- `set_sync_word(uint16_t)` — raw sync-word register value for non-standard networks (the inherited
  overload selects `lora_network` semantically).
- `irq_flag`, `irq_status()`, `clear_irq_status(mask)` — direct access to the chip's IRQ register,
  for advanced/diagnostic use.
- `write_command(opcode, params)` / `read_command(opcode, response)` /
  `write_register(addr, data)` / `read_register(addr, buffer)` / `write_buffer(offset, data)` /
  `read_buffer(offset, buffer)` — low-level opcode primitives. Writes take an `idfxx::byte_view`
  (command and register writes also take literal bytes, e.g.
  `write_register(0x0740, {0x34, 0x44})`). Reads fill the whole of an `idfxx::byte_span`. A buffer
  access transfers at most 256 bytes, the size of the chip's data buffer.
- `read_command<N>(opcode)` / `read_register<N>(addr)` — fixed-size reads returning
  `std::array<uint8_t, N>` (e.g. `read_command<2>(0x12)` for GetIrqStatus).
- `write_register(addr, value)` / `read_register(addr)` — single-register write and read of a
  `uint8_t` (e.g. `write_register(0x08AC, 0x96)`).
- `spi()` returns the underlying `spi::master_device&`.

## Configuration

`sx126x::config` fields:

| Field                  | Default                          | Notes                                                                                  |
| ---------------------- | -------------------------------- | -------------------------------------------------------------------------------------- |
| `variant`              | `sx1262`                         | Picks the PA table and max-power validation.                                           |
| `cs`, `busy`, `dio1`   | none                             | Required pins. SX126x has no DIO0.                                                     |
| `nreset`               | `gpio::nc()`                     | Active-low reset. Strongly recommended. `nc()` skips the hardware reset at construction. |
| `clock_speed`          | `10 MHz`                         | Datasheet maximum is 16 MHz.                                                           |
| `dio2_as_rf_switch`    | `true`                           | Matches most modules (Heltec, LilyGo, RAK).                                            |
| `rxen`, `txen`         | `gpio::nc()`                     | Optional external RF-switch control pins, driven to match TX / RX / idle. `nc()` if unused. |
| `tcxo`                 | `std::nullopt`                   | When set, DIO3 powers the TCXO (default 1.7 V, 5 ms startup) and the chip is calibrated. |
| `regulator`            | `regulator::ldo`                 | Universal safe choice. DC-DC requires board support.                                   |
| `rx_boost`             | `false`                          | Boosted-RX gain: better sensitivity, but higher receive current.                       |
| `busy_timeout`         | `100 ms`                         | Chip worst-case post-sleep wake is ~3.5 ms.                                            |
| `dio1_mask`            | `std::nullopt`                   | IRQ flags latched and routed to DIO1. Unset = driver default (tx/rx done, preamble, CRC, CAD). |
| `warm_start`           | `false`                          | Attach to an already configured chip: no reset pulse, no configuration commands.       |
| `loop`                 | `nullptr`                        | Event loop to post radio events on. `nullptr` disables event posting.                  |
| `worker_priority`      | `18`                             | High enough to keep up with PHY events. Must be below `configMAX_PRIORITIES`.          |
| `worker_stack_size`    | `4096`                           | At least `configMINIMAL_STACK_SIZE`.                                                   |
| `worker_core`          | `std::nullopt`                   | Either core.                                                                           |

## Error Handling

Uses `idfxx::result<T>` / `idfxx::errc` from `idfxx_core`:

- `errc::timeout` — BUSY did not go low in time, or a blocking call timed out.
- `errc::invalid_arg` — requested output power is outside the variant's supported range, a
  configuration value is out of range, a low-level opcode frame is too long, or a buffer
  access is larger than the chip's 256-byte data buffer.
- `errc::invalid_state` — operation called in an incompatible state.
- `errc::not_found` — no chip answered on the SPI bus (cold-start construction or `probe()`), or
  `read_received` found no packet queued.
- `errc::invalid_crc` — received packet failed its CRC check.
- `errc::invalid_size` — received packet was larger than the buffer supplied to
  `receive`/`start_receive`/`read_received`. It is discarded, not truncated.
- `errc::not_finished` — the future's operation was canceled by `standby()`/`sleep()` (or driver
  teardown) before it completed.

## Important Notes

- **GPIO ISR service must already be installed** before constructing the driver. Call
  `idfxx::gpio::install_isr_service()` once at startup.
- The constructor performs these steps:
  1. It pulses NRESET and waits for BUSY low.
  2. It applies a default LoRa configuration (sync word `0x3444`, buffer base 0/0) and the variant's
     `SetPaConfig` table.
  3. When `config::tcxo` is set, it runs a full chip calibration.
  4. It writes the sync-word register and reads it back, to confirm that a chip is actually
     answering. Without this check, bring-up can succeed with nothing attached if the BUSY line is
     floating.

  `set_frequency` additionally runs an image calibration for the target band.
- `config::warm_start` skips the presence check and the rest of bring-up. Call `probe()` to run the
  check explicitly. In the wake-on-radio flow below, call it after `standby()`, because its SPI
  traffic would end an autonomous listen. Probing a sleeping chip wakes it and leaves it in standby.
- Events are only posted if you supply `config::loop` (your own loop or `event_loop::system()`).
  With no loop, blocking `transmit`/`receive` still work. Events are dropped.
- `transmit`/`receive`/`start_transmit`/`start_receive`/`start_listening`/`scan_channel`/
  `start_channel_scan` form the single data path of an instance and must not be called concurrently.
  A second data-path call before the first completes returns `errc::invalid_state`.
- `start_transmit` returns once the transmission has started. Wait on the returned future for
  completion (a `tx_done` event is also posted when a loop is configured). The transmit has no
  built-in timeout. If a hardware fault prevents it from completing, the driver stays "busy". Wait
  on the future with `try_wait_for` and a timeout based on `time_on_air`, and call `standby()` to
  recover (the `duty_cycle_listener` example shows the pattern).
- The buffer passed to single-shot `start_receive(buffer)` must remain valid until its future
  completes or the operation is canceled (`standby()`/`sleep()`). Transmit payloads are staged
  on-chip before `start_transmit` returns.
- Pin allocation for ESP32-S3 modules with embedded flash/PSRAM must avoid GPIO 26–32 (flash/PSRAM
  lines) and GPIO 19/20 (USB).
- The driver creates a worker task (default priority 18, 4 KB stack) that consumes DIO1
  notifications. Increase stack only if you observe overflow.
- For the SX1268 (high-power PA, sub-GHz, 410–810 MHz band), set `variant = chip_variant::sx1268`
  and a band-appropriate `set_frequency`.
- Register, opcode, and errata references throughout the source cite the Semtech SX1261/2 datasheet
  **DS_SX1261-2 rev. 2.2**. The low-level opcode methods
  (`write_command`/`write_register`/`write_buffer` and their `read` counterparts) are intended for
  use with that document.
- **Wake-on-radio across host deep sleep**:
  1. Leave the chip duty-cycle listening (`start_listening(rx_duty_cycle)`).
  2. Route DIO1 to an RTC wake pin.
  3. Put the host in deep sleep *without destroying the driver*. The driver's destructor puts the
     chip to sleep.
  4. After the wake, reconstruct the driver with `.nreset = gpio::nc(), .warm_start = true`.
  5. Call `adopt_pending()`. It moves the packet that the chip received while the host slept into
     the receive FIFO, where `read_received` returns it.
  6. Call `standby()` before reconfiguring or transmitting. Call it after `adopt_pending`, because
     standby clears latched IRQs.

  In this design, set `config::dio1_mask` without `preamble_detected`. Otherwise the host wakes on
  every preamble symbol instead of once per completed packet.

## License

Apache License 2.0 - see [LICENSE](LICENSE) for details.
