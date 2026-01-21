// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#include <idfxx/spi/master>

#include <esp_log.h>

namespace {
const char* TAG = "idfxx::spi::bus";
}

namespace idfxx::spi {

static result<void> init_bus(enum host_device host, const struct bus_config& config, enum dma_chan dma_chan) {
    if (static_cast<spi_host_device_t>(host) >= SPI_HOST_MAX) {
        return error(errc::invalid_arg);
    }

    spi_bus_config_t spi_config{
        .mosi_io_num = config.mosi_io_num.idf_num(),
        .miso_io_num = config.miso_io_num.idf_num(),
        .sclk_io_num = config.sclk_io_num.idf_num(),
        .quadwp_io_num = config.quadwp_io_num.idf_num(),
        .quadhd_io_num = config.quadhd_io_num.idf_num(),
        .data4_io_num = config.data4_io_num.idf_num(),
        .data5_io_num = config.data5_io_num.idf_num(),
        .data6_io_num = config.data6_io_num.idf_num(),
        .data7_io_num = config.data7_io_num.idf_num(),
        .data_io_default_level = config.data_io_default_level,
        .max_transfer_sz = config.max_transfer_sz,
        .flags = config.flags.value(),
        .isr_cpu_id = static_cast<esp_intr_cpu_affinity_t>(config.isr_cpu_id),
        .intr_flags = config.intr_flags.value(),
    };

    esp_err_t res =
        spi_bus_initialize(static_cast<spi_host_device_t>(host), &spi_config, static_cast<spi_dma_chan_t>(dma_chan));
    if (res != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SPI bus: %s", esp_err_to_name(res));
        switch (res) {
        case ESP_ERR_INVALID_ARG:
            return error(errc::invalid_arg);
        case ESP_ERR_INVALID_STATE:
            return error(errc::invalid_state);
        case ESP_ERR_NOT_FOUND:
            return error(errc::not_found);
        case ESP_ERR_NO_MEM:
            return error(errc::no_mem);
        default:
            return error(errc::invalid_state);
        }
    }

    return {};
}

result<std::unique_ptr<master_bus>>
master_bus::make(enum host_device host, enum dma_chan dma_chan, struct bus_config config) {
    return init_bus(host, config, dma_chan).transform([&] {
        return std::unique_ptr<master_bus>(new master_bus{host});
    });
}

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
master_bus::master_bus(enum host_device host, enum dma_chan dma_chan, struct bus_config config) {
    unwrap(init_bus(host, config, dma_chan));
    _host = host;
}
#endif

master_bus::master_bus(enum host_device host)
    : _host(host) {}

master_bus::~master_bus() {
    spi_bus_free(static_cast<spi_host_device_t>(_host));
}

} // namespace idfxx::spi
