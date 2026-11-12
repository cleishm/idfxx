// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#include "sdcard_erase.hpp"
#include "sdcard_spi_clock.hpp"

#include <idfxx/sdcard>

#include <algorithm>
#include <array>
#include <cstring>
#include <driver/sdspi_host.h>
#include <driver/spi_master.h>
#include <esp_clk_tree.h>
#include <esp_log.h>
#include <limits>
#include <mutex>
#include <optional>
#include <sd_protocol_defs.h>
#include <sdmmc_cmd.h>
#include <utility>

#if CONFIG_IDF_TARGET_ESP32
#include <esp_private/spi_common_internal.h>
#endif
#if SOC_SDMMC_HOST_SUPPORTED
#include "sdcard_dma.hpp"

#include <driver/sdmmc_host.h>
#endif
#if CONFIG_IDF_TARGET_ESP32 || CONFIG_IDF_TARGET_ESP32P4
#include <soc/sdmmc_pins.h>
#endif
#if SOC_SDMMC_IO_POWER_EXTERNAL
#include <sd_pwr_ctrl_by_on_chip_ldo.h>
#endif

namespace {
const char* TAG = "idfxx::sdcard";
}

namespace idfxx::sdcard {

namespace {

// The largest SDHC card is (0xFF5F + 1) * 512 KiB, just under 32 GiB; SDXC cards start at 32 GiB.
constexpr uint64_t sdxc_min_size = uint64_t{32} << 30;

// The longest single SPI transaction the SD SPI driver issues: a 512-byte data block read together
// with its CRC and the start of the next block's response.
constexpr size_t sdspi_max_transaction_length = 512 + 4;

gpio_num_t pin_or_nc(idfxx::gpio g) {
    return g.is_connected() ? g.idf_num() : GPIO_NUM_NC;
}

// True if no connected pin appears twice.
template<size_t N>
bool distinct(const std::array<idfxx::gpio, N>& pins) {
    for (size_t i = 0; i < N; ++i) {
        if (!pins[i].is_connected()) {
            continue;
        }
        for (size_t j = i + 1; j < N; ++j) {
            if (pins[i] == pins[j]) {
                return false;
            }
        }
    }
    return true;
}

// The card's sector count. ESP-IDF stores it in an int, so a card of 2^31 sectors (1 TiB) or more
// reads as negative there; it is unsigned 32-bit on the bus.
uint32_t capacity_of(const sdmmc_card_t& c) {
    return static_cast<uint32_t>(c.csd.capacity);
}

uint64_t size_of(const sdmmc_card_t& c) {
    return uint64_t{capacity_of(c)} * static_cast<uint64_t>(c.csd.sector_size);
}

card_type classify(const sdmmc_card_t& c) {
    if (c.is_mmc) {
        return card_type::mmc;
    }
    if (!(c.ocr & SD_OCR_SDHC_CAP)) {
        return card_type::sdsc;
    }
    return size_of(c) >= sdxc_min_size ? card_type::sdxc : card_type::sdhc;
}

// Runs the card initialization sequence on a host whose device or slot is set up.
result<void> init_card(const sdmmc_host_t& host, sdmmc_card_t& c) {
    if (auto err = sdmmc_card_init(&host, &c); err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize card: %s", esp_err_to_name(err));
        return error(err);
    }
    if (!c.is_mem) {
        ESP_LOGE(TAG, "SDIO-only cards are not supported");
        return error(errc::not_supported);
    }
    return {};
}

#if SOC_SDMMC_HOST_SUPPORTED
// Serializes SDMMC host initialization and slot bookkeeping between cards.
std::mutex sdmmc_mutex;
std::array<bool, SOC_SDMMC_NUM_SLOTS> sdmmc_slot_in_use{};

// Validates a configured max_frequency and converts it for sdmmc_host_t::max_freq_khz. The card is
// identified at SDMMC_FREQ_PROBING, so a lower bound could not be honored.
result<int> to_max_freq_khz(quanta::frequency::hertz f) {
    if (f.count() < int64_t{SDMMC_FREQ_PROBING} * 1000) {
        return error(errc::invalid_arg);
    }
    return static_cast<int>(std::min<int64_t>(f.count() / 1000, std::numeric_limits<int>::max()));
}

bool valid_width(bus_width w) {
    return w == bus_width::one || w == bus_width::four || w == bus_width::eight;
}

#if !SOC_SDMMC_USE_GPIO_MATRIX
#if CONFIG_IDF_TARGET_ESP32
// Each slot's pins are fixed, in the order CLK, CMD, D0-D7: slot 0 has eight data lines, slot 1
// four.
constexpr std::array<std::array<idfxx::gpio, 10>, SOC_SDMMC_NUM_SLOTS> sdmmc_slot_pins{{
    {
        gpio_constant<SDMMC_SLOT0_IOMUX_PIN_NUM_CLK>::value,
        gpio_constant<SDMMC_SLOT0_IOMUX_PIN_NUM_CMD>::value,
        gpio_constant<SDMMC_SLOT0_IOMUX_PIN_NUM_D0>::value,
        gpio_constant<SDMMC_SLOT0_IOMUX_PIN_NUM_D1>::value,
        gpio_constant<SDMMC_SLOT0_IOMUX_PIN_NUM_D2>::value,
        gpio_constant<SDMMC_SLOT0_IOMUX_PIN_NUM_D3>::value,
        gpio_constant<SDMMC_SLOT0_IOMUX_PIN_NUM_D4>::value,
        gpio_constant<SDMMC_SLOT0_IOMUX_PIN_NUM_D5>::value,
        gpio_constant<SDMMC_SLOT0_IOMUX_PIN_NUM_D6>::value,
        gpio_constant<SDMMC_SLOT0_IOMUX_PIN_NUM_D7>::value,
    },
    {
        gpio_constant<SDMMC_SLOT1_IOMUX_PIN_NUM_CLK>::value,
        gpio_constant<SDMMC_SLOT1_IOMUX_PIN_NUM_CMD>::value,
        gpio_constant<SDMMC_SLOT1_IOMUX_PIN_NUM_D0>::value,
        gpio_constant<SDMMC_SLOT1_IOMUX_PIN_NUM_D1>::value,
        gpio_constant<SDMMC_SLOT1_IOMUX_PIN_NUM_D2>::value,
        gpio_constant<SDMMC_SLOT1_IOMUX_PIN_NUM_D3>::value,
        gpio::nc(),
        gpio::nc(),
        gpio::nc(),
        gpio::nc(),
    },
}};
#else
#error "idfxx_sdcard: slot pins are not known for this target's dedicated SDMMC pins"
#endif
#endif

#if CONFIG_IDF_TARGET_ESP32P4
// Slot 0 is connected through IO MUX only: CLK, CMD, D0-D7.
constexpr std::array<idfxx::gpio, 10> p4_slot0_pins{
    gpio_constant<SDMMC_SLOT0_IOMUX_PIN_NUM_CLK>::value,
    gpio_constant<SDMMC_SLOT0_IOMUX_PIN_NUM_CMD>::value,
    gpio_constant<SDMMC_SLOT0_IOMUX_PIN_NUM_D0>::value,
    gpio_constant<SDMMC_SLOT0_IOMUX_PIN_NUM_D1>::value,
    gpio_constant<SDMMC_SLOT0_IOMUX_PIN_NUM_D2>::value,
    gpio_constant<SDMMC_SLOT0_IOMUX_PIN_NUM_D3>::value,
    gpio_constant<SDMMC_SLOT0_IOMUX_PIN_NUM_D4>::value,
    gpio_constant<SDMMC_SLOT0_IOMUX_PIN_NUM_D5>::value,
    gpio_constant<SDMMC_SLOT0_IOMUX_PIN_NUM_D6>::value,
    gpio_constant<SDMMC_SLOT0_IOMUX_PIN_NUM_D7>::value,
};
#endif

// Validates the slot's pins and returns the bus width to configure it with.
result<bus_width> resolve_sdmmc_width(const card::sdmmc_config& cfg) {
    if (cfg.width && !valid_width(*cfg.width)) {
        return error(errc::invalid_arg);
    }

    // The signal pins, CLK, CMD and D0-D7, and the widest bus they allow.
#if SOC_SDMMC_USE_GPIO_MATRIX
    const std::array<idfxx::gpio, 10> signals{
        cfg.clk,
        cfg.cmd,
        cfg.d0,
        cfg.d1,
        cfg.d2,
        cfg.d3,
        cfg.d4,
        cfg.d5,
        cfg.d6,
        cfg.d7,
    };
    for (size_t i = 0; i < signals.size(); ++i) {
        const auto& pin = signals[i];
        if (!pin.is_connected()) {
            if (i < 3) { // CLK, CMD and D0 are always needed
                return error(errc::invalid_arg);
            }
            continue;
        }
        if (!pin.is_output_capable()) {
            return error(errc::invalid_arg);
        }
#if CONFIG_IDF_TARGET_ESP32P4
        if (cfg.slot == card::slot::slot_0 && pin != p4_slot0_pins[i]) {
            return error(errc::invalid_arg);
        }
#endif
    }
    // Data lines come in groups: D1-D3 for a 4-bit bus, D4-D7 (with D1-D3) for an 8-bit bus.
    const auto connected = [&](size_t from, size_t to) {
        return static_cast<size_t>(
            std::count_if(signals.begin() + from, signals.begin() + to + 1, [](auto g) {
                return g.is_connected();
            })
        );
    };
    const size_t d1_d3 = connected(3, 5);
    const size_t d4_d7 = connected(6, 9);
    if ((d1_d3 != 0 && d1_d3 != 3) || (d4_d7 != 0 && (d4_d7 != 4 || d1_d3 != 3))) {
        return error(errc::invalid_arg);
    }
    const bus_width available = d4_d7 == 4 ? bus_width::eight
        : d1_d3 == 3                       ? bus_width::four
                                           : bus_width::one;
#else
    auto signals = sdmmc_slot_pins[std::to_underlying(cfg.slot)];
    const auto available = static_cast<bus_width>(
        std::count_if(signals.begin() + 2, signals.end(), [](auto g) { return g.is_connected(); })
    );
#endif

    if (cfg.width && std::to_underlying(*cfg.width) > std::to_underlying(available)) {
        return error(errc::invalid_arg);
    }
    const bus_width width = cfg.width.value_or(available);
#if !SOC_SDMMC_USE_GPIO_MATRIX
    // Only the slot's pins that the bus uses: the driver leaves the data lines above the bus width
    // untouched, so they are free for card detect or write protect.
    std::fill(signals.begin() + 2 + std::to_underlying(width), signals.end(), idfxx::gpio::nc());
#endif

    std::array<idfxx::gpio, 12> all{};
    std::ranges::copy(signals, all.begin());
    all[10] = cfg.card_detect;
    all[11] = cfg.write_protect;
    if (!distinct(all)) {
        return error(errc::invalid_arg);
    }
    return width;
}

// The sdmmc_host_t and slot settings that select a bus speed mode.
struct sdmmc_speed_settings {
    int max_freq_khz;
    bool ddr;
    bool uhs1;
};

#if SOC_SDMMC_UHS_I_SUPPORTED
// A UHS-I mode is selected by its exact clock, which the mode's timing tuning also needs, so no
// limit can be applied. The switch to 1.8 V goes through the power control.
result<sdmmc_speed_settings> uhs1_speed(const card::sdmmc_config& cfg, int freq_khz, bool ddr) {
    if (cfg.max_frequency) {
        return error(errc::invalid_arg);
    }
#if SOC_SDMMC_IO_POWER_EXTERNAL
    if (!cfg.io_power_ldo) {
        return error(errc::invalid_arg);
    }
#endif
    return sdmmc_speed_settings{freq_khz, ddr, true};
}
#endif

// Validates the configured speed mode and clock limit, and returns the settings that select them.
// ESP-IDF reads sdmmc_host_t::max_freq_khz as a mode selector, not a limit: at or below
// SDMMC_FREQ_DEFAULT the card stays in default speed; above it an SD card is switched to high speed
// and clocked at max_freq_khz, whatever its own maximum; SDMMC_FREQ_SDR50 and SDMMC_FREQ_SDR104
// select those UHS-I modes; and with SDMMC_HOST_FLAG_DDR, a UHS-I SD card is switched to DDR50 at
// any clock, and an MMC device that supports it to dual data rate on a 4-bit bus at 26 MHz or more.
result<sdmmc_speed_settings> resolve_sdmmc_speed(const card::sdmmc_config& cfg) {
    switch (cfg.speed) {
    case bus_speed::default_speed:
    case bus_speed::high_speed: {
        const bool high_speed = cfg.speed == bus_speed::high_speed;
        // Both are below every UHS-I selector value, and so is any lower limit.
        int freq_khz = high_speed ? SDMMC_FREQ_HIGHSPEED : SDMMC_FREQ_DEFAULT;
        if (cfg.max_frequency) {
            auto limit = to_max_freq_khz(*cfg.max_frequency);
            if (!limit) {
                return error(limit.error());
            }
            freq_khz = std::min(freq_khz, *limit);
        }
        // Without a UHS-I slot an SD card is never switched to DDR50, so the flag only lets an MMC
        // device use dual data rate in high speed. ESP-IDF enables that at 26 MHz but high-speed
        // timing only above it, so the flag is left clear at or below 26 MHz.
        return sdmmc_speed_settings{freq_khz, high_speed && freq_khz > SDMMC_FREQ_26M, false};
    }
#if SOC_SDMMC_UHS_I_SUPPORTED
    case bus_speed::sdr50:
        return uhs1_speed(cfg, SDMMC_FREQ_SDR50, false);
    case bus_speed::ddr50:
        return uhs1_speed(cfg, SDMMC_FREQ_DDR50, true);
#endif
#if SOC_SDMMC_UHS_I_SUPPORTED && ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
    case bus_speed::sdr104:
        return uhs1_speed(cfg, SDMMC_FREQ_SDR104, false);
#endif
    default:
        return error(errc::invalid_arg);
    }
}

// The card's sdmmc_host_t::check_buffer_alignment. ESP-IDF's sector reads and writes copy a buffer
// through a DMA-capable one when this returns false, but its default only checks the alignment, and
// hands a buffer that the DMA can't reach (in flash, IRAM or RTC memory, for example) to the DMA as
// it is. PSRAM is left to ESP-IDF's own check, which copies it where the DMA can't reach it.
// Installed in the card's sdmmc_card_t, this also covers ESP-IDF code given idf_handle(), such as a
// FAT filesystem. (ESP-IDF's SDIO functions fail with ESP_ERR_INVALID_ARG instead of copying, where
// they would otherwise give the DMA a buffer it can't reach.)
bool sdmmc_check_buffer(int slot, const void* buf, size_t size) {
    return sdmmc_host_check_buffer_alignment(slot, buf, size) &&
        (size == 0 || !internal::sdmmc_needs_dma_copy(buf, size));
}
#endif // SOC_SDMMC_HOST_SUPPORTED

} // namespace

result<internal::spi_clock_choice> internal::choose_sdspi_clock(
    sdspi_dev_handle_t handle,
    [[maybe_unused]] spi_host_device_t host,
    quanta::frequency::hertz cap
) {
    // The SD SPI driver leaves the device's clock source at the default, whose frequency the SPI
    // master driver reads as this.
    uint32_t source_hz = 0;
    if (auto err = esp_clk_tree_src_get_freq_hz(
            static_cast<soc_module_clk_t>(SPI_CLK_SRC_DEFAULT),
            ESP_CLK_TREE_SRC_FREQ_PRECISION_CACHED,
            &source_hz
        );
        err != ESP_OK) {
        return error(err);
    }
    auto limit_hz =
        static_cast<uint32_t>(std::clamp<int64_t>(cap.count(), 0, sd_high_speed_max_hz));
#if CONFIG_IDF_TARGET_ESP32
    // The ESP32's SPI master driver rejects a full-duplex device whose reads the bus's pins can't
    // keep up with (over the GPIO matrix, above 26.7 MHz), and a device it rejects that way leaves
    // one of the bus's device slots taken until the bus is freed, which then fails an assertion. So
    // no clock above that limit is tried. The SD SPI driver sets no input delay.
    const spi_bus_attr_t* attr = spi_bus_get_attr(host);
    const bool gpio_matrix = attr == nullptr || (attr->flags & SPICOMMON_BUSFLAG_IOMUX_PINS) == 0;
    limit_hz = std::min(limit_hz, static_cast<uint32_t>(spi_get_freq_limit(gpio_matrix, 0)));
#endif
    // A rejected clock leaves the device removed from the bus; the next clock set adds it again.
    const auto choice =
        choose_spi_clock(source_hz, limit_hz, [handle](uint32_t khz) -> std::optional<uint32_t> {
            int reported_khz = 0;
            if (sdspi_host_set_card_clk(handle, khz) != ESP_OK ||
                sdspi_host_get_real_freq(handle, &reported_khz) != ESP_OK || reported_khz < 0) {
                return std::nullopt;
            }
            return static_cast<uint32_t>(reported_khz);
        });
    // Card initialization starts at the clock the device was added with, without setting it.
    if (auto err = sdspi_host_set_card_clk(handle, SDMMC_FREQ_PROBING); err != ESP_OK) {
        return error(err);
    }
    if (!choice) {
        return error(errc::invalid_arg);
    }
    return *choice;
}

// =============================================================================
// card_id
// =============================================================================

card_id::card_id(const sdmmc_card_t& c) noexcept
    : _cid(c.cid)
    , _is_mmc(c.is_mmc) {
    // Product name length: five characters on SD cards, seven on MMC v1 and six on later MMC
    // versions. The name is not necessarily NUL-terminated within the field.
    size_t max_len = 5;
    if (c.is_mmc) {
        max_len = c.csd.mmc_ver <= MMC_CSD_MMCVER_1_4 ? 7 : 6;
    }
    _name_len = static_cast<uint8_t>(strnlen(_cid.name, max_len));
}

std::string_view card_id::product_name() const noexcept {
    return {_cid.name, _name_len};
}

uint8_t card_id::manufacturer_id() const noexcept {
    return static_cast<uint8_t>(_cid.mfg_id);
}

uint16_t card_id::oem_id() const noexcept {
    return static_cast<uint16_t>(_cid.oem_id);
}

uint8_t card_id::revision() const noexcept {
    return static_cast<uint8_t>(_cid.revision);
}

uint32_t card_id::serial_number() const noexcept {
    return static_cast<uint32_t>(_cid.serial);
}

std::optional<std::chrono::year_month> card_id::manufacture_date() const noexcept {
    const auto d = static_cast<unsigned>(_cid.date);
    std::chrono::year_month ym;
    if (_is_mmc) {
        // MMC v1: month in bits 7-4, years since 1997 in bits 3-0. ESP-IDF reports 0 for later MMC
        // versions, which gives month 0 (not ok()).
        ym = std::chrono::year{1997 + static_cast<int>(d & 0xF)} /
            std::chrono::month{(d >> 4) & 0xF};
    } else {
        // SD: years since 2000 in bits 11-4, month in bits 3-0.
        ym = std::chrono::year{2000 + static_cast<int>((d >> 4) & 0xFF)} /
            std::chrono::month{d & 0xF};
    }
    if (!ym.ok()) {
        return std::nullopt;
    }
    return ym;
}

// =============================================================================
// card
// =============================================================================

struct card::state {
    state() = default;
    state(const state&) = delete;
    state& operator=(const state&) = delete;

