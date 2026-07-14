// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

// Private SX126x driver internals — opcodes, register addresses, per-variant
// PA-config tables, and the out-of-line driver state. Not part of the public
// API and not installed. Pure register encoders live in sx126x_codec.hpp.

#include "sx126x_codec.hpp"

#include <idfxx/event>
#include <idfxx/event_group>
#include <idfxx/flags>
#include <idfxx/gpio>
#include <idfxx/radio/sx126x.hpp>
#include <idfxx/spi/master>
#include <idfxx/task>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <span>

namespace idfxx::radio::sx126x_internal {

// =============================================================================
// Opcodes
//
// Opcodes are listed in the DS_SX1261-2_V2.2 command tables (§11, tables
// 11-1..11-5) and each command is detailed in §13. The trailing reference on
// each line points at the command's subsection in §13.
// =============================================================================

// §13.1 Operational-mode commands.
inline constexpr uint8_t op_get_status = 0xC0;         // §13.5.1 GetStatus
inline constexpr uint8_t op_set_sleep = 0x84;          // §13.1.1 SetSleep
inline constexpr uint8_t op_set_standby = 0x80;        // §13.1.2 SetStandby
inline constexpr uint8_t op_set_tx = 0x83;             // §13.1.4 SetTx
inline constexpr uint8_t op_set_rx = 0x82;             // §13.1.5 SetRx
inline constexpr uint8_t op_set_rx_duty_cycle = 0x94;  // §13.1.7 SetRxDutyCycle
inline constexpr uint8_t op_set_cad = 0xC5;            // §13.1.8 SetCAD
inline constexpr uint8_t op_set_regulator_mode = 0x96; // §13.1.11 SetRegulatorMode
inline constexpr uint8_t op_calibrate = 0x89;          // §13.1.12 Calibrate
inline constexpr uint8_t op_calibrate_image = 0x98;    // §13.1.13 CalibrateImage
inline constexpr uint8_t op_set_pa_config = 0x95;      // §13.1.14 SetPaConfig

// §13.2 Register and buffer access.
inline constexpr uint8_t op_write_register = 0x0D; // §13.2.1 WriteRegister
inline constexpr uint8_t op_read_register = 0x1D;  // §13.2.2 ReadRegister
inline constexpr uint8_t op_write_buffer = 0x0E;   // §13.2.3 WriteBuffer
inline constexpr uint8_t op_read_buffer = 0x1E;    // §13.2.4 ReadBuffer

// §13.3 DIO and IRQ control.
inline constexpr uint8_t op_set_dio_irq_params = 0x08;    // §13.3.1 SetDioIrqParams
inline constexpr uint8_t op_get_irq_status = 0x12;        // §13.3.3 GetIrqStatus
inline constexpr uint8_t op_clear_irq_status = 0x02;      // §13.3.4 ClearIrqStatus
inline constexpr uint8_t op_set_dio2_as_rf_switch = 0x9D; // §13.3.5 SetDIO2AsRfSwitchCtrl
inline constexpr uint8_t op_set_dio3_as_tcxo_ctrl = 0x97; // §13.3.6 SetDIO3AsTCXOCtrl

// §13.4 RF, modulation and packet commands.
inline constexpr uint8_t op_set_rf_frequency = 0x86;     // §13.4.1 SetRfFrequency
inline constexpr uint8_t op_set_packet_type = 0x8A;      // §13.4.2 SetPacketType
inline constexpr uint8_t op_set_tx_params = 0x8E;        // §13.4.4 SetTxParams
inline constexpr uint8_t op_set_mod_params = 0x8B;       // §13.4.5 SetModulationParams
inline constexpr uint8_t op_set_packet_params = 0x8C;    // §13.4.6 SetPacketParams
inline constexpr uint8_t op_set_cad_params = 0x88;       // §13.4.7 SetCadParams
inline constexpr uint8_t op_set_buffer_base_addr = 0x8F; // §13.4.8 SetBufferBaseAddress

// §13.5 Communication status.
inline constexpr uint8_t op_get_rx_buffer_status = 0x13; // §13.5.2 GetRxBufferStatus
inline constexpr uint8_t op_get_packet_status = 0x14;    // §13.5.3 GetPacketStatus
inline constexpr uint8_t op_get_rssi_inst = 0x15;        // §13.5.4 GetRssiInst

// Packet type (§13.4.2 SetPacketType, table 13-38).
inline constexpr uint8_t packet_type_lora = 0x01;

// Standby clock source (§13.1.2 SetStandby, table 13-4).
inline constexpr uint8_t stdby_rc = 0x00;
inline constexpr uint8_t stdby_xosc = 0x01;

// Sleep config bits (§13.1.1 SetSleep, table 13-2).
inline constexpr uint8_t sleep_warm_start = 0b0000'0100; // bit 2: retain config

// Calibrate all blocks: RC64k, RC13M, PLL, ADC ×3, image (§13.1.12, table 13-18).
inline constexpr uint8_t calibrate_all = 0x7F;

// =============================================================================
// Register addresses (DS_SX1261-2_V2.2, §12 table 12-1; errata §15)
// =============================================================================

// LoRa sync-word register address (16-bit value, MSB first; table 12-1).
inline constexpr uint16_t reg_lora_sync_word_msb = 0x0740;

// Errata / tuning registers (addresses from table 12-1; the errata bit
// definitions are in §15).
inline constexpr uint16_t reg_tx_modulation = 0x0889;   // §15.1 BW500 modulation quality
inline constexpr uint16_t reg_rx_gain = 0x08AC;         // Rx gain (§9.6, table 9-3)
inline constexpr uint16_t reg_tx_clamp_config = 0x08D8; // §15.2 antenna-mismatch Tx clamp

inline constexpr uint8_t tx_modulation_bw500_bit = 0x04; // §15.1: bit 2 of reg_tx_modulation
inline constexpr uint8_t rx_gain_boosted = 0x96;         // table 9-3: Rx boosted gain (best sensitivity)
inline constexpr uint8_t tx_clamp_bits = 0x1E;           // §15.2: bits 4..1 of reg_tx_clamp_config

// =============================================================================
// PA-config table (one row per chip variant)
// =============================================================================

// The four SetPaConfig parameter bytes (DS §13.1.14, table 13-20).
struct pa_config_row {
    uint8_t pa_duty_cycle;
    uint8_t hp_max;
    uint8_t device_sel; // 0 = SX1262/SX1268 (high-power PA), 1 = SX1261 (low-power PA)
    uint8_t pa_lut;     // reserved, always 0x01
};

/// Output-power limits for a chip variant (DS §13.4.4 SetTxParams; the
/// SX1261's +15 dBm is reached via the table 13-21 PA row, not a SetTxParams
/// byte — see tx_power_config_for).
struct power_limits {
    int8_t min_dbm;
    int8_t max_dbm;
};

[[nodiscard]] constexpr power_limits power_limits_for(sx126x::chip_variant v) noexcept {
    if (v == sx126x::chip_variant::sx1261) {
        return {.min_dbm = -17, .max_dbm = 15};
    }
    return {.min_dbm = -9, .max_dbm = 22}; // SX1262 / SX1268
}

/// PA config plus the SetTxParams power byte realizing a requested output
/// power. The dBm must already be validated against power_limits_for().
struct tx_power_config {
    pa_config_row pa;
    int8_t power_byte;
};

/// Returns the PA configuration and SetTxParams power byte for a requested
/// output power (DS §13.1.14.1, table 13-21 "PA Operating Modes with Optimal
/// Settings"). The SX1262/SX1268 high-power PA uses its maximum-power row and
/// trims via SetTxParams. The SX1261's SetTxParams accepts only -17..+14
/// (§13.4.4); +15 dBm requires the high-duty-cycle PA row with a +14 power byte.
[[nodiscard]] constexpr tx_power_config tx_power_config_for(sx126x::chip_variant v, int8_t dbm) noexcept {
    if (v == sx126x::chip_variant::sx1261) {
        if (dbm >= 15) {
            return {
                .pa = {.pa_duty_cycle = 0x06, .hp_max = 0x00, .device_sel = 0x01, .pa_lut = 0x01}, .power_byte = 14
            };
        }
        return {.pa = {.pa_duty_cycle = 0x04, .hp_max = 0x00, .device_sel = 0x01, .pa_lut = 0x01}, .power_byte = dbm};
    }
    // SX1262 / SX1268 high-power PA, +22 dBm row.
    return {.pa = {.pa_duty_cycle = 0x04, .hp_max = 0x07, .device_sel = 0x00, .pa_lut = 0x01}, .power_byte = dbm};
}

/// True for the high-power PA variants (SX1262/SX1268) that the §15.2 Tx-clamp
/// errata workaround targets.
[[nodiscard]] constexpr bool is_high_power(sx126x::chip_variant v) noexcept {
    return v != sx126x::chip_variant::sx1261;
}

// =============================================================================
// Driver state
// =============================================================================

/// Largest SPI frame the driver stages: 4-byte register header + 256-byte
/// payload. Sizes every transfer buffer so reads/writes are never silently
/// truncated.
inline constexpr size_t max_frame = 260;

/// High-level driver activity, distinct from the chip's hardware mode.
enum class driver_activity : uint8_t {
    idle,
    tx_in_flight,
    rx_single,
    rx_continuous,
    cad_scan, ///< Blocking channel scan; the worker completes a waiter on cad_done.
};

/// Single-bit event group used to signal async-operation completion from the
/// worker task.
enum class completion_bit : uint32_t {
    done = 1u << 0,
};

} // namespace idfxx::radio::sx126x_internal

