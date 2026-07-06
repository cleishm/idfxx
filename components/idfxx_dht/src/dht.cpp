// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#include "dht_decode.hpp"

#include <idfxx/chrono>
#include <idfxx/dht.hpp>
#include <idfxx/sched>

#include <chrono>
#include <driver/rmt_rx.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <memory>
#include <utility>

namespace idfxx::dht {

namespace {

using namespace std::chrono_literals;

// The capture is armed while the host still holds the line low, so the frame
// begins with the release edge; 64 symbols comfortably fits release + the
// sensor preamble + 40 bits + the closing edge.
constexpr size_t rx_buffer_symbols = 64;

// End of frame: nothing on the wire is quieter than ~200 us mid-frame, and
// the line idles high afterwards.
constexpr uint32_t signal_range_min_ns = 1'000;     // glitch filter
constexpr uint32_t signal_range_max_ns = 1'000'000; // 1 ms idle terminates

// Per-model timing. The two models have different start-signal specs and
// refresh rates, so each knob is model-specific; keep them together so adding
// a model means editing one place.
//
// Start pulse — how long the host holds the line low to request a reading:
//   - DHT11 wants >=18 ms. A task delay yields the CPU; nudged up a tick so it
//     clears the floor at any FreeRTOS tick rate. The DHT11 has no tight upper
//     bound, so the tick-rounding overshoot is harmless.
//   - DHT22 wants ~1 ms and specifies a 20 ms ceiling. idfxx::delay busy-waits
//     precisely for a sub-10 ms duration, staying clear of that ceiling where
//     a coarse task delay (stretched by the +1 tick) could overrun it.
constexpr auto dht11_start_pulse = 20ms;
constexpr auto dht22_start_pulse = 2ms;

// Minimum interval between reads — the sensor won't refresh any faster.
constexpr auto dht11_min_interval = 1000ms;
constexpr auto dht22_min_interval = 2000ms;

// Full reply (release + preamble + 40 bits) is under 6 ms; allow generous
// scheduling slack on top of the 1 ms idle-termination window.
constexpr TickType_t reply_timeout_ticks = pdMS_TO_TICKS(100);

} // namespace

struct sensor::state {
    config cfg{};
    rmt_channel_handle_t channel = nullptr;
    QueueHandle_t done_queue = nullptr;
    std::array<rmt_symbol_word_t, rx_buffer_symbols> buffer{};
    int64_t last_read_us = 0; // esp_timer time of last completed start pulse; 0 = never

