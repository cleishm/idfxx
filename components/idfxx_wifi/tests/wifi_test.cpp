// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#include <idfxx/nvs>
#include <idfxx/wifi>

#include <array>
#include <chrono>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <esp_idf_version.h>
#include <limits>
#include <optional>
#include <span>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <unity.h>
#include <utility>
#include <vector>

using namespace idfxx::wifi;

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 1, 0)
static_assert(std::to_underlying(event_id::nan_pairing_indication) == 50);
static_assert(std::to_underlying(event_id::nan_pairing_confirm) == 51);
static_assert(std::to_underlying(event_id::nan_cluster_join) == 52);
static_assert(std::is_same_v<decltype(init_config::privacy_enhancements), std::optional<bool>>);
static_assert(std::is_same_v<decltype(init_config::rmac_auto_reset_interval), std::optional<std::chrono::hours>>);
#endif

// The vendor IE callback receives the whole element as a byte view
static_assert(std::is_same_v<
              vendor_ie_cb,
              void (*)(void*, enum vendor_ie_type, idfxx::mac_address, idfxx::byte_view, quanta::electro::dbm)>);

// =============================================================================
// Connect overloads
// =============================================================================

// connect() initiates only; connect(timeout) waits for and returns the address
static_assert(std::is_same_v<decltype(try_connect()), idfxx::result<void>>);
static_assert(std::is_same_v<decltype(try_connect(std::chrono::seconds{30})), idfxx::result<idfxx::net::ipv4_info>>);
#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
static_assert(std::is_same_v<decltype(connect()), void>);
static_assert(std::is_same_v<decltype(connect(std::chrono::seconds{30})), idfxx::net::ipv4_info>);
#endif

// =============================================================================
// Raw 802.11
// =============================================================================

// tx_80211 takes the frame as a byte view: byte or uint8_t ranges, or {ptr, len}
template<typename T>
concept tx_80211_accepts = requires(T frame) {
    { try_tx_80211(role::sta, frame, false) } -> std::same_as<idfxx::result<void>>;
};
static_assert(tx_80211_accepts<std::array<uint8_t, 24>&>);
static_assert(tx_80211_accepts<const std::vector<std::byte>&>);
static_assert(tx_80211_accepts<std::span<const uint8_t>>);
static_assert(!tx_80211_accepts<std::string_view>);
static_assert(requires(const uint8_t* p, size_t n) { { try_tx_80211(role::ap, {p, n}, true) } -> std::same_as<idfxx::result<void>>; });
#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
static_assert(requires(std::array<std::byte, 24> frame) { { tx_80211(role::sta, frame, false) } -> std::same_as<void>; });
#endif

// =============================================================================
// Vendor IE
// =============================================================================

// set_vendor_ie takes the whole element as a byte view: byte or uint8_t ranges, or {ptr, len}
template<typename T>
concept vendor_ie_accepts = requires(T ie) {
    { try_set_vendor_ie(true, vendor_ie_type::beacon, vendor_ie_id::id_0, ie) } -> std::same_as<idfxx::result<void>>;
};
static_assert(vendor_ie_accepts<std::array<uint8_t, 8>&>);
static_assert(vendor_ie_accepts<const std::vector<std::byte>&>);
static_assert(vendor_ie_accepts<std::span<const uint8_t>>);
static_assert(!vendor_ie_accepts<std::string_view>);
static_assert(!vendor_ie_accepts<const void*>);
static_assert(requires(const uint8_t* p, size_t n) { { try_set_vendor_ie(true, vendor_ie_type::probe_resp, vendor_ie_id::id_1, {p, n}) } -> std::same_as<idfxx::result<void>>; });
static_assert(requires { { try_set_vendor_ie(false, vendor_ie_type::beacon, vendor_ie_id::id_0, {}) } -> std::same_as<idfxx::result<void>>; });
#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
static_assert(requires(std::array<std::byte, 8> ie) { { set_vendor_ie(true, vendor_ie_type::beacon, vendor_ie_id::id_0, ie) } -> std::same_as<void>; });
#endif

// =============================================================================
// Type properties
// =============================================================================

