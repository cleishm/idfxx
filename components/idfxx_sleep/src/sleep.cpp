// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#include <idfxx/sleep>

#include <bit>
#include <esp_idf_version.h>
#include <esp_sleep.h>
#include <string>
#include <utility>

// Verify wakeup_source values match ESP-IDF constants. Only the prefix up to
// uart is stable across IDF versions (v6.0 inserted UART1/UART2 after it,
// shifting every later value); the remaining sources are translated by
// to_esp_source/from_esp_cause below instead of relying on equal values.
static_assert(std::to_underlying(idfxx::sleep::wakeup_source::ext0) == ESP_SLEEP_WAKEUP_EXT0);
static_assert(std::to_underlying(idfxx::sleep::wakeup_source::ext1) == ESP_SLEEP_WAKEUP_EXT1);
static_assert(std::to_underlying(idfxx::sleep::wakeup_source::timer) == ESP_SLEEP_WAKEUP_TIMER);
static_assert(std::to_underlying(idfxx::sleep::wakeup_source::touchpad) == ESP_SLEEP_WAKEUP_TOUCHPAD);
static_assert(std::to_underlying(idfxx::sleep::wakeup_source::ulp) == ESP_SLEEP_WAKEUP_ULP);
static_assert(std::to_underlying(idfxx::sleep::wakeup_source::gpio) == ESP_SLEEP_WAKEUP_GPIO);
static_assert(std::to_underlying(idfxx::sleep::wakeup_source::uart) == ESP_SLEEP_WAKEUP_UART);

// Verify ext1_mode values match ESP-IDF constants. The low trigger is named
// all_low on the original ESP32 (it wakes only when all pins are low) and
// any_low everywhere else; only the matching enumerator is defined per target.
#if SOC_PM_SUPPORT_EXT1_WAKEUP
#if CONFIG_IDF_TARGET_ESP32
static_assert(std::to_underlying(idfxx::sleep::ext1_mode::all_low) == ESP_EXT1_WAKEUP_ALL_LOW);
#else
static_assert(std::to_underlying(idfxx::sleep::ext1_mode::any_low) == ESP_EXT1_WAKEUP_ANY_LOW);
#endif
static_assert(std::to_underlying(idfxx::sleep::ext1_mode::any_high) == ESP_EXT1_WAKEUP_ANY_HIGH);
#endif

// Verify deep_sleep_gpio_mode values match ESP-IDF constants
#if SOC_GPIO_SUPPORT_DEEPSLEEP_WAKEUP
static_assert(std::to_underlying(idfxx::sleep::deep_sleep_gpio_mode::wake_low) == ESP_GPIO_WAKEUP_GPIO_LOW);
static_assert(std::to_underlying(idfxx::sleep::deep_sleep_gpio_mode::wake_high) == ESP_GPIO_WAKEUP_GPIO_HIGH);
#endif