template<>
inline constexpr bool idfxx::enable_flags_operators<idfxx::radio::sx126x_internal::completion_bit> = true;

namespace idfxx::radio::sx126x_internal {

/// Completion latch for one async data-path operation (transmit, single-shot
/// receive, or channel scan). Heap-allocated per operation and shared between
/// the driver state (which holds it while the operation is in flight) and any
/// futures handed to the caller, so a future outliving the operation — or the
/// driver itself — still observes the result. The event group broadcasts the
/// wake-up to every future copy; `done` gives futures a cheap non-consuming
/// completion check.
struct op_latch {
    idfxx::event_group<completion_bit> eg;
    std::atomic<bool> done{false};
    std::atomic<int32_t> error{0};  ///< errc value; 0 = success.
    rx_info rx{};                   ///< Receive result (written before `done`, read after).
    bool cad_detected = false;      ///< Channel-scan result (written before `done`, read after).
    std::span<uint8_t> rx_target{}; ///< Caller's buffer (single-shot receive only); cleared on cancel.

    /// Publishes the operation result and wakes every waiter. The result
    /// fields (`rx`, `cad_detected`) must be written before this is called;
    /// callers serialize completion under the driver state's mutex.
    void complete(int32_t err_code) noexcept {
        error.store(err_code, std::memory_order_relaxed);
        done.store(true, std::memory_order_release);
        (void)eg.set(completion_bit::done);
    }
};

} // namespace idfxx::radio::sx126x_internal