    ~state() {
        if (spi_device) {
            // Releases the chip select first. sdspi_host_remove_device() leaves the pin reserved as
            // an output while making it an input, which logs a GPIO conflict, and logs another when
            // a card next uses the pin.
            spi_cs.reset();
            if (auto err = sdspi_host_remove_device(*spi_device); err != ESP_OK) {
                ESP_LOGW(TAG, "Failed to remove SD SPI device: %s", esp_err_to_name(err));
            }
            // The removal leaves the chip select an input without a pull-up, floating on a bus that
            // may still be in use. Resetting it again enables its pull-up, which keeps the card
            // deselected.
            spi_cs.reset();
        }
#if SOC_SDMMC_HOST_SUPPORTED
#if SOC_SDMMC_IO_POWER_EXTERNAL
        // Created after the slot was taken, so released before it.
        if (sdmmc_io_power) {
            if (auto err = sd_pwr_ctrl_del_on_chip_ldo(sdmmc_io_power); err != ESP_OK) {
                ESP_LOGW(
                    TAG, "Failed to release the card's I/O power LDO: %s", esp_err_to_name(err)
                );
            }
        }
#endif
        if (sdmmc_slot) {
            std::lock_guard lock(sdmmc_mutex);
            // Releases the slot only; the host is deinitialized with its last slot.
            if (auto err = sdmmc_host_deinit_slot(*sdmmc_slot); err != ESP_OK) {
                ESP_LOGW(
                    TAG, "Failed to release SDMMC slot %d: %s", *sdmmc_slot, esp_err_to_name(err)
                );
            }
            sdmmc_slot_in_use[*sdmmc_slot] = false;
        }
#endif
    }

