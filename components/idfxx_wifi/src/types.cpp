// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#include <idfxx/wifi>

#include <cstring>
#include <esp_event.h>
#include <esp_idf_version.h>
#include <esp_netif.h>
#include <esp_wifi.h>
#include <esp_wifi_types.h>
#include <utility>

// =============================================================================
// Verify enum values match ESP-IDF constants
// =============================================================================

// role
static_assert(std::to_underlying(idfxx::wifi::role::sta) == WIFI_MODE_STA);
static_assert(std::to_underlying(idfxx::wifi::role::ap) == WIFI_MODE_AP);
static_assert(
    (std::to_underlying(idfxx::wifi::role::sta) | std::to_underlying(idfxx::wifi::role::ap)) == WIFI_MODE_APSTA
);

// auth_mode
static_assert(std::to_underlying(idfxx::wifi::auth_mode::open) == WIFI_AUTH_OPEN);
static_assert(std::to_underlying(idfxx::wifi::auth_mode::wep) == WIFI_AUTH_WEP);
static_assert(std::to_underlying(idfxx::wifi::auth_mode::wpa_psk) == WIFI_AUTH_WPA_PSK);
static_assert(std::to_underlying(idfxx::wifi::auth_mode::wpa2_psk) == WIFI_AUTH_WPA2_PSK);
static_assert(std::to_underlying(idfxx::wifi::auth_mode::wpa_wpa2_psk) == WIFI_AUTH_WPA_WPA2_PSK);
static_assert(std::to_underlying(idfxx::wifi::auth_mode::wpa2_enterprise) == WIFI_AUTH_WPA2_ENTERPRISE);
static_assert(std::to_underlying(idfxx::wifi::auth_mode::wpa3_psk) == WIFI_AUTH_WPA3_PSK);
static_assert(std::to_underlying(idfxx::wifi::auth_mode::wpa2_wpa3_psk) == WIFI_AUTH_WPA2_WPA3_PSK);
static_assert(std::to_underlying(idfxx::wifi::auth_mode::wapi_psk) == WIFI_AUTH_WAPI_PSK);
static_assert(std::to_underlying(idfxx::wifi::auth_mode::owe) == WIFI_AUTH_OWE);
static_assert(std::to_underlying(idfxx::wifi::auth_mode::wpa3_ent_192) == WIFI_AUTH_WPA3_ENT_192);
static_assert(std::to_underlying(idfxx::wifi::auth_mode::dpp) == WIFI_AUTH_DPP);
static_assert(std::to_underlying(idfxx::wifi::auth_mode::wpa3_enterprise) == WIFI_AUTH_WPA3_ENTERPRISE);
static_assert(std::to_underlying(idfxx::wifi::auth_mode::wpa2_wpa3_enterprise) == WIFI_AUTH_WPA2_WPA3_ENTERPRISE);
static_assert(std::to_underlying(idfxx::wifi::auth_mode::wpa_enterprise) == WIFI_AUTH_WPA_ENTERPRISE);

// power_save
static_assert(std::to_underlying(idfxx::wifi::power_save::none) == WIFI_PS_NONE);
static_assert(std::to_underlying(idfxx::wifi::power_save::min_modem) == WIFI_PS_MIN_MODEM);
static_assert(std::to_underlying(idfxx::wifi::power_save::max_modem) == WIFI_PS_MAX_MODEM);

// bandwidth
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
static_assert(std::to_underlying(idfxx::wifi::bandwidth::ht20) == WIFI_BW20);
static_assert(std::to_underlying(idfxx::wifi::bandwidth::ht40) == WIFI_BW40);
#else
static_assert(std::to_underlying(idfxx::wifi::bandwidth::ht20) == WIFI_BW_HT20);
static_assert(std::to_underlying(idfxx::wifi::bandwidth::ht40) == WIFI_BW_HT40);
#endif
static_assert(std::to_underlying(idfxx::wifi::bandwidth::bw80) == WIFI_BW80);
static_assert(std::to_underlying(idfxx::wifi::bandwidth::bw160) == WIFI_BW160);
static_assert(std::to_underlying(idfxx::wifi::bandwidth::bw80_80) == WIFI_BW80_BW80);

// scan_type
static_assert(std::to_underlying(idfxx::wifi::scan_type::active) == WIFI_SCAN_TYPE_ACTIVE);
static_assert(std::to_underlying(idfxx::wifi::scan_type::passive) == WIFI_SCAN_TYPE_PASSIVE);

// second_channel
static_assert(std::to_underlying(idfxx::wifi::second_channel::above) == WIFI_SECOND_CHAN_ABOVE);
static_assert(std::to_underlying(idfxx::wifi::second_channel::below) == WIFI_SECOND_CHAN_BELOW);

// sort_method
static_assert(std::to_underlying(idfxx::wifi::sort_method::by_rssi) == WIFI_CONNECT_AP_BY_SIGNAL);
static_assert(std::to_underlying(idfxx::wifi::sort_method::by_security) == WIFI_CONNECT_AP_BY_SECURITY);