// config structs are aggregates
static_assert(std::is_aggregate_v<init_config>);
static_assert(std::is_aggregate_v<sta_config>);
static_assert(std::is_aggregate_v<scan_config>);
static_assert(std::is_aggregate_v<ap_record>);
static_assert(std::is_aggregate_v<connected_event_data>);
static_assert(std::is_aggregate_v<disconnected_event_data>);
static_assert(std::is_aggregate_v<pmf_config>);
static_assert(std::is_aggregate_v<ap_config>);
static_assert(std::is_aggregate_v<scan_default_params>);
static_assert(std::is_aggregate_v<protocols_config>);
static_assert(std::is_aggregate_v<bandwidths_config>);
static_assert(std::is_aggregate_v<channel_info>);
static_assert(std::is_aggregate_v<ftm_initiator_config>);
static_assert(std::is_aggregate_v<csi_config>);
static_assert(std::is_aggregate_v<he_ap_info>);
static_assert(std::is_aggregate_v<country_config>);
static_assert(std::is_aggregate_v<sta_info>);
static_assert(std::is_aggregate_v<ftm_report_entry>);
static_assert(std::is_aggregate_v<scan_done_event_data>);
static_assert(std::is_aggregate_v<authmode_change_event_data>);
static_assert(std::is_aggregate_v<ap_sta_connected_event_data>);
static_assert(std::is_aggregate_v<ap_sta_disconnected_event_data>);
static_assert(std::is_aggregate_v<ap_probe_req_event_data>);
static_assert(std::is_aggregate_v<bss_rssi_low_event_data>);
static_assert(std::is_aggregate_v<home_channel_change_event_data>);
static_assert(std::is_aggregate_v<ftm_report_event_data>);

// flags operator checks
static_assert(idfxx::enable_flags_operators<role>);
static_assert(idfxx::enable_flags_operators<sae_pwe_method>);
static_assert(idfxx::enable_flags_operators<protocol>);
static_assert(idfxx::enable_flags_operators<promiscuous_filter>);
static_assert(idfxx::enable_flags_operators<promiscuous_ctrl_filter>);
static_assert(idfxx::enable_flags_operators<channel_5g>);
static_assert(idfxx::enable_flags_operators<event_mask>);

// =============================================================================
// Runtime tests
// =============================================================================

// The Wi-Fi driver's default init keeps its configuration in NVS, so NVS flash must be initialized
// first.
static void ensure_nvs_init() {
    auto result = idfxx::nvs::flash::try_init();
    if (!result && (result.error() == idfxx::nvs::errc::no_free_pages ||
                    result.error() == idfxx::nvs::errc::new_version_found)) {
        TEST_ASSERT_TRUE(idfxx::nvs::flash::try_erase().has_value());
        result = idfxx::nvs::flash::try_init();
    }
    TEST_ASSERT_TRUE(result.has_value());
}

TEST_CASE("wifi init and deinit", "[idfxx][wifi][hw]") {
    ensure_nvs_init();
    auto result = try_init();
    TEST_ASSERT_TRUE(result.has_value());

    auto deinit_result = try_deinit();
    TEST_ASSERT_TRUE(deinit_result.has_value());
}

TEST_CASE("wifi init, set roles, set sta config, start, stop, deinit", "[idfxx][wifi][hw]") {
    ensure_nvs_init();
    auto init_result = try_init();
    TEST_ASSERT_TRUE(init_result.has_value());

    auto mode_result = try_set_roles(role::sta);
    TEST_ASSERT_TRUE(mode_result.has_value());

    auto cfg_result = try_set_sta_config({.ssid = "test_network", .password = "test_password"});
    TEST_ASSERT_TRUE(cfg_result.has_value());

    auto start_result = try_start();
    TEST_ASSERT_TRUE(start_result.has_value());

    auto stop_result = try_stop();
    TEST_ASSERT_TRUE(stop_result.has_value());

    auto deinit_result = try_deinit();
    TEST_ASSERT_TRUE(deinit_result.has_value());
}

TEST_CASE("wifi get roles after set", "[idfxx][wifi][hw]") {
    ensure_nvs_init();
    auto init_result = try_init();
    TEST_ASSERT_TRUE(init_result.has_value());

    auto set_result = try_set_roles(role::sta);
    TEST_ASSERT_TRUE(set_result.has_value());

    auto get_result = try_get_roles();
    TEST_ASSERT_TRUE(get_result.has_value());
    TEST_ASSERT_TRUE(get_result->contains(role::sta));

    auto deinit_result = try_deinit();
    TEST_ASSERT_TRUE(deinit_result.has_value());
}