namespace idfxx::radio {

/// Out-of-line driver state. One per sx126x instance; lives on the heap so the
/// public type stays cheap to move while the worker task and DIO1 ISR see a
/// stable address.
struct sx126x::state {
    sx126x::config cfg;
    spi::master_device device;

    // Declared before `worker` so that, even absent the explicit teardown in
    // ~state, the ISR is removed before the worker is joined.
    gpio::unique_isr_handle dio1_isr;
    std::optional<task> worker;

    std::mutex mu;

    // Serializes raw chip SPI access (the BUSY wait + transfer sequence in
    // transfer()/transmit()) between API callers and the worker task. A
    // dedicated leaf-level mutex: `mu` cannot serve — the worker's drain path
    // holds it across read_buffer(). spi_device_acquire_bus() cannot serve
    // either — it is not mutual exclusion between threads sharing one device
    // handle (it errors out, and a polling transaction in flight on the same
    // device makes it fail outright).
    std::mutex spi_mu;

    // Read lock-free by current_mode(); written under `mu` alongside driver_state.
    std::atomic<chip_mode> mode{chip_mode::stdby};
    sx126x_internal::driver_activity driver_state = sx126x_internal::driver_activity::idle;

    // Most-recent packet params written via set_lora_packet_params; transmit
    // re-issues these with an updated payload length so it never clobbers the
    // caller's header/CRC/IQ choices.
    lora_packet_params last_packet_params{};

    // Band of the most recent CalibrateImage, so retuning within the same band
    // (e.g. frequency hopping) skips the redundant recalibration. Cleared on
    // cold sleep, which loses the chip's calibration.
    std::optional<sx126x_internal::image_cal_band> cal_band;