// disconnect_reason (IEEE 802.11)
static_assert(std::to_underlying(idfxx::wifi::disconnect_reason::unspecified) == WIFI_REASON_UNSPECIFIED);
static_assert(std::to_underlying(idfxx::wifi::disconnect_reason::auth_expire) == WIFI_REASON_AUTH_EXPIRE);
static_assert(std::to_underlying(idfxx::wifi::disconnect_reason::auth_leave) == WIFI_REASON_AUTH_LEAVE);
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
static_assert(
    std::to_underlying(idfxx::wifi::disconnect_reason::assoc_expire) == WIFI_REASON_DISASSOC_DUE_TO_INACTIVITY
);
static_assert(std::to_underlying(idfxx::wifi::disconnect_reason::assoc_toomany) == WIFI_REASON_ASSOC_TOOMANY);
static_assert(
    std::to_underlying(idfxx::wifi::disconnect_reason::not_authed) == WIFI_REASON_CLASS2_FRAME_FROM_NONAUTH_STA
);
static_assert(
    std::to_underlying(idfxx::wifi::disconnect_reason::not_assoced) == WIFI_REASON_CLASS3_FRAME_FROM_NONASSOC_STA
);
#else
static_assert(std::to_underlying(idfxx::wifi::disconnect_reason::assoc_expire) == WIFI_REASON_ASSOC_EXPIRE);
static_assert(std::to_underlying(idfxx::wifi::disconnect_reason::assoc_toomany) == WIFI_REASON_ASSOC_TOOMANY);
static_assert(std::to_underlying(idfxx::wifi::disconnect_reason::not_authed) == WIFI_REASON_NOT_AUTHED);
static_assert(std::to_underlying(idfxx::wifi::disconnect_reason::not_assoced) == WIFI_REASON_NOT_ASSOCED);
#endif
static_assert(std::to_underlying(idfxx::wifi::disconnect_reason::assoc_leave) == WIFI_REASON_ASSOC_LEAVE);
static_assert(std::to_underlying(idfxx::wifi::disconnect_reason::assoc_not_authed) == WIFI_REASON_ASSOC_NOT_AUTHED);
static_assert(
    std::to_underlying(idfxx::wifi::disconnect_reason::disassoc_pwrcap_bad) == WIFI_REASON_DISASSOC_PWRCAP_BAD
);
static_assert(
    std::to_underlying(idfxx::wifi::disconnect_reason::disassoc_supchan_bad) == WIFI_REASON_DISASSOC_SUPCHAN_BAD
);
static_assert(
    std::to_underlying(idfxx::wifi::disconnect_reason::bss_transition_disassoc) == WIFI_REASON_BSS_TRANSITION_DISASSOC
);
static_assert(std::to_underlying(idfxx::wifi::disconnect_reason::ie_invalid) == WIFI_REASON_IE_INVALID);
static_assert(std::to_underlying(idfxx::wifi::disconnect_reason::mic_failure) == WIFI_REASON_MIC_FAILURE);
static_assert(
    std::to_underlying(idfxx::wifi::disconnect_reason::fourway_handshake_timeout) == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT
);
static_assert(
    std::to_underlying(idfxx::wifi::disconnect_reason::group_key_update_timeout) == WIFI_REASON_GROUP_KEY_UPDATE_TIMEOUT
);
static_assert(std::to_underlying(idfxx::wifi::disconnect_reason::ie_in_4way_differs) == WIFI_REASON_IE_IN_4WAY_DIFFERS);
static_assert(
    std::to_underlying(idfxx::wifi::disconnect_reason::group_cipher_invalid) == WIFI_REASON_GROUP_CIPHER_INVALID
);
static_assert(
    std::to_underlying(idfxx::wifi::disconnect_reason::pairwise_cipher_invalid) == WIFI_REASON_PAIRWISE_CIPHER_INVALID
);
static_assert(std::to_underlying(idfxx::wifi::disconnect_reason::akmp_invalid) == WIFI_REASON_AKMP_INVALID);
static_assert(
    std::to_underlying(idfxx::wifi::disconnect_reason::unsupp_rsn_ie_version) == WIFI_REASON_UNSUPP_RSN_IE_VERSION
);
static_assert(std::to_underlying(idfxx::wifi::disconnect_reason::invalid_rsn_ie_cap) == WIFI_REASON_INVALID_RSN_IE_CAP);
static_assert(
    std::to_underlying(idfxx::wifi::disconnect_reason::ieee_802_1x_auth_failed) == WIFI_REASON_802_1X_AUTH_FAILED
);
static_assert(
    std::to_underlying(idfxx::wifi::disconnect_reason::cipher_suite_rejected) == WIFI_REASON_CIPHER_SUITE_REJECTED
);
static_assert(
    std::to_underlying(idfxx::wifi::disconnect_reason::tdls_peer_unreachable) == WIFI_REASON_TDLS_PEER_UNREACHABLE
);
static_assert(std::to_underlying(idfxx::wifi::disconnect_reason::tdls_unspecified) == WIFI_REASON_TDLS_UNSPECIFIED);
static_assert(
    std::to_underlying(idfxx::wifi::disconnect_reason::ssp_requested_disassoc) == WIFI_REASON_SSP_REQUESTED_DISASSOC
);
static_assert(
    std::to_underlying(idfxx::wifi::disconnect_reason::no_ssp_roaming_agreement) == WIFI_REASON_NO_SSP_ROAMING_AGREEMENT
);
static_assert(std::to_underlying(idfxx::wifi::disconnect_reason::bad_cipher_or_akm) == WIFI_REASON_BAD_CIPHER_OR_AKM);
static_assert(
    std::to_underlying(idfxx::wifi::disconnect_reason::not_authorized_this_location) ==
    WIFI_REASON_NOT_AUTHORIZED_THIS_LOCATION
);
static_assert(
    std::to_underlying(idfxx::wifi::disconnect_reason::service_change_precludes_ts) ==
    WIFI_REASON_SERVICE_CHANGE_PERCLUDES_TS
);
static_assert(std::to_underlying(idfxx::wifi::disconnect_reason::unspecified_qos) == WIFI_REASON_UNSPECIFIED_QOS);
static_assert(
    std::to_underlying(idfxx::wifi::disconnect_reason::not_enough_bandwidth) == WIFI_REASON_NOT_ENOUGH_BANDWIDTH
);
static_assert(std::to_underlying(idfxx::wifi::disconnect_reason::missing_acks) == WIFI_REASON_MISSING_ACKS);
static_assert(std::to_underlying(idfxx::wifi::disconnect_reason::exceeded_txop) == WIFI_REASON_EXCEEDED_TXOP);
static_assert(std::to_underlying(idfxx::wifi::disconnect_reason::sta_leaving) == WIFI_REASON_STA_LEAVING);
static_assert(std::to_underlying(idfxx::wifi::disconnect_reason::end_ba) == WIFI_REASON_END_BA);
static_assert(std::to_underlying(idfxx::wifi::disconnect_reason::unknown_ba) == WIFI_REASON_UNKNOWN_BA);
static_assert(std::to_underlying(idfxx::wifi::disconnect_reason::timeout) == WIFI_REASON_TIMEOUT);
static_assert(std::to_underlying(idfxx::wifi::disconnect_reason::peer_initiated) == WIFI_REASON_PEER_INITIATED);
static_assert(std::to_underlying(idfxx::wifi::disconnect_reason::ap_initiated) == WIFI_REASON_AP_INITIATED);
static_assert(
    std::to_underlying(idfxx::wifi::disconnect_reason::invalid_ft_action_frame_count) ==
    WIFI_REASON_INVALID_FT_ACTION_FRAME_COUNT
);
static_assert(std::to_underlying(idfxx::wifi::disconnect_reason::invalid_pmkid) == WIFI_REASON_INVALID_PMKID);
static_assert(std::to_underlying(idfxx::wifi::disconnect_reason::invalid_mde) == WIFI_REASON_INVALID_MDE);
static_assert(std::to_underlying(idfxx::wifi::disconnect_reason::invalid_fte) == WIFI_REASON_INVALID_FTE);
static_assert(
    std::to_underlying(idfxx::wifi::disconnect_reason::transmission_link_establish_failed) ==
    WIFI_REASON_TRANSMISSION_LINK_ESTABLISH_FAILED
);
static_assert(
    std::to_underlying(idfxx::wifi::disconnect_reason::alterative_channel_occupied) ==
    WIFI_REASON_ALTERATIVE_CHANNEL_OCCUPIED
);

// disconnect_reason (ESP-IDF specific)
static_assert(std::to_underlying(idfxx::wifi::disconnect_reason::beacon_timeout) == WIFI_REASON_BEACON_TIMEOUT);
static_assert(std::to_underlying(idfxx::wifi::disconnect_reason::no_ap_found) == WIFI_REASON_NO_AP_FOUND);
static_assert(std::to_underlying(idfxx::wifi::disconnect_reason::auth_fail) == WIFI_REASON_AUTH_FAIL);
static_assert(std::to_underlying(idfxx::wifi::disconnect_reason::assoc_fail) == WIFI_REASON_ASSOC_FAIL);
static_assert(std::to_underlying(idfxx::wifi::disconnect_reason::handshake_timeout) == WIFI_REASON_HANDSHAKE_TIMEOUT);
static_assert(std::to_underlying(idfxx::wifi::disconnect_reason::connection_fail) == WIFI_REASON_CONNECTION_FAIL);
static_assert(std::to_underlying(idfxx::wifi::disconnect_reason::ap_tsf_reset) == WIFI_REASON_AP_TSF_RESET);
static_assert(std::to_underlying(idfxx::wifi::disconnect_reason::roaming) == WIFI_REASON_ROAMING);
static_assert(
    std::to_underlying(idfxx::wifi::disconnect_reason::assoc_comeback_time_too_long) ==
    WIFI_REASON_ASSOC_COMEBACK_TIME_TOO_LONG
);
static_assert(std::to_underlying(idfxx::wifi::disconnect_reason::sa_query_timeout) == WIFI_REASON_SA_QUERY_TIMEOUT);
static_assert(
    std::to_underlying(idfxx::wifi::disconnect_reason::no_ap_found_w_compatible_security) ==
    WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY
);
static_assert(
    std::to_underlying(idfxx::wifi::disconnect_reason::no_ap_found_in_authmode_threshold) ==
    WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD
);
static_assert(
    std::to_underlying(idfxx::wifi::disconnect_reason::no_ap_found_in_rssi_threshold) ==
    WIFI_REASON_NO_AP_FOUND_IN_RSSI_THRESHOLD
);

// cipher_type
static_assert(std::to_underlying(idfxx::wifi::cipher_type::none) == WIFI_CIPHER_TYPE_NONE);
static_assert(std::to_underlying(idfxx::wifi::cipher_type::wep40) == WIFI_CIPHER_TYPE_WEP40);
static_assert(std::to_underlying(idfxx::wifi::cipher_type::wep104) == WIFI_CIPHER_TYPE_WEP104);
static_assert(std::to_underlying(idfxx::wifi::cipher_type::tkip) == WIFI_CIPHER_TYPE_TKIP);
static_assert(std::to_underlying(idfxx::wifi::cipher_type::ccmp) == WIFI_CIPHER_TYPE_CCMP);
static_assert(std::to_underlying(idfxx::wifi::cipher_type::tkip_ccmp) == WIFI_CIPHER_TYPE_TKIP_CCMP);
static_assert(std::to_underlying(idfxx::wifi::cipher_type::aes_cmac128) == WIFI_CIPHER_TYPE_AES_CMAC128);
static_assert(std::to_underlying(idfxx::wifi::cipher_type::sms4) == WIFI_CIPHER_TYPE_SMS4);
static_assert(std::to_underlying(idfxx::wifi::cipher_type::gcmp) == WIFI_CIPHER_TYPE_GCMP);
static_assert(std::to_underlying(idfxx::wifi::cipher_type::gcmp256) == WIFI_CIPHER_TYPE_GCMP256);
static_assert(std::to_underlying(idfxx::wifi::cipher_type::aes_gmac128) == WIFI_CIPHER_TYPE_AES_GMAC128);
static_assert(std::to_underlying(idfxx::wifi::cipher_type::aes_gmac256) == WIFI_CIPHER_TYPE_AES_GMAC256);
static_assert(std::to_underlying(idfxx::wifi::cipher_type::unknown) == WIFI_CIPHER_TYPE_UNKNOWN);