    // Kept at a fixed address for the card's lifetime: a mounted filesystem holds a pointer to it.
    sdmmc_card_t card{};
    // Serializes commands sent through this object.
    std::mutex mtx;
    idfxx::gpio card_detect = gpio::nc();
    idfxx::gpio write_protect = gpio::nc();
    gpio::level write_protect_active_level = gpio::level::low;
    std::optional<sdspi_dev_handle_t> spi_device;
    idfxx::gpio spi_cs = gpio::nc();
#if SOC_SDMMC_HOST_SUPPORTED
    std::optional<int> sdmmc_slot;
#endif
#if SOC_SDMMC_IO_POWER_EXTERNAL
    sd_pwr_ctrl_handle_t sdmmc_io_power = nullptr;
#endif

    // Converts a byte count to a sector count, checking that the range lies on the card.
    [[nodiscard]] result<size_t> sectors_for(size_t first, size_t bytes) const {
        const auto sector_size = static_cast<size_t>(card.csd.sector_size);
        if (bytes % sector_size != 0) {
            return error(errc::invalid_size);
        }
        const size_t count = bytes / sector_size;
        if (auto r = check_range(first, count); !r) {
            return error(r.error());
        }
        return count;
    }

    [[nodiscard]] result<void> check_range(size_t first, size_t count) const {
        const size_t total = capacity_of(card);
        if (first > total || count > total - first) {
            return error(errc::invalid_size);
        }
        return {};
    }

