// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#include <idfxx/spi/master>

#include <algorithm>
#include <esp_idf_version.h>
#include <esp_log.h>
// On some targets, ESP-IDF 5.5's esp_private/periph_ctrl.h (included from here)
// pastes a string macro directly after a literal, which is not valid C++11.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wliteral-suffix"
#include <esp_private/spi_common_internal.h>
#pragma GCC diagnostic pop
#include <hal/spi_ll.h>
#include <limits>
#include <utility>

namespace {
const char* TAG = "idfxx::spi::bus";
}

namespace idfxx::spi {

static result<void> init_bus(enum host_device host, const bus_config& cfg, enum dma_chan dma_chan) {
    if (static_cast<spi_host_device_t>(host) >= SPI_HOST_MAX) {
        return error(errc::invalid_arg);
    }
    if (cfg.max_transfer_sz == 0u ||
        cfg.max_transfer_sz.value_or(0) > static_cast<size_t>(std::numeric_limits<int>::max())) {
        return error(errc::invalid_arg);
    }
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 1, 0)
    if (cfg.dma_burst_size == 0u) {
        return error(errc::invalid_arg);
    }
#endif

    spi_bus_config_t spi_config{
        .mosi_io_num = cfg.mosi.idf_num(),
        .miso_io_num = cfg.miso.idf_num(),
        .sclk_io_num = cfg.sclk.idf_num(),
        .quadwp_io_num = cfg.quadwp.idf_num(),
        .quadhd_io_num = cfg.quadhd.idf_num(),
        .data4_io_num = cfg.data4.idf_num(),
        .data5_io_num = cfg.data5.idf_num(),
        .data6_io_num = cfg.data6.idf_num(),
        .data7_io_num = cfg.data7.idf_num(),
        .data_io_default_level = cfg.data_idle_level == gpio::level::high,
        // 0 selects the driver default
        .max_transfer_sz = static_cast<int>(cfg.max_transfer_sz.value_or(0)),
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 1, 0)
        .dma_burst_size = cfg.dma_burst_size.value_or(0), // 0 selects the driver default
#endif
        .flags = to_underlying(cfg.flags),
        // core_id is 0-based (core_0=0, core_1=1), but esp_intr_cpu_affinity_t is
        // AUTO=0, CORE0=1, CORE1=2, so +1 converts to the ESP-IDF enum.
        .isr_cpu_id = cfg.isr_cpu_id
            ? static_cast<esp_intr_cpu_affinity_t>(std::to_underlying(*cfg.isr_cpu_id) + 1)
            : ESP_INTR_CPU_AFFINITY_AUTO,
        .intr_flags = to_underlying(cfg.intr_levels) | to_underlying(cfg.intr_flags),
    };

    esp_err_t res = spi_bus_initialize(
        static_cast<spi_host_device_t>(host), &spi_config, static_cast<spi_dma_chan_t>(dma_chan)
    );
    if (res != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SPI bus: %s", esp_err_to_name(res));
        switch (res) {
        case ESP_ERR_NO_MEM:
            raise_no_mem();
        case ESP_ERR_INVALID_ARG:
            return error(errc::invalid_arg);
        case ESP_ERR_INVALID_STATE:
            return error(errc::invalid_state);
        case ESP_ERR_NOT_FOUND:
            return error(errc::not_found);
        default:
            return error(errc::invalid_state);
        }
    }

    return {};
}

result<master_bus>
master_bus::make(enum host_device host, enum dma_chan dma_chan, struct bus_config cfg) {
    return init_bus(host, cfg, dma_chan).transform([&] { return master_bus{host}; });
}

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
master_bus::master_bus(enum host_device host, enum dma_chan dma_chan, struct bus_config cfg)
    : _host(host)
    , _initialized(false) {
    unwrap(init_bus(host, cfg, dma_chan));
    _initialized = true;
}
#endif

master_bus::master_bus(enum host_device host)
    : _host(host) {}

master_bus::master_bus(master_bus&& other) noexcept
    : _host(other._host)
    , _initialized(std::exchange(other._initialized, false)) {}

master_bus& master_bus::operator=(master_bus&& other) noexcept {
    if (this != &other) {
        if (_initialized) {
            spi_bus_free(static_cast<spi_host_device_t>(_host));
        }
        _host = other._host;
        _initialized = std::exchange(other._initialized, false);
    }
    return *this;
}

master_bus::~master_bus() {
    if (_initialized) {
        spi_bus_free(static_cast<spi_host_device_t>(_host));
    }
}

size_t master_bus::max_transaction_length() const {
    if (!_initialized) {
        return 0;
    }
    // spi_bus_get_max_transaction_len() fails while the bus has no devices, so
    // apply the limit it computes to the bus attributes directly.
    const spi_bus_attr_t* attr = spi_bus_get_attr(static_cast<spi_host_device_t>(_host));
    if (attr == nullptr) {
        return 0;
    }
    const int hw_max =
        static_cast<int>((attr->dma_enabled ? SPI_LL_DMA_MAX_BIT_LEN : SPI_LL_CPU_MAX_BIT_LEN) / 8);
    return static_cast<size_t>(std::min(attr->max_transfer_sz, hw_max));
}

} // namespace idfxx::spi