// phy_mode
static_assert(std::to_underlying(idfxx::wifi::phy_mode::lr) == WIFI_PHY_MODE_LR);
static_assert(std::to_underlying(idfxx::wifi::phy_mode::b11b) == WIFI_PHY_MODE_11B);
static_assert(std::to_underlying(idfxx::wifi::phy_mode::b11g) == WIFI_PHY_MODE_11G);
static_assert(std::to_underlying(idfxx::wifi::phy_mode::b11a) == WIFI_PHY_MODE_11A);
static_assert(std::to_underlying(idfxx::wifi::phy_mode::ht20) == WIFI_PHY_MODE_HT20);
static_assert(std::to_underlying(idfxx::wifi::phy_mode::ht40) == WIFI_PHY_MODE_HT40);
static_assert(std::to_underlying(idfxx::wifi::phy_mode::he20) == WIFI_PHY_MODE_HE20);
static_assert(std::to_underlying(idfxx::wifi::phy_mode::vht20) == WIFI_PHY_MODE_VHT20);

// band
static_assert(std::to_underlying(idfxx::wifi::band::ghz_2g) == WIFI_BAND_2G);
static_assert(std::to_underlying(idfxx::wifi::band::ghz_5g) == WIFI_BAND_5G);

// band_mode
static_assert(std::to_underlying(idfxx::wifi::band_mode::ghz_2g_only) == WIFI_BAND_MODE_2G_ONLY);
static_assert(std::to_underlying(idfxx::wifi::band_mode::ghz_5g_only) == WIFI_BAND_MODE_5G_ONLY);
static_assert(std::to_underlying(idfxx::wifi::band_mode::auto_mode) == WIFI_BAND_MODE_AUTO);

// storage
static_assert(std::to_underlying(idfxx::wifi::storage::flash) == WIFI_STORAGE_FLASH);
static_assert(std::to_underlying(idfxx::wifi::storage::ram) == WIFI_STORAGE_RAM);

// country_policy
static_assert(std::to_underlying(idfxx::wifi::country_policy::auto_policy) == WIFI_COUNTRY_POLICY_AUTO);
static_assert(std::to_underlying(idfxx::wifi::country_policy::manual) == WIFI_COUNTRY_POLICY_MANUAL);

// channel_5g
static_assert(std::to_underlying(idfxx::wifi::channel_5g::ch36) == WIFI_CHANNEL_36);
static_assert(std::to_underlying(idfxx::wifi::channel_5g::ch40) == WIFI_CHANNEL_40);
static_assert(std::to_underlying(idfxx::wifi::channel_5g::ch44) == WIFI_CHANNEL_44);
static_assert(std::to_underlying(idfxx::wifi::channel_5g::ch48) == WIFI_CHANNEL_48);
static_assert(std::to_underlying(idfxx::wifi::channel_5g::ch52) == WIFI_CHANNEL_52);
static_assert(std::to_underlying(idfxx::wifi::channel_5g::ch56) == WIFI_CHANNEL_56);
static_assert(std::to_underlying(idfxx::wifi::channel_5g::ch60) == WIFI_CHANNEL_60);
static_assert(std::to_underlying(idfxx::wifi::channel_5g::ch64) == WIFI_CHANNEL_64);
static_assert(std::to_underlying(idfxx::wifi::channel_5g::ch100) == WIFI_CHANNEL_100);
static_assert(std::to_underlying(idfxx::wifi::channel_5g::ch104) == WIFI_CHANNEL_104);
static_assert(std::to_underlying(idfxx::wifi::channel_5g::ch108) == WIFI_CHANNEL_108);
static_assert(std::to_underlying(idfxx::wifi::channel_5g::ch112) == WIFI_CHANNEL_112);
static_assert(std::to_underlying(idfxx::wifi::channel_5g::ch116) == WIFI_CHANNEL_116);
static_assert(std::to_underlying(idfxx::wifi::channel_5g::ch120) == WIFI_CHANNEL_120);
static_assert(std::to_underlying(idfxx::wifi::channel_5g::ch124) == WIFI_CHANNEL_124);
static_assert(std::to_underlying(idfxx::wifi::channel_5g::ch128) == WIFI_CHANNEL_128);
static_assert(std::to_underlying(idfxx::wifi::channel_5g::ch132) == WIFI_CHANNEL_132);
static_assert(std::to_underlying(idfxx::wifi::channel_5g::ch136) == WIFI_CHANNEL_136);
static_assert(std::to_underlying(idfxx::wifi::channel_5g::ch140) == WIFI_CHANNEL_140);
static_assert(std::to_underlying(idfxx::wifi::channel_5g::ch144) == WIFI_CHANNEL_144);
static_assert(std::to_underlying(idfxx::wifi::channel_5g::ch149) == WIFI_CHANNEL_149);
static_assert(std::to_underlying(idfxx::wifi::channel_5g::ch153) == WIFI_CHANNEL_153);
static_assert(std::to_underlying(idfxx::wifi::channel_5g::ch157) == WIFI_CHANNEL_157);
static_assert(std::to_underlying(idfxx::wifi::channel_5g::ch161) == WIFI_CHANNEL_161);
static_assert(std::to_underlying(idfxx::wifi::channel_5g::ch165) == WIFI_CHANNEL_165);
static_assert(std::to_underlying(idfxx::wifi::channel_5g::ch169) == WIFI_CHANNEL_169);
static_assert(std::to_underlying(idfxx::wifi::channel_5g::ch173) == WIFI_CHANNEL_173);
static_assert(std::to_underlying(idfxx::wifi::channel_5g::ch177) == WIFI_CHANNEL_177);

// scan_method
static_assert(std::to_underlying(idfxx::wifi::scan_method::fast) == WIFI_FAST_SCAN);
static_assert(std::to_underlying(idfxx::wifi::scan_method::all_channel) == WIFI_ALL_CHANNEL_SCAN);

// sae_pwe_method
static_assert(std::to_underlying(idfxx::wifi::sae_pwe_method::hunt_and_peck) == WPA3_SAE_PWE_HUNT_AND_PECK);
static_assert(std::to_underlying(idfxx::wifi::sae_pwe_method::hash_to_element) == WPA3_SAE_PWE_HASH_TO_ELEMENT);
static_assert(
    (std::to_underlying(idfxx::wifi::sae_pwe_method::hunt_and_peck) |
     std::to_underlying(idfxx::wifi::sae_pwe_method::hash_to_element)) == WPA3_SAE_PWE_BOTH
);

// sae_pk_mode
static_assert(std::to_underlying(idfxx::wifi::sae_pk_mode::automatic) == WPA3_SAE_PK_MODE_AUTOMATIC);
static_assert(std::to_underlying(idfxx::wifi::sae_pk_mode::only) == WPA3_SAE_PK_MODE_ONLY);
static_assert(std::to_underlying(idfxx::wifi::sae_pk_mode::disabled) == WPA3_SAE_PK_MODE_DISABLED);

// protocol
static_assert(std::to_underlying(idfxx::wifi::protocol::b11b) == WIFI_PROTOCOL_11B);
static_assert(std::to_underlying(idfxx::wifi::protocol::b11g) == WIFI_PROTOCOL_11G);
static_assert(std::to_underlying(idfxx::wifi::protocol::b11n) == WIFI_PROTOCOL_11N);
static_assert(std::to_underlying(idfxx::wifi::protocol::lr) == WIFI_PROTOCOL_LR);
static_assert(std::to_underlying(idfxx::wifi::protocol::b11a) == WIFI_PROTOCOL_11A);
static_assert(std::to_underlying(idfxx::wifi::protocol::b11ac) == WIFI_PROTOCOL_11AC);
static_assert(std::to_underlying(idfxx::wifi::protocol::b11ax) == WIFI_PROTOCOL_11AX);

// event_mask
static_assert(std::to_underlying(idfxx::wifi::event_mask::all) == WIFI_EVENT_MASK_ALL);
static_assert(std::to_underlying(idfxx::wifi::event_mask::none) == WIFI_EVENT_MASK_NONE);
static_assert(std::to_underlying(idfxx::wifi::event_mask::ap_probe_req_rx) == WIFI_EVENT_MASK_AP_PROBEREQRECVED);