    [[nodiscard]] bool is_write_protected() const {
        return write_protect.is_connected() &&
            write_protect.get_level() == write_protect_active_level;
    }

    // Erases or discards a range of sectors in chunks on multiples of
    // internal::erase_chunk_sectors, stopping at the first error. The card stays locked throughout.
    [[nodiscard]] result<void> erase(size_t first, size_t count, sdmmc_erase_arg_t arg) {
        if (count == 0) {
            return {};
        }
        if (auto r = check_range(first, count); !r) {
            return r;
        }
        std::lock_guard lock(mtx);
        if (is_write_protected()) {
            return error(errc::invalid_state);
        }
        while (count > 0) {
            const size_t n = std::min(
                count, internal::erase_chunk_sectors - (first & (internal::erase_chunk_sectors - 1))
            );
            if (auto r = wrap(sdmmc_erase_sectors(&card, first, n, arg)); !r) {
                return r;
            }
            first += n;
            count -= n;
        }
        return {};
    }
};

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
card::card(spi::master_bus& bus, const spi_config& cfg)
    : card(unwrap(make(bus, cfg))) {}
#endif

result<card> card::make(spi::master_bus& bus, const spi_config& cfg) {
    const idfxx::gpio cs = cfg.cs;
    if (!cs.is_connected() || !cs.is_output_capable()) {
        return error(errc::invalid_arg);
    }
    if (!distinct(std::array{cs, cfg.card_detect, cfg.write_protect})) {
        return error(errc::invalid_arg);
    }
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 5, 3)
    if (cfg.miso_wait_timeout &&
        (cfg.miso_wait_timeout->count() < 0 || cfg.miso_wait_timeout->count() > 127)) {
        return error(errc::invalid_arg);
    }
#endif