namespace idfxx::sleep {

namespace {

/// Translates a wakeup_source to the ESP-IDF source value. Values up to uart
/// are identical; later ones shift between IDF versions. The caller must
/// exclude wakeup_source::other.
esp_sleep_source_t to_esp_source(wakeup_source source) noexcept {
    switch (source) {
    case wakeup_source::wifi:
        return ESP_SLEEP_WAKEUP_WIFI;
    case wakeup_source::cocpu:
        return ESP_SLEEP_WAKEUP_COCPU;
    case wakeup_source::cocpu_trap:
        return ESP_SLEEP_WAKEUP_COCPU_TRAP_TRIG;
    case wakeup_source::bt:
        return ESP_SLEEP_WAKEUP_BT;
    default:
        return static_cast<esp_sleep_source_t>(source);
    }
}

/// Translates an ESP-IDF wake-up cause to a wakeup_source. The inverse of
/// to_esp_source; causes this component has no enumerator for report as
/// wakeup_source::other.
wakeup_source from_esp_cause(esp_sleep_wakeup_cause_t cause) noexcept {
    switch (cause) {
    case ESP_SLEEP_WAKEUP_EXT0:
    case ESP_SLEEP_WAKEUP_EXT1:
    case ESP_SLEEP_WAKEUP_TIMER:
    case ESP_SLEEP_WAKEUP_TOUCHPAD:
    case ESP_SLEEP_WAKEUP_ULP:
    case ESP_SLEEP_WAKEUP_GPIO:
    case ESP_SLEEP_WAKEUP_UART:
        return static_cast<wakeup_source>(cause);
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
    case ESP_SLEEP_WAKEUP_UART1:
    case ESP_SLEEP_WAKEUP_UART2:
        return wakeup_source::uart;
#endif
    case ESP_SLEEP_WAKEUP_WIFI:
        return wakeup_source::wifi;
    case ESP_SLEEP_WAKEUP_COCPU:
        return wakeup_source::cocpu;
    case ESP_SLEEP_WAKEUP_COCPU_TRAP_TRIG:
        return wakeup_source::cocpu_trap;
    case ESP_SLEEP_WAKEUP_BT:
        return wakeup_source::bt;
    default:
        return wakeup_source::other;
    }
}

} // namespace

// =============================================================================
// Wake-up source arming
// =============================================================================

namespace detail {

result<void> arm(const timer_wake& source) {
    if (source.after < std::chrono::microseconds::zero()) {
        return error(errc::invalid_arg);
    }
    return wrap(esp_sleep_enable_timer_wakeup(static_cast<uint64_t>(source.after.count())));
}

void disarm(const timer_wake& /*source*/) noexcept {
    (void)esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
}

result<void> arm(const gpio_wake& source) {
    if (!source.pin.is_connected()) {
        return error(errc::invalid_arg);
    }
    const auto intr = source.level == idfxx::gpio::level::high ? idfxx::gpio::intr_type::high_level
                                                               : idfxx::gpio::intr_type::low_level;
    auto pin = source.pin;
    if (auto e = pin.try_wakeup_enable(intr); !e) {
        return e;
    }
    return wrap(esp_sleep_enable_gpio_wakeup());
}

void disarm(const gpio_wake& source) noexcept {
    // Disarms only the pin; the GPIO wake-up source itself stays enabled (it
    // is inert with no armed pins, and other pins may still be armed).
    auto pin = source.pin;
    (void)pin.try_wakeup_disable();
}

#if SOC_PM_SUPPORT_EXT0_WAKEUP
result<void> arm(const ext0_wake& source) {
    if (!source.pin.is_connected()) {
        return error(errc::invalid_arg);
    }
    return wrap(esp_sleep_enable_ext0_wakeup(source.pin.idf_num(), std::to_underlying(source.level)));
}

void disarm(const ext0_wake& /*source*/) noexcept {
    (void)esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_EXT0);
}
#endif // SOC_PM_SUPPORT_EXT0_WAKEUP

#if SOC_PM_SUPPORT_EXT1_WAKEUP
result<void> arm(const ext1_wake& source) {
    if (source.pin_mask() == 0) {
        return error(errc::invalid_arg);
    }
    return wrap(
        esp_sleep_enable_ext1_wakeup_io(source.pin_mask(), static_cast<esp_sleep_ext1_wakeup_mode_t>(source.mode()))
    );
}

void disarm(const ext1_wake& source) noexcept {
    (void)esp_sleep_disable_ext1_wakeup_io(source.pin_mask());
}
#endif // SOC_PM_SUPPORT_EXT1_WAKEUP

#if SOC_GPIO_SUPPORT_DEEPSLEEP_WAKEUP
result<void> arm(const deep_sleep_gpio_wake& source) {
    if (source.pin_mask() == 0) {
        return error(errc::invalid_arg);
    }
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
    return wrap(esp_sleep_enable_gpio_wakeup_on_hp_periph_powerdown(
        source.pin_mask(), static_cast<esp_sleep_gpio_wake_up_mode_t>(source.mode())
    ));
#else
    return wrap(esp_deep_sleep_enable_gpio_wakeup(
        source.pin_mask(), static_cast<esp_deepsleep_gpio_wake_up_mode_t>(source.mode())
    ));
#endif
}

void disarm(const deep_sleep_gpio_wake& /*source*/) noexcept {
    (void)esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_GPIO);
}
#endif // SOC_GPIO_SUPPORT_DEEPSLEEP_WAKEUP