TEST_CASE("wifi error category name", "[idfxx][wifi]") {
    TEST_ASSERT_EQUAL_STRING("wifi::Error", idfxx::wifi_category().name());
}

TEST_CASE("wifi error code conversion", "[idfxx][wifi]") {
    auto ec = make_error_code(errc::not_init);
    TEST_ASSERT_EQUAL_STRING("wifi::Error", ec.category().name());
    TEST_ASSERT_FALSE(ec.message().empty());
}

TEST_CASE("wifi AP role lifecycle", "[idfxx][wifi][hw]") {
    ensure_nvs_init();
    auto init_result = try_init();
    TEST_ASSERT_TRUE(init_result.has_value());

    auto mode_result = try_set_roles(role::ap);
    TEST_ASSERT_TRUE(mode_result.has_value());

    auto cfg_result = try_set_ap_config({.ssid = "test_ap", .password = "12345678", .authmode = auth_mode::wpa2_psk});
    TEST_ASSERT_TRUE(cfg_result.has_value());

    auto start_result = try_start();
    TEST_ASSERT_TRUE(start_result.has_value());

    auto list_result = try_get_sta_list();
    TEST_ASSERT_TRUE(list_result.has_value());
    TEST_ASSERT_EQUAL(0, list_result->size());

    auto stop_result = try_stop();
    TEST_ASSERT_TRUE(stop_result.has_value());

    auto deinit_result = try_deinit();
    TEST_ASSERT_TRUE(deinit_result.has_value());
}

TEST_CASE("wifi error category covers new error codes", "[idfxx][wifi]") {
    auto ec = make_error_code(errc::twt_full);
    TEST_ASSERT_FALSE(ec.message().empty());
    ec = make_error_code(errc::discard);
    TEST_ASSERT_FALSE(ec.message().empty());
    ec = make_error_code(errc::roc_in_progress);
    TEST_ASSERT_FALSE(ec.message().empty());
}

TEST_CASE("wifi value_too_large is equivalent to std::errc::value_too_large", "[idfxx][wifi]") {
    auto ec = make_error_code(errc::value_too_large);
    TEST_ASSERT_EQUAL_STRING("wifi::Error", ec.category().name());
    TEST_ASSERT_FALSE(ec.message().empty());
    TEST_ASSERT_TRUE(ec == std::errc::value_too_large);
    TEST_ASSERT_TRUE(ec == errc::value_too_large);
}

TEST_CASE("wifi try_init rejects oversized buffer count", "[idfxx][wifi]") {
    init_config cfg;
    cfg.static_rx_buf_num =
        static_cast<unsigned int>(std::numeric_limits<int>::max()) + 1u;

    auto r = try_init(cfg);
    TEST_ASSERT_FALSE(r.has_value());
    TEST_ASSERT_TRUE(r.error() == errc::value_too_large);
    TEST_ASSERT_TRUE(r.error() == std::errc::value_too_large);
    TEST_ASSERT_EQUAL_STRING("wifi::Error", r.error().category().name());
}

TEST_CASE("wifi sta_config listen interval defaults to unset", "[idfxx][wifi]") {
    sta_config cfg;
    TEST_ASSERT_FALSE(cfg.listen_interval.has_value());
}

TEST_CASE("wifi try_set_sta_config rejects zero listen interval", "[idfxx][wifi]") {
    sta_config cfg;
    cfg.listen_interval = 0;
    auto r = try_set_sta_config(cfg);
    TEST_ASSERT_FALSE(r.has_value());
    TEST_ASSERT_TRUE(r.error() == idfxx::errc::invalid_arg);
}

TEST_CASE("wifi try_set_sta_config rejects an over-long SSID or password", "[idfxx][wifi]") {
    auto r = try_set_sta_config({.ssid = std::string(33, 'a'), .password = ""});
    TEST_ASSERT_FALSE(r.has_value());
    TEST_ASSERT_TRUE(r.error() == errc::ssid);

    r = try_set_sta_config({.ssid = "net", .password = std::string(65, 'a')});
    TEST_ASSERT_FALSE(r.has_value());
    TEST_ASSERT_TRUE(r.error() == errc::password);
}