    auto s = std::make_unique<state>();
    s->card_detect = cfg.card_detect;
    s->write_protect = cfg.write_protect;
    s->write_protect_active_level = cfg.write_protect_active_level;

    // Value-initialized: duty_cycle_pos selects the driver default, as does wait_for_miso unless a
    // timeout is configured.
    sdspi_device_config_t dev_cfg{};
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 5, 3)
    if (cfg.miso_wait_timeout) {
        // -1 for no wait, since 0 selects the driver default.
        const auto ms = cfg.miso_wait_timeout->count();
        dev_cfg.wait_for_miso = ms == 0 ? int8_t{-1} : static_cast<int8_t>(ms);
    }
#endif
    dev_cfg.host_id = static_cast<spi_host_device_t>(bus.host());
    dev_cfg.gpio_cs = cs.idf_num();
    dev_cfg.gpio_cd = pin_or_nc(cfg.card_detect);
    dev_cfg.gpio_wp = pin_or_nc(cfg.write_protect);
    dev_cfg.gpio_int = GPIO_NUM_NC;
    dev_cfg.gpio_wp_polarity = cfg.write_protect_active_level == gpio::level::high;

    sdspi_dev_handle_t handle;
    if (auto err = sdspi_host_init_device(&dev_cfg, &handle); err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add SD SPI device: %s", esp_err_to_name(err));
        if (err == ESP_ERR_NOT_FOUND) {
            // From spi_bus_add_device(): the bus has no free device slot. Reported as
            // invalid_state, so that not_found keeps meaning that there is no card.
            return error(errc::invalid_state);
        }
        return error(err);
    }
    s->spi_device = handle;
    s->spi_cs = cs;

    // A bus without DMA would fail the card's data transfers. (With DMA, transactions can be at
    // least 4092 bytes long, whatever the bus's max_transfer_sz.) Checked once the card's device is
    // on the bus, since ESP-IDF reports the length only for a bus with a device, and before the
    // card is contacted. On failure, destroying the state removes the device again.
    if (bus.max_transaction_length() < sdspi_max_transaction_length) {
        return error(errc::invalid_arg);
    }
    // Found by setting clocks on the card's device, before the card is contacted. Done after the
    // check above, which would fail while a rejected clock has the device removed from the bus.
    auto clock = internal::choose_sdspi_clock(handle, dev_cfg.host_id, cfg.max_frequency);
    if (!clock) {
        return error(clock.error());
    }

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = handle;
    host.max_freq_khz = static_cast<int>(clock->request_khz);
    if (auto r = init_card(host, s->card); !r) {
        return error(r.error());
    }
    return card{std::move(s)};
}

