// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#include <idfxx/chrono>
#include <idfxx/spi/master>

#include <atomic>
#include <condition_variable>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

namespace {
const char* TAG = "idfxx::spi::device";
}

namespace idfxx::spi {

namespace {

struct pool_slot {
    spi_transaction_ext_t idf_ext{};
    std::atomic<bool> done{true};
    std::atomic<uint16_t> refcount{0};
};

} // namespace

// Per-device async state: owns the fixed-size pool of transaction slots, the
// free-list that feeds queue_trans, and the mutex/cv coordination for both the
// pool and the cooperative-drain wait path.
struct master_device::async_state {
    size_t queue_size;
    std::unique_ptr<pool_slot[]> slots;
    std::vector<uint16_t> free_list;
    std::mutex pool_mtx;
    std::condition_variable pool_cv;
    std::timed_mutex wait_mtx;

    explicit async_state(size_t qs)
        : queue_size(qs)
        , slots(std::make_unique<pool_slot[]>(qs)) {
        free_list.reserve(qs);
        for (size_t i = 0; i < qs; ++i) {
            free_list.push_back(static_cast<uint16_t>(i));
        }
    }
};

static result<spi_device_handle_t> add_device(master_bus& bus, const master_device::config& config) {
    spi_device_interface_config_t dev_config{
        .command_bits = config.command_bits,
        .address_bits = config.address_bits,
        .dummy_bits = config.dummy_bits,
        .mode = config.mode,
        .clock_source = SPI_CLK_SRC_DEFAULT,
        .duty_cycle_pos = config.duty_cycle_pos,
        .cs_ena_pretrans = config.cs_ena_pretrans,
        .cs_ena_posttrans = config.cs_ena_posttrans,
        .clock_speed_hz = static_cast<int>(config.clock_speed.count()),
        .input_delay_ns = static_cast<int>(config.input_delay.count()),
        .sample_point = {},
        .spics_io_num = config.cs.idf_num(),
        .flags = to_underlying(config.flags),
        .queue_size = config.queue_size,
        .pre_cb = nullptr,
        .post_cb = nullptr,
    };

    spi_device_handle_t handle;
    auto err = spi_bus_add_device(static_cast<spi_host_device_t>(bus.host()), &dev_config, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add SPI device: %s", esp_err_to_name(err));
        return error(err);
    }

    return handle;
}

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
master_device::master_device(master_bus& bus, const struct config& config)
    : master_device(&bus, unwrap(add_device(bus, config)), static_cast<size_t>(config.queue_size)) {}
#endif

result<master_device> master_device::make(master_bus& bus, const struct config& config) {
    return add_device(bus, config).transform([&](auto handle) {
        return master_device(&bus, handle, static_cast<size_t>(config.queue_size));
    });
}

master_device::master_device(master_bus* bus, spi_device_handle_t handle, size_t queue_size)
    : _bus(bus)
    , _handle(handle)
    , _async(std::make_unique<async_state>(queue_size)) {}

master_device::master_device(master_device&& other) noexcept
    : _bus(std::exchange(other._bus, nullptr))
    , _handle(std::exchange(other._handle, nullptr))
    , _async(std::move(other._async)) {}

master_device& master_device::operator=(master_device&& other) noexcept {
    if (this != &other) {
        _delete();
        _bus = std::exchange(other._bus, nullptr);
        _handle = std::exchange(other._handle, nullptr);
        _async = std::move(other._async);
    }
    return *this;
}

master_device::~master_device() {
    _delete();
}

void master_device::_delete() noexcept {
    if (_handle != nullptr) {
        // ESP-IDF rejects spi_bus_remove_device with ESP_ERR_INVALID_STATE if any
        // transaction is still in its internal queue — drain them first.
        if (_async) {
            size_t pending = 0;
            for (size_t i = 0; i < _async->queue_size; ++i) {
                if (!_async->slots[i].done.load(std::memory_order_acquire)) {
                    ++pending;
                }
            }
            while (pending > 0) {
                spi_transaction_t* idf_trans = nullptr;
                auto err = spi_device_get_trans_result(_handle, &idf_trans, portMAX_DELAY);
                if (err != ESP_OK) {
                    ESP_LOGE(TAG, "Drain failed, %zu slot(s) still pending: %s", pending, esp_err_to_name(err));
                    break;
                }
                auto* drained = static_cast<pool_slot*>(idf_trans->user);
                drained->done.store(true, std::memory_order_release);
                --pending;
            }
        }
        auto err = spi_bus_remove_device(_handle);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to remove SPI device: %s", esp_err_to_name(err));
        }
        _handle = nullptr;
    }
}