TEST_CASE("wifi try_set_ap_config rejects an over-long SSID or password", "[idfxx][wifi]") {
    auto r = try_set_ap_config({.ssid = std::string(33, 'a'), .password = ""});
    TEST_ASSERT_FALSE(r.has_value());
    TEST_ASSERT_TRUE(r.error() == errc::ssid);

    r = try_set_ap_config({.ssid = "net", .password = std::string(64, 'a')});
    TEST_ASSERT_FALSE(r.has_value());
    TEST_ASSERT_TRUE(r.error() == errc::password);
}

TEST_CASE("wifi try_set_vendor_ie rejects malformed elements", "[idfxx][wifi]") {
    auto set = [](idfxx::byte_view ie) {
        return try_set_vendor_ie(true, vendor_ie_type::beacon, vendor_ie_id::id_0, ie);
    };

    // Empty
    auto r = set({});
    TEST_ASSERT_FALSE(r.has_value());
    TEST_ASSERT_TRUE(r.error() == idfxx::errc::invalid_arg);

    // Shorter than the 6-byte header (ID, length, OUI, OUI type)
    std::array<uint8_t, 5> short_ie{0xDD, 3, 0x12, 0x34, 0x56};
    r = set(short_ie);
    TEST_ASSERT_FALSE(r.has_value());
    TEST_ASSERT_TRUE(r.error() == idfxx::errc::invalid_arg);

    // Wrong element ID
    std::array<uint8_t, 8> wrong_id{0xDC, 6, 0x12, 0x34, 0x56, 0x01, 0xCA, 0xFE};
    r = set(wrong_id);
    TEST_ASSERT_FALSE(r.has_value());
    TEST_ASSERT_TRUE(r.error() == idfxx::errc::invalid_arg);

    // Length byte larger than the bytes that follow it
    std::array<uint8_t, 8> long_len{0xDD, 7, 0x12, 0x34, 0x56, 0x01, 0xCA, 0xFE};
    r = set(long_len);
    TEST_ASSERT_FALSE(r.has_value());
    TEST_ASSERT_TRUE(r.error() == idfxx::errc::invalid_arg);

    // Length byte smaller than the bytes that follow it
    std::array<uint8_t, 8> short_len{0xDD, 5, 0x12, 0x34, 0x56, 0x01, 0xCA, 0xFE};
    r = set(short_len);
    TEST_ASSERT_FALSE(r.has_value());
    TEST_ASSERT_TRUE(r.error() == idfxx::errc::invalid_arg);
}

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 1, 0)
TEST_CASE("wifi init_config privacy fields default to unset", "[idfxx][wifi]") {
    init_config cfg;
    TEST_ASSERT_FALSE(cfg.privacy_enhancements.has_value());
    TEST_ASSERT_FALSE(cfg.rmac_auto_reset_interval.has_value());
}

TEST_CASE("wifi try_init rejects out-of-range rmac interval", "[idfxx][wifi]") {
    init_config cfg;
    cfg.rmac_auto_reset_interval = std::chrono::hours{0};
    auto r = try_init(cfg);
    TEST_ASSERT_FALSE(r.has_value());
    TEST_ASSERT_TRUE(r.error() == idfxx::errc::invalid_arg);

    cfg.rmac_auto_reset_interval = std::chrono::hours{25};
    r = try_init(cfg);
    TEST_ASSERT_FALSE(r.has_value());
    TEST_ASSERT_TRUE(r.error() == idfxx::errc::invalid_arg);
}
#endif

TEST_CASE("wifi get and set country", "[idfxx][wifi][hw]") {
    ensure_nvs_init();
    auto init_result = try_init();
    TEST_ASSERT_TRUE(init_result.has_value());

    auto set_result =
        try_set_country({.cc = {'U', 'S', '\0'}, .start_channel = 1, .num_channels = 11, .max_tx_power = quanta::electro::dbm{20}});
    TEST_ASSERT_TRUE(set_result.has_value());

    auto get_result = try_get_country();
    TEST_ASSERT_TRUE(get_result.has_value());
    TEST_ASSERT_EQUAL('U', get_result->cc[0]);
    TEST_ASSERT_EQUAL('S', get_result->cc[1]);

    auto deinit_result = try_deinit();
    TEST_ASSERT_TRUE(deinit_result.has_value());
}