#if SOC_SDMMC_HOST_SUPPORTED
#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
card::card(const sdmmc_config& cfg)
    : card(unwrap(make(cfg))) {}
#endif

result<card> card::make(const sdmmc_config& cfg) {
    const int slot_num = std::to_underlying(cfg.slot);
    if (slot_num < 0 || slot_num >= SOC_SDMMC_NUM_SLOTS) {
        return error(errc::invalid_arg);
    }
    auto width = resolve_sdmmc_width(cfg);
    if (!width) {
        return error(width.error());
    }
    auto speed = resolve_sdmmc_speed(cfg);
    if (!speed) {
        return error(speed.error());
    }

    auto s = std::make_unique<state>();
    s->card_detect = cfg.card_detect;
    s->write_protect = cfg.write_protect;
    s->write_protect_active_level = cfg.write_protect_active_level;

    sdmmc_slot_config_t slot_cfg{};
#if SOC_SDMMC_USE_GPIO_MATRIX
    // Never all zero (CLK, CMD and D0 are distinct and connected), which the driver would take to
    // mean IO MUX.
    slot_cfg.clk = cfg.clk->idf_num();
    slot_cfg.cmd = cfg.cmd->idf_num();
    slot_cfg.d0 = cfg.d0->idf_num();
    slot_cfg.d1 = pin_or_nc(cfg.d1);
    slot_cfg.d2 = pin_or_nc(cfg.d2);
    slot_cfg.d3 = pin_or_nc(cfg.d3);
    slot_cfg.d4 = pin_or_nc(cfg.d4);
    slot_cfg.d5 = pin_or_nc(cfg.d5);
    slot_cfg.d6 = pin_or_nc(cfg.d6);
    slot_cfg.d7 = pin_or_nc(cfg.d7);
#else
    // The slot's dedicated pins are used; these fields are ignored.
    slot_cfg.clk = slot_cfg.cmd = slot_cfg.d0 = slot_cfg.d1 = slot_cfg.d2 = slot_cfg.d3 =
        GPIO_NUM_NC;
    slot_cfg.d4 = slot_cfg.d5 = slot_cfg.d6 = slot_cfg.d7 = GPIO_NUM_NC;
#endif
    slot_cfg.cd = pin_or_nc(cfg.card_detect);
    slot_cfg.wp = pin_or_nc(cfg.write_protect);
    // Always explicit: the driver's default is the slot's maximum, regardless of the pins supplied.
    slot_cfg.width = std::to_underlying(*width);
    slot_cfg.flags = 0;
    if (cfg.internal_pullups) {
        slot_cfg.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
    }
    if (cfg.write_protect_active_level == gpio::level::high) {
        slot_cfg.flags |= SDMMC_SLOT_FLAG_WP_ACTIVE_HIGH;
    }
    if (speed->uhs1) {
        slot_cfg.flags |= SDMMC_SLOT_FLAG_UHS1;
    }

    {
        std::lock_guard lock(sdmmc_mutex);
        // Checked here because ESP-IDF 5.5 reconfigures a slot that is already in use rather than
        // rejecting it.
        if (sdmmc_slot_in_use[slot_num]) {
            return error(errc::invalid_state);
        }
        // Zero-initialized: ESP-IDF 6.x leaves the struct untouched when the host is not
        // initialized.
        sdmmc_host_state_t host_state{};
        (void)sdmmc_host_get_state(&host_state);
        // The host is shared between slots, and may already be initialized by other code. ESP-IDF
        // then returns ESP_OK; ESP_ERR_INVALID_STATE is tolerated too.
        if (auto err = sdmmc_host_init(); err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
            ESP_LOGE(TAG, "Failed to initialize SDMMC host: %s", esp_err_to_name(err));
            return error(err);
        }
        if (auto err = sdmmc_host_init_slot(slot_num, &slot_cfg); err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initialize SDMMC slot %d: %s", slot_num, esp_err_to_name(err));
            // Releasing a slot deinitializes the host along with its last slot, so a host left with
            // no slots only arises here, and a host that was not yet initialized has no slot in use
            // by another card. Undo the host initialization if this call did it; a host that was
            // already up belongs to someone else (or to another card), and stays.
            if (!host_state.host_initialized) {
                if (auto deinit_err = sdmmc_host_deinit(); deinit_err != ESP_OK) {
                    ESP_LOGW(
                        TAG, "Failed to deinitialize SDMMC host: %s", esp_err_to_name(deinit_err)
                    );
                }
            }
            return error(err);
        }
        sdmmc_slot_in_use[slot_num] = true;
        s->sdmmc_slot = slot_num;
    }

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.slot = slot_num;
    host.check_buffer_alignment = &sdmmc_check_buffer;
    host.max_freq_khz = speed->max_freq_khz;
    if (speed->ddr) {
        host.flags |= SDMMC_HOST_FLAG_DDR;
    } else {
        host.flags &= ~SDMMC_HOST_FLAG_DDR;
    }
#if SOC_SDMMC_IO_POWER_EXTERNAL
    if (cfg.io_power_ldo) {
        const sd_pwr_ctrl_ldo_config_t ldo_cfg{.ldo_chan_id = *cfg.io_power_ldo};
        if (auto err = sd_pwr_ctrl_new_on_chip_ldo(&ldo_cfg, &s->sdmmc_io_power); err != ESP_OK) {
            ESP_LOGE(
                TAG,
                "Failed to take LDO channel %d for the card's I/O power: %s",
                *cfg.io_power_ldo,
                esp_err_to_name(err)
            );
            return error(err);
        }
        host.pwr_ctrl_handle = s->sdmmc_io_power;
    }
#endif
    if (auto r = init_card(host, s->card); !r) {
        return error(r.error());
    }
    return card{std::move(s)};
}
#endif // SOC_SDMMC_HOST_SUPPORTED