// =============================================================================
// Accessors
// =============================================================================

freq::hertz master_device::frequency() const {
    if (_handle == nullptr) {
        return freq::hertz{0};
    }
    int freq_khz = 0;
    spi_device_get_actual_freq(_handle, &freq_khz);
    return freq::hertz{static_cast<int64_t>(freq_khz) * 1000};
}

// =============================================================================
// Simple API helpers
// =============================================================================

void master_device::_prepare_idf_trans(spi_transaction_t& idf_trans, const transaction& trans) {
    idf_trans = {};
    idf_trans.flags = to_underlying(trans.flags);
    idf_trans.cmd = trans.cmd;
    idf_trans.addr = trans.addr;
    idf_trans.length = trans.length;
    idf_trans.rxlength = trans.rx_length;
    idf_trans.override_freq_hz = static_cast<int>(trans.override_freq.count());
    idf_trans.tx_buffer = trans.tx_buffer.data();
    idf_trans.rx_buffer = trans.rx_buffer.data();
}

void master_device::_prepare_idf_trans_ext(spi_transaction_ext_t& idf_ext, const transaction& trans) {
    _prepare_idf_trans(idf_ext.base, trans);
    idf_ext.command_bits = trans.command_bits;
    idf_ext.address_bits = trans.address_bits;
    idf_ext.dummy_bits = trans.dummy_bits;
}

static bool uses_variable_fields(const transaction& trans) {
    return trans.flags.contains_any(
        trans_flags::variable_cmd | trans_flags::variable_addr | trans_flags::variable_dummy
    );
}

static result<void> validate_trans_buffers(const transaction& trans) {
    if (!trans.tx_buffer.empty() && trans.length > trans.tx_buffer.size_bytes() * 8) {
        ESP_LOGD(
            TAG,
            "SPI transaction length %zu bits exceeds tx_buffer capacity %zu bits",
            trans.length,
            trans.tx_buffer.size_bytes() * 8
        );
        return error(errc::invalid_arg);
    }
    size_t rx_bits = trans.rx_length != 0 ? trans.rx_length : trans.length;
    if (!trans.rx_buffer.empty() && rx_bits > trans.rx_buffer.size_bytes() * 8) {
        ESP_LOGD(
            TAG,
            "SPI transaction rx_length %zu bits exceeds rx_buffer capacity %zu bits",
            rx_bits,
            trans.rx_buffer.size_bytes() * 8
        );
        return error(errc::invalid_arg);
    }
    return {};
}

// =============================================================================
// Shared dispatch helpers
// =============================================================================

result<void> master_device::_simple_trans(std::span<const uint8_t> tx, std::span<uint8_t> rx, _trans_fn fn) {
    if (_handle == nullptr) {
        return error(errc::invalid_state);
    }
    spi_transaction_t trans{};
    if (!tx.empty()) {
        trans.length = tx.size() * 8;
        trans.tx_buffer = tx.data();
    }
    if (!rx.empty()) {
        if (trans.length == 0) {
            trans.length = rx.size() * 8;
        }
        trans.rxlength = rx.size() * 8;
        trans.rx_buffer = rx.data();
    }
    return wrap(fn(_handle, &trans));
}

result<void> master_device::_full_trans(const transaction& trans, _trans_fn fn) {
    if (_handle == nullptr) {
        return error(errc::invalid_state);
    }
    if (auto v = validate_trans_buffers(trans); !v) {
        return v;
    }
    if (uses_variable_fields(trans)) {
        spi_transaction_ext_t ext;
        _prepare_idf_trans_ext(ext, trans);
        return wrap(fn(_handle, &ext.base));
    }
    spi_transaction_t idf_trans;
    _prepare_idf_trans(idf_trans, trans);
    return wrap(fn(_handle, &idf_trans));
}