    ~state() {
        if (channel) {
            (void)rmt_disable(channel);
            (void)rmt_del_channel(channel);
        }
        if (done_queue) {
            vQueueDelete(done_queue);
        }
    }
};

namespace {

bool IRAM_ATTR on_recv_done(rmt_channel_handle_t, const rmt_rx_done_event_data_t* edata, void* user_ctx) {
    auto* queue = static_cast<QueueHandle_t>(user_ctx);
    BaseType_t high_task_wakeup = pdFALSE;
    xQueueSendFromISR(queue, edata, &high_task_wakeup);
    return high_task_wakeup == pdTRUE;
}

} // namespace

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
sensor::sensor(config cfg)
    : sensor(unwrap(make(std::move(cfg)))) {}
#endif

sensor::sensor(std::unique_ptr<state> s) noexcept
    : _state(std::move(s)) {}

result<sensor> sensor::make(config cfg) {
    if (!cfg.pin.is_connected()) {
        return error(errc::invalid_arg);
    }

    auto s = std::make_unique<state>();
    s->cfg = cfg;

    s->done_queue = xQueueCreate(1, sizeof(rmt_rx_done_event_data_t));
    if (!s->done_queue) {
        return error(errc::fail);
    }

    // The RMT channel taps the pad's input path through the GPIO matrix; the
    // pad itself is then configured as an open-drain output so the same line
    // can carry the host's start pulse.
    rmt_rx_channel_config_t rx_cfg{};
    rx_cfg.gpio_num = cfg.pin.idf_num();
    rx_cfg.clk_src = RMT_CLK_SRC_DEFAULT;
    rx_cfg.resolution_hz = internal::rmt_resolution_hz;
    rx_cfg.mem_block_symbols = rx_buffer_symbols;
    if (auto e = wrap(rmt_new_rx_channel(&rx_cfg, &s->channel)); !e) {
        return error(e.error());
    }

    rmt_rx_event_callbacks_t cbs{};
    cbs.on_recv_done = on_recv_done;
    if (auto e = wrap(rmt_rx_register_event_callbacks(s->channel, &cbs, s->done_queue)); !e) {
        return error(e.error());
    }
    if (auto e = wrap(rmt_enable(s->channel)); !e) {
        return error(e.error());
    }

    auto pin = cfg.pin;
    if (auto e = pin.try_set_direction(gpio::mode::input_output_od); !e) {
        return error(e.error());
    }
    if (cfg.internal_pullup) {
        if (auto e = pin.try_pullup_enable(); !e) {
            return error(e.error());
        }
    }
    pin.set_level(gpio::level::high); // release the bus

    return sensor{std::move(s)};
}

sensor::~sensor() = default;

sensor::sensor(sensor&& other) noexcept = default;

sensor& sensor::operator=(sensor&& other) noexcept = default;

idfxx::gpio sensor::pin() const noexcept {
    return _state ? _state->cfg.pin : gpio::nc();
}

enum model sensor::model() const noexcept {
    return _state ? _state->cfg.model : model::dht22;
}

std::chrono::milliseconds sensor::min_read_interval() const noexcept {
    return (_state && _state->cfg.model == model::dht11) ? dht11_min_interval : dht22_min_interval;
}

result<reading> sensor::try_read() {
    if (!_state) {
        return error(errc::invalid_state);
    }
    auto& s = *_state;

    const int64_t now_us = esp_timer_get_time();
    const int64_t min_gap_us = std::chrono::duration_cast<std::chrono::microseconds>(min_read_interval()).count();
    if (s.last_read_us != 0 && now_us - s.last_read_us < min_gap_us) {
        return error(errc::invalid_state);
    }

    auto pin = s.cfg.pin;
    xQueueReset(s.done_queue);

    // Start pulse: hold the line low, then arm the capture while still low —
    // the first captured edge is our own release, so the sensor's reply
    // (which starts 20–40 us later) can never race the arm.
    pin.set_level(gpio::level::low);
    if (s.cfg.model == model::dht11) {
        vTaskDelay(chrono::ticks(dht11_start_pulse) + 1);
    } else {
        delay(dht22_start_pulse);
    }
    // The start pulse has already perturbed the sensor, so record it now: the
    // sampling-interval guard must hold even if arming the capture below fails.
    s.last_read_us = esp_timer_get_time();

    rmt_receive_config_t rx_cfg{};
    rx_cfg.signal_range_min_ns = signal_range_min_ns;
    rx_cfg.signal_range_max_ns = signal_range_max_ns;
    if (auto e = wrap(rmt_receive(s.channel, s.buffer.data(), sizeof(s.buffer), &rx_cfg)); !e) {
        pin.set_level(gpio::level::high);
        return error(e.error());
    }

    pin.set_level(gpio::level::high);

    rmt_rx_done_event_data_t done{};
    if (xQueueReceive(s.done_queue, &done, reply_timeout_ticks) != pdTRUE) {
        // Cancel the in-flight capture so the next read can re-arm.
        (void)rmt_disable(s.channel);
        (void)rmt_enable(s.channel);
        return error(errc::timeout);
    }

    auto frame = internal::decode_frame({done.received_symbols, done.num_symbols});
    if (!frame) {
        ESP_LOGD("idfxx::dht", "undecodable reply: %u symbols", static_cast<unsigned>(done.num_symbols));
        for (size_t i = 0; i < done.num_symbols && i < 8; ++i) {
            const auto& sym = done.received_symbols[i];
            ESP_LOGD(
                "idfxx::dht",
                "  sym[%u]: l%u=%uus l%u=%uus",
                static_cast<unsigned>(i),
                sym.level0,
                sym.duration0,
                sym.level1,
                sym.duration1
            );
        }
        return error(frame.error());
    }
    return internal::to_reading(*frame, s.cfg.model);
}

} // namespace idfxx::dht
