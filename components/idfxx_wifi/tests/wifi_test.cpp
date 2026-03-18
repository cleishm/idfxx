// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#include <idfxx/wifi>

#include <type_traits>
#include <unity.h>
#include <utility>

using namespace idfxx::wifi;

// =============================================================================
// Type properties
// =============================================================================

// config structs are aggregates
static_assert(std::is_aggregate_v<init_config>);
static_assert(std::is_aggregate_v<sta_config>);
static_assert(std::is_aggregate_v<scan_config>);
static_assert(std::is_aggregate_v<ap_record>);
static_assert(std::is_aggregate_v<connected_info>);
static_assert(std::is_aggregate_v<disconnected_info>);
static_assert(std::is_aggregate_v<got_ip_info>);
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
static_assert(std::is_aggregate_v<scan_done_info>);
static_assert(std::is_aggregate_v<authmode_change_info>);
static_assert(std::is_aggregate_v<ap_sta_connected_info>);
static_assert(std::is_aggregate_v<ap_sta_disconnected_info>);
static_assert(std::is_aggregate_v<ap_probe_req_info>);
static_assert(std::is_aggregate_v<bss_rssi_low_info>);
static_assert(std::is_aggregate_v<home_channel_change_info>);
static_assert(std::is_aggregate_v<ftm_report_info>);

// flags operator checks
static_assert(idfxx::enable_flags_operators<protocol>);
static_assert(idfxx::enable_flags_operators<promiscuous_filter>);
static_assert(idfxx::enable_flags_operators<promiscuous_ctrl_filter>);
static_assert(idfxx::enable_flags_operators<channel_5g>);
static_assert(idfxx::enable_flags_operators<event_mask>);

// =============================================================================
// Runtime tests
// =============================================================================

TEST_CASE("wifi init and deinit", "[idfxx][wifi][hw]") {
    auto result = try_init();
    TEST_ASSERT_TRUE(result.has_value());

    auto deinit_result = try_deinit();
    TEST_ASSERT_TRUE(deinit_result.has_value());
}

TEST_CASE("wifi init, set mode, set sta config, start, stop, deinit", "[idfxx][wifi][hw]") {
    auto init_result = try_init();
    TEST_ASSERT_TRUE(init_result.has_value());

    auto mode_result = try_set_mode(mode::sta);
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

TEST_CASE("wifi get mode after set", "[idfxx][wifi][hw]") {
    auto init_result = try_init();
    TEST_ASSERT_TRUE(init_result.has_value());

    auto set_result = try_set_mode(mode::sta);
    TEST_ASSERT_TRUE(set_result.has_value());

    auto get_result = try_get_mode();
    TEST_ASSERT_TRUE(get_result.has_value());
    TEST_ASSERT_EQUAL(std::to_underlying(mode::sta), std::to_underlying(*get_result));

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

TEST_CASE("wifi AP mode lifecycle", "[idfxx][wifi][hw]") {
    auto init_result = try_init();
    TEST_ASSERT_TRUE(init_result.has_value());

    auto mode_result = try_set_mode(mode::ap);
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

TEST_CASE("wifi get and set country", "[idfxx][wifi][hw]") {
    auto init_result = try_init();
    TEST_ASSERT_TRUE(init_result.has_value());

    auto set_result =
        try_set_country({.cc = {'U', 'S', '\0'}, .start_channel = 1, .num_channels = 11, .max_tx_power = 20});
    TEST_ASSERT_TRUE(set_result.has_value());

    auto get_result = try_get_country();
    TEST_ASSERT_TRUE(get_result.has_value());
    TEST_ASSERT_EQUAL('U', get_result->cc[0]);
    TEST_ASSERT_EQUAL('S', get_result->cc[1]);

    auto deinit_result = try_deinit();
    TEST_ASSERT_TRUE(deinit_result.has_value());
}

TEST_CASE("wifi get and set max tx power", "[idfxx][wifi][hw]") {
    auto init_result = try_init();
    TEST_ASSERT_TRUE(init_result.has_value());

    auto mode_result = try_set_mode(mode::sta);
    TEST_ASSERT_TRUE(mode_result.has_value());

    auto start_result = try_start();
    TEST_ASSERT_TRUE(start_result.has_value());

    auto set_result = try_set_max_tx_power(60);
    TEST_ASSERT_TRUE(set_result.has_value());

    auto get_result = try_get_max_tx_power();
    TEST_ASSERT_TRUE(get_result.has_value());
    // TX power may be clamped by hardware, just check it's non-zero
    TEST_ASSERT_TRUE(*get_result > 0);

    auto stop_result = try_stop();
    TEST_ASSERT_TRUE(stop_result.has_value());

    auto deinit_result = try_deinit();
    TEST_ASSERT_TRUE(deinit_result.has_value());
}

TEST_CASE("wifi set and get sta config", "[idfxx][wifi][hw]") {
    auto init_result = try_init();
    TEST_ASSERT_TRUE(init_result.has_value());

    auto mode_result = try_set_mode(mode::sta);
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