// =============================================================================
// Simple API (interrupt-based blocking)
// =============================================================================

result<void> master_device::try_transmit(std::span<const uint8_t> tx_data) {
    return _simple_trans(tx_data, {}, spi_device_transmit);
}

result<void> master_device::try_receive(std::span<uint8_t> rx_data) {
    return _simple_trans({}, rx_data, spi_device_transmit);
}

result<std::vector<uint8_t>> master_device::try_receive(size_t size) {
    if (_handle == nullptr) {
        return error(errc::invalid_state);
    }
    std::vector<uint8_t> buf(size);
    return try_receive(std::span<uint8_t>(buf)).transform([&]() { return std::move(buf); });
}

result<void> master_device::try_transfer(std::span<const uint8_t> tx_data, std::span<uint8_t> rx_data) {
    return _simple_trans(tx_data, rx_data, spi_device_transmit);
}

// =============================================================================
// Full transaction API (interrupt-based blocking)
// =============================================================================

result<void> master_device::try_transmit(const transaction& trans) {
    return _full_trans(trans, spi_device_transmit);
}

// =============================================================================
// Polling API
// =============================================================================

result<void> master_device::try_polling_transmit(std::span<const uint8_t> tx_data) {
    return _simple_trans(tx_data, {}, spi_device_polling_transmit);
}

result<void> master_device::try_polling_receive(std::span<uint8_t> rx_data) {
    return _simple_trans({}, rx_data, spi_device_polling_transmit);
}

result<std::vector<uint8_t>> master_device::try_polling_receive(size_t size) {
    if (_handle == nullptr) {
        return error(errc::invalid_state);
    }
    std::vector<uint8_t> buf(size);
    return try_polling_receive(std::span<uint8_t>(buf)).transform([&]() { return std::move(buf); });
}

result<void> master_device::try_polling_transfer(std::span<const uint8_t> tx_data, std::span<uint8_t> rx_data) {
    return _simple_trans(tx_data, rx_data, spi_device_polling_transmit);
}

result<void> master_device::try_polling_transmit(const transaction& trans) {
    return _full_trans(trans, spi_device_polling_transmit);
}

// =============================================================================
// Async queue API
// =============================================================================

result<uint16_t> master_device::_acquire_slot(std::optional<std::chrono::milliseconds> timeout) {
    std::unique_lock lk(_async->pool_mtx);
    if (timeout) {
        auto deadline = std::chrono::steady_clock::now() + *timeout;
        if (!_async->pool_cv.wait_until(lk, deadline, [&]() { return !_async->free_list.empty(); })) {
            return error(errc::timeout);
        }
    } else {
        _async->pool_cv.wait(lk, [&]() { return !_async->free_list.empty(); });
    }
    uint16_t idx = _async->free_list.back();
    _async->free_list.pop_back();
    return idx;
}

void master_device::_release_slot_ref(uint16_t slot_idx) noexcept {
    auto prev = _async->slots[slot_idx].refcount.fetch_sub(1, std::memory_order_acq_rel);
    if (prev == 1) {
        {
            std::lock_guard lk(_async->pool_mtx);
            _async->free_list.push_back(slot_idx);
        }
        _async->pool_cv.notify_one();
    }
}

bool master_device::_slot_done(uint16_t slot_idx) const noexcept {
    return _async->slots[slot_idx].done.load(std::memory_order_acquire);
}

result<idfxx::future<void>> master_device::try_queue_trans(const transaction& trans) {
    return _try_queue_trans(trans, std::nullopt);
}