TEST_CASE("wifi get and set max tx power", "[idfxx][wifi][hw]") {
    ensure_nvs_init();
    auto init_result = try_init();
    TEST_ASSERT_TRUE(init_result.has_value());

    auto mode_result = try_set_roles(role::sta);
    TEST_ASSERT_TRUE(mode_result.has_value());

    auto start_result = try_start();
    TEST_ASSERT_TRUE(start_result.has_value());

    auto set_result = try_set_max_tx_power(quanta::electro::dbm{15});
    TEST_ASSERT_TRUE(set_result.has_value());

    auto get_result = try_get_max_tx_power();
    TEST_ASSERT_TRUE(get_result.has_value());
    // TX power may be clamped by hardware, just check it's non-zero
    TEST_ASSERT_TRUE(*get_result > quanta::electro::dbm{0});

    auto stop_result = try_stop();
    TEST_ASSERT_TRUE(stop_result.has_value());

    auto deinit_result = try_deinit();
    TEST_ASSERT_TRUE(deinit_result.has_value());
}

TEST_CASE("wifi set and get sta config", "[idfxx][wifi][hw]") {
    ensure_nvs_init();
    auto init_result = try_init();
    TEST_ASSERT_TRUE(init_result.has_value());

    auto mode_result = try_set_roles(role::sta);
    TEST_ASSERT_TRUE(mode_result.has_value());

    sta_config cfg{.ssid = "roundtrip_test", .password = "test_pass_123", .channel = 6};
    auto set_result = try_set_sta_config(cfg);
    TEST_ASSERT_TRUE(set_result.has_value());

    auto get_result = try_get_sta_config();
    TEST_ASSERT_TRUE(get_result.has_value());
    TEST_ASSERT_EQUAL_STRING("roundtrip_test", get_result->ssid.c_str());
    TEST_ASSERT_EQUAL(6, get_result->channel);

    auto deinit_result = try_deinit();
    TEST_ASSERT_TRUE(deinit_result.has_value());
}

TEST_CASE("wifi sta config round-trips a 32-byte SSID and a 64-digit PSK", "[idfxx][wifi][hw]") {
    ensure_nvs_init();
    auto init_result = try_init();
    TEST_ASSERT_TRUE(init_result.has_value());

    auto mode_result = try_set_roles(role::sta);
    TEST_ASSERT_TRUE(mode_result.has_value());

    // Both fill their driver fields completely, leaving no NUL terminator
    const std::string ssid = "abcdefghijklmnopqrstuvwxyz012345";
    const std::string psk(64, 'f');
    TEST_ASSERT_EQUAL(32, ssid.size());
    auto set_result = try_set_sta_config({.ssid = ssid, .password = psk});
    TEST_ASSERT_TRUE(set_result.has_value());

    auto get_result = try_get_sta_config();
    TEST_ASSERT_TRUE(get_result.has_value());
    TEST_ASSERT_TRUE(get_result->ssid == ssid);
    TEST_ASSERT_TRUE(get_result->password == psk);

    auto deinit_result = try_deinit();
    TEST_ASSERT_TRUE(deinit_result.has_value());
}

TEST_CASE("wifi ap config round-trips a 32-byte SSID", "[idfxx][wifi][hw]") {
    ensure_nvs_init();
    auto init_result = try_init();
    TEST_ASSERT_TRUE(init_result.has_value());

    auto mode_result = try_set_roles(role::ap);
    TEST_ASSERT_TRUE(mode_result.has_value());

    const std::string ssid = "abcdefghijklmnopqrstuvwxyz012345";
    const std::string password(63, 'p');
    auto set_result = try_set_ap_config(
        {.ssid = ssid, .password = password, .authmode = auth_mode::wpa2_psk}
    );
    TEST_ASSERT_TRUE(set_result.has_value());

    auto get_result = try_get_ap_config();
    TEST_ASSERT_TRUE(get_result.has_value());
    TEST_ASSERT_TRUE(get_result->ssid == ssid);
    TEST_ASSERT_TRUE(get_result->password == password);

    auto deinit_result = try_deinit();
    TEST_ASSERT_TRUE(deinit_result.has_value());
}
