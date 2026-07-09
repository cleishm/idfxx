# idfxx_radio

Abstract radio transceiver interface and LoRa types.

## Features

- Abstract `idfxx::radio::transceiver` base class — drivers for any LoRa chip (SX126x,
  SX127x, LR1110, ...) implement this single interface so application code can
  target any of them.
- Modulation-agnostic core: frequency, output power, sync word, transmit,
  receive, and RSSI are set through the base class; LoRa modulation and packet
  framing arrive via `set_lora_modulation` and `set_lora_packet_params`.
- Semantic LoRa types (spreading factor, bandwidth, coding rate, ramp time,
  packet parameters) that map to each chip's own register encoding.
- Chip-agnostic event base for `tx_done`, `rx_done`, `crc_error`,
  `cad_done`, and `preamble_detected`.
- Three coordinated API styles for every concrete driver: blocking calls,
  future-based one-shot operations, and event-loop dispatch for packet
  streams.
- One-shot async operations (`start_transmit`, single-shot
  `start_receive(buffer)`, `start_channel_scan`) return an `idfxx::future` —
  completion is awaitable without configuring an event loop.
- Low-power operation: duty-cycled receive (`start_receive(rx, sleep)`) and
  channel-activity detection (`scan_channel` / `start_channel_scan`) for
  listen-before-talk.
- `constexpr` `time_on_air` helper (`<idfxx/radio/airtime>`) for sizing timeouts
  and scheduling without touching hardware.
- `constexpr` `rx_duty_cycle_for` helper (`<idfxx/radio/duty_cycle>`) that
  computes listen/sleep windows guaranteed to catch packets for a given
  modulation and sender preamble length, with automatic fallback to
  continuous receive.

## Requirements

- ESP-IDF 5.5 or later
- C++23 compiler
- `idfxx_core` (error handling)
- `idfxx_event` (event loop and event base)

## Installation

### ESP-IDF Component Manager

Add to your project's `idf_component.yml`:

```yaml
dependencies:
  idfxx_radio:
    version: "^1.0.0"
```

Or add `idfxx_radio` to the `REQUIRES` list in your component's
`CMakeLists.txt`.

This component is rarely used on its own — depend on a concrete driver such as
`idfxx_radio_sx126x`, which pulls in `idfxx_radio` automatically.

## Usage

`idfxx_radio` defines no concrete radio; it's an interface. Application code
that talks to *any* LoRa radio receives an `idfxx::radio::transceiver&`:

```cpp
#include <idfxx/radio/transceiver>
#include <idfxx/radio/events>

void send_one(idfxx::radio::transceiver& radio) {
    radio.set_lora_modulation({
        .sf = idfxx::radio::spreading_factor::sf9,
        .bw = idfxx::radio::bandwidth::bw_125,
        .cr = idfxx::radio::coding_rate::cr_4_5,
    });

    std::array<uint8_t, 4> payload = {'p', 'i', 'n', 'g'};
    radio.transmit(payload, std::chrono::seconds{2});
}
```

Construct a concrete driver (e.g. `idfxx::radio::sx126x`) and pass it where a
`transceiver&` is expected. See `idfxx_radio_sx126x` for a working example.

### Future-based async operations

One-shot operations return an `idfxx::future` that completes with the
operation's result — no event loop required:

```cpp
auto tx = radio.start_transmit(payload);
// ... other work while the packet is on the air ...
tx.wait_for(std::chrono::seconds{2});  // throws errc::timeout if not done

std::array<uint8_t, 256> buf;
auto rx = radio.start_receive(buf);    // single-shot: one packet into buf
auto info = rx.wait();                 // rx_info once it arrives
```

The buffer passed to single-shot `start_receive` must stay valid until the
future completes (or the operation is cancelled with `standby()`/`sleep()`).
Dropping a future without waiting is safe — the operation continues and its
completion event still posts when an event loop is configured.

### Event-driven receive