// promiscuous_pkt_type
static_assert(std::to_underlying(idfxx::wifi::promiscuous_pkt_type::mgmt) == WIFI_PKT_MGMT);
static_assert(std::to_underlying(idfxx::wifi::promiscuous_pkt_type::ctrl) == WIFI_PKT_CTRL);
static_assert(std::to_underlying(idfxx::wifi::promiscuous_pkt_type::data) == WIFI_PKT_DATA);
static_assert(std::to_underlying(idfxx::wifi::promiscuous_pkt_type::misc) == WIFI_PKT_MISC);

// promiscuous_filter
static_assert(std::to_underlying(idfxx::wifi::promiscuous_filter::all) == WIFI_PROMIS_FILTER_MASK_ALL);
static_assert(std::to_underlying(idfxx::wifi::promiscuous_filter::mgmt) == WIFI_PROMIS_FILTER_MASK_MGMT);
static_assert(std::to_underlying(idfxx::wifi::promiscuous_filter::ctrl) == WIFI_PROMIS_FILTER_MASK_CTRL);
static_assert(std::to_underlying(idfxx::wifi::promiscuous_filter::data) == WIFI_PROMIS_FILTER_MASK_DATA);
static_assert(std::to_underlying(idfxx::wifi::promiscuous_filter::misc) == WIFI_PROMIS_FILTER_MASK_MISC);
static_assert(std::to_underlying(idfxx::wifi::promiscuous_filter::data_mpdu) == WIFI_PROMIS_FILTER_MASK_DATA_MPDU);
static_assert(std::to_underlying(idfxx::wifi::promiscuous_filter::data_ampdu) == WIFI_PROMIS_FILTER_MASK_DATA_AMPDU);
static_assert(std::to_underlying(idfxx::wifi::promiscuous_filter::fcsfail) == WIFI_PROMIS_FILTER_MASK_FCSFAIL);

// promiscuous_ctrl_filter
static_assert(
    std::to_underlying(idfxx::wifi::promiscuous_ctrl_filter::all) == uint32_t(WIFI_PROMIS_CTRL_FILTER_MASK_ALL)
);
static_assert(
    std::to_underlying(idfxx::wifi::promiscuous_ctrl_filter::wrapper) == uint32_t(WIFI_PROMIS_CTRL_FILTER_MASK_WRAPPER)
);
static_assert(
    std::to_underlying(idfxx::wifi::promiscuous_ctrl_filter::bar) == uint32_t(WIFI_PROMIS_CTRL_FILTER_MASK_BAR)
);
static_assert(
    std::to_underlying(idfxx::wifi::promiscuous_ctrl_filter::ba) == uint32_t(WIFI_PROMIS_CTRL_FILTER_MASK_BA)
);
static_assert(
    std::to_underlying(idfxx::wifi::promiscuous_ctrl_filter::pspoll) == uint32_t(WIFI_PROMIS_CTRL_FILTER_MASK_PSPOLL)
);
static_assert(
    std::to_underlying(idfxx::wifi::promiscuous_ctrl_filter::rts) == uint32_t(WIFI_PROMIS_CTRL_FILTER_MASK_RTS)
);
static_assert(
    std::to_underlying(idfxx::wifi::promiscuous_ctrl_filter::cts) == uint32_t(WIFI_PROMIS_CTRL_FILTER_MASK_CTS)
);
static_assert(
    std::to_underlying(idfxx::wifi::promiscuous_ctrl_filter::ack) == uint32_t(WIFI_PROMIS_CTRL_FILTER_MASK_ACK)
);
static_assert(
    std::to_underlying(idfxx::wifi::promiscuous_ctrl_filter::cfend) == uint32_t(WIFI_PROMIS_CTRL_FILTER_MASK_CFEND)
);
static_assert(
    std::to_underlying(idfxx::wifi::promiscuous_ctrl_filter::cfendack) ==
    uint32_t(WIFI_PROMIS_CTRL_FILTER_MASK_CFENDACK)
);

// vendor_ie_type
static_assert(std::to_underlying(idfxx::wifi::vendor_ie_type::beacon) == WIFI_VND_IE_TYPE_BEACON);
static_assert(std::to_underlying(idfxx::wifi::vendor_ie_type::probe_req) == WIFI_VND_IE_TYPE_PROBE_REQ);
static_assert(std::to_underlying(idfxx::wifi::vendor_ie_type::probe_resp) == WIFI_VND_IE_TYPE_PROBE_RESP);
static_assert(std::to_underlying(idfxx::wifi::vendor_ie_type::assoc_req) == WIFI_VND_IE_TYPE_ASSOC_REQ);
static_assert(std::to_underlying(idfxx::wifi::vendor_ie_type::assoc_resp) == WIFI_VND_IE_TYPE_ASSOC_RESP);

// vendor_ie_id
static_assert(std::to_underlying(idfxx::wifi::vendor_ie_id::id_0) == WIFI_VND_IE_ID_0);
static_assert(std::to_underlying(idfxx::wifi::vendor_ie_id::id_1) == WIFI_VND_IE_ID_1);

// ftm_status
static_assert(std::to_underlying(idfxx::wifi::ftm_status::success) == FTM_STATUS_SUCCESS);
static_assert(std::to_underlying(idfxx::wifi::ftm_status::unsupported) == FTM_STATUS_UNSUPPORTED);
static_assert(std::to_underlying(idfxx::wifi::ftm_status::conf_rejected) == FTM_STATUS_CONF_REJECTED);
static_assert(std::to_underlying(idfxx::wifi::ftm_status::no_response) == FTM_STATUS_NO_RESPONSE);
static_assert(std::to_underlying(idfxx::wifi::ftm_status::fail) == FTM_STATUS_FAIL);
static_assert(std::to_underlying(idfxx::wifi::ftm_status::no_valid_msmt) == FTM_STATUS_NO_VALID_MSMT);
static_assert(std::to_underlying(idfxx::wifi::ftm_status::user_term) == FTM_STATUS_USER_TERM);