card::card(std::unique_ptr<state> s) noexcept
    : _state(std::move(s)) {}

card::~card() = default;
card::card(card&& other) noexcept = default;
card& card::operator=(card&& other) noexcept = default;

// =============================================================================
// Identity and geometry
// =============================================================================

card_type card::type() const noexcept {
    return classify(_state->card);
}

card_id card::id() const noexcept {
    return card_id{_state->card};
}

uint64_t card::size() const noexcept {
    return size_of(_state->card);
}

size_t card::sector_size() const noexcept {
    return static_cast<size_t>(_state->card.csd.sector_size);
}

size_t card::sector_count() const noexcept {
    return capacity_of(_state->card);
}

quanta::frequency::hertz card::frequency() const noexcept {
    return quanta::frequency::hertz{int64_t{_state->card.real_freq_khz} * 1000};
}

enum bus_width card::width() const noexcept {
    return static_cast<enum bus_width>(1u << _state->card.log_bus_width);
}

// =============================================================================
// Slot state
// =============================================================================

bool card::is_present() const {
    return !_state->card_detect.is_connected() ||
        _state->card_detect.get_level() == gpio::level::low;
}

bool card::is_write_protected() const {
    return _state->is_write_protected();
}

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
void card::probe() const {
    unwrap(try_probe());
}
#endif

result<void> card::try_probe() const {
    std::lock_guard lock(_state->mtx);
    return wrap(sdmmc_get_status(&_state->card));
}

