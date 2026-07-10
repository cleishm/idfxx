// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#include "sx126x_internal.hpp"

#include <idfxx/future>
#include <idfxx/radio/sx126x>

#include <algorithm>
#include <array>
#include <chrono>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>

namespace idfxx::radio {

namespace internal = sx126x_internal;

// Forward declaration; defined in sx126x_isr.cpp. These are external-linkage
// helpers within the idfxx::radio namespace shared between the two TU's.
void sx126x_worker_fn(task::self& self, sx126x::state* state);
void sx126x_dio1_isr_thunk(void* arg);
result<rx_info> sx126x_drain_received(sx126x::state& s);

namespace {

constexpr const char* TAG = "idfxx::radio::sx126x";

// Default IRQ mask routed to DIO1 in v1. The chip `timeout` IRQ is intentionally
// excluded: the driver manages tx/rx timeouts in software (SetTx/SetRx are
// always issued with no chip timeout), so it never fires.
constexpr uint16_t default_irq_mask = idfxx::to_underlying(
    sx126x::irq_flag::tx_done | sx126x::irq_flag::rx_done | sx126x::irq_flag::preamble_detected |
    sx126x::irq_flag::crc_err | sx126x::irq_flag::cad_done | sx126x::irq_flag::cad_detected
);

// SX126x sync-word register values for the standard LoRa networks.
constexpr uint16_t sync_word_public = 0x3444;
constexpr uint16_t sync_word_private = 0x1424;

// Completes and detaches the in-flight async-op latch (if any) with `err`,
// dropping the reference to the caller's receive buffer first so the worker
// can never write to it afterwards. Must be called with s.mu held.
void cancel_active_op(sx126x::state& s, errc err) {
    if (auto latch = std::move(s.active_op)) {
        latch->rx_target = {};
        latch->complete(static_cast<int32_t>(std::to_underlying(err)));
    }
}

// Resets the driver activity back to idle, parking the chip-mode bookkeeping
// and RF switch in `parked`. Used to unwind a partially armed
// transmit/receive/scan on an early error, and by the standby/sleep
// cancellation paths. Completes any latch the unwound operation had armed.
void reset_idle(sx126x::state& s, chip_mode parked = chip_mode::stdby) {
    std::lock_guard g(s.mu);
    s.driver_state = internal::driver_activity::idle;
    s.mode = parked;
    cancel_active_op(s, errc::not_finished);
    sx126x_set_rf_switch(s, parked);
}

// Writes the boosted-RX gain register before a receive, if requested. Refreshing
// it before every SetRx also sidesteps the warm-start gain loss noted in the
// datasheet. (Register value re-verify against DS_SX1261-2 before shipping.)
result<void> apply_rx_boost(sx126x::state& s) {
    if (!s.cfg.rx_boost) {
        return {};
    }
    std::array<uint8_t, 1> gain{internal::rx_gain_boosted};
    return s.write_register(internal::reg_rx_gain, gain);
}

// Claims the data path for a new one-shot operation (idle -> `activity`) and
// arms the operation latch — recording the caller's receive buffer on it when
// one is supplied — in a single critical section, so the invariant tying
// active_op, driver_state, and mode together holds at every observable point.
result<std::shared_ptr<internal::op_latch>> claim_data_path(
    sx126x::state& s,
    internal::driver_activity activity,
    chip_mode mode,
    std::span<uint8_t> rx_target = {}
) {
    std::lock_guard g(s.mu);
    if (s.driver_state != internal::driver_activity::idle) {
        return error(errc::invalid_state);
    }
    auto latch = std::make_shared<internal::op_latch>();
    latch->rx_target = rx_target;
    s.active_op = latch;
    s.driver_state = activity;
    s.mode = mode;
    return latch;
}

// Arms a stream receive (continuous or duty-cycled) with the given SetRx /
// SetRxDutyCycle command: refreshes the boosted-RX gain (the chip drops it
// across duty-cycle sleep windows, so refreshing before every arm dodges the
// warm-start sensitivity loss), points the RF switch at the receiver, issues
// the command, and records the rx bookkeeping. Duty-cycled receive shares
// rx_continuous: the worker already posts rx_done and never completes a
// blocking waiter for it, which is exactly the duty-cycle behaviour.
result<void> arm_stream_receive(sx126x::state& s, uint8_t opcode, std::span<const uint8_t> params) {
    if (auto e = apply_rx_boost(s); !e) {
        return error(e.error());
    }
    sx126x_set_rf_switch(s, chip_mode::rx);
    if (auto e = s.write_command(opcode, params); !e) {
        return e;
    }
    std::lock_guard g(s.mu);
    s.mode = chip_mode::rx;
    s.driver_state = internal::driver_activity::rx_continuous;
    return {};
}

// SetCadParams: 7 bytes. cad_symbol_num, det_peak, det_min, exit_mode
// (CAD_ONLY), timeout (3 bytes, unused). Shared by the default-config path and
// the public set_cad_params; the chip retains the values, so user tuning
// persists across scans.
result<void> write_cad_params(sx126x::state& s, sx126x::cad_params params) {
    std::array<uint8_t, 7> p{
        params.symbol_num,
        params.det_peak,
        params.det_min,
        0x00, // CAD_ONLY exit mode
        0x00,
        0x00,
        0x00, // no timeout
    };
    return s.write_command(internal::op_set_cad_params, p);
}

result<void> apply_default_config(sx126x::state& s) {
    // Standby (RC clock).
    if (auto e = s.command1(internal::op_set_standby, internal::stdby_rc); !e) {
        return e;
    }

    // Optional TCXO via DIO3, followed by a full calibration. Once the chip runs
    // from a TCXO it must be (re)calibrated before use (DS §9.2.1 / §13.1.12).
    if (s.cfg.tcxo) {
        auto params = internal::pack_tcxo_params(s.cfg.tcxo->voltage, s.cfg.tcxo->startup);
        if (auto e = s.write_command(internal::op_set_dio3_as_tcxo_ctrl, params); !e) {
            return e;
        }
        // Calibrate all blocks; calibration holds BUSY high (~3.5 ms).
        if (auto e = s.command1(internal::op_calibrate, internal::calibrate_all); !e) {
            return e;
        }
        if (auto e = s.wait_busy(); !e) {
            return e;
        }
    }

    // Regulator.
    if (auto e = s.command1(internal::op_set_regulator_mode, std::to_underlying(s.cfg.regulator)); !e) {
        return e;
    }

    // DIO2 as RF switch.
    if (s.cfg.dio2_as_rf_switch) {
        if (auto e = s.command1(internal::op_set_dio2_as_rf_switch, 0x01); !e) {
            return e;
        }
    }

    // Packet type = LoRa.
    if (auto e = s.command1(internal::op_set_packet_type, internal::packet_type_lora); !e) {
        return e;
    }

    // PA config from the variant's maximum-power row; set_output_power
    // re-issues it alongside SetTxParams for the requested power.
    auto limits = internal::power_limits_for(s.cfg.variant);
    auto pa = internal::tx_power_config_for(s.cfg.variant, limits.max_dbm).pa;
    std::array<uint8_t, 4> pa_params{pa.pa_duty_cycle, pa.hp_max, pa.device_sel, pa.pa_lut};
    if (auto e = s.write_command(internal::op_set_pa_config, pa_params); !e) {
        return e;
    }

    // Errata §15.2: for the high-power PA (SX1262/SX1268), set the Tx-clamp bits
    // to improve resistance to antenna mismatch. (Re-verify register/mask
    // against DS_SX1261-2 before shipping.)
    if (internal::is_high_power(s.cfg.variant)) {
        std::array<uint8_t, 1> clamp{};
        if (auto e = s.read_register(internal::reg_tx_clamp_config, clamp); !e) {
            return e;
        }
        clamp[0] |= internal::tx_clamp_bits;
        if (auto e = s.write_register(internal::reg_tx_clamp_config, clamp); !e) {
            return e;
        }
    }

    // SetDioIrqParams: 8 bytes. (irq_mask, dio1_mask, dio2_mask, dio3_mask) MSB/LSB each.
    // The same mask is used for latching and DIO1 routing; config::dio1_mask
    // overrides the driver default.
    const uint16_t irq_mask = s.cfg.dio1_mask ? idfxx::to_underlying(*s.cfg.dio1_mask) : default_irq_mask;
    std::array<uint8_t, 8> irq_params{
        static_cast<uint8_t>(irq_mask >> 8),
        static_cast<uint8_t>(irq_mask),
        static_cast<uint8_t>(irq_mask >> 8),
        static_cast<uint8_t>(irq_mask),
        0x00,
        0x00,
        0x00,
        0x00,
    };
    if (auto e = s.write_command(internal::op_set_dio_irq_params, irq_params); !e) {
        return e;
    }

    // Buffer base addresses (TX=0, RX=0).
    std::array<uint8_t, 2> bases{0x00, 0x00};
    if (auto e = s.write_command(internal::op_set_buffer_base_addr, bases); !e) {
        return e;
    }

    // Default LoRa sync word = public network (0x3444).
    auto sync = internal::pack_sync_word(sync_word_public);
    if (auto e = s.write_register(internal::reg_lora_sync_word_msb, sync); !e) {
        return e;
    }

    // Default CAD parameters.
    if (auto e = write_cad_params(s, sx126x::cad_params{}); !e) {
        return e;
    }

    return {};
}

result<void> reset_chip(sx126x::state& s) {
    if (!s.cfg.nreset.is_connected()) {
        return {};
    }
    auto reset_pin = s.cfg.nreset;
    if (auto e = reset_pin.try_set_direction(gpio::mode::output); !e) {
        return e;
    }
    reset_pin.set_level(gpio::level::low);
    vTaskDelay(pdMS_TO_TICKS(2));
    reset_pin.set_level(gpio::level::high);
    vTaskDelay(pdMS_TO_TICKS(5));
    return s.wait_busy();
}

result<void> setup_busy_pin(sx126x::state& s) {
    if (!s.cfg.busy.is_connected()) {
        return {};
    }
    auto busy = s.cfg.busy;
    return busy.try_set_direction(gpio::mode::input);
}

// Configures any connected external RF-switch control pins as outputs driven
// low (antenna parked). Unconnected (nc()) pins are skipped.
result<void> setup_rf_switch_pins(sx126x::state& s) {
    for (auto pin : {s.cfg.rxen, s.cfg.txen}) {
        if (!pin.is_connected()) {
            continue;
        }
        if (auto e = pin.try_set_direction(gpio::mode::output); !e) {
            return e;
        }
        pin.set_level(gpio::level::low);
    }
    return {};
}

result<gpio::unique_isr_handle> setup_dio1_isr(sx126x::state& s) {
    if (!s.cfg.dio1.is_connected()) {
        return error(errc::invalid_arg);
    }
    auto dio1 = s.cfg.dio1;
    if (auto e = dio1.try_set_direction(gpio::mode::input); !e) {
        return error(e.error());
    }
    dio1.set_intr_type(gpio::intr_type::posedge);
    auto h = dio1.try_isr_handler_add(&sx126x_dio1_isr_thunk, &s);
    if (!h) {
        return error(h.error());
    }
    return gpio::unique_isr_handle{*h};
}

// Stages a payload and starts a transmission, returning as soon as SetTx is
// issued. Claims the data path (idle -> tx_in_flight) and arms the operation
// latch, re-issues the cached packet params with this payload's length,
// stages the buffer, throws the RF switch to TX, and issues SetTx with no
// chip timeout. Unwinds to idle/standby on any post-claim error. The worker's
// tx_done path completes the returned latch, resets the driver state, and
// posts the event.
result<std::shared_ptr<internal::op_latch>> begin_transmit(sx126x::state& s, std::span<const uint8_t> data) {
    // The payload has already been validated (non-empty, <= 255 bytes) by the
    // transceiver base-class wrappers.

    auto latch = claim_data_path(s, internal::driver_activity::tx_in_flight, chip_mode::tx);
    if (!latch) {
        return latch;
    }

    // Re-issue the cached packet params with this payload's length, preserving
    // the caller's header/CRC/IQ; write directly so the cache itself is unchanged.
    lora_packet_params params = s.last_packet_params;
    params.payload_length = static_cast<uint8_t>(data.size());
    auto packed = internal::pack_packet_params(params);
    if (auto e = s.write_command(internal::op_set_packet_params, packed); !e) {
        reset_idle(s);
        return error(e.error());
    }

    // Stage the payload directly from the caller's span (the write completes
    // before this returns, so no internal copy is needed).
    if (auto e = s.write_buffer(0, data); !e) {
        reset_idle(s);
        return error(e.error());
    }

    // Point the RF switch at the PA and start the transmission (no chip
    // timeout — software-managed).
    sx126x_set_rf_switch(s, chip_mode::tx);
    std::array<uint8_t, 3> tx_params{0x00, 0x00, 0x00};
    if (auto e = s.write_command(internal::op_set_tx, tx_params); !e) {
        reset_idle(s);
        return error(e.error());
    }
    return *latch;
}

// Builds the caller-facing future for an armed operation latch. The waiter
// blocks on the latch alone (never on driver state), so futures carry no
// lifetime dependency on the sx126x object beyond the latch's shared_ptr:
// teardown simply completes the latch. `extract` pulls the typed result out
// of a completed latch; the release/acquire pair on `done` makes the result
// fields written before complete() visible here.
template<typename T, typename Extract>
idfxx::future<T> make_op_future(std::shared_ptr<internal::op_latch> latch, Extract extract) {
    auto waiter = [latch, extract](std::optional<std::chrono::milliseconds> timeout) -> result<T> {
        if (timeout) {
            // clear_on_exit=false: every future copy sharing this latch must
            // observe the (one-shot) completion bit.
            (void)latch->eg.try_wait(internal::completion_bit::done, *timeout, /*clear_on_exit=*/false);
        } else {
            while (!latch->done.load(std::memory_order_acquire)) {
                (void)latch->eg.try_wait(internal::completion_bit::done, wait_mode::all, /*clear_on_exit=*/false);
            }
        }
        if (!latch->done.load(std::memory_order_acquire)) {
            return error(errc::timeout);
        }
        if (int32_t err = latch->error.load(std::memory_order_relaxed); err != 0) {
            return error(static_cast<errc>(err));
        }
        return extract(*latch);
    };
    auto done_check = [latch]() noexcept { return latch->done.load(std::memory_order_acquire); };
    return idfxx::future<T>(std::move(waiter), std::move(done_check));
}

} // namespace

// External-linkage RF-switch helper declared in sx126x_internal.hpp; the worker
// in sx126x_isr.cpp calls it too. Drives the configured external RF-switch pins
// to match the chip mode: tx asserts txen, rx/cad assert rxen, and every other
// mode parks both low. A no-op on any pin left unconnected (nc()).
void sx126x_set_rf_switch(sx126x::state& s, chip_mode mode) noexcept {
    bool rx_on = false;
    bool tx_on = false;
    switch (mode) {
    case chip_mode::tx:
        tx_on = true;
        break;
    case chip_mode::rx:
    case chip_mode::cad:
        rx_on = true;
        break;
    default:
        break; // sleep / stdby -> both low (antenna parked)
    }
    if (s.cfg.rxen.is_connected()) {
        s.cfg.rxen.set_level(rx_on ? gpio::level::high : gpio::level::low);
    }
    if (s.cfg.txen.is_connected()) {
        s.cfg.txen.set_level(tx_on ? gpio::level::high : gpio::level::low);
    }
}

// =============================================================================
// SPI I/O (shared by the public API and the worker task)
// =============================================================================

result<void> sx126x::state::wait_busy() {
    if (!cfg.busy.is_connected()) {
        return {};
    }
    const auto start = std::chrono::steady_clock::now();
    const auto deadline = start + cfg.busy_timeout;
    // Normal command turnaround is ~1 us, so spin-yield first; back off to a
    // tick sleep for the rare long waits (e.g. ~3.5 ms after Calibrate) so
    // they don't monopolize the CPU.
    constexpr auto spin_limit = std::chrono::microseconds(500);
    while (cfg.busy.get_level() == gpio::level::high) {
        auto now = std::chrono::steady_clock::now();
        if (now > deadline) {
            ESP_LOGW(TAG, "BUSY timeout");
            return error(errc::timeout);
        }
        vTaskDelay(now - start < spin_limit ? 0 : 1);
    }
    return {};
}

// Frames at or below this length use polling transactions (cheaper than the
// interrupt/context-switch overhead); longer frames — payload buffer reads and
// writes — use interrupt transactions so the CPU isn't busy-spun for the
// duration of the transfer.
constexpr size_t polling_frame_max = 32;

result<void> sx126x::state::wait_busy_waking() {
    auto e = wait_busy();
    if (!e && e.error() == errc::timeout) {
        // BUSY held high through the whole window: no command processing
        // lasts that long, so the chip is asleep (e.g. the sleep phase of a
        // duty-cycled receive) and needs an NSS edge before it can respond.
        ESP_LOGD(TAG, "BUSY held high — waking chip with an NSS edge");
        const std::array<uint8_t, 2> get_status{internal::op_get_status, 0x00};
        (void)device.try_polling_transmit(get_status);
        e = wait_busy();
    }
    return e;
}

result<void> sx126x::state::transfer(std::span<const uint8_t> tx, std::span<uint8_t> rx) {
    std::lock_guard guard(spi_mu);
    if (auto e = wait_busy_waking(); !e) {
        return e;
    }
    if (tx.size() > polling_frame_max) {
        return device.try_transfer(tx, rx);
    }
    return device.try_polling_transfer(tx, rx);
}

result<void> sx126x::state::transmit(std::span<const uint8_t> tx) {
    std::lock_guard guard(spi_mu);
    if (auto e = wait_busy_waking(); !e) {
        return e;
    }
    if (tx.size() > polling_frame_max) {
        return device.try_transmit(tx);
    }
    return device.try_polling_transmit(tx);
}

namespace {

// Stages `header` followed by `data` into a single frame and transmits it.
// The staging buffer is deliberately left uninitialized: every transmitted
// byte is written below.
result<void> write_framed(sx126x::state& s, std::span<const uint8_t> header, std::span<const uint8_t> data) {
    std::array<uint8_t, internal::max_frame> buf;
    const size_t total = header.size() + data.size();
    if (total > buf.size()) {
        return error(errc::invalid_arg);
    }
    auto it = std::copy(header.begin(), header.end(), buf.begin());
    std::copy(data.begin(), data.end(), it);
    return s.transmit({buf.data(), total});
}

// Transmits `header` (which includes the NOP turnaround byte), clocking out
// one NOP per response byte, and copies the response into `out`.
result<void> read_framed(sx126x::state& s, std::span<const uint8_t> header, std::span<uint8_t> out) {
    std::array<uint8_t, internal::max_frame> tx_buf;
    std::array<uint8_t, internal::max_frame> rx_buf;
    const size_t total = header.size() + out.size();
    if (total > tx_buf.size()) {
        return error(errc::invalid_arg);
    }
    auto it = std::copy(header.begin(), header.end(), tx_buf.begin());
    std::fill_n(it, out.size(), 0); // NOPs clocked while the chip answers
    if (auto e = s.transfer({tx_buf.data(), total}, {rx_buf.data(), total}); !e) {
        return e;
    }
    std::copy_n(rx_buf.data() + header.size(), out.size(), out.data());
    return {};
}

} // namespace

result<void> sx126x::state::command(uint8_t opcode) {
    return transmit({&opcode, 1});
}

result<void> sx126x::state::command1(uint8_t opcode, uint8_t param) {
    std::array<uint8_t, 2> buf{opcode, param};
    return transmit(buf);
}

result<void> sx126x::state::write_command(uint8_t opcode, std::span<const uint8_t> params) {
    std::array<uint8_t, 1> header{opcode};
    return write_framed(*this, header, params);
}

result<void> sx126x::state::read_command(uint8_t opcode, std::span<uint8_t> response) {
    std::array<uint8_t, 2> header{opcode, 0x00}; // opcode + NOP turnaround
    return read_framed(*this, header, response);
}

result<void> sx126x::state::write_register(uint16_t addr, std::span<const uint8_t> data) {
    std::array<uint8_t, 3> header{
        internal::op_write_register, static_cast<uint8_t>(addr >> 8), static_cast<uint8_t>(addr)
    };
    return write_framed(*this, header, data);
}

result<void> sx126x::state::read_register(uint16_t addr, std::span<uint8_t> data) {
    std::array<uint8_t, 4> header{
        internal::op_read_register, static_cast<uint8_t>(addr >> 8), static_cast<uint8_t>(addr), 0x00
    }; // trailing NOP turnaround
    return read_framed(*this, header, data);
}

result<void> sx126x::state::write_buffer(uint8_t offset, std::span<const uint8_t> data) {
    std::array<uint8_t, 2> header{internal::op_write_buffer, offset};
    return write_framed(*this, header, data);
}

result<void> sx126x::state::read_buffer(uint8_t offset, std::span<uint8_t> data) {
    std::array<uint8_t, 3> header{internal::op_read_buffer, offset, 0x00}; // trailing NOP turnaround
    return read_framed(*this, header, data);
}

result<uint16_t> sx126x::state::read_irq_status() {
    std::array<uint8_t, 2> r{};
    if (auto e = read_command(internal::op_get_irq_status, r); !e) {
        return error(e.error());
    }
    return static_cast<uint16_t>((static_cast<uint16_t>(r[0]) << 8) | r[1]);
}

result<void> sx126x::state::clear_irq_status(uint16_t mask) {
    std::array<uint8_t, 2> p{static_cast<uint8_t>(mask >> 8), static_cast<uint8_t>(mask)};
    return write_command(internal::op_clear_irq_status, p);
}

// =============================================================================
// state teardown
// =============================================================================

sx126x::state::~state() {
    // Remove the DIO1 ISR first so a stale GPIO edge can't wake the worker
    // mid-shutdown, then stop and join the worker, then park the chip in sleep.
    // Running here (rather than in ~sx126x) means move-assignment, which
    // destroys the displaced state via unique_ptr, also tears down cleanly.
    dio1_isr = gpio::unique_isr_handle{};
    worker.reset();
    // With the worker joined, nothing can complete an in-flight operation any
    // more — cancel it so outstanding futures can't hang. The latch itself
    // stays alive through the futures' shared_ptr copies.
    {
        std::lock_guard g(mu);
        cancel_active_op(*this, errc::not_finished);
    }
    (void)command1(internal::op_set_sleep, internal::sleep_warm_start);
}

// =============================================================================
// Lifecycle
// =============================================================================

sx126x::sx126x(std::unique_ptr<state> s) noexcept
    : _state(std::move(s)) {}

result<sx126x> sx126x::make(spi::master_bus& bus, config cfg) {
    // Validate required pins.
    if (!cfg.cs.is_connected() || !cfg.busy.is_connected() || !cfg.dio1.is_connected()) {
        ESP_LOGE(TAG, "cs/busy/dio1 must all be set");
        return error(errc::invalid_arg);
    }

    // Construct SPI device for the chip.
    spi::master_device::config dev_cfg{
        .mode = 0,
        .clock_speed = cfg.clock_speed,
        .cs = cfg.cs,
        .queue_size = 4,
    };
    auto dev = spi::master_device::make(bus, dev_cfg);
    if (!dev) {
        return error(dev.error());
    }

    // Allocate state on the heap.
    auto s = std::make_unique<state>(std::move(*dev), std::move(cfg));

    // BUSY input pin direction (no pull — chip drives it).
    if (auto e = setup_busy_pin(*s); !e) {
        return error(e.error());
    }

    // External RF-switch control pins (if any) as outputs, parked low.
    if (auto e = setup_rf_switch_pins(*s); !e) {
        return error(e.error());
    }

    // Warm start attaches to a chip that already carries its configuration
    // (e.g. left duty-cycle listening across a host deep sleep): no reset
    // pulse, no configuration commands — either would destroy the chip state
    // (SetStandby kills an in-flight receive) or clear a latched IRQ the
    // caller wants to adopt via try_adopt_pending().
    if (!s->cfg.warm_start) {
        // Reset chip.
        if (auto e = reset_chip(*s); !e) {
            return error(e.error());
        }

        // Apply default chip configuration.
        if (auto e = apply_default_config(*s); !e) {
            return error(e.error());
        }
    }

    s->mode = chip_mode::stdby;

    // Spawn the worker task. The task body lives in sx126x_isr.cpp.
    task::config tcfg{
        .name = "radio_wkr",
        .stack_size = s->cfg.worker_stack_size,
        .priority = s->cfg.worker_priority,
        .core_affinity = s->cfg.worker_core,
    };
    state* state_ptr = s.get();
    s->worker.emplace(tcfg, [state_ptr](task::self& self) { sx126x_worker_fn(self, state_ptr); });

    // Install DIO1 ISR last (worker must exist when an event might arrive).
    auto isr = setup_dio1_isr(*s);
    if (!isr) {
        return error(isr.error());
    }
    s->dio1_isr = std::move(*isr);

    return sx126x{std::move(s)};
}

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
sx126x::sx126x(spi::master_bus& bus, config cfg)
    : sx126x(unwrap(make(bus, std::move(cfg)))) {}
#endif

sx126x::sx126x(sx126x&& other) noexcept = default;
sx126x& sx126x::operator=(sx126x&& other) noexcept = default;
sx126x::~sx126x() = default;

// =============================================================================
// Accessors
// =============================================================================

sx126x::chip_variant sx126x::variant() const noexcept {
    return _state ? _state->cfg.variant : chip_variant::sx1262;
}

spi::master_device& sx126x::spi() noexcept {
    return _state->device;
}

chip_mode sx126x::do_current_mode() const noexcept {
    if (!_state) {
        return chip_mode::sleep;
    }
    return _state->mode.load(std::memory_order_relaxed);
}

// =============================================================================
// Mode control
// =============================================================================

result<void> sx126x::do_standby() {
    return try_standby(standby_clock::rc);
}

result<void> sx126x::try_standby(standby_clock clock) {
    if (!_state) {
        return error(errc::invalid_state);
    }
    uint8_t cfg = clock == standby_clock::xosc ? internal::stdby_xosc : internal::stdby_rc;
    auto cmd = _state->command1(internal::op_set_standby, cfg);
    if (cmd) {
        // Clear all chip IRQs so an operation cancelled mid-flight can't leave
        // a stale DIO1 edge behind for the next operation to misread.
        (void)_state->clear_irq_status(0xFFFF);
    }
    // Reset the driver-side state and cancel any in-flight async operation
    // even when the standby command failed: callers — including the base
    // class's blocking-timeout recovery — rely on standby detaching the
    // operation so its receive buffer is never written after this returns.
    reset_idle(*_state);
    return cmd;
}

result<void> sx126x::do_sleep() {
    return try_sleep(sleep_mode::warm);
}

result<void> sx126x::try_sleep(sleep_mode mode) {
    if (!_state) {
        return error(errc::invalid_state);
    }
    uint8_t cfg = mode == sleep_mode::warm ? internal::sleep_warm_start : 0x00;
    auto cmd = _state->command1(internal::op_set_sleep, cfg);
    if (mode == sleep_mode::cold) {
        // Cold sleep loses the chip's calibration along with the rest of its
        // configuration; the next set_frequency must recalibrate.
        _state->cal_band.reset();
    }
    // As in try_standby: reset and cancel even on command failure so an
    // in-flight async operation can never strand its waiters or write a
    // caller's buffer after this returns.
    reset_idle(*_state, chip_mode::sleep);
    return cmd;
}

result<void> sx126x::do_start_listening() {
    if (!_state) {
        return error(errc::invalid_state);
    }
    // SetRx with timeout = 0xFFFFFF = continuous mode.
    std::array<uint8_t, 3> params{0xFF, 0xFF, 0xFF};
    return arm_stream_receive(*_state, internal::op_set_rx, params);
}

result<void> sx126x::do_start_listening(std::chrono::microseconds rx_period, std::chrono::microseconds sleep_period) {
    if (!_state) {
        return error(errc::invalid_state);
    }
    auto params = internal::pack_rx_duty_cycle(rx_period, sleep_period);
    return arm_stream_receive(*_state, internal::op_set_rx_duty_cycle, params);
}

std::chrono::microseconds sx126x::do_rx_duty_cycle_min_sleep() const noexcept {
    // The chip restarts its TCXO (when configured) on every wake from a
    // duty-cycle sleep window, so that start-up time is unavailable for
    // saving power and raises the shortest worthwhile sleep.
    auto min_sleep = default_min_rx_sleep;
    if (_state && _state->cfg.tcxo) {
        min_sleep += _state->cfg.tcxo->startup;
    }
    return min_sleep;
}

result<idfxx::future<cad_info>> sx126x::do_start_channel_scan() {
    if (!_state) {
        return error(errc::invalid_state);
    }
    auto& s = *_state;

    // Claim the data path (idle -> cad_scan) and arm the latch so the worker
    // completes the future on cad_done alongside posting the event.
    auto latch = claim_data_path(s, internal::driver_activity::cad_scan, chip_mode::cad);
    if (!latch) {
        return error(latch.error());
    }

    // Point the RF switch at the receiver and start CAD. The chip's CAD
    // parameters were set at construction (and possibly tuned via
    // set_cad_params); they persist across scans.
    sx126x_set_rf_switch(s, chip_mode::cad);
    if (auto e = s.command(internal::op_set_cad); !e) {
        reset_idle(s);
        return error(e.error());
    }

    return make_op_future<cad_info>(std::move(*latch), [](internal::op_latch& l) -> result<cad_info> {
        return cad_info{.detected = l.cad_detected};
    });
}

// =============================================================================
// Configuration
// =============================================================================

result<void> sx126x::do_set_frequency(freq::hertz hz) {
    if (!_state) {
        return error(errc::invalid_state);
    }
    // Calibrate the image rejection for this frequency's band before tuning
    // (DS §9.2.1), unless the band is already calibrated — frequency hops
    // within one band then skip straight to SetRfFrequency. Band bytes
    // re-verify against DS_SX1261-2 before shipping.
    auto band = internal::calibrate_image_bytes(static_cast<uint64_t>(hz.count()));
    if (_state->cal_band != band) {
        std::array<uint8_t, 2> img{band.f1, band.f2};
        if (auto e = _state->write_command(internal::op_calibrate_image, img); !e) {
            return e;
        }
        _state->cal_band = band;
    }
    uint32_t reg = internal::freq_to_register(static_cast<uint64_t>(hz.count()));
    std::array<uint8_t, 4> params{
        static_cast<uint8_t>(reg >> 24),
        static_cast<uint8_t>(reg >> 16),
        static_cast<uint8_t>(reg >> 8),
        static_cast<uint8_t>(reg),
    };
    return _state->write_command(internal::op_set_rf_frequency, params);
}

result<void> sx126x::do_set_output_power(electro::dbm power, ramp_time ramp) {
    if (!_state) {
        return error(errc::invalid_state);
    }
    auto limits = internal::power_limits_for(_state->cfg.variant);
    if (power.count() < limits.min_dbm || power.count() > limits.max_dbm) {
        ESP_LOGW(
            TAG,
            "output power %d dBm outside variant range %d..%d dBm",
            static_cast<int>(power.count()),
            limits.min_dbm,
            limits.max_dbm
        );
        return error(errc::invalid_arg);
    }
    // SetPaConfig for the requested power (the SX1261's +15 dBm needs a
    // different PA duty cycle and a +14 SetTxParams byte), then SetTxParams.
    auto txp = internal::tx_power_config_for(_state->cfg.variant, static_cast<int8_t>(power.count()));
    std::array<uint8_t, 4> pa_params{txp.pa.pa_duty_cycle, txp.pa.hp_max, txp.pa.device_sel, txp.pa.pa_lut};
    if (auto e = _state->write_command(internal::op_set_pa_config, pa_params); !e) {
        return e;
    }
    std::array<uint8_t, 2> params{static_cast<uint8_t>(txp.power_byte), internal::ramp_time_byte(ramp)};
    return _state->write_command(internal::op_set_tx_params, params);
}

result<void> sx126x::do_set_lora_modulation(lora_modulation mod) {
    if (!_state) {
        return error(errc::invalid_state);
    }
    std::array<uint8_t, 4> params{
        internal::spreading_factor_byte(mod.sf),
        internal::bandwidth_byte(mod.bw),
        internal::coding_rate_byte(mod.cr),
        static_cast<uint8_t>(mod.low_data_rate_optimize ? 0x01 : 0x00),
    };
    if (auto e = _state->write_command(internal::op_set_mod_params, params); !e) {
        return e;
    }
    // Errata §15.1: bit 2 of reg_tx_modulation must be cleared for 500 kHz LoRa
    // bandwidth and set for every other bandwidth to meet the modulation-quality
    // spec. Applies to all SX126x variants. (Re-verify register/bit vs datasheet.)
    std::array<uint8_t, 1> tm{};
    if (auto e = _state->read_register(internal::reg_tx_modulation, tm); !e) {
        return e;
    }
    if (mod.bw == bandwidth::bw_500) {
        tm[0] &= static_cast<uint8_t>(~internal::tx_modulation_bw500_bit);
    } else {
        tm[0] |= internal::tx_modulation_bw500_bit;
    }
    return _state->write_register(internal::reg_tx_modulation, tm);
}

result<void> sx126x::do_set_lora_packet_params(lora_packet_params params) {
    if (!_state) {
        return error(errc::invalid_state);
    }
    auto packed = internal::pack_packet_params(params);
    if (auto e = _state->write_command(internal::op_set_packet_params, packed); !e) {
        return e;
    }
    // Cache so transmit can re-issue with an updated payload length without
    // clobbering the caller's header/CRC/IQ choices.
    _state->last_packet_params = params;
    return {};
}

result<void> sx126x::do_set_sync_word(lora_network network) {
    return try_set_sync_word(network == lora_network::public_network ? sync_word_public : sync_word_private);
}

result<void> sx126x::try_set_sync_word(uint16_t sync_word) {
    if (!_state) {
        return error(errc::invalid_state);
    }
    auto sync = internal::pack_sync_word(sync_word);
    return _state->write_register(internal::reg_lora_sync_word_msb, sync);
}

// =============================================================================
// Data path: async
// =============================================================================

result<idfxx::future<void>> sx126x::do_start_transmit(std::span<const uint8_t> data) {
    if (!_state) {
        return error(errc::invalid_state);
    }
    // Stage and start the transmission, then return immediately. The worker's
    // tx_done path completes the latch, resets the driver state, and posts the
    // tx_done event.
    auto latch = begin_transmit(*_state, data);
    if (!latch) {
        return error(latch.error());
    }
    return make_op_future<void>(std::move(*latch), [](internal::op_latch&) -> result<void> { return {}; });
}

result<idfxx::future<rx_info>> sx126x::do_start_receive(std::span<uint8_t> buffer) {
    if (!_state) {
        return error(errc::invalid_state);
    }
    auto& s = *_state;

    // Claim the data path (idle -> rx_single) and arm the latch, recording the
    // caller's buffer on it. The worker copies the payload there on rx_done;
    // cancellation paths clear the reference before completing the latch, so
    // the buffer is never written after the future completes.
    auto latch = claim_data_path(s, internal::driver_activity::rx_single, chip_mode::rx, buffer);
    if (!latch) {
        return error(latch.error());
    }

    if (auto e = apply_rx_boost(s); !e) {
        reset_idle(s);
        return error(e.error());
    }

    // Start a single-shot receive with no chip timeout (the chip's SetRx
    // timeout has an RTC-counter errata; timeouts are managed in software by
    // waiting on the future).
    sx126x_set_rf_switch(s, chip_mode::rx);
    std::array<uint8_t, 3> rx_params{0x00, 0x00, 0x00};
    if (auto e = s.write_command(internal::op_set_rx, rx_params); !e) {
        reset_idle(s);
        return error(e.error());
    }

    return make_op_future<rx_info>(std::move(*latch), [](internal::op_latch& l) -> result<rx_info> { return l.rx; });
}

result<rx_info> sx126x::do_read_received(std::span<uint8_t> buffer) {
    if (!_state) {
        return error(errc::invalid_state);
    }
    std::lock_guard g(_state->mu);
    if (_state->last_rx.length == 0) {
        return error(errc::not_found);
    }
    size_t n = std::min<size_t>(_state->last_rx.length, buffer.size());
    std::copy_n(_state->last_rx_buf.data(), n, buffer.data());
    rx_info info = _state->last_rx;
    info.length = static_cast<uint8_t>(n);
    return info;
}

// =============================================================================
// Status
// =============================================================================

result<packet_status> sx126x::do_last_packet_status() {
    if (!_state) {
        return error(errc::invalid_state);
    }
    std::array<uint8_t, 3> r{};
    if (auto e = _state->read_command(internal::op_get_packet_status, r); !e) {
        return error(e.error());
    }
    return internal::decode_packet_status(r);
}

result<electro::centi_dbm> sx126x::do_current_rssi() {
    if (!_state) {
        return error(errc::invalid_state);
    }
    std::array<uint8_t, 1> r{};
    if (auto e = _state->read_command(internal::op_get_rssi_inst, r); !e) {
        return error(e.error());
    }
    return internal::decode_rssi_byte(r[0]);
}

// =============================================================================
// SX126x-specific surface
// =============================================================================

result<std::optional<rx_info>> sx126x::try_adopt_pending() {
    if (!_state) {
        return error(errc::invalid_state);
    }
    auto& s = *_state;

    auto mask = s.read_irq_status();
    if (!mask) {
        return error(mask.error());
    }
    if (*mask == 0) {
        return std::optional<rx_info>{};
    }
    if (auto e = s.clear_irq_status(*mask); !e) {
        return error(e.error());
    }

    const flags<irq_flag> irqs{static_cast<irq_flag>(*mask)};

    if (irqs.contains(irq_flag::rx_done)) {
        // After a latched rx_done the chip has already fallen back to
        // standby on its own (single-shot and duty-cycled receive both stop
        // there); align the driver's bookkeeping before handing back.
        {
            std::lock_guard g(s.mu);
            s.mode = chip_mode::stdby;
            sx126x_set_rf_switch(s, chip_mode::stdby);
        }
        auto drained = sx126x_drain_received(s);
        if (!drained) {
            return error(drained.error());
        }
        if (irqs.contains(irq_flag::crc_err)) {
            return error(errc::invalid_crc);
        }
        return std::optional<rx_info>{*drained};
    }

    if (irqs.contains(irq_flag::crc_err)) {
        return error(errc::invalid_crc);
    }

    // Something else was latched (e.g. a stale tx_done); nothing to adopt.
    return std::optional<rx_info>{};
}

result<void> sx126x::try_set_cad_params(cad_params params) {
    if (!_state) {
        return error(errc::invalid_state);
    }
    return write_cad_params(*_state, params);
}

result<flags<sx126x::irq_flag>> sx126x::try_irq_status() {
    if (!_state) {
        return error(errc::invalid_state);
    }
    auto raw = _state->read_irq_status();
    if (!raw) {
        return error(raw.error());
    }
    return flags<irq_flag>{static_cast<irq_flag>(*raw)};
}

result<void> sx126x::try_clear_irq_status(flags<irq_flag> mask) {
    if (!_state) {
        return error(errc::invalid_state);
    }
    return _state->clear_irq_status(idfxx::to_underlying(mask));
}

// =============================================================================
// Low-level opcode escape hatch
// =============================================================================

result<void> sx126x::try_write_command(uint8_t opcode, std::span<const uint8_t> params) {
    if (!_state) {
        return error(errc::invalid_state);
    }
    return _state->write_command(opcode, params);
}

result<void> sx126x::try_read_command(uint8_t opcode, std::span<uint8_t> response) {
    if (!_state) {
        return error(errc::invalid_state);
    }
    return _state->read_command(opcode, response);
}

result<void> sx126x::try_write_register(uint16_t addr, std::span<const uint8_t> data) {
    if (!_state) {
        return error(errc::invalid_state);
    }
    return _state->write_register(addr, data);
}

result<void> sx126x::try_read_register(uint16_t addr, std::span<uint8_t> data) {
    if (!_state) {
        return error(errc::invalid_state);
    }
    return _state->read_register(addr, data);
}

result<void> sx126x::try_write_buffer(uint8_t offset, std::span<const uint8_t> data) {
    if (!_state) {
        return error(errc::invalid_state);
    }
    return _state->write_buffer(offset, data);
}

result<void> sx126x::try_read_buffer(uint8_t offset, std::span<uint8_t> data) {
    if (!_state) {
        return error(errc::invalid_state);
    }
    return _state->read_buffer(offset, data);
}

} // namespace idfxx::radio