// phy_rate
static_assert(std::to_underlying(idfxx::wifi::phy_rate::rate_1m_l) == WIFI_PHY_RATE_1M_L);
static_assert(std::to_underlying(idfxx::wifi::phy_rate::rate_2m_l) == WIFI_PHY_RATE_2M_L);
static_assert(std::to_underlying(idfxx::wifi::phy_rate::rate_5m_l) == WIFI_PHY_RATE_5M_L);
static_assert(std::to_underlying(idfxx::wifi::phy_rate::rate_11m_l) == WIFI_PHY_RATE_11M_L);
static_assert(std::to_underlying(idfxx::wifi::phy_rate::rate_2m_s) == WIFI_PHY_RATE_2M_S);
static_assert(std::to_underlying(idfxx::wifi::phy_rate::rate_5m_s) == WIFI_PHY_RATE_5M_S);
static_assert(std::to_underlying(idfxx::wifi::phy_rate::rate_11m_s) == WIFI_PHY_RATE_11M_S);
static_assert(std::to_underlying(idfxx::wifi::phy_rate::rate_48m) == WIFI_PHY_RATE_48M);
static_assert(std::to_underlying(idfxx::wifi::phy_rate::rate_24m) == WIFI_PHY_RATE_24M);
static_assert(std::to_underlying(idfxx::wifi::phy_rate::rate_12m) == WIFI_PHY_RATE_12M);
static_assert(std::to_underlying(idfxx::wifi::phy_rate::rate_6m) == WIFI_PHY_RATE_6M);
static_assert(std::to_underlying(idfxx::wifi::phy_rate::rate_54m) == WIFI_PHY_RATE_54M);
static_assert(std::to_underlying(idfxx::wifi::phy_rate::rate_36m) == WIFI_PHY_RATE_36M);
static_assert(std::to_underlying(idfxx::wifi::phy_rate::rate_18m) == WIFI_PHY_RATE_18M);
static_assert(std::to_underlying(idfxx::wifi::phy_rate::rate_9m) == WIFI_PHY_RATE_9M);
static_assert(std::to_underlying(idfxx::wifi::phy_rate::mcs0_lgi) == WIFI_PHY_RATE_MCS0_LGI);
static_assert(std::to_underlying(idfxx::wifi::phy_rate::mcs1_lgi) == WIFI_PHY_RATE_MCS1_LGI);
static_assert(std::to_underlying(idfxx::wifi::phy_rate::mcs2_lgi) == WIFI_PHY_RATE_MCS2_LGI);
static_assert(std::to_underlying(idfxx::wifi::phy_rate::mcs3_lgi) == WIFI_PHY_RATE_MCS3_LGI);
static_assert(std::to_underlying(idfxx::wifi::phy_rate::mcs4_lgi) == WIFI_PHY_RATE_MCS4_LGI);
static_assert(std::to_underlying(idfxx::wifi::phy_rate::mcs5_lgi) == WIFI_PHY_RATE_MCS5_LGI);
static_assert(std::to_underlying(idfxx::wifi::phy_rate::mcs6_lgi) == WIFI_PHY_RATE_MCS6_LGI);
static_assert(std::to_underlying(idfxx::wifi::phy_rate::mcs7_lgi) == WIFI_PHY_RATE_MCS7_LGI);
#if SOC_WIFI_HE_SUPPORT
static_assert(std::to_underlying(idfxx::wifi::phy_rate::mcs8_lgi) == WIFI_PHY_RATE_MCS8_LGI);
static_assert(std::to_underlying(idfxx::wifi::phy_rate::mcs9_lgi) == WIFI_PHY_RATE_MCS9_LGI);
#endif
static_assert(std::to_underlying(idfxx::wifi::phy_rate::mcs0_sgi) == WIFI_PHY_RATE_MCS0_SGI);
static_assert(std::to_underlying(idfxx::wifi::phy_rate::mcs1_sgi) == WIFI_PHY_RATE_MCS1_SGI);
static_assert(std::to_underlying(idfxx::wifi::phy_rate::mcs2_sgi) == WIFI_PHY_RATE_MCS2_SGI);
static_assert(std::to_underlying(idfxx::wifi::phy_rate::mcs3_sgi) == WIFI_PHY_RATE_MCS3_SGI);
static_assert(std::to_underlying(idfxx::wifi::phy_rate::mcs4_sgi) == WIFI_PHY_RATE_MCS4_SGI);
static_assert(std::to_underlying(idfxx::wifi::phy_rate::mcs5_sgi) == WIFI_PHY_RATE_MCS5_SGI);
static_assert(std::to_underlying(idfxx::wifi::phy_rate::mcs6_sgi) == WIFI_PHY_RATE_MCS6_SGI);
static_assert(std::to_underlying(idfxx::wifi::phy_rate::mcs7_sgi) == WIFI_PHY_RATE_MCS7_SGI);
#if SOC_WIFI_HE_SUPPORT
static_assert(std::to_underlying(idfxx::wifi::phy_rate::mcs8_sgi) == WIFI_PHY_RATE_MCS8_SGI);
static_assert(std::to_underlying(idfxx::wifi::phy_rate::mcs9_sgi) == WIFI_PHY_RATE_MCS9_SGI);
#endif
static_assert(std::to_underlying(idfxx::wifi::phy_rate::lora_250k) == WIFI_PHY_RATE_LORA_250K);
static_assert(std::to_underlying(idfxx::wifi::phy_rate::lora_500k) == WIFI_PHY_RATE_LORA_500K);

// event IDs
static_assert(std::to_underlying(idfxx::wifi::event_id::ready) == WIFI_EVENT_WIFI_READY);
static_assert(std::to_underlying(idfxx::wifi::event_id::scan_done) == WIFI_EVENT_SCAN_DONE);
static_assert(std::to_underlying(idfxx::wifi::event_id::sta_start) == WIFI_EVENT_STA_START);
static_assert(std::to_underlying(idfxx::wifi::event_id::sta_stop) == WIFI_EVENT_STA_STOP);
static_assert(std::to_underlying(idfxx::wifi::event_id::sta_connected) == WIFI_EVENT_STA_CONNECTED);
static_assert(std::to_underlying(idfxx::wifi::event_id::sta_disconnected) == WIFI_EVENT_STA_DISCONNECTED);
static_assert(std::to_underlying(idfxx::wifi::event_id::sta_authmode_change) == WIFI_EVENT_STA_AUTHMODE_CHANGE);
static_assert(std::to_underlying(idfxx::wifi::event_id::sta_wps_er_success) == WIFI_EVENT_STA_WPS_ER_SUCCESS);
static_assert(std::to_underlying(idfxx::wifi::event_id::sta_wps_er_failed) == WIFI_EVENT_STA_WPS_ER_FAILED);
static_assert(std::to_underlying(idfxx::wifi::event_id::sta_wps_er_timeout) == WIFI_EVENT_STA_WPS_ER_TIMEOUT);
static_assert(std::to_underlying(idfxx::wifi::event_id::sta_wps_er_pin) == WIFI_EVENT_STA_WPS_ER_PIN);
static_assert(std::to_underlying(idfxx::wifi::event_id::sta_wps_er_pbc_overlap) == WIFI_EVENT_STA_WPS_ER_PBC_OVERLAP);
static_assert(std::to_underlying(idfxx::wifi::event_id::ap_start) == WIFI_EVENT_AP_START);
static_assert(std::to_underlying(idfxx::wifi::event_id::ap_stop) == WIFI_EVENT_AP_STOP);
static_assert(std::to_underlying(idfxx::wifi::event_id::ap_sta_connected) == WIFI_EVENT_AP_STACONNECTED);
static_assert(std::to_underlying(idfxx::wifi::event_id::ap_sta_disconnected) == WIFI_EVENT_AP_STADISCONNECTED);
static_assert(std::to_underlying(idfxx::wifi::event_id::ap_probe_req_received) == WIFI_EVENT_AP_PROBEREQRECVED);
static_assert(std::to_underlying(idfxx::wifi::event_id::ftm_report) == WIFI_EVENT_FTM_REPORT);
static_assert(std::to_underlying(idfxx::wifi::event_id::sta_bss_rssi_low) == WIFI_EVENT_STA_BSS_RSSI_LOW);
static_assert(std::to_underlying(idfxx::wifi::event_id::action_tx_status) == WIFI_EVENT_ACTION_TX_STATUS);
static_assert(std::to_underlying(idfxx::wifi::event_id::roc_done) == WIFI_EVENT_ROC_DONE);
static_assert(std::to_underlying(idfxx::wifi::event_id::sta_beacon_timeout) == WIFI_EVENT_STA_BEACON_TIMEOUT);
static_assert(
    std::to_underlying(idfxx::wifi::event_id::connectionless_module_wake_interval_start) ==
    WIFI_EVENT_CONNECTIONLESS_MODULE_WAKE_INTERVAL_START
);
static_assert(std::to_underlying(idfxx::wifi::event_id::ap_wps_rg_success) == WIFI_EVENT_AP_WPS_RG_SUCCESS);
static_assert(std::to_underlying(idfxx::wifi::event_id::ap_wps_rg_failed) == WIFI_EVENT_AP_WPS_RG_FAILED);
static_assert(std::to_underlying(idfxx::wifi::event_id::ap_wps_rg_timeout) == WIFI_EVENT_AP_WPS_RG_TIMEOUT);
static_assert(std::to_underlying(idfxx::wifi::event_id::ap_wps_rg_pin) == WIFI_EVENT_AP_WPS_RG_PIN);
static_assert(std::to_underlying(idfxx::wifi::event_id::ap_wps_rg_pbc_overlap) == WIFI_EVENT_AP_WPS_RG_PBC_OVERLAP);
static_assert(std::to_underlying(idfxx::wifi::event_id::itwt_setup) == WIFI_EVENT_ITWT_SETUP);
static_assert(std::to_underlying(idfxx::wifi::event_id::itwt_teardown) == WIFI_EVENT_ITWT_TEARDOWN);
static_assert(std::to_underlying(idfxx::wifi::event_id::itwt_probe) == WIFI_EVENT_ITWT_PROBE);
static_assert(std::to_underlying(idfxx::wifi::event_id::itwt_suspend) == WIFI_EVENT_ITWT_SUSPEND);
static_assert(std::to_underlying(idfxx::wifi::event_id::twt_wakeup) == WIFI_EVENT_TWT_WAKEUP);
static_assert(std::to_underlying(idfxx::wifi::event_id::btwt_setup) == WIFI_EVENT_BTWT_SETUP);
static_assert(std::to_underlying(idfxx::wifi::event_id::btwt_teardown) == WIFI_EVENT_BTWT_TEARDOWN);
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
static_assert(std::to_underlying(idfxx::wifi::event_id::nan_started) == WIFI_EVENT_NAN_SYNC_STARTED);
static_assert(std::to_underlying(idfxx::wifi::event_id::nan_stopped) == WIFI_EVENT_NAN_SYNC_STOPPED);
#else
static_assert(std::to_underlying(idfxx::wifi::event_id::nan_started) == WIFI_EVENT_NAN_STARTED);
static_assert(std::to_underlying(idfxx::wifi::event_id::nan_stopped) == WIFI_EVENT_NAN_STOPPED);
#endif
static_assert(std::to_underlying(idfxx::wifi::event_id::nan_svc_match) == WIFI_EVENT_NAN_SVC_MATCH);
static_assert(std::to_underlying(idfxx::wifi::event_id::nan_replied) == WIFI_EVENT_NAN_REPLIED);
static_assert(std::to_underlying(idfxx::wifi::event_id::nan_receive) == WIFI_EVENT_NAN_RECEIVE);
static_assert(std::to_underlying(idfxx::wifi::event_id::ndp_indication) == WIFI_EVENT_NDP_INDICATION);
static_assert(std::to_underlying(idfxx::wifi::event_id::ndp_confirm) == WIFI_EVENT_NDP_CONFIRM);
static_assert(std::to_underlying(idfxx::wifi::event_id::ndp_terminated) == WIFI_EVENT_NDP_TERMINATED);
static_assert(std::to_underlying(idfxx::wifi::event_id::home_channel_change) == WIFI_EVENT_HOME_CHANNEL_CHANGE);
static_assert(std::to_underlying(idfxx::wifi::event_id::sta_neighbor_rep) == WIFI_EVENT_STA_NEIGHBOR_REP);
static_assert(std::to_underlying(idfxx::wifi::event_id::ap_wrong_password) == WIFI_EVENT_AP_WRONG_PASSWORD);
static_assert(
    std::to_underlying(idfxx::wifi::event_id::sta_beacon_offset_unstable) == WIFI_EVENT_STA_BEACON_OFFSET_UNSTABLE
);
static_assert(std::to_underlying(idfxx::wifi::event_id::dpp_uri_ready) == WIFI_EVENT_DPP_URI_READY);
static_assert(std::to_underlying(idfxx::wifi::event_id::dpp_cfg_recvd) == WIFI_EVENT_DPP_CFG_RECVD);
static_assert(std::to_underlying(idfxx::wifi::event_id::dpp_failed) == WIFI_EVENT_DPP_FAILED);