Events are posted only when the concrete driver is given an event loop (see the
driver's `config`). Subscribe on that loop, then start continuous receive:

```cpp
loop.listener_add(idfxx::radio::rx_done,
    [&radio](const idfxx::radio::rx_info& info) {
        std::array<uint8_t, 256> buf;
        auto rx = radio.read_received(buf);
        // ... process rx.length bytes ...
    });

radio.start_receive();
```

### Low-power duty-cycled receive

Instead of listening continuously, the radio can alternate between short
listen windows and sleep. `rx_duty_cycle_for` computes windows that cannot
miss a packet, given the preamble length senders use; when the link's
parameters make duty-cycling infeasible it returns `std::nullopt`, which
`start_receive` accepts as "receive continuously":

```cpp
#include <idfxx/radio/duty_cycle>

constexpr idfxx::radio::lora_modulation mod{.sf = idfxx::radio::spreading_factor::sf9};
constexpr idfxx::radio::lora_packet_params pkt{.preamble_length = 100};

radio.set_lora_modulation(mod);
radio.set_lora_packet_params(pkt);
radio.start_receive(idfxx::radio::rx_duty_cycle_for(mod, pkt.preamble_length));
```

## API Overview

### `idfxx::radio::transceiver` (abstract)

**Mode control:** `standby`, `sleep`, `current_mode`.

**Configuration:** `set_frequency`, `set_output_power`, `set_lora_modulation`,
`set_lora_packet_params`, `set_sync_word`.

**Data path (blocking):** `transmit(span, timeout)`, `receive(span, timeout)`,
`scan_channel()`.

**Data path (async, future-based):** `start_transmit(span)` →
`idfxx::future<void>`, `start_receive(buffer)` (single-shot) →
`idfxx::future<rx_info>`, `start_channel_scan()` → `idfxx::future<cad_info>`.

**Data path (event-loop streams):** `start_receive()` (continuous),
`start_receive(rx, sleep)` / `start_receive(rx_duty_cycle)` (duty-cycled, also
accepting `std::optional<rx_duty_cycle>` with continuous fallback), paired
with `read_received`.

**Status:** `get_packet_status`, `get_rssi_inst_dbm`.

**Air-time:** `time_on_air(mod, pkt, payload_length)` (free function in
`<idfxx/radio/airtime>`) — the packet's on-air duration as a `constexpr`
`std::chrono::microseconds`.

**Duty-cycle windows:** `rx_duty_cycle_for(mod, sender_preamble, min_symbols,
min_sleep)` (free function in `<idfxx/radio/duty_cycle>`) — `constexpr`
listen/sleep windows as `std::optional<rx_duty_cycle>`, `std::nullopt` when
continuous receive is required.

Every method has a result-returning `try_*` form; the exception-throwing forms
above are inline wrappers guarded by `CONFIG_COMPILER_CXX_EXCEPTIONS`.

### Types

- `spreading_factor` — `sf5` through `sf12`, encoded as the LoRa spec values.
- `bandwidth` — semantic enum (`bw_7_8` through `bw_500`) named by kHz.
- `coding_rate` — `cr_4_5` through `cr_4_8`.
- `header_type` — `variable` or `fixed`.
- `ramp_time` — semantic buckets (`us_10`, `us_40`, `us_200`, `us_800`,
  `us_3400`). Drivers map to the closest supported value.
- `chip_mode` — `sleep`, `stdby`, `fs`, `tx`, `rx`, `cad`.
- `lora_modulation`, `lora_packet_params` — packet/modulation config structs.
- `rx_duty_cycle` — listen/sleep windows for duty-cycled receive.
- `rx_info`, `cad_info`, `packet_status` — result payloads.

### Events

Defined in `<idfxx/radio/events>`:

- `tx_done`
- `rx_done` (carries `rx_info`)
- `crc_error`
- `cad_done` (carries `cad_info`)
- `preamble_detected`

## Error Handling

The interface uses `idfxx::result<T>` and `idfxx::errc` from `idfxx_core`. CRC
errors surface as `errc::invalid_crc`; timed-out blocking operations return
`errc::timeout`. Out-of-range parameters (e.g. an unsupported output power for
a particular chip variant) return `errc::invalid_arg`. A future whose
operation is cancelled by a mode change (`standby`/`sleep`) before completing
reports `errc::not_finished`. Using a moved-from driver object is undefined
behavior.

## Important Notes

- `idfxx_radio` itself ships no register-level details: enum values are
  semantic, not chip-register encodings. Each driver translates internally.
- Single-shot `start_receive(buffer)`: the buffer must remain valid until the
  returned future completes or the operation is cancelled (`standby`/`sleep`);
  the radio returns to standby after the packet. Transmit buffers are staged
  before `start_transmit` returns and need not outlive the call.
- Dropping a future mid-flight is safe: the operation continues, and its
  completion is still observable through events when an event loop is
  configured.
- The base interface intentionally omits chip-specific concerns such as TCXO
  configuration, IRQ-register bitfields, and CAD detection thresholds. Those
  live on concrete drivers like `idfxx::radio::sx126x`.
- Sync word is `uint16_t`; chips that natively use 8-bit sync words use the
  low byte.

## License

Apache License 2.0 - see [LICENSE](LICENSE) for details.