    // RX cache populated by the worker on rx_done so the event-loop client can
    // read out the most recent packet.
    rx_info last_rx{};
    std::array<uint8_t, 256> last_rx_buf{};

    // Latch for the in-flight async data-path operation (transmit, single-shot
    // receive, or channel scan); null when none is in flight. Guarded by `mu`
    // alongside driver_state: the starter installs it, and exactly one of the
    // worker (on the completion IRQ) or a cancellation path (standby, sleep,
    // teardown) detaches and completes it. Futures keep the latch alive via
    // their own shared_ptr copies.
    std::shared_ptr<sx126x_internal::op_latch> active_op;

    explicit state(spi::master_device dev, sx126x::config c)
        : cfg(std::move(c))
        , device(std::move(dev)) {}

    ~state();

    state(const state&) = delete;
    state& operator=(const state&) = delete;

    // =========================================================================
    // SPI I/O — the single home for the BUSY-wait + transfer sequence. Shared
    // by the public API (sx126x.cpp) and the worker (sx126x_isr.cpp). Each
    // method takes `spi_mu` first, then waits BUSY low, then transfers, so
    // the BUSY check and the transaction are atomic with respect to the lock.
    // =========================================================================

    /// Waits for BUSY to go low (no-op if BUSY is not wired). Must be called
    /// with `spi_mu` held by the transfer primitives; safe standalone after
    /// reset, where no transfer immediately follows.
    [[nodiscard]] result<void> wait_busy();

    /// wait_busy(), waking the chip first if it turns out to be asleep: in
    /// SLEEP mode — including the sleep phases of a duty-cycled receive —
    /// BUSY idles high and only an NSS falling edge wakes the chip, so a
    /// bare BUSY wait can never succeed. A full wait_busy() timeout (far
    /// beyond any real command's processing time) is taken as "asleep": a
    /// GetStatus frame is clocked to provide the NSS edge (its content is
    /// ignored by a sleeping chip), then BUSY is waited again. Requires
    /// `spi_mu` held.
    [[nodiscard]] result<void> wait_busy_waking();

    /// Full-duplex transfer of equal-length tx/rx spans.
    [[nodiscard]] result<void> transfer(std::span<const uint8_t> tx, std::span<uint8_t> rx);
    /// Half-duplex transmit.
    [[nodiscard]] result<void> transmit(std::span<const uint8_t> tx);

    /// Single-opcode command, no parameters.
    [[nodiscard]] result<void> command(uint8_t opcode);
    /// Opcode + one parameter byte.
    [[nodiscard]] result<void> command1(uint8_t opcode, uint8_t param);
    /// Opcode + parameter bytes.
    [[nodiscard]] result<void> write_command(uint8_t opcode, std::span<const uint8_t> params);
    /// Opcode + 1-byte turnaround, then reads `response` bytes.
    [[nodiscard]] result<void> read_command(uint8_t opcode, std::span<uint8_t> response);
    /// Writes bytes to a 16-bit register address.
    [[nodiscard]] result<void> write_register(uint16_t addr, std::span<const uint8_t> data);
    /// Reads bytes from a 16-bit register address.
    [[nodiscard]] result<void> read_register(uint16_t addr, std::span<uint8_t> data);
    /// Writes bytes to the chip's data buffer at an offset.
    [[nodiscard]] result<void> write_buffer(uint8_t offset, std::span<const uint8_t> data);
    /// Reads bytes from the chip's data buffer at an offset.
    [[nodiscard]] result<void> read_buffer(uint8_t offset, std::span<uint8_t> data);

    /// Reads the 16-bit GetIrqStatus register.
    [[nodiscard]] result<uint16_t> read_irq_status();
    /// Clears the given chip IRQ bits.
    [[nodiscard]] result<void> clear_irq_status(uint16_t mask);
};

// External-linkage helper shared by the public API (sx126x.cpp) and the worker
// (sx126x_isr.cpp), following the sx126x_worker_fn/sx126x_dio1_isr_thunk
// precedent. Drives the external RF-switch GPIOs (config rxen/txen) to match the
// requested chip mode: tx asserts txen, rx/cad assert rxen, everything else
// parks both low. Defined once in sx126x.cpp. No-op on unconnected pins.
void sx126x_set_rf_switch(sx126x::state& s, chip_mode mode) noexcept;

} // namespace idfxx::radio