// errc
static_assert(std::to_underlying(idfxx::wifi::errc::not_init) == ESP_ERR_WIFI_NOT_INIT);
static_assert(std::to_underlying(idfxx::wifi::errc::not_started) == ESP_ERR_WIFI_NOT_STARTED);
static_assert(std::to_underlying(idfxx::wifi::errc::not_stopped) == ESP_ERR_WIFI_NOT_STOPPED);
static_assert(std::to_underlying(idfxx::wifi::errc::if_error) == ESP_ERR_WIFI_IF);
static_assert(std::to_underlying(idfxx::wifi::errc::mode) == ESP_ERR_WIFI_MODE);
static_assert(std::to_underlying(idfxx::wifi::errc::state) == ESP_ERR_WIFI_STATE);
static_assert(std::to_underlying(idfxx::wifi::errc::conn) == ESP_ERR_WIFI_CONN);
static_assert(std::to_underlying(idfxx::wifi::errc::nvs) == ESP_ERR_WIFI_NVS);
static_assert(std::to_underlying(idfxx::wifi::errc::mac) == ESP_ERR_WIFI_MAC);
static_assert(std::to_underlying(idfxx::wifi::errc::ssid) == ESP_ERR_WIFI_SSID);
static_assert(std::to_underlying(idfxx::wifi::errc::password) == ESP_ERR_WIFI_PASSWORD);
static_assert(std::to_underlying(idfxx::wifi::errc::timeout) == ESP_ERR_WIFI_TIMEOUT);
static_assert(std::to_underlying(idfxx::wifi::errc::wake_fail) == ESP_ERR_WIFI_WAKE_FAIL);
static_assert(std::to_underlying(idfxx::wifi::errc::would_block) == ESP_ERR_WIFI_WOULD_BLOCK);
static_assert(std::to_underlying(idfxx::wifi::errc::not_connect) == ESP_ERR_WIFI_NOT_CONNECT);
static_assert(std::to_underlying(idfxx::wifi::errc::post) == ESP_ERR_WIFI_POST);
static_assert(std::to_underlying(idfxx::wifi::errc::init_state) == ESP_ERR_WIFI_INIT_STATE);
static_assert(std::to_underlying(idfxx::wifi::errc::stop_state) == ESP_ERR_WIFI_STOP_STATE);
static_assert(std::to_underlying(idfxx::wifi::errc::not_assoc) == ESP_ERR_WIFI_NOT_ASSOC);
static_assert(std::to_underlying(idfxx::wifi::errc::tx_disallow) == ESP_ERR_WIFI_TX_DISALLOW);
static_assert(std::to_underlying(idfxx::wifi::errc::twt_full) == ESP_ERR_WIFI_TWT_FULL);
static_assert(std::to_underlying(idfxx::wifi::errc::twt_setup_timeout) == ESP_ERR_WIFI_TWT_SETUP_TIMEOUT);
static_assert(std::to_underlying(idfxx::wifi::errc::twt_setup_txfail) == ESP_ERR_WIFI_TWT_SETUP_TXFAIL);
static_assert(std::to_underlying(idfxx::wifi::errc::twt_setup_reject) == ESP_ERR_WIFI_TWT_SETUP_REJECT);
static_assert(std::to_underlying(idfxx::wifi::errc::discard) == ESP_ERR_WIFI_DISCARD);
static_assert(std::to_underlying(idfxx::wifi::errc::roc_in_progress) == ESP_ERR_WIFI_ROC_IN_PROGRESS);

// =============================================================================
// Event base definitions
// =============================================================================

namespace idfxx::wifi {

const event_base<event_id> events{WIFI_EVENT};

} // namespace idfxx::wifi