result<wakeup_source> do_light_sleep() {
    if (auto e = wrap(esp_light_sleep_start()); !e) {
        return error(e.error());
    }
    // A completed light sleep always has a cause; `other` covers the
    // (unexpected) case where the chip cannot report one.
    return wakeup_cause().value_or(wakeup_source::other);
}

void do_deep_sleep() noexcept {
    esp_deep_sleep_start();
}

} // namespace detail

// =============================================================================
// Entering sleep
// =============================================================================

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
wakeup_source light_sleep_for(std::chrono::microseconds duration) {
    return unwrap(try_light_sleep_for(duration));
}
#endif

result<wakeup_source> try_light_sleep_for(std::chrono::microseconds duration) {
    return try_light_sleep(timer_wake{duration});
}

void deep_sleep() noexcept {
    detail::do_deep_sleep();
}

void deep_sleep(std::chrono::microseconds duration) noexcept {
    const auto clamped = duration < std::chrono::microseconds::zero() ? std::chrono::microseconds::zero() : duration;
    esp_deep_sleep(static_cast<uint64_t>(clamped.count()));
}

// =============================================================================
// Status
// =============================================================================

std::string to_string(wakeup_source source) {
    switch (source) {
    case wakeup_source::ext0:
        return "ext0";
    case wakeup_source::ext1:
        return "ext1";
    case wakeup_source::timer:
        return "timer";
    case wakeup_source::touchpad:
        return "touchpad";
    case wakeup_source::ulp:
        return "ulp";
    case wakeup_source::gpio:
        return "gpio";
    case wakeup_source::uart:
        return "uart";
    case wakeup_source::wifi:
        return "wifi";
    case wakeup_source::cocpu:
        return "cocpu";
    case wakeup_source::cocpu_trap:
        return "cocpu_trap";
    case wakeup_source::bt:
        return "bt";
    case wakeup_source::other:
        return "other";
    }
    return "unknown(" + std::to_string(std::to_underlying(source)) + ")";
}

std::optional<wakeup_source> wakeup_cause() noexcept {
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
    // esp_sleep_get_wakeup_cause() is deprecated in v6; the replacement
    // returns a bitmap with bit n set for cause n.
    const uint32_t causes = esp_sleep_get_wakeup_causes();
    if (causes == 0 || (causes & (uint32_t{1} << ESP_SLEEP_WAKEUP_UNDEFINED)) != 0) {
        return std::nullopt;
    }
    return from_esp_cause(static_cast<esp_sleep_wakeup_cause_t>(std::countr_zero(causes)));
#else
    const auto cause = esp_sleep_get_wakeup_cause();
    if (cause == ESP_SLEEP_WAKEUP_UNDEFINED) {
        return std::nullopt;
    }
    return from_esp_cause(cause);
#endif
}

#if SOC_PM_SUPPORT_EXT1_WAKEUP
uint64_t ext1_wakeup_status() noexcept {
    return esp_sleep_get_ext1_wakeup_status();
}
#endif

// =============================================================================
// Persistent wake-up source configuration
// =============================================================================

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
void disable_wakeup_source(wakeup_source source) {
    unwrap(try_disable_wakeup_source(source));
}

void disable_all_wakeup_sources() {
    unwrap(try_disable_all_wakeup_sources());
}
#endif

result<void> try_disable_wakeup_source(wakeup_source source) {
    if (source == wakeup_source::other) {
        return error(errc::invalid_arg);
    }
    return wrap(esp_sleep_disable_wakeup_source(to_esp_source(source)));
}

result<void> try_disable_all_wakeup_sources() {
    return wrap(esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL));
}

} // namespace idfxx::sleep
