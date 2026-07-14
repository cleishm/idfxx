// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#include "sx126x_internal.hpp"

#include <idfxx/radio/sx126x>

#include <algorithm>
#include <array>
#include <cstddef>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <mutex>
#include <utility>

namespace idfxx::radio {

namespace internal = sx126x_internal;

namespace {

constexpr const char* TAG = "idfxx::radio::sx126x::isr";

// Parks the chip-mode bookkeeping (and RF switch) in standby and, when the
// driver is still in `expected` activity — a cancellation may have detached
// the latch already — returns it to idle and completes the in-flight latch,
// with `fill` writing the operation's result fields first. Used by the
// tx_done and cad_done paths; rx_done has its own variant because it must
// park only when a single-shot receive was in flight (a continuous receive
// stays in rx) and copies the payload out under the same lock.
template<typename Fill>
void park_and_complete(sx126x::state& s, internal::driver_activity expected, Fill&& fill) {
    std::lock_guard g(s.mu);
    s.mode = chip_mode::stdby;
    sx126x_set_rf_switch(s, chip_mode::stdby);
    if (s.driver_state != expected) {
        return;
    }
    s.driver_state = internal::driver_activity::idle;
    if (auto latch = std::move(s.active_op)) {
        fill(*latch);
        latch->complete(0);
    }
}

} // namespace

// Reads chip RX buffer status + packet status + the payload into the state's
// last_rx cache. The single-RX caller's buffer (if any) is filled from that
// cache by the rx_done path, under the same lock that completes the latch.
// External linkage: also the recovery path of sx126x::try_adopt_pending().
result<rx_info> sx126x_drain_received(sx126x::state& s) {
    rx_info info{};

    // GetRxBufferStatus (§13.5.2) -> [payload_len, rx_start_buffer_ptr].
    std::array<uint8_t, 2> rxs{};
    if (auto e = s.read_command(internal::op_get_rx_buffer_status, rxs); !e) {
        return error(e.error());
    }
    info.length = rxs[0];
    uint8_t rx_offset = rxs[1];

    // GetPacketStatus (§13.5.3) -> [rssi_pkt, snr_pkt, signal_rssi_pkt].
    std::array<uint8_t, 3> pkt{};
    if (auto e = s.read_command(internal::op_get_packet_status, pkt); !e) {
        return error(e.error());
    }
    auto ps = internal::decode_packet_status(pkt);
    info.rssi = ps.rssi;
    info.snr = ps.snr;

    // Read the payload from the chip buffer (ReadBuffer, §13.2.4) straight into
    // the last-packet cache (full length; per-caller copies clamp). Holding
    // `mu` across the read keeps the cache coherent for concurrent
    // read_received callers.
    std::lock_guard g(s.mu);
    if (info.length > 0) {
        if (auto e = s.read_buffer(rx_offset, {s.last_rx_buf.data(), info.length}); !e) {
            // The cache bytes may be partially overwritten; invalidate it.
            s.last_rx = {};
            return error(e.error());
        }
    }
    s.last_rx = info;

    return info;
}

void sx126x_dio1_isr_thunk(void* arg) {
    auto* state = static_cast<sx126x::state*>(arg);
    if (state && state->worker) {
        bool need_yield = state->worker->notify_from_isr();
        if (need_yield) {
            portYIELD_FROM_ISR();
        }
    }
}

void sx126x_worker_fn(task::self& self, sx126x::state* sp) {
    if (!sp) {
        return;
    }
    sx126x::state& s = *sp;
    event_loop* const loop = s.cfg.loop; // may be null — posting is then skipped

    while (!self.stop_requested()) {
        // Wait for an ISR notification, with a poll fallback: DIO1 is
        // edge-triggered, so a missed edge (e.g. a transient SPI failure
        // aborting the drain loop below while the line is still high, or an
        // edge lost during a flash-cache-disabled window) would otherwise
        // leave the chip's IRQ latched — and the line high — forever. The
        // periodic status poll turns that permanent wedge into a bounded
        // delay.
        const bool notified = self.wait_for(std::chrono::seconds{1});
        if (self.stop_requested()) {
            break;
        }
        if (!notified) {
            // Poll tick: skip while the chip sleeps — SPI activity (NSS
            // low) would wake it, and BUSY stays high anyway.
            std::unique_lock g(s.mu);
            if (s.mode == chip_mode::sleep) {
                continue;
            }
            // Same for the sleep phases of a duty-cycled receive (mode is
            // `rx` there): BUSY idles high and an SPI poll would wake the
            // chip and abort the autonomous listen. BUSY high with nothing
            // latched also means nothing to drain — an rx_done returns the
            // chip to standby, where BUSY is low.
            if (s.cfg.busy.is_connected() && s.cfg.busy.get_level() == gpio::level::high) {
                continue;
            }
        }

        // Drain pending IRQs in a loop in case multiple events stacked up.
        for (;;) {
            auto mask = s.read_irq_status();
            if (!mask) {
                ESP_LOGW(TAG, "read_irq_status failed: %s", mask.error().message().c_str());
                break;
            }
            if (*mask == 0) {
                break;
            }
            // Clear immediately so the chip can re-arm DIO1.
            if (auto e = s.clear_irq_status(*mask); !e) {
                ESP_LOGW(TAG, "clear_irq_status failed: %s", e.error().message().c_str());
            }

            const flags<sx126x::irq_flag> irqs{static_cast<sx126x::irq_flag>(*mask)};

            // TX done: complete the operation latch (waking any futures), post
            // the event.
            if (irqs.contains(sx126x::irq_flag::tx_done)) {
                park_and_complete(s, internal::driver_activity::tx_in_flight, [](internal::op_latch&) {});
                if (loop) {
                    (void)loop->try_post(tx_done);
                }
            }

            // RX done: drain payload, complete a pending single-RX and/or post.
            if (irqs.contains(sx126x::irq_flag::rx_done)) {
                bool crc_failed = irqs.contains(sx126x::irq_flag::crc_err);
                auto drained = sx126x_drain_received(s);
                rx_info info{};
                int32_t err_code = 0;
                if (!drained) {
                    err_code = static_cast<int32_t>(std::to_underlying(errc::fail));
                } else {
                    info = *drained;
                    if (crc_failed) {
                        err_code = static_cast<int32_t>(std::to_underlying(errc::invalid_crc));
                    }
                }

                // Complete the single-RX (if any): copy the payload into the
                // caller's buffer (clamped to its size, with the reported
                // length matching the clamp) and finish the latch, all under
                // one lock so cancellation can't interleave.
                {
                    std::lock_guard g(s.mu);
                    if (s.driver_state == internal::driver_activity::rx_single) {
                        s.mode = chip_mode::stdby;
                        sx126x_set_rf_switch(s, chip_mode::stdby);
                        s.driver_state = internal::driver_activity::idle;
                        if (auto latch = std::move(s.active_op)) {
                            size_t n = std::min<size_t>(info.length, latch->rx_target.size());
                            if (drained && n > 0) {
                                std::copy_n(s.last_rx_buf.data(), n, latch->rx_target.data());
                            }
                            latch->rx = info;
                            latch->rx.length = static_cast<uint8_t>(n);
                            latch->rx_target = {};
                            latch->complete(err_code);
                        }
                    }
                }

                if (loop) {
                    if (crc_failed) {
                        (void)loop->try_post(crc_error);
                    } else if (drained) {
                        (void)loop->try_post(rx_done, info);
                    }
                }
            } else if (irqs.contains_any(sx126x::irq_flag::crc_err | sx126x::irq_flag::header_err) && loop) {
                // CRC or header error without rx_done — surface as event. A
                // header error also fires when an explicit-header packet's
                // received length exceeds the programmed payload_length.
                (void)loop->try_post(crc_error);
            }

            // CAD done: complete the scan's latch with the detected flag, post
            // event.
            if (irqs.contains(sx126x::irq_flag::cad_done)) {
                cad_info ci{.detected = irqs.contains(sx126x::irq_flag::cad_detected)};
                park_and_complete(s, internal::driver_activity::cad_scan, [&](internal::op_latch& l) {
                    l.cad_detected = ci.detected;
                });
                if (loop) {
                    (void)loop->try_post(cad_done, ci);
                }
            }

            // Preamble detected (continuous RX only).
            if (irqs.contains(sx126x::irq_flag::preamble_detected) && loop) {
                (void)loop->try_post(preamble_detected);
            }
        }
    }
}

} // namespace idfxx::radio