result<idfxx::future<void>>
master_device::_try_queue_trans(const transaction& trans, std::optional<std::chrono::milliseconds> timeout) {
    if (_handle == nullptr) {
        return error(errc::invalid_state);
    }
    if (auto v = validate_trans_buffers(trans); !v) {
        return error(v.error());
    }

    auto slot_idx_res = _acquire_slot(timeout);
    if (!slot_idx_res) {
        return error(slot_idx_res.error());
    }
    uint16_t slot_idx = *slot_idx_res;

    auto& s = _async->slots[slot_idx];
    _prepare_idf_trans_ext(s.idf_ext, trans);
    s.idf_ext.base.user = &s;
    s.done.store(false, std::memory_order_relaxed);
    // +2 refs: one for ESP-IDF (released on drain), one for the future-side
    // slot_guard captured below.
    s.refcount.store(2, std::memory_order_release);

    auto err = spi_device_queue_trans(_handle, &s.idf_ext.base, portMAX_DELAY);
    if (err != ESP_OK) {
        // ESP-IDF did not take the slot: collapse the two refs to zero so
        // _release_slot_ref returns the slot to the free list.
        s.done.store(true, std::memory_order_relaxed);
        s.refcount.store(1, std::memory_order_release);
        _release_slot_ref(slot_idx);
        return error(err);
    }

    // RAII guard captured by value inside the waiter lambda — holds the
    // future-side slot reference for the life of the underlying shared state.
    struct slot_guard {
        master_device* dev;
        uint16_t idx;
        slot_guard(master_device* d, uint16_t i) noexcept
            : dev(d)
            , idx(i) {}
        slot_guard(slot_guard&& o) noexcept
            : dev(o.dev)
            , idx(o.idx) {
            o.dev = nullptr;
        }
        slot_guard(const slot_guard&) = delete;
        slot_guard& operator=(const slot_guard&) = delete;
        slot_guard& operator=(slot_guard&&) = delete;
        ~slot_guard() {
            if (dev) {
                dev->_release_slot_ref(idx);
            }
        }
    };

    return idfxx::future<void>{
        [dev = this, idx = slot_idx, g = slot_guard{this, slot_idx}](std::optional<std::chrono::milliseconds> t) mutable
            -> result<void> { return dev->_try_wait_for(idx, t); },
        [dev = this, idx = slot_idx]() noexcept -> bool { return dev->_slot_done(idx); }
    };
}

result<void> master_device::_try_wait_for(uint16_t slot_idx, std::optional<std::chrono::milliseconds> timeout) {
    if (_handle == nullptr) {
        return error(errc::invalid_state);
    }
    using clock = std::chrono::steady_clock;
    std::optional<clock::time_point> deadline = timeout.transform([](auto t) { return clock::now() + t; });

    auto& target = _async->slots[slot_idx];
    if (target.done.load(std::memory_order_acquire)) {
        return {};
    }

    // Only one task may sit in spi_device_get_trans_result at a time: ESP-IDF's
    // return queue delivers each completion to exactly one waiter, so concurrent
    // callers could otherwise receive each other's completions and the one left
    // holding an empty queue would block forever.
    std::unique_lock lk(_async->wait_mtx, std::defer_lock);
    if (!deadline) {
        lk.lock();
    } else if (!lk.try_lock_until(*deadline)) {
        return target.done.load(std::memory_order_acquire) ? result<void>{} : error(errc::timeout);
    }

    while (!target.done.load(std::memory_order_acquire)) {
        TickType_t ticks = portMAX_DELAY;
        if (deadline) {
            auto now = clock::now();
            if (now >= *deadline) {
                return error(errc::timeout);
            }
            ticks = idfxx::chrono::ticks(*deadline - now);
        }

        spi_transaction_t* idf_trans = nullptr;
        auto err = spi_device_get_trans_result(_handle, &idf_trans, ticks);
        if (err != ESP_OK) {
            return error(err);
        }
        auto* completed_slot = static_cast<pool_slot*>(idf_trans->user);
        completed_slot->done.store(true, std::memory_order_release);
        uint16_t completed_idx = static_cast<uint16_t>(completed_slot - _async->slots.get());
        _release_slot_ref(completed_idx);
    }
    return {};
}

// =============================================================================
// Bus exclusivity
// =============================================================================

void master_device::lock() const {
    if (_handle == nullptr) {
        return;
    }
    spi_device_acquire_bus(_handle, portMAX_DELAY);
}

bool master_device::try_lock() const noexcept {
    if (_handle == nullptr) {
        return false;
    }
    return spi_device_acquire_bus(_handle, 0) == ESP_OK;
}

void master_device::unlock() const {
    if (_handle == nullptr) {
        return;
    }
    spi_device_release_bus(_handle);
}

} // namespace idfxx::spi