namespace idfxx {

// =============================================================================
// Error category
// =============================================================================

const wifi::error_category& wifi_category() noexcept {
    static const wifi::error_category instance{};
    return instance;
}

const char* wifi::error_category::name() const noexcept {
    return "wifi::Error";
}

std::string wifi::error_category::message(int ec) const {
    switch (wifi::errc(ec)) {
    case wifi::errc::not_init:
        return "WiFi driver was not initialized";
    case wifi::errc::not_started:
        return "WiFi driver was not started";
    case wifi::errc::not_stopped:
        return "WiFi driver was not stopped";
    case wifi::errc::if_error:
        return "WiFi interface error";
    case wifi::errc::mode:
        return "WiFi mode error";
    case wifi::errc::state:
        return "WiFi internal state error";
    case wifi::errc::conn:
        return "WiFi internal control block of station error";
    case wifi::errc::nvs:
        return "WiFi internal NVS module error";
    case wifi::errc::mac:
        return "MAC address is invalid";
    case wifi::errc::ssid:
        return "SSID is invalid";
    case wifi::errc::password:
        return "Password is invalid";
    case wifi::errc::timeout:
        return "WiFi timeout";
    case wifi::errc::wake_fail:
        return "WiFi is in sleep state and wakeup failed";
    case wifi::errc::would_block:
        return "The caller would block";
    case wifi::errc::not_connect:
        return "Station still in disconnect status";
    case wifi::errc::post:
        return "Failed to post event to WiFi task";
    case wifi::errc::init_state:
        return "Invalid WiFi state when init/deinit is called";
    case wifi::errc::stop_state:
        return "WiFi stop in progress";
    case wifi::errc::not_assoc:
        return "WiFi connection not associated";
    case wifi::errc::tx_disallow:
        return "WiFi TX is disallowed";
    case wifi::errc::twt_full:
        return "No available TWT flow ID";
    case wifi::errc::twt_setup_timeout:
        return "TWT setup response timeout";
    case wifi::errc::twt_setup_txfail:
        return "TWT setup frame TX failed";
    case wifi::errc::twt_setup_reject:
        return "TWT setup request was rejected by AP";
    case wifi::errc::discard:
        return "Frame discarded";
    case wifi::errc::roc_in_progress:
        return "Remain-on-channel operation in progress";
    default:
        return esp_err_to_name(static_cast<esp_err_t>(ec));
    }
}

// =============================================================================
// WiFi error helper
// =============================================================================

static bool is_wifi_error(esp_err_t e) noexcept {
    return (e & 0xFF00) == ESP_ERR_WIFI_BASE;
}

std::unexpected<std::error_code> wifi_error(esp_err_t e) {
    if (e == ESP_ERR_NO_MEM) {
        raise_no_mem();
    }
    if (is_wifi_error(e)) {
        return std::unexpected(std::error_code{e, wifi_category()});
    }
    return error(e);
}

// =============================================================================
// Verify from_opaque exists on all event data types whose layout differs from ESP-IDF
// =============================================================================

// clang-format off
static_assert(requires(const void* p) { { wifi::connected_event_data::from_opaque(p) } -> std::same_as<wifi::connected_event_data>; },
    "connected_event_data requires from_opaque — layout differs from ESP-IDF struct");
static_assert(requires(const void* p) { { wifi::disconnected_event_data::from_opaque(p) } -> std::same_as<wifi::disconnected_event_data>; },
    "disconnected_event_data requires from_opaque — layout differs from ESP-IDF struct");
static_assert(requires(const void* p) { { wifi::scan_done_event_data::from_opaque(p) } -> std::same_as<wifi::scan_done_event_data>; },
    "scan_done_event_data requires from_opaque — layout differs from ESP-IDF struct");
static_assert(requires(const void* p) { { wifi::authmode_change_event_data::from_opaque(p) } -> std::same_as<wifi::authmode_change_event_data>; },
    "authmode_change_event_data requires from_opaque — layout differs from ESP-IDF struct");
static_assert(requires(const void* p) { { wifi::ap_sta_connected_event_data::from_opaque(p) } -> std::same_as<wifi::ap_sta_connected_event_data>; },
    "ap_sta_connected_event_data requires from_opaque — layout differs from ESP-IDF struct");
static_assert(requires(const void* p) { { wifi::ap_sta_disconnected_event_data::from_opaque(p) } -> std::same_as<wifi::ap_sta_disconnected_event_data>; },
    "ap_sta_disconnected_event_data requires from_opaque — layout differs from ESP-IDF struct");
static_assert(requires(const void* p) { { wifi::ap_probe_req_event_data::from_opaque(p) } -> std::same_as<wifi::ap_probe_req_event_data>; },
    "ap_probe_req_event_data requires from_opaque — layout differs from ESP-IDF struct");
static_assert(requires(const void* p) { { wifi::bss_rssi_low_event_data::from_opaque(p) } -> std::same_as<wifi::bss_rssi_low_event_data>; },
    "bss_rssi_low_event_data requires from_opaque — layout differs from ESP-IDF struct");
static_assert(requires(const void* p) { { wifi::home_channel_change_event_data::from_opaque(p) } -> std::same_as<wifi::home_channel_change_event_data>; },
    "home_channel_change_event_data requires from_opaque — layout differs from ESP-IDF struct");
static_assert(requires(const void* p) { { wifi::ftm_report_event_data::from_opaque(p) } -> std::same_as<wifi::ftm_report_event_data>; },
    "ftm_report_event_data requires from_opaque — layout differs from ESP-IDF struct");
// clang-format on

// =============================================================================
// Event data wrapper implementations
// =============================================================================

wifi::connected_event_data wifi::connected_event_data::from_opaque(const void* event_data) {
    auto* info = static_cast<const wifi_event_sta_connected_t*>(event_data);
    connected_event_data result;
    std::memcpy(result.bssid.data(), info->bssid, 6);
    result.ssid = std::string(reinterpret_cast<const char*>(info->ssid), info->ssid_len);
    result.channel = info->channel;
    result.authmode = static_cast<enum wifi::auth_mode>(info->authmode);
    return result;
}

wifi::disconnected_event_data wifi::disconnected_event_data::from_opaque(const void* event_data) {
    auto* info = static_cast<const wifi_event_sta_disconnected_t*>(event_data);
    disconnected_event_data result;
    std::memcpy(result.bssid.data(), info->bssid, 6);
    result.ssid = std::string(reinterpret_cast<const char*>(info->ssid), info->ssid_len);
    result.reason = static_cast<enum wifi::disconnect_reason>(info->reason);
    return result;
}

wifi::scan_done_event_data wifi::scan_done_event_data::from_opaque(const void* event_data) {
    auto* info = static_cast<const wifi_event_sta_scan_done_t*>(event_data);
    return {info->status, info->number, info->scan_id};
}

wifi::authmode_change_event_data wifi::authmode_change_event_data::from_opaque(const void* event_data) {
    auto* info = static_cast<const wifi_event_sta_authmode_change_t*>(event_data);
    return {static_cast<auth_mode>(info->old_mode), static_cast<auth_mode>(info->new_mode)};
}

wifi::ap_sta_connected_event_data wifi::ap_sta_connected_event_data::from_opaque(const void* event_data) {
    auto* info = static_cast<const wifi_event_ap_staconnected_t*>(event_data);
    ap_sta_connected_event_data result;
    std::memcpy(result.mac.data(), info->mac, 6);
    result.aid = info->aid;
    return result;
}

wifi::ap_sta_disconnected_event_data wifi::ap_sta_disconnected_event_data::from_opaque(const void* event_data) {
    auto* info = static_cast<const wifi_event_ap_stadisconnected_t*>(event_data);
    ap_sta_disconnected_event_data result;
    std::memcpy(result.mac.data(), info->mac, 6);
    result.aid = info->aid;
    result.reason = static_cast<disconnect_reason>(info->reason);
    return result;
}

wifi::ap_probe_req_event_data wifi::ap_probe_req_event_data::from_opaque(const void* event_data) {
    auto* info = static_cast<const wifi_event_ap_probe_req_rx_t*>(event_data);
    ap_probe_req_event_data result;
    result.rssi = info->rssi;
    std::memcpy(result.mac.data(), info->mac, 6);
    return result;
}

wifi::bss_rssi_low_event_data wifi::bss_rssi_low_event_data::from_opaque(const void* event_data) {
    auto* info = static_cast<const wifi_event_bss_rssi_low_t*>(event_data);
    return {info->rssi};
}

static std::optional<wifi::second_channel> from_idf_second_chan(wifi_second_chan_t sc) {
    return sc == WIFI_SECOND_CHAN_NONE ? std::nullopt : std::optional(static_cast<wifi::second_channel>(sc));
}

wifi::home_channel_change_event_data wifi::home_channel_change_event_data::from_opaque(const void* event_data) {
    auto* info = static_cast<const wifi_event_home_channel_change_t*>(event_data);
    return {
        info->old_chan,
        from_idf_second_chan(info->old_snd),
        info->new_chan,
        from_idf_second_chan(info->new_snd),
    };
}

wifi::ftm_report_event_data wifi::ftm_report_event_data::from_opaque(const void* event_data) {
    auto* info = static_cast<const wifi_event_ftm_report_t*>(event_data);
    ftm_report_event_data result;
    std::memcpy(result.peer_mac.data(), info->peer_mac, 6);
    result.status = static_cast<ftm_status>(info->status);
    result.rtt_raw = info->rtt_raw;
    result.rtt_est = info->rtt_est;
    result.dist_est = info->dist_est;
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
    if (info->ftm_report_num_entries > 0) {
        std::vector<wifi_ftm_report_entry_t> raw(info->ftm_report_num_entries);
        if (esp_wifi_ftm_get_report(raw.data(), info->ftm_report_num_entries) == ESP_OK) {
            result.entries.reserve(info->ftm_report_num_entries);
            for (auto& e : raw) {
                result.entries.push_back(ftm_report_entry{e.dlog_token, e.rssi, e.rtt, e.t1, e.t2, e.t3, e.t4});
            }
        }
    }
#else
    if (info->ftm_report_data && info->ftm_report_num_entries > 0) {
        result.entries.reserve(info->ftm_report_num_entries);
        for (uint8_t i = 0; i < info->ftm_report_num_entries; ++i) {
            auto& e = info->ftm_report_data[i];
            result.entries.push_back(ftm_report_entry{e.dlog_token, e.rssi, e.rtt, e.t1, e.t2, e.t3, e.t4});
        }
    }
#endif
    return result;
}

// =============================================================================
// String conversion
// =============================================================================

std::string to_string(flags<wifi::role> roles) {
    if (roles.bits == 0) {
        return "none";
    }
    std::string s;
    if (roles.contains(wifi::role::sta)) {
        s += "sta";
    }
    if (roles.contains(wifi::role::ap)) {
        if (!s.empty()) {
            s += '|';
        }
        s += "ap";
    }
    auto known = std::to_underlying(wifi::role::sta) | std::to_underlying(wifi::role::ap);
    if (roles.bits & ~known) {
        if (!s.empty()) {
            s += '|';
        }
        s += "unknown(" + std::to_string(roles.bits & ~known) + ")";
    }
    return s;
}

std::string to_string(wifi::auth_mode m) {
    switch (m) {
    case wifi::auth_mode::open:
        return "OPEN";
    case wifi::auth_mode::wep:
        return "WEP";
    case wifi::auth_mode::wpa_psk:
        return "WPA_PSK";
    case wifi::auth_mode::wpa2_psk:
        return "WPA2_PSK";
    case wifi::auth_mode::wpa_wpa2_psk:
        return "WPA/WPA2_PSK";
    case wifi::auth_mode::wpa2_enterprise:
        return "WPA2_ENT";
    case wifi::auth_mode::wpa3_psk:
        return "WPA3_PSK";
    case wifi::auth_mode::wpa2_wpa3_psk:
        return "WPA2/WPA3_PSK";
    case wifi::auth_mode::wapi_psk:
        return "WAPI_PSK";
    case wifi::auth_mode::owe:
        return "OWE";
    case wifi::auth_mode::wpa3_ent_192:
        return "WPA3_ENT_192";
    case wifi::auth_mode::dpp:
        return "DPP";
    case wifi::auth_mode::wpa3_enterprise:
        return "WPA3_ENT";
    case wifi::auth_mode::wpa2_wpa3_enterprise:
        return "WPA2/WPA3_ENT";
    case wifi::auth_mode::wpa_enterprise:
        return "WPA_ENT";
    default:
        return "unknown(" + std::to_string(static_cast<int>(m)) + ")";
    }
}

std::string to_string(wifi::cipher_type c) {
    switch (c) {
    case wifi::cipher_type::none:
        return "NONE";
    case wifi::cipher_type::wep40:
        return "WEP40";
    case wifi::cipher_type::wep104:
        return "WEP104";
    case wifi::cipher_type::tkip:
        return "TKIP";
    case wifi::cipher_type::ccmp:
        return "CCMP";
    case wifi::cipher_type::tkip_ccmp:
        return "TKIP_CCMP";
    case wifi::cipher_type::aes_cmac128:
        return "AES_CMAC128";
    case wifi::cipher_type::sms4:
        return "SMS4";
    case wifi::cipher_type::gcmp:
        return "GCMP";
    case wifi::cipher_type::gcmp256:
        return "GCMP256";
    case wifi::cipher_type::aes_gmac128:
        return "AES_GMAC128";
    case wifi::cipher_type::aes_gmac256:
        return "AES_GMAC256";
    case wifi::cipher_type::unknown:
        return "UNKNOWN";
    default:
        return "unknown(" + std::to_string(static_cast<int>(c)) + ")";
    }
}

std::string to_string(wifi::disconnect_reason r) {
    switch (r) {
    case wifi::disconnect_reason::unspecified:
        return "unspecified";
    case wifi::disconnect_reason::auth_expire:
        return "auth_expire";
    case wifi::disconnect_reason::auth_leave:
        return "auth_leave";
    case wifi::disconnect_reason::assoc_expire:
        return "assoc_expire";
    case wifi::disconnect_reason::assoc_toomany:
        return "assoc_toomany";
    case wifi::disconnect_reason::not_authed:
        return "not_authed";
    case wifi::disconnect_reason::not_assoced:
        return "not_assoced";
    case wifi::disconnect_reason::assoc_leave:
        return "assoc_leave";
    case wifi::disconnect_reason::assoc_not_authed:
        return "assoc_not_authed";
    case wifi::disconnect_reason::disassoc_pwrcap_bad:
        return "disassoc_pwrcap_bad";
    case wifi::disconnect_reason::disassoc_supchan_bad:
        return "disassoc_supchan_bad";
    case wifi::disconnect_reason::bss_transition_disassoc:
        return "bss_transition_disassoc";
    case wifi::disconnect_reason::ie_invalid:
        return "ie_invalid";
    case wifi::disconnect_reason::mic_failure:
        return "mic_failure";
    case wifi::disconnect_reason::fourway_handshake_timeout:
        return "4way_handshake_timeout";
    case wifi::disconnect_reason::group_key_update_timeout:
        return "group_key_update_timeout";
    case wifi::disconnect_reason::ie_in_4way_differs:
        return "ie_in_4way_differs";
    case wifi::disconnect_reason::group_cipher_invalid:
        return "group_cipher_invalid";
    case wifi::disconnect_reason::pairwise_cipher_invalid:
        return "pairwise_cipher_invalid";
    case wifi::disconnect_reason::akmp_invalid:
        return "akmp_invalid";
    case wifi::disconnect_reason::unsupp_rsn_ie_version:
        return "unsupp_rsn_ie_version";
    case wifi::disconnect_reason::invalid_rsn_ie_cap:
        return "invalid_rsn_ie_cap";
    case wifi::disconnect_reason::ieee_802_1x_auth_failed:
        return "802_1x_auth_failed";
    case wifi::disconnect_reason::cipher_suite_rejected:
        return "cipher_suite_rejected";
    case wifi::disconnect_reason::tdls_peer_unreachable:
        return "tdls_peer_unreachable";
    case wifi::disconnect_reason::tdls_unspecified:
        return "tdls_unspecified";
    case wifi::disconnect_reason::ssp_requested_disassoc:
        return "ssp_requested_disassoc";
    case wifi::disconnect_reason::no_ssp_roaming_agreement:
        return "no_ssp_roaming_agreement";
    case wifi::disconnect_reason::bad_cipher_or_akm:
        return "bad_cipher_or_akm";
    case wifi::disconnect_reason::not_authorized_this_location:
        return "not_authorized_this_location";
    case wifi::disconnect_reason::service_change_precludes_ts:
        return "service_change_precludes_ts";
    case wifi::disconnect_reason::unspecified_qos:
        return "unspecified_qos";
    case wifi::disconnect_reason::not_enough_bandwidth:
        return "not_enough_bandwidth";
    case wifi::disconnect_reason::missing_acks:
        return "missing_acks";
    case wifi::disconnect_reason::exceeded_txop:
        return "exceeded_txop";
    case wifi::disconnect_reason::sta_leaving:
        return "sta_leaving";
    case wifi::disconnect_reason::end_ba:
        return "end_ba";
    case wifi::disconnect_reason::unknown_ba:
        return "unknown_ba";
    case wifi::disconnect_reason::timeout:
        return "timeout";
    case wifi::disconnect_reason::peer_initiated:
        return "peer_initiated";
    case wifi::disconnect_reason::ap_initiated:
        return "ap_initiated";
    case wifi::disconnect_reason::invalid_ft_action_frame_count:
        return "invalid_ft_action_frame_count";
    case wifi::disconnect_reason::invalid_pmkid:
        return "invalid_pmkid";
    case wifi::disconnect_reason::invalid_mde:
        return "invalid_mde";
    case wifi::disconnect_reason::invalid_fte:
        return "invalid_fte";
    case wifi::disconnect_reason::transmission_link_establish_failed:
        return "transmission_link_establish_failed";
    case wifi::disconnect_reason::alterative_channel_occupied:
        return "alterative_channel_occupied";
    case wifi::disconnect_reason::beacon_timeout:
        return "beacon_timeout";
    case wifi::disconnect_reason::no_ap_found:
        return "no_ap_found";
    case wifi::disconnect_reason::auth_fail:
        return "auth_fail";
    case wifi::disconnect_reason::assoc_fail:
        return "assoc_fail";
    case wifi::disconnect_reason::handshake_timeout:
        return "handshake_timeout";
    case wifi::disconnect_reason::connection_fail:
        return "connection_fail";
    case wifi::disconnect_reason::ap_tsf_reset:
        return "ap_tsf_reset";
    case wifi::disconnect_reason::roaming:
        return "roaming";
    case wifi::disconnect_reason::assoc_comeback_time_too_long:
        return "assoc_comeback_time_too_long";
    case wifi::disconnect_reason::sa_query_timeout:
        return "sa_query_timeout";
    case wifi::disconnect_reason::no_ap_found_w_compatible_security:
        return "no_ap_found_w_compatible_security";
    case wifi::disconnect_reason::no_ap_found_in_authmode_threshold:
        return "no_ap_found_in_authmode_threshold";
    case wifi::disconnect_reason::no_ap_found_in_rssi_threshold:
        return "no_ap_found_in_rssi_threshold";
    default:
        return "unknown(" + std::to_string(static_cast<int>(r)) + ")";
    }
}

} // namespace idfxx