// =============================================================================
// Raw sector I/O
// =============================================================================

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
void card::read_sectors(size_t first, byte_span dst) const {
    unwrap(try_read_sectors(first, dst));
}
#endif

result<void> card::try_read_sectors(size_t first, byte_span dst) const {
    if (dst.empty()) {
        return {};
    }
    auto count = _state->sectors_for(first, dst.size());
    if (!count) {
        return error(count.error());
    }
    std::lock_guard lock(_state->mtx);
    return wrap(sdmmc_read_sectors(&_state->card, dst.data(), first, *count));
}

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
void card::write_sectors(size_t first, byte_view src) {
    unwrap(try_write_sectors(first, src));
}
#endif

result<void> card::try_write_sectors(size_t first, byte_view src) {
    if (src.empty()) {
        return {};
    }
    auto count = _state->sectors_for(first, src.size());
    if (!count) {
        return error(count.error());
    }
    std::lock_guard lock(_state->mtx);
    if (_state->is_write_protected()) {
        return error(errc::invalid_state);
    }
    return wrap(sdmmc_write_sectors(&_state->card, src.data(), first, *count));
}

// =============================================================================
// Erase
// =============================================================================

bool card::supports_discard() const noexcept {
    return sdmmc_can_discard(&_state->card) == ESP_OK;
}

bool card::supports_sanitize() const noexcept {
    return sdmmc_mmc_can_sanitize(&_state->card) == ESP_OK;
}

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
void card::erase_sectors(size_t first, size_t count) {
    unwrap(try_erase_sectors(first, count));
}

void card::erase_sectors(size_t first, size_t count, discard_t) {
    unwrap(try_erase_sectors(first, count, discard));
}

void card::erase_all() {
    unwrap(try_erase_all());
}

void card::sanitize(std::chrono::milliseconds timeout) {
    unwrap(try_sanitize(timeout));
}
#endif

result<void> card::try_erase_sectors(size_t first, size_t count) {
    // On MMC/eMMC, ESP-IDF sends this as a trim.
    return _state->erase(first, count, SDMMC_ERASE_ARG);
}

result<void> card::try_erase_sectors(size_t first, size_t count, discard_t) {
    return _state->erase(first, count, SDMMC_DISCARD_ARG);
}

result<void> card::try_erase_all() {
    return _state->erase(0, capacity_of(_state->card), SDMMC_ERASE_ARG);
}

result<void> card::try_sanitize(std::chrono::milliseconds timeout) {
    if (timeout.count() <= 0) {
        return error(errc::invalid_arg);
    }
    const auto timeout_ms = static_cast<uint32_t>(std::min<std::chrono::milliseconds::rep>(
        timeout.count(), std::numeric_limits<uint32_t>::max()
    ));
    std::lock_guard lock(_state->mtx);
    if (_state->is_write_protected()) {
        return error(errc::invalid_state);
    }
    return wrap(sdmmc_mmc_sanitize(&_state->card, timeout_ms));
}

sdmmc_card_t* card::idf_handle() const noexcept {
    return _state ? &_state->card : nullptr;
}

} // namespace idfxx::sdcard

namespace idfxx {

std::string to_string(sdcard::card_type t) {
    switch (t) {
    case sdcard::card_type::sdsc:
        return "SDSC";
    case sdcard::card_type::sdhc:
        return "SDHC";
    case sdcard::card_type::sdxc:
        return "SDXC";
    case sdcard::card_type::mmc:
        return "MMC";
    default:
        return "unknown(" + std::to_string(std::to_underlying(t)) + ')';
    }
}

std::string to_string(sdcard::bus_width w) {
    switch (w) {
    case sdcard::bus_width::one:
        return "1-bit";
    case sdcard::bus_width::four:
        return "4-bit";
    case sdcard::bus_width::eight:
        return "8-bit";
    default:
        return "unknown(" + std::to_string(std::to_underlying(w)) + ')';
    }
}

std::string to_string(sdcard::bus_speed s) {
    switch (s) {
    case sdcard::bus_speed::default_speed:
        return "default speed";
    case sdcard::bus_speed::high_speed:
        return "high speed";
#if SOC_SDMMC_UHS_I_SUPPORTED
    case sdcard::bus_speed::sdr50:
        return "SDR50";
    case sdcard::bus_speed::ddr50:
        return "DDR50";
#endif
#if SOC_SDMMC_UHS_I_SUPPORTED && ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
    case sdcard::bus_speed::sdr104:
        return "SDR104";
#endif
    default:
        return "unknown(" + std::to_string(std::to_underlying(s)) + ')';
    }
}

#if SOC_SDMMC_HOST_SUPPORTED
std::string to_string(sdcard::card::slot s) {
    switch (s) {
    case sdcard::card::slot::slot_0:
        return "slot_0";
    case sdcard::card::slot::slot_1:
        return "slot_1";
    default:
        return "unknown(" + std::to_string(std::to_underlying(s)) + ')';
    }
}
#endif

} // namespace idfxx
