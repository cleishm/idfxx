// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

/**
 * @headerfile <idfxx/wifi>
 * @file wifi.hpp
 * @brief WiFi type definitions and free function API.
 *
 * @defgroup idfxx_wifi WiFi Component
 * @brief Type-safe WiFi management with station and access point mode support.
 *
 * Provides WiFi lifecycle management, station (STA) and access point (AP) modes,
 * scanning, promiscuous mode, CSI, FTM, and advanced configuration.
 *
 * Depends on @ref idfxx_core for error handling and @ref idfxx_event for
 * event loop integration.
 * @{
 */

#include <idfxx/cpu>
#include <idfxx/error>
#include <idfxx/event>
#include <idfxx/flags>
#include <idfxx/mac>

#include <array>
#include <cstdint>
#include <optional>
#include <soc/soc_caps.h>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace idfxx::wifi {

// =============================================================================
// Enumerations
// =============================================================================

/**
 * @headerfile <idfxx/wifi>
 * @brief WiFi operating mode.
 */
enum class mode : int {
    // clang-format off
    null   = 0, /*!< No WiFi mode (disabled). */
    sta    = 1, /*!< Station (STA) mode. */
    ap     = 2, /*!< Access point (AP) mode. */
    ap_sta = 3, /*!< Simultaneous AP and STA mode. */
    // clang-format on
};

/**
 * @headerfile <idfxx/wifi>
 * @brief WiFi interface identifier.
 */
enum class interface : int {
    // clang-format off
    sta = 0, /*!< Station interface. */
    ap  = 1, /*!< Access point interface. */
    // clang-format on
};

/**
 * @headerfile <idfxx/wifi>
 * @brief WiFi authentication mode.
 */
enum class auth_mode : int {
    // clang-format off
    open                 = 0,  /*!< Open (no authentication). */
    wep                  = 1,  /*!< WEP authentication. */
    wpa_psk              = 2,  /*!< WPA-PSK authentication. */
    wpa2_psk             = 3,  /*!< WPA2-PSK authentication. */
    wpa_wpa2_psk         = 4,  /*!< WPA/WPA2-PSK authentication. */
    wpa2_enterprise      = 5,  /*!< WPA2-Enterprise authentication. */
    wpa3_psk             = 6,  /*!< WPA3-PSK authentication. */
    wpa2_wpa3_psk        = 7,  /*!< WPA2/WPA3-PSK authentication. */
    wapi_psk             = 8,  /*!< WAPI-PSK authentication. */
    owe                  = 9,  /*!< OWE (Opportunistic Wireless Encryption). */
    wpa3_ent_192         = 10, /*!< WPA3-Enterprise Suite B 192-bit. */
    dpp                  = 13, /*!< DPP (Device Provisioning Protocol). */
    wpa3_enterprise      = 14, /*!< WPA3-Enterprise Only Mode. */
    wpa2_wpa3_enterprise = 15, /*!< WPA3-Enterprise Transition Mode. */
    wpa_enterprise       = 16, /*!< WPA-Enterprise authentication. */
    // clang-format on
};

/**
 * @headerfile <idfxx/wifi>
 * @brief WiFi power save mode.
 */
enum class power_save : int {
    // clang-format off
    none      = 0, /*!< No power saving. */
    min_modem = 1, /*!< Minimum modem power saving. Station wakes up every DTIM. */
    max_modem = 2, /*!< Maximum modem power saving. Station wakes up every listen interval. */
    // clang-format on
};

/**
 * @headerfile <idfxx/wifi>
 * @brief WiFi channel bandwidth.
 */
enum class bandwidth : int {
    // clang-format off
    ht20    = 1, /*!< HT20 (20 MHz bandwidth). */
    ht40    = 2, /*!< HT40 (40 MHz bandwidth). */
    bw80    = 3, /*!< 80 MHz bandwidth. */
    bw160   = 4, /*!< 160 MHz bandwidth. */
    bw80_80 = 5, /*!< 80+80 MHz bandwidth. */
    // clang-format on
};

/**
 * @headerfile <idfxx/wifi>
 * @brief WiFi scan type.
 */
enum class scan_type : int {
    // clang-format off
    active  = 0, /*!< Active scan (sends probe requests). */
    passive = 1, /*!< Passive scan (listens for beacons). */
    // clang-format on
};

/**
 * @headerfile <idfxx/wifi>
 * @brief WiFi secondary channel position.
 */
enum class second_channel : int {
    // clang-format off
    none  = 0, /*!< No secondary channel (HT20 mode). */
    above = 1, /*!< Secondary channel is above the primary channel. */
    below = 2, /*!< Secondary channel is below the primary channel. */
    // clang-format on
};

/**
 * @headerfile <idfxx/wifi>
 * @brief WiFi AP sort method for connection.
 */
enum class sort_method : int {
    // clang-format off
    by_rssi     = 0, /*!< Sort by signal strength (RSSI). */
    by_security = 1, /*!< Sort by security mode. */
    // clang-format on
};

/**
 * @headerfile <idfxx/wifi>
 * @brief WiFi disconnection reason codes.
 *
 * Values 1-68 are IEEE 802.11 standard reason codes.
 * Values 200+ are vendor-specific reason codes for events such as beacon timeout and authentication failure.
 */
enum class disconnect_reason : uint16_t {
    // clang-format off
    // IEEE 802.11 standard reasons
    unspecified                        = 1,   /*!< Unspecified reason. */
    auth_expire                        = 2,   /*!< Authentication expired. */
    auth_leave                         = 3,   /*!< Deauthenticated because sending STA is leaving. */
    assoc_expire                       = 4,   /*!< Disassociated due to inactivity. */
    assoc_toomany                      = 5,   /*!< Disassociated because AP is unable to handle all associated STAs. */
    not_authed                         = 6,   /*!< Class 2 frame received from nonauthenticated STA. */
    not_assoced                        = 7,   /*!< Class 3 frame received from nonassociated STA. */
    assoc_leave                        = 8,   /*!< Disassociated because sending STA is leaving. */
    assoc_not_authed                   = 9,   /*!< STA requesting association is not authenticated. */
    disassoc_pwrcap_bad                = 10,  /*!< Disassociated because power capability is unacceptable. */
    disassoc_supchan_bad               = 11,  /*!< Disassociated because supported channels are unacceptable. */
    bss_transition_disassoc            = 12,  /*!< Disassociated due to BSS transition management. */
    ie_invalid                         = 13,  /*!< Invalid information element. */
    mic_failure                        = 14,  /*!< Message integrity code failure. */
    fourway_handshake_timeout          = 15,  /*!< 4-way handshake timeout. */
    group_key_update_timeout           = 16,  /*!< Group key handshake timeout. */
    ie_in_4way_differs                 = 17,  /*!< Information element in 4-way handshake differs. */
    group_cipher_invalid               = 18,  /*!< Invalid group cipher. */
    pairwise_cipher_invalid            = 19,  /*!< Invalid pairwise cipher. */
    akmp_invalid                       = 20,  /*!< Invalid AKMP. */
    unsupp_rsn_ie_version              = 21,  /*!< Unsupported RSN information element version. */
    invalid_rsn_ie_cap                 = 22,  /*!< Invalid RSN information element capabilities. */
    ieee_802_1x_auth_failed            = 23,  /*!< IEEE 802.1X authentication failed. */
    cipher_suite_rejected              = 24,  /*!< Cipher suite rejected. */
    tdls_peer_unreachable              = 25,  /*!< TDLS peer unreachable. */
    tdls_unspecified                   = 26,  /*!< TDLS unspecified reason. */
    ssp_requested_disassoc             = 27,  /*!< SSP requested disassociation. */
    no_ssp_roaming_agreement           = 28,  /*!< No SSP roaming agreement. */
    bad_cipher_or_akm                  = 29,  /*!< Bad cipher or AKM. */
    not_authorized_this_location       = 30,  /*!< Not authorized for this location. */
    service_change_precludes_ts        = 31,  /*!< Service change precludes TS. */
    unspecified_qos                    = 32,  /*!< Unspecified QoS reason. */
    not_enough_bandwidth               = 33,  /*!< Not enough bandwidth. */
    missing_acks                       = 34,  /*!< Missing ACKs. */
    exceeded_txop                      = 35,  /*!< Exceeded TXOP. */
    sta_leaving                        = 36,  /*!< STA leaving. */
    end_ba                             = 37,  /*!< Block Ack session ended. */
    unknown_ba                         = 38,  /*!< Unknown Block Ack. */
    timeout                            = 39,  /*!< Timeout. */
    peer_initiated                     = 46,  /*!< Peer initiated disconnect. */
    ap_initiated                       = 47,  /*!< AP initiated disconnect. */
    invalid_ft_action_frame_count      = 48,  /*!< Invalid FT action frame count. */
    invalid_pmkid                      = 49,  /*!< Invalid PMKID. */
    invalid_mde                        = 50,  /*!< Invalid MDE. */
    invalid_fte                        = 51,  /*!< Invalid FTE. */
    transmission_link_establish_failed = 67,  /*!< Transmission link establishment failed. */
    alterative_channel_occupied        = 68,  /*!< Alternative channel occupied. */

    // Vendor-specific reasons (200+)
    beacon_timeout                     = 200, /*!< Beacon timeout (AP not responding). */
    no_ap_found                        = 201, /*!< No AP found matching the configured SSID. */
    auth_fail                          = 202, /*!< Authentication failed. */
    assoc_fail                         = 203, /*!< Association failed. */
    handshake_timeout                  = 204, /*!< Handshake timeout. */
    connection_fail                    = 205, /*!< Connection failed. */
    ap_tsf_reset                       = 206, /*!< AP TSF was reset. */
    roaming                            = 207, /*!< Roaming to another AP. */
    assoc_comeback_time_too_long       = 208, /*!< Association comeback time too long. */
    sa_query_timeout                   = 209, /*!< SA query timeout. */
    no_ap_found_w_compatible_security  = 210, /*!< No AP found with compatible security. */
    no_ap_found_in_authmode_threshold  = 211, /*!< No AP found in authmode threshold. */
    no_ap_found_in_rssi_threshold      = 212, /*!< No AP found in RSSI threshold. */
    // clang-format on
};

/**
 * @headerfile <idfxx/wifi>
 * @brief WiFi cipher type.
 */
enum class cipher_type : int {
    // clang-format off
    none        = 0,  /*!< No cipher. */
    wep40       = 1,  /*!< WEP40 cipher. */
    wep104      = 2,  /*!< WEP104 cipher. */
    tkip        = 3,  /*!< TKIP cipher. */
    ccmp        = 4,  /*!< CCMP cipher. */
    tkip_ccmp   = 5,  /*!< TKIP and CCMP cipher. */
    aes_cmac128 = 6,  /*!< AES-CMAC-128 cipher. */
    sms4        = 7,  /*!< SMS4 cipher. */
    gcmp        = 8,  /*!< GCMP cipher. */
    gcmp256     = 9,  /*!< GCMP-256 cipher. */
    aes_gmac128 = 10, /*!< AES-GMAC-128 cipher. */
    aes_gmac256 = 11, /*!< AES-GMAC-256 cipher. */
    unknown     = 12, /*!< Unknown cipher type. */
    // clang-format on
};

/**
 * @headerfile <idfxx/wifi>
 * @brief WiFi PHY mode.
 */
enum class phy_mode : int {
    // clang-format off
    lr    = 0, /*!< Long Range mode. */
    b11b  = 1, /*!< 802.11b mode. */
    b11g  = 2, /*!< 802.11g mode. */
    b11a  = 3, /*!< 802.11a mode. */
    ht20  = 4, /*!< HT20 mode. */
    ht40  = 5, /*!< HT40 mode. */
    he20  = 6, /*!< HE20 mode. */
    vht20 = 7, /*!< VHT20 mode. */
    // clang-format on
};

/**
 * @headerfile <idfxx/wifi>
 * @brief WiFi frequency band.
 */
enum class band : int {
    // clang-format off
    ghz_2g = 1, /*!< 2.4 GHz band. */
    ghz_5g = 2, /*!< 5 GHz band. */
    // clang-format on
};

/**
 * @headerfile <idfxx/wifi>
 * @brief WiFi band mode.
 */
enum class band_mode : int {
    // clang-format off
    ghz_2g_only = 1, /*!< 2.4 GHz only. */
    ghz_5g_only = 2, /*!< 5 GHz only. */
    auto_mode   = 3, /*!< Automatic band selection. */
    // clang-format on
};

/**
 * @headerfile <idfxx/wifi>
 * @brief 5 GHz WiFi channel flags.
 *
 * Each value represents a specific 5 GHz channel. Can be combined using
 * bitwise operators via the flags<channel_5g> type to form a channel mask.
 */
enum class channel_5g : uint32_t {
    // clang-format off
    ch36  = 1u << 1,  /*!< Channel 36. */
    ch40  = 1u << 2,  /*!< Channel 40. */
    ch44  = 1u << 3,  /*!< Channel 44. */
    ch48  = 1u << 4,  /*!< Channel 48. */
    ch52  = 1u << 5,  /*!< Channel 52. */
    ch56  = 1u << 6,  /*!< Channel 56. */
    ch60  = 1u << 7,  /*!< Channel 60. */
    ch64  = 1u << 8,  /*!< Channel 64. */
    ch100 = 1u << 9,  /*!< Channel 100. */
    ch104 = 1u << 10, /*!< Channel 104. */
    ch108 = 1u << 11, /*!< Channel 108. */
    ch112 = 1u << 12, /*!< Channel 112. */
    ch116 = 1u << 13, /*!< Channel 116. */
    ch120 = 1u << 14, /*!< Channel 120. */
    ch124 = 1u << 15, /*!< Channel 124. */
    ch128 = 1u << 16, /*!< Channel 128. */
    ch132 = 1u << 17, /*!< Channel 132. */
    ch136 = 1u << 18, /*!< Channel 136. */
    ch140 = 1u << 19, /*!< Channel 140. */
    ch144 = 1u << 20, /*!< Channel 144. */
    ch149 = 1u << 21, /*!< Channel 149. */
    ch153 = 1u << 22, /*!< Channel 153. */
    ch157 = 1u << 23, /*!< Channel 157. */
    ch161 = 1u << 24, /*!< Channel 161. */
    ch165 = 1u << 25, /*!< Channel 165. */
    ch169 = 1u << 26, /*!< Channel 169. */
    ch173 = 1u << 27, /*!< Channel 173. */
    ch177 = 1u << 28, /*!< Channel 177. */
    all   = 0x1FFFFFFE, /*!< All 5 GHz channels. */
    // clang-format on
};

/**
 * @headerfile <idfxx/wifi>
 * @brief WiFi configuration storage location.
 */
enum class storage : int {
    // clang-format off
    flash = 0, /*!< Store configuration in flash (NVS). */
    ram   = 1, /*!< Store configuration in RAM only. */
    // clang-format on
};

/**
 * @headerfile <idfxx/wifi>
 * @brief WiFi country policy.
 */
enum class country_policy : int {
    // clang-format off
    auto_policy = 0, /*!< Automatically set country info based on connected AP. */
    manual      = 1, /*!< Manually configured country info. */
    // clang-format on
};

/**
 * @headerfile <idfxx/wifi>
 * @brief WiFi scan method.
 */
enum class scan_method : int {
    // clang-format off
    fast        = 0, /*!< Fast scan (stop on first match). */
    all_channel = 1, /*!< Scan all channels. */
    // clang-format on
};

/**
 * @headerfile <idfxx/wifi>
 * @brief SAE PWE (Password Element) derivation method.
 */
enum class sae_pwe_method : int {
    // clang-format off
    unspecified    = 0, /*!< Unspecified (auto-detect). */
    hunt_and_peck  = 1, /*!< Hunting and Pecking method. */
    hash_to_element = 2, /*!< Hash-to-Element method. */
    both           = 3, /*!< Both methods supported. */
    // clang-format on
};

/**
 * @headerfile <idfxx/wifi>
 * @brief SAE-PK (Public Key) mode.
 */
enum class sae_pk_mode : int {
    // clang-format off
    automatic = 0, /*!< Automatic SAE-PK mode. */
    only      = 1, /*!< SAE-PK only mode. */
    disabled  = 2, /*!< SAE-PK disabled. */
    // clang-format on
};

/**
 * @headerfile <idfxx/wifi>
 * @brief WiFi protocol flags.
 *
 * Can be combined using bitwise operators via the flags<protocol> type.
 */
enum class protocol : uint8_t {
    // clang-format off
    b11b  = 0x01, /*!< 802.11b protocol. */
    b11g  = 0x02, /*!< 802.11g protocol. */
    b11n  = 0x04, /*!< 802.11n protocol. */
    lr    = 0x08, /*!< Long Range protocol. */
    b11a  = 0x10, /*!< 802.11a protocol. */
    b11ac = 0x20, /*!< 802.11ac protocol. */
    b11ax = 0x40, /*!< 802.11ax protocol. */
    // clang-format on
};

/**
 * @headerfile <idfxx/wifi>
 * @brief Promiscuous mode packet type.
 */
enum class promiscuous_pkt_type : int {
    // clang-format off
    mgmt = 0, /*!< Management frame. */
    ctrl = 1, /*!< Control frame. */
    data = 2, /*!< Data frame. */
    misc = 3, /*!< Miscellaneous frame. */
    // clang-format on
};

/**
 * @headerfile <idfxx/wifi>
 * @brief Promiscuous mode filter flags.
 *
 * Can be combined using bitwise operators via the flags<promiscuous_filter> type.
 */
enum class promiscuous_filter : uint32_t {
    // clang-format off
    all        = 0xFFFFFFFF, /*!< Accept all packet types. */
    mgmt       = 1u << 0,   /*!< Accept management packets. */
    ctrl       = 1u << 1,   /*!< Accept control packets. */
    data       = 1u << 2,   /*!< Accept data packets. */
    misc       = 1u << 3,   /*!< Accept miscellaneous packets. */
    data_mpdu  = 1u << 4,   /*!< Accept data MPDU packets. */
    data_ampdu = 1u << 5,   /*!< Accept data AMPDU packets. */
    fcsfail    = 1u << 6,   /*!< Accept FCS-failed packets. */
    // clang-format on
};

/**
 * @headerfile <idfxx/wifi>
 * @brief Promiscuous mode control frame sub-type filter flags.
 *
 * Can be combined using bitwise operators via the flags<promiscuous_ctrl_filter> type.
 */
enum class promiscuous_ctrl_filter : uint32_t {
    // clang-format off
    all     = 0xFF800000,  /*!< Accept all control sub-types. */
    wrapper = 1u << 23,    /*!< Accept wrapper frames. */
    bar     = 1u << 24,    /*!< Accept Block Ack Request frames. */
    ba      = 1u << 25,    /*!< Accept Block Ack frames. */
    pspoll  = 1u << 26,    /*!< Accept PS-Poll frames. */
    rts     = 1u << 27,    /*!< Accept RTS frames. */
    cts     = 1u << 28,    /*!< Accept CTS frames. */
    ack     = 1u << 29,    /*!< Accept ACK frames. */
    cfend   = 1u << 30,    /*!< Accept CF-End frames. */
    cfendack = 1u << 31,   /*!< Accept CF-End + CF-Ack frames. */
    // clang-format on
};

/**
 * @headerfile <idfxx/wifi>
 * @brief WiFi event mask flags.
 *
 * Controls which events are suppressed from the event loop.
 * Can be combined using bitwise operators via the flags<event_mask> type.
 */
enum class event_mask : uint32_t {
    // clang-format off
    all              = 0xFFFFFFFF, /*!< All events. */
    none             = 0,          /*!< No events. */
    ap_probe_req_rx  = 1u << 0,   /*!< AP probe request received event. */
    // clang-format on
};

/**
 * @headerfile <idfxx/wifi>
 * @brief WiFi statistics module flags.
 *
 * Identifies which internal WiFi modules should dump their statistics.
 * Can be combined using bitwise operators via the flags<statis_module> type.
 */
enum class statis_module : uint32_t {
    // clang-format off
    buffer = 1u << 0,      /*!< Buffer allocation statistics. */
    rxtx   = 1u << 1,      /*!< RX/TX statistics. */
    hw     = 1u << 2,      /*!< Hardware statistics. */
    diag   = 1u << 3,      /*!< Diagnostic statistics. */
    ps     = 1u << 4,      /*!< Power-save statistics. */
    all    = 0xFFFFFFFF,   /*!< All modules. */
    // clang-format on
};

/**
 * @headerfile <idfxx/wifi>
 * @brief Vendor-specific IE type.
 */
enum class vendor_ie_type : int {
    // clang-format off
    beacon     = 0, /*!< Beacon frame. */
    probe_req  = 1, /*!< Probe request frame. */
    probe_resp = 2, /*!< Probe response frame. */
    assoc_req  = 3, /*!< Association request frame. */
    assoc_resp = 4, /*!< Association response frame. */
    // clang-format on
};

/**
 * @headerfile <idfxx/wifi>
 * @brief Vendor-specific IE index.
 */
enum class vendor_ie_id : int {
    // clang-format off
    id_0 = 0, /*!< Vendor IE index 0. */
    id_1 = 1, /*!< Vendor IE index 1. */
    // clang-format on
};

/**
 * @headerfile <idfxx/wifi>
 * @brief FTM (Fine Timing Measurement) session status.
 */
enum class ftm_status : int {
    // clang-format off
    success        = 0, /*!< FTM session completed successfully. */
    unsupported    = 1, /*!< FTM not supported by peer. */
    conf_rejected  = 2, /*!< FTM configuration rejected by peer. */
    no_response    = 3, /*!< No response from peer. */
    fail           = 4, /*!< FTM session failed. */
    no_valid_msmt  = 5, /*!< No valid measurements obtained. */
    user_term      = 6, /*!< FTM session terminated by user. */
    // clang-format on
};

/**
 * @headerfile <idfxx/wifi>
 * @brief WiFi PHY transmission rate.
 *
 * Specifies the physical layer data rate for 802.11 frame transmission.
 * Includes rates for 802.11b (long/short preamble), OFDM (802.11a/g),
 * HT (802.11n) with long/short guard interval, and Long Range modes.
 */
enum class phy_rate : int {
    // clang-format off
    // 802.11b long preamble
    rate_1m_l  = 0x00, /*!< 1 Mbps with long preamble. */
    rate_2m_l  = 0x01, /*!< 2 Mbps with long preamble. */
    rate_5m_l  = 0x02, /*!< 5.5 Mbps with long preamble. */
    rate_11m_l = 0x03, /*!< 11 Mbps with long preamble. */
    // 802.11b short preamble
    rate_2m_s  = 0x05, /*!< 2 Mbps with short preamble. */
    rate_5m_s  = 0x06, /*!< 5.5 Mbps with short preamble. */
    rate_11m_s = 0x07, /*!< 11 Mbps with short preamble. */
    // OFDM (802.11a/g)
    rate_48m = 0x08, /*!< 48 Mbps (OFDM). */
    rate_24m = 0x09, /*!< 24 Mbps (OFDM). */
    rate_12m = 0x0A, /*!< 12 Mbps (OFDM). */
    rate_6m  = 0x0B, /*!< 6 Mbps (OFDM). */
    rate_54m = 0x0C, /*!< 54 Mbps (OFDM). */
    rate_36m = 0x0D, /*!< 36 Mbps (OFDM). */
    rate_18m = 0x0E, /*!< 18 Mbps (OFDM). */
    rate_9m  = 0x0F, /*!< 9 Mbps (OFDM). */
    // HT (802.11n) long GI
    mcs0_lgi = 0x10, /*!< MCS0 with long guard interval. */
    mcs1_lgi = 0x11, /*!< MCS1 with long guard interval. */
    mcs2_lgi = 0x12, /*!< MCS2 with long guard interval. */
    mcs3_lgi = 0x13, /*!< MCS3 with long guard interval. */
    mcs4_lgi = 0x14, /*!< MCS4 with long guard interval. */
    mcs5_lgi = 0x15, /*!< MCS5 with long guard interval. */
    mcs6_lgi = 0x16, /*!< MCS6 with long guard interval. */
    mcs7_lgi = 0x17, /*!< MCS7 with long guard interval. */
#if SOC_WIFI_HE_SUPPORT
    mcs8_lgi = 0x18, /*!< MCS8 with long guard interval. */
    mcs9_lgi = 0x19, /*!< MCS9 with long guard interval. */
#endif
    // HT (802.11n) short GI
#if SOC_WIFI_HE_SUPPORT
    mcs0_sgi = 0x1A, /*!< MCS0 with short guard interval. */
#else
    mcs0_sgi = 0x18, /*!< MCS0 with short guard interval. */
#endif
    mcs1_sgi = mcs0_sgi + 1, /*!< MCS1 with short guard interval. */
    mcs2_sgi = mcs0_sgi + 2, /*!< MCS2 with short guard interval. */
    mcs3_sgi = mcs0_sgi + 3, /*!< MCS3 with short guard interval. */
    mcs4_sgi = mcs0_sgi + 4, /*!< MCS4 with short guard interval. */
    mcs5_sgi = mcs0_sgi + 5, /*!< MCS5 with short guard interval. */
    mcs6_sgi = mcs0_sgi + 6, /*!< MCS6 with short guard interval. */
    mcs7_sgi = mcs0_sgi + 7, /*!< MCS7 with short guard interval. */
#if SOC_WIFI_HE_SUPPORT
    mcs8_sgi = mcs0_sgi + 8, /*!< MCS8 with short guard interval. */
    mcs9_sgi = mcs0_sgi + 9, /*!< MCS9 with short guard interval. */
#endif
    // Long Range (Espressif proprietary)
    lora_250k = 0x29, /*!< 250 Kbps Long Range rate. */
    lora_500k = 0x2A, /*!< 500 Kbps Long Range rate. */
    // clang-format on
};

} // namespace idfxx::wifi

/** @cond INTERNAL */
template<>
inline constexpr bool idfxx::enable_flags_operators<idfxx::wifi::protocol> = true;
template<>
inline constexpr bool idfxx::enable_flags_operators<idfxx::wifi::promiscuous_filter> = true;
template<>
inline constexpr bool idfxx::enable_flags_operators<idfxx::wifi::promiscuous_ctrl_filter> = true;
template<>
inline constexpr bool idfxx::enable_flags_operators<idfxx::wifi::channel_5g> = true;
template<>
inline constexpr bool idfxx::enable_flags_operators<idfxx::wifi::event_mask> = true;
template<>
inline constexpr bool idfxx::enable_flags_operators<idfxx::wifi::statis_module> = true;
/** @endcond */

namespace idfxx::wifi {

// =============================================================================
// Error handling
// =============================================================================

/**
 * @headerfile <idfxx/wifi>
 * @brief Error codes for WiFi operations.
 */
enum class errc : esp_err_t {
    // clang-format off
    not_init            = 0x3001, /*!< WiFi driver was not initialized. */
    not_started         = 0x3002, /*!< WiFi driver was not started. */
    not_stopped         = 0x3003, /*!< WiFi driver was not stopped. */
    if_error            = 0x3004, /*!< WiFi interface error. */
    mode                = 0x3005, /*!< WiFi mode error. */
    state               = 0x3006, /*!< WiFi internal state error. */
    conn                = 0x3007, /*!< WiFi internal control block of station error. */
    nvs                 = 0x3008, /*!< WiFi internal NVS module error. */
    mac                 = 0x3009, /*!< MAC address is invalid. */
    ssid                = 0x300A, /*!< SSID is invalid. */
    password            = 0x300B, /*!< Password is invalid. */
    timeout             = 0x300C, /*!< WiFi timeout. */
    wake_fail           = 0x300D, /*!< WiFi is in sleep state and wakeup failed. */
    would_block         = 0x300E, /*!< The caller would block. */
    not_connect         = 0x300F, /*!< Station still in disconnect status. */
    post                = 0x3012, /*!< Failed to post event to WiFi task. */
    init_state          = 0x3013, /*!< Invalid WiFi state when init/deinit is called. */
    stop_state          = 0x3014, /*!< WiFi stop in progress. */
    not_assoc           = 0x3015, /*!< WiFi connection not associated. */
    tx_disallow         = 0x3016, /*!< WiFi TX is disallowed. */
    twt_full            = 0x3017, /*!< No available TWT flow ID. */
    twt_setup_timeout   = 0x3018, /*!< TWT setup response timeout. */
    twt_setup_txfail    = 0x3019, /*!< TWT setup frame TX failed. */
    twt_setup_reject    = 0x301A, /*!< TWT setup request was rejected by AP. */
    discard             = 0x301B, /*!< Frame discarded. */
    roc_in_progress     = 0x301C, /*!< Remain-on-channel operation in progress. */
    // clang-format on
};

/**
 * @headerfile <idfxx/wifi>
 * @brief Error category for WiFi errors.
 */
class error_category : public std::error_category {
public:
    /** @brief Returns the name of the error category. */
    [[nodiscard]] const char* name() const noexcept override final;

    /** @brief Returns a human-readable message for the given error code. */
    [[nodiscard]] std::string message(int ec) const override final;
};

// =============================================================================
// Data types
// =============================================================================

/**
 * @headerfile <idfxx/wifi>
 * @brief HE (High Efficiency) AP information.
 *
 * Contains HE-specific information about an access point discovered during scanning.
 */
struct he_ap_info {
    uint8_t bss_color;       /*!< BSS Color value. */
    bool partial_bss_color;  /*!< AID assignment based on BSS color. */
    bool bss_color_disabled; /*!< BSS color usage disabled. */
    uint8_t bssid_index;     /*!< Non-transmitted BSSID index in M-BSSID set. */
};

/**
 * @headerfile <idfxx/wifi>
 * @brief WiFi country configuration.
 */
struct country_config {
    std::array<char, 3> cc = {'0', '1', '\0'};                /*!< Country code string. */
    uint8_t start_channel = 1;                                /*!< Start channel. */
    uint8_t num_channels = 11;                                /*!< Number of channels. */
    int8_t max_tx_power = 20;                                 /*!< Maximum TX power (dBm). */
    enum country_policy policy = country_policy::auto_policy; /*!< Country policy. */
#if SOC_WIFI_SUPPORT_5G
    flags<channel_5g> channels_5g{}; /*!< Allowed 5GHz channels (empty = use regulatory defaults). */
#endif
};

/**
 * @headerfile <idfxx/wifi>
 * @brief Information about a discovered access point.
 *
 * Contains the details of an AP found during a WiFi scan.
 */
struct ap_record {
    mac_address bssid;                /*!< MAC address of the access point. */
    std::string ssid;                 /*!< SSID (network name). */
    int8_t rssi;                      /*!< Received signal strength indicator (dBm). */
    uint8_t primary_channel;          /*!< Primary channel number. */
    enum second_channel second;       /*!< Secondary channel position. */
    enum auth_mode authmode;          /*!< Authentication mode. */
    enum cipher_type pairwise_cipher; /*!< Pairwise cipher type. */
    enum cipher_type group_cipher;    /*!< Group cipher type. */
    bool phy_11b;                     /*!< AP supports 802.11b. */
    bool phy_11g;                     /*!< AP supports 802.11g. */
    bool phy_11n;                     /*!< AP supports 802.11n. */
    bool phy_lr;                      /*!< AP supports Long Range mode. */
    bool phy_11a;                     /*!< AP supports 802.11a. */
    bool phy_11ac;                    /*!< AP supports 802.11ac. */
    bool phy_11ax;                    /*!< AP supports 802.11ax. */
    bool wps;                         /*!< AP supports WPS. */
    bool ftm_responder;               /*!< AP supports FTM responder. */
    bool ftm_initiator;               /*!< AP supports FTM initiator. */
    struct country_config country;    /*!< Country information. */
    struct he_ap_info he_ap;          /*!< HE AP information. */
    enum bandwidth bw;                /*!< Channel bandwidth. */
    uint8_t vht_ch_freq1;             /*!< VHT channel center frequency segment 1. */
    uint8_t vht_ch_freq2;             /*!< VHT channel center frequency segment 2. */
};

/**
 * @headerfile <idfxx/wifi>
 * @brief Information about a station connected to the soft-AP.
 */
struct sta_info {
    mac_address mac; /*!< MAC address. */
    int8_t rssi;     /*!< RSSI value. */
    bool phy_11b;    /*!< Station supports 802.11b. */
    bool phy_11g;    /*!< Station supports 802.11g. */
    bool phy_11n;    /*!< Station supports 802.11n. */
    bool phy_lr;     /*!< Station supports Long Range mode. */
    bool phy_11a;    /*!< Station supports 802.11a. */
    bool phy_11ac;   /*!< Station supports 802.11ac. */
    bool phy_11ax;   /*!< Station supports 802.11ax. */
};

/**
 * @headerfile <idfxx/wifi>
 * @brief Single FTM report measurement entry.
 */
struct ftm_report_entry {
    uint8_t dlog_token; /*!< Dialog token. */
    int8_t rssi;        /*!< RSSI of the FTM frame. */
    uint32_t rtt;       /*!< Round-trip time in picoseconds. */
    uint64_t t1;        /*!< Timestamp T1. */
    uint64_t t2;        /*!< Timestamp T2. */
    uint64_t t3;        /*!< Timestamp T3. */
    uint64_t t4;        /*!< Timestamp T4. */
};

// =============================================================================
// Configuration structs
// =============================================================================

/**
 * @headerfile <idfxx/wifi>
 * @brief Configuration for WiFi initialization.
 *
 * Controls buffer allocation, AMPDU settings, NVS persistence, and task
 * pinning for the WiFi subsystem. All fields default to @c std::nullopt,
 * which preserves the Kconfig default values.
 *
 * @code
 * idfxx::wifi::init({
 *     .static_rx_buf_num = 8,
 *     .ampdu_rx_enable = true,
 *     .nvs_enable = false,
 * });
 * @endcode
 */
struct init_config {
    std::optional<unsigned int> static_rx_buf_num;  /*!< Static RX buffer count. */
    std::optional<unsigned int> dynamic_rx_buf_num; /*!< Dynamic RX buffer count. */
    std::optional<unsigned int> static_tx_buf_num;  /*!< Static TX buffer count. */
    std::optional<unsigned int> dynamic_tx_buf_num; /*!< Dynamic TX buffer count. */
    std::optional<unsigned int> cache_tx_buf_num;   /*!< TX cache buffer count. */
    std::optional<unsigned int> rx_ba_win;          /*!< Block Ack RX window size. */
    std::optional<bool> ampdu_rx_enable;            /*!< AMPDU RX feature enable. */
    std::optional<bool> ampdu_tx_enable;            /*!< AMPDU TX feature enable. */
    std::optional<bool> nvs_enable;                 /*!< NVS flash for WiFi config persistence. */
    std::optional<core_id> wifi_task_core_id;       /*!< WiFi task core ID. */
};

/**
 * @headerfile <idfxx/wifi>
 * @brief Protected Management Frame (PMF) configuration.
 */
struct pmf_config {
    bool capable = false;  /*!< PMF capability advertised. */
    bool required = false; /*!< PMF required (reject non-PMF). */
};

/**
 * @headerfile <idfxx/wifi>
 * @brief Configuration for WiFi station mode.
 */
struct sta_config {
    std::string ssid;                                              /*!< SSID of the target access point. */
    std::string password;                                          /*!< Password for the target access point. */
    std::optional<mac_address> bssid = {};                         /*!< Target AP BSSID (empty = any). */
    uint8_t channel = 0;                                           /*!< Channel hint (0 = scan all). */
    enum scan_method scan_method = scan_method::fast;              /*!< Scan method when connecting. */
    enum sort_method sort_method = sort_method::by_rssi;           /*!< AP sort method when connecting. */
    enum auth_mode auth_threshold = auth_mode::open;               /*!< Minimum authentication mode to accept. */
    int8_t rssi_threshold = -127;                                  /*!< Minimum RSSI to accept. */
    struct pmf_config pmf = {};                                    /*!< PMF configuration. */
    uint16_t listen_interval = 0;                                  /*!< Listen interval for power save (0 = default). */
    bool rm_enabled = false;                                       /*!< Radio Measurement enabled. */
    bool btm_enabled = false;                                      /*!< BSS Transition Management enabled. */
    bool mbo_enabled = false;                                      /*!< MBO (Multi-Band Operation) enabled. */
    bool ft_enabled = false;                                       /*!< Fast BSS Transition (802.11r) enabled. */
    bool owe_enabled = false;                                      /*!< OWE Transition mode enabled. */
    bool transition_disable = false;                               /*!< Transition Disable indication. */
    enum sae_pwe_method sae_pwe_h2e = sae_pwe_method::unspecified; /*!< SAE PWE derivation method. */
    enum sae_pk_mode sae_pk_mode = sae_pk_mode::automatic;         /*!< SAE-PK mode. */
    uint8_t failure_retry_cnt = 0; /*!< Connection failure retry count (0 = no retry). */
};

/**
 * @headerfile <idfxx/wifi>
 * @brief Configuration for WiFi access point mode.
 */
struct ap_config {
    std::string ssid;                                              /*!< SSID of the access point. */
    std::string password;                                          /*!< Password for the access point. */
    uint8_t channel = 1;                                           /*!< Channel number. */
    enum auth_mode authmode = auth_mode::open;                     /*!< Authentication mode. */
    uint8_t ssid_hidden = 0;                                       /*!< Whether SSID is hidden (0 = broadcast). */
    uint8_t max_connection = 4;                                    /*!< Maximum number of connected stations. */
    uint16_t beacon_interval = 100;                                /*!< Beacon interval in milliseconds. */
    enum cipher_type pairwise_cipher = cipher_type::tkip_ccmp;     /*!< Pairwise cipher type. */
    bool ftm_responder = false;                                    /*!< FTM responder mode enabled. */
    struct pmf_config pmf = {};                                    /*!< PMF configuration. */
    enum sae_pwe_method sae_pwe_h2e = sae_pwe_method::unspecified; /*!< SAE PWE derivation method. */
};

/**
 * @headerfile <idfxx/wifi>
 * @brief Configuration for a WiFi scan operation.
 */
struct scan_config {
    std::string ssid;                             /*!< SSID to filter for (empty = all). */
    uint8_t channel = 0;                          /*!< Channel to scan (0 = all). */
    bool show_hidden = false;                     /*!< Include hidden networks. */
    enum scan_type scan_type = scan_type::active; /*!< Active or passive scan. */
};

/**
 * @headerfile <idfxx/wifi>
 * @brief Default scan timing parameters.
 */
struct scan_default_params {
    uint32_t active_scan_min_ms = 0;      /*!< Minimum active scan time per channel (ms). */
    uint32_t active_scan_max_ms = 120;    /*!< Maximum active scan time per channel (ms). */
    uint32_t passive_scan_ms = 360;       /*!< Passive scan time per channel (ms). */
    uint8_t home_chan_dwell_time_ms = 30; /*!< Home channel dwell time (ms). */
};

/**
 * @headerfile <idfxx/wifi>
 * @brief Protocol configuration for dual-band operation.
 */
struct protocols_config {
    flags<protocol> ghz_2g; /*!< 2.4 GHz protocols. */
    flags<protocol> ghz_5g; /*!< 5 GHz protocols. */
};

/**
 * @headerfile <idfxx/wifi>
 * @brief Bandwidth configuration for dual-band operation.
 */
struct bandwidths_config {
    enum bandwidth ghz_2g; /*!< 2.4 GHz bandwidth. */
    enum bandwidth ghz_5g; /*!< 5 GHz bandwidth. */
};

/**
 * @headerfile <idfxx/wifi>
 * @brief WiFi channel information.
 */
struct channel_info {
    uint8_t primary;            /*!< Primary channel number. */
    enum second_channel second; /*!< Secondary channel position. */
};

/**
 * @headerfile <idfxx/wifi>
 * @brief FTM initiator session configuration.
 */
struct ftm_initiator_config {
    mac_address resp_mac;            /*!< Responder MAC address. */
    uint8_t channel = 0;             /*!< Channel for FTM session. */
    uint8_t frame_count = 0;         /*!< Number of FTM frames per burst. */
    uint16_t burst_period = 0;       /*!< Burst period in 100ms units. */
    bool use_get_report_api = false; /*!< Use ftm_get_report() to retrieve results. */
};

/**
 * @headerfile <idfxx/wifi>
 * @brief CSI (Channel State Information) configuration.
 */
struct csi_config {
    bool lltf_en = true;           /*!< Enable receiving legacy long training field. */
    bool htltf_en = true;          /*!< Enable receiving HT long training field. */
    bool stbc_htltf2_en = true;    /*!< Enable receiving STBC HT-LTF2. */
    bool ltf_merge_en = true;      /*!< Enable LTF merging. */
    bool channel_filter_en = true; /*!< Enable channel filter. */
    bool manu_scale = false;       /*!< Manually scale CSI data. */
    uint8_t shift = 0;             /*!< Manual scale shift value. */
    bool dump_ack_en = false;      /*!< Enable dumping ACK frames. */
};

// =============================================================================
// WiFi events
// =============================================================================

/**
 * @headerfile <idfxx/wifi>
 * @brief WiFi event IDs.
 *
 * Used with the wifi::events event base to register listeners and post events.
 *
 * @code
 * loop.listener_add(idfxx::wifi::sta_connected,
 *     [](const idfxx::wifi::connected_info& info) {
 *         // use info.ssid, info.channel, etc.
 *     });
 * @endcode
 */
enum class event_id : int32_t {
    // clang-format off
    ready                                      = 0,  /*!< WiFi ready. */
    scan_done                                  = 1,  /*!< Scan completed. */
    sta_start                                  = 2,  /*!< Station started. */
    sta_stop                                   = 3,  /*!< Station stopped. */
    sta_connected                              = 4,  /*!< Station connected to AP. */
    sta_disconnected                           = 5,  /*!< Station disconnected from AP. */
    sta_authmode_change                        = 6,  /*!< Station authentication mode changed. */
    sta_wps_er_success                         = 7,  /*!< WPS enrollee success. */
    sta_wps_er_failed                          = 8,  /*!< WPS enrollee failed. */
    sta_wps_er_timeout                         = 9,  /*!< WPS enrollee timeout. */
    sta_wps_er_pin                             = 10, /*!< WPS enrollee PIN code received. */
    sta_wps_er_pbc_overlap                     = 11, /*!< WPS PBC overlap detected. */
    ap_start                                   = 12, /*!< Soft-AP started. */
    ap_stop                                    = 13, /*!< Soft-AP stopped. */
    ap_sta_connected                           = 14, /*!< Station connected to soft-AP. */
    ap_sta_disconnected                        = 15, /*!< Station disconnected from soft-AP. */
    ap_probe_req_received                      = 16, /*!< Soft-AP received probe request. */
    ftm_report                                 = 17, /*!< FTM report received. */
    sta_bss_rssi_low                           = 18, /*!< Station BSS RSSI is below threshold. */
    action_tx_status                           = 19, /*!< Action frame TX status. */
    roc_done                                   = 20, /*!< Remain-on-channel done. */
    sta_beacon_timeout                         = 21, /*!< Station beacon timeout. */
    connectionless_module_wake_interval_start  = 22, /*!< Connectionless module wake interval start. */
    ap_wps_rg_success                          = 23, /*!< AP WPS registrar success. */
    ap_wps_rg_failed                           = 24, /*!< AP WPS registrar failed. */
    ap_wps_rg_timeout                          = 25, /*!< AP WPS registrar timeout. */
    ap_wps_rg_pin                              = 26, /*!< AP WPS registrar PIN received. */
    ap_wps_rg_pbc_overlap                      = 27, /*!< AP WPS PBC overlap detected. */
    itwt_setup                                 = 28, /*!< Individual TWT setup. */
    itwt_teardown                              = 29, /*!< Individual TWT teardown. */
    itwt_probe                                 = 30, /*!< Individual TWT probe. */
    itwt_suspend                               = 31, /*!< Individual TWT suspend. */
    twt_wakeup                                 = 32, /*!< TWT wakeup. */
    btwt_setup                                 = 33, /*!< Broadcast TWT setup. */
    btwt_teardown                              = 34, /*!< Broadcast TWT teardown. */
    nan_started                                = 35, /*!< NAN started. */
    nan_stopped                                = 36, /*!< NAN stopped. */
    nan_svc_match                              = 37, /*!< NAN service match. */
    nan_replied                                = 38, /*!< NAN replied. */
    nan_receive                                = 39, /*!< NAN receive. */
    ndp_indication                             = 40, /*!< NDP indication. */
    ndp_confirm                                = 41, /*!< NDP confirm. */
    ndp_terminated                             = 42, /*!< NDP terminated. */
    home_channel_change                        = 43, /*!< Home channel changed. */
    sta_neighbor_rep                           = 44, /*!< Station neighbor report received. */
    ap_wrong_password                          = 45, /*!< AP detected wrong password from station. */
    sta_beacon_offset_unstable                 = 46, /*!< Station beacon offset unstable. */
    dpp_uri_ready                              = 47, /*!< DPP URI ready. */
    dpp_cfg_recvd                              = 48, /*!< DPP configuration received. */
    dpp_failed                                 = 49, /*!< DPP failed. */
    // clang-format on
};

/**
 * @headerfile <idfxx/wifi>
 * @brief WiFi event base.
 *
 * Event base for WiFi events. Use with the idfxx event loop to register listeners for
 * WiFi lifecycle, connection, and scan events.
 */
extern const event_base<event_id> events;

/** @cond INTERNAL */
inline event_base<event_id> idfxx_get_event_base(event_id*) {
    return events;
}
/** @endcond */

/**
 * @headerfile <idfxx/wifi>
 * @brief IP event IDs relevant to WiFi.
 *
 * Used with the wifi::ip_events event base to register listeners for IP-related events.
 *
 * @code
 * loop.listener_add(idfxx::wifi::sta_got_ip,
 *     [](const idfxx::wifi::got_ip_info& info) {
 *         // use info.ip, info.netmask, etc.
 *     });
 * @endcode
 */
enum class ip_event_id : int32_t {
    // clang-format off
    sta_got_ip         = 0, /*!< Station received IP address from AP. */
    sta_lost_ip        = 1, /*!< Station lost IP address. */
    ap_sta_ip_assigned = 2, /*!< Soft-AP assigned IP to a connected station. */
    got_ip6            = 3, /*!< Received IPv6 address. */
    // clang-format on
};

/**
 * @headerfile <idfxx/wifi>
 * @brief IP event base.
 *
 * Event base for IP events related to WiFi. Use with the idfxx event loop to register
 * listeners for IP address acquisition and loss.
 */
extern const event_base<ip_event_id> ip_events;

/** @cond INTERNAL */
inline event_base<ip_event_id> idfxx_get_event_base(ip_event_id*) {
    return ip_events;
}
/** @endcond */

// =============================================================================
// Event data wrappers
// =============================================================================

/**
 * @headerfile <idfxx/wifi>
 * @brief Information about a successful station connection.
 *
 * Dispatched with the wifi::event_id::sta_connected event.
 */
struct connected_info {
    mac_address bssid;       /*!< MAC address of the connected AP. */
    std::string ssid;        /*!< SSID of the connected AP. */
    uint8_t channel;         /*!< Channel of the connected AP. */
    enum auth_mode authmode; /*!< Authentication mode used. */

    /// @cond INTERNAL
    static connected_info from_opaque(const void* event_data);
    /// @endcond
};

/**
 * @headerfile <idfxx/wifi>
 * @brief Information about a station disconnection.
 *
 * Dispatched with the wifi::event_id::sta_disconnected event.
 */
struct disconnected_info {
    mac_address bssid;             /*!< MAC address of the AP. */
    std::string ssid;              /*!< SSID of the AP. */
    enum disconnect_reason reason; /*!< Reason for disconnection. */

    /// @cond INTERNAL
    static disconnected_info from_opaque(const void* event_data);
    /// @endcond
};

/**
 * @headerfile <idfxx/wifi>
 * @brief Information about an acquired IP address.
 *
 * Dispatched with the wifi::ip_event_id::sta_got_ip event.
 */
struct got_ip_info {
    uint32_t ip;      /*!< IPv4 address (network byte order). */
    uint32_t netmask; /*!< IPv4 netmask (network byte order). */
    uint32_t gateway; /*!< IPv4 gateway (network byte order). */
    bool changed;     /*!< Whether the IP address changed. */

    /// @cond INTERNAL
    static got_ip_info from_opaque(const void* event_data);
    /// @endcond
};

/**
 * @headerfile <idfxx/wifi>
 * @brief Information about a completed scan.
 *
 * Dispatched with the wifi::event_id::scan_done event.
 */
struct scan_done_info {
    uint32_t status; /*!< Scan status (0 = success). */
    uint8_t number;  /*!< Number of APs found. */
    uint8_t scan_id; /*!< Scan ID. */

    /// @cond INTERNAL
    static scan_done_info from_opaque(const void* event_data);
    /// @endcond
};

/**
 * @headerfile <idfxx/wifi>
 * @brief Information about an authentication mode change.
 *
 * Dispatched with the wifi::event_id::sta_authmode_change event.
 */
struct authmode_change_info {
    enum auth_mode old_mode; /*!< Previous authentication mode. */
    enum auth_mode new_mode; /*!< New authentication mode. */

    /// @cond INTERNAL
    static authmode_change_info from_opaque(const void* event_data);
    /// @endcond
};

/**
 * @headerfile <idfxx/wifi>
 * @brief Information about a station connecting to the soft-AP.
 *
 * Dispatched with the wifi::event_id::ap_sta_connected event.
 */
struct ap_sta_connected_info {
    mac_address mac; /*!< MAC address of the connected station. */
    uint8_t aid;     /*!< Association ID. */

    /// @cond INTERNAL
    static ap_sta_connected_info from_opaque(const void* event_data);
    /// @endcond
};

/**
 * @headerfile <idfxx/wifi>
 * @brief Information about a station disconnecting from the soft-AP.
 *
 * Dispatched with the wifi::event_id::ap_sta_disconnected event.
 */
struct ap_sta_disconnected_info {
    mac_address mac;               /*!< MAC address of the disconnected station. */
    uint8_t aid;                   /*!< Association ID. */
    enum disconnect_reason reason; /*!< Reason for disconnection. */

    /// @cond INTERNAL
    static ap_sta_disconnected_info from_opaque(const void* event_data);
    /// @endcond
};

/**
 * @headerfile <idfxx/wifi>
 * @brief Information about a probe request received by the soft-AP.
 *
 * Dispatched with the wifi::event_id::ap_probe_req_received event.
 */
struct ap_probe_req_info {
    int rssi;        /*!< RSSI of the probe request. */
    mac_address mac; /*!< MAC address of the requesting station. */

    /// @cond INTERNAL
    static ap_probe_req_info from_opaque(const void* event_data);
    /// @endcond
};

/**
 * @headerfile <idfxx/wifi>
 * @brief Information about BSS RSSI dropping below threshold.
 *
 * Dispatched with the wifi::event_id::sta_bss_rssi_low event.
 */
struct bss_rssi_low_info {
    int32_t rssi; /*!< Current RSSI value. */

    /// @cond INTERNAL
    static bss_rssi_low_info from_opaque(const void* event_data);
    /// @endcond
};

/**
 * @headerfile <idfxx/wifi>
 * @brief Information about a home channel change.
 *
 * Dispatched with the wifi::event_id::home_channel_change event.
 */
struct home_channel_change_info {
    uint8_t old_primary;            /*!< Previous primary channel. */
    enum second_channel old_second; /*!< Previous secondary channel position. */
    uint8_t new_primary;            /*!< New primary channel. */
    enum second_channel new_second; /*!< New secondary channel position. */

    /// @cond INTERNAL
    static home_channel_change_info from_opaque(const void* event_data);
    /// @endcond
};

/**
 * @headerfile <idfxx/wifi>
 * @brief Information about an FTM report.
 *
 * Dispatched with the wifi::event_id::ftm_report event.
 */
struct ftm_report_info {
    mac_address peer_mac;                  /*!< Peer MAC address. */
    enum ftm_status status;                /*!< FTM session status. */
    uint32_t rtt_raw;                      /*!< Raw round-trip time (picoseconds). */
    uint32_t rtt_est;                      /*!< Estimated round-trip time (picoseconds). */
    uint32_t dist_est;                     /*!< Estimated distance (centimeters). */
    std::vector<ftm_report_entry> entries; /*!< Individual measurement entries. */

    /// @cond INTERNAL
    static ftm_report_info from_opaque(const void* event_data);
    /// @endcond
};

// =============================================================================
// Typed events
// =============================================================================

// WiFi events with data
/** @brief Station connected event with connection details. */
inline constexpr idfxx::event<event_id, connected_info> sta_connected{event_id::sta_connected};
/** @brief Station disconnected event with disconnection details. */
inline constexpr idfxx::event<event_id, disconnected_info> sta_disconnected{event_id::sta_disconnected};
/** @brief Scan completed event with scan results summary. */
inline constexpr idfxx::event<event_id, scan_done_info> scan_done{event_id::scan_done};
/** @brief Station authentication mode changed event. */
inline constexpr idfxx::event<event_id, authmode_change_info> sta_authmode_change{event_id::sta_authmode_change};
/** @brief Station connected to soft-AP event. */
inline constexpr idfxx::event<event_id, ap_sta_connected_info> ap_sta_connected{event_id::ap_sta_connected};
/** @brief Station disconnected from soft-AP event. */
inline constexpr idfxx::event<event_id, ap_sta_disconnected_info> ap_sta_disconnected{event_id::ap_sta_disconnected};
/** @brief Probe request received by soft-AP event. */
inline constexpr idfxx::event<event_id, ap_probe_req_info> ap_probe_req_received{event_id::ap_probe_req_received};
/** @brief BSS RSSI dropped below threshold event. */
inline constexpr idfxx::event<event_id, bss_rssi_low_info> sta_bss_rssi_low{event_id::sta_bss_rssi_low};
/** @brief Home channel changed event. */
inline constexpr idfxx::event<event_id, home_channel_change_info> home_channel_change{event_id::home_channel_change};
/** @brief FTM report received event. */
inline constexpr idfxx::event<event_id, ftm_report_info> ftm_report{event_id::ftm_report};

// WiFi events without data
/** @brief WiFi ready event. */
inline constexpr idfxx::event<event_id> ready{event_id::ready};
/** @brief Station started event. */
inline constexpr idfxx::event<event_id> sta_start{event_id::sta_start};
/** @brief Station stopped event. */
inline constexpr idfxx::event<event_id> sta_stop{event_id::sta_stop};
/** @brief Soft-AP started event. */
inline constexpr idfxx::event<event_id> ap_start{event_id::ap_start};
/** @brief Soft-AP stopped event. */
inline constexpr idfxx::event<event_id> ap_stop{event_id::ap_stop};
/** @brief Station beacon timeout event. */
inline constexpr idfxx::event<event_id> sta_beacon_timeout{event_id::sta_beacon_timeout};
/** @brief WPS enrollee success event. */
inline constexpr idfxx::event<event_id> sta_wps_er_success{event_id::sta_wps_er_success};
/** @brief WPS enrollee failed event. */
inline constexpr idfxx::event<event_id> sta_wps_er_failed{event_id::sta_wps_er_failed};
/** @brief WPS enrollee timeout event. */
inline constexpr idfxx::event<event_id> sta_wps_er_timeout{event_id::sta_wps_er_timeout};
/** @brief WPS enrollee PIN received event. */
inline constexpr idfxx::event<event_id> sta_wps_er_pin{event_id::sta_wps_er_pin};
/** @brief WPS PBC overlap detected event. */
inline constexpr idfxx::event<event_id> sta_wps_er_pbc_overlap{event_id::sta_wps_er_pbc_overlap};
/** @brief Action frame TX status event. */
inline constexpr idfxx::event<event_id> action_tx_status{event_id::action_tx_status};
/** @brief Remain-on-channel done event. */
inline constexpr idfxx::event<event_id> roc_done{event_id::roc_done};
/** @brief Connectionless module wake interval start event. */
inline constexpr idfxx::event<event_id> connectionless_module_wake_interval_start{
    event_id::connectionless_module_wake_interval_start
};
/** @brief AP WPS registrar success event. */
inline constexpr idfxx::event<event_id> ap_wps_rg_success{event_id::ap_wps_rg_success};
/** @brief AP WPS registrar failed event. */
inline constexpr idfxx::event<event_id> ap_wps_rg_failed{event_id::ap_wps_rg_failed};
/** @brief AP WPS registrar timeout event. */
inline constexpr idfxx::event<event_id> ap_wps_rg_timeout{event_id::ap_wps_rg_timeout};
/** @brief AP WPS registrar PIN received event. */
inline constexpr idfxx::event<event_id> ap_wps_rg_pin{event_id::ap_wps_rg_pin};
/** @brief AP WPS PBC overlap detected event. */
inline constexpr idfxx::event<event_id> ap_wps_rg_pbc_overlap{event_id::ap_wps_rg_pbc_overlap};
/** @brief Individual TWT setup event. */
inline constexpr idfxx::event<event_id> itwt_setup{event_id::itwt_setup};
/** @brief Individual TWT teardown event. */
inline constexpr idfxx::event<event_id> itwt_teardown{event_id::itwt_teardown};
/** @brief Individual TWT probe event. */
inline constexpr idfxx::event<event_id> itwt_probe{event_id::itwt_probe};
/** @brief Individual TWT suspend event. */
inline constexpr idfxx::event<event_id> itwt_suspend{event_id::itwt_suspend};
/** @brief TWT wakeup event. */
inline constexpr idfxx::event<event_id> twt_wakeup{event_id::twt_wakeup};
/** @brief Broadcast TWT setup event. */
inline constexpr idfxx::event<event_id> btwt_setup{event_id::btwt_setup};
/** @brief Broadcast TWT teardown event. */
inline constexpr idfxx::event<event_id> btwt_teardown{event_id::btwt_teardown};
/** @brief NAN started event. */
inline constexpr idfxx::event<event_id> nan_started{event_id::nan_started};
/** @brief NAN stopped event. */
inline constexpr idfxx::event<event_id> nan_stopped{event_id::nan_stopped};
/** @brief NAN service match event. */
inline constexpr idfxx::event<event_id> nan_svc_match{event_id::nan_svc_match};
/** @brief NAN replied event. */
inline constexpr idfxx::event<event_id> nan_replied{event_id::nan_replied};
/** @brief NAN receive event. */
inline constexpr idfxx::event<event_id> nan_receive{event_id::nan_receive};
/** @brief NDP indication event. */
inline constexpr idfxx::event<event_id> ndp_indication{event_id::ndp_indication};
/** @brief NDP confirm event. */
inline constexpr idfxx::event<event_id> ndp_confirm{event_id::ndp_confirm};
/** @brief NDP terminated event. */
inline constexpr idfxx::event<event_id> ndp_terminated{event_id::ndp_terminated};
/** @brief Station neighbor report received event. */
inline constexpr idfxx::event<event_id> sta_neighbor_rep{event_id::sta_neighbor_rep};
/** @brief AP detected wrong password from station event. */
inline constexpr idfxx::event<event_id> ap_wrong_password{event_id::ap_wrong_password};
/** @brief Station beacon offset unstable event. */
inline constexpr idfxx::event<event_id> sta_beacon_offset_unstable{event_id::sta_beacon_offset_unstable};
/** @brief DPP URI ready event. */
inline constexpr idfxx::event<event_id> dpp_uri_ready{event_id::dpp_uri_ready};
/** @brief DPP configuration received event. */
inline constexpr idfxx::event<event_id> dpp_cfg_recvd{event_id::dpp_cfg_recvd};
/** @brief DPP failed event. */
inline constexpr idfxx::event<event_id> dpp_failed{event_id::dpp_failed};

// IP events
/** @brief Station received IP address event with IP details. */
inline constexpr idfxx::event<ip_event_id, got_ip_info> sta_got_ip{ip_event_id::sta_got_ip};
/** @brief Station lost IP address event. */
inline constexpr idfxx::event<ip_event_id> sta_lost_ip{ip_event_id::sta_lost_ip};
/** @brief Soft-AP assigned IP to a connected station event. */
inline constexpr idfxx::event<ip_event_id> ap_sta_ip_assigned{ip_event_id::ap_sta_ip_assigned};
/** @brief Received IPv6 address event. */
inline constexpr idfxx::event<ip_event_id> got_ip6{ip_event_id::got_ip6};

// =============================================================================
// Free function API — Lifecycle
// =============================================================================

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
/**
 * @brief Initializes the WiFi subsystem.
 *
 * Must be called before any other WiFi functions. When called with no
 * arguments or an empty config, all Kconfig defaults are used.
 *
 * @param cfg Initialization configuration. Defaults to Kconfig values.
 *
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
 * @throws std::system_error on failure.
 */
void init(const init_config& cfg = {});

/**
 * @brief Deinitializes the WiFi subsystem and frees resources.
 *
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
 * @throws std::system_error on failure.
 */
void deinit();

/**
 * @brief Starts the WiFi subsystem.
 *
 * After starting, configured interfaces become active. A mode must be
 * set before starting.
 *
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
 * @throws std::system_error on failure.
 */
void start();

/**
 * @brief Stops the WiFi subsystem.
 *
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
 * @throws std::system_error on failure.
 */
void stop();

/**
 * @brief Restores WiFi stack persistent settings to defaults.
 *
 * Resets configuration stored in NVS to factory defaults.
 *
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
 * @throws std::system_error on failure.
 */
void restore();
#endif

/**
 * @brief Initializes the WiFi subsystem.
 *
 * Must be called before any other WiFi functions. When called with no
 * arguments or an empty config, all Kconfig defaults are used.
 *
 * @param cfg Initialization configuration. Defaults to Kconfig values.
 * @return Success, or an error.
 */
[[nodiscard]] result<void> try_init(const init_config& cfg = {});

/**
 * @brief Deinitializes the WiFi subsystem and frees resources.
 *
 * @return Success, or an error.
 */
result<void> try_deinit();

/**
 * @brief Starts the WiFi subsystem.
 *
 * After starting, configured interfaces become active. A mode must be
 * set before starting.
 *
 * @return Success, or an error.
 */
[[nodiscard]] result<void> try_start();

/**
 * @brief Stops the WiFi subsystem.
 *
 * @return Success, or an error.
 */
result<void> try_stop();

/**
 * @brief Restores WiFi stack persistent settings to defaults.
 *
 * Resets configuration stored in NVS to factory defaults.
 *
 * @return Success, or an error.
 */
[[nodiscard]] result<void> try_restore();

// =============================================================================
// Free function API — Mode
// =============================================================================

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
/**
 * @brief Sets the WiFi operating mode.
 *
 * @param m The operating mode to set.
 *
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
 * @throws std::system_error on failure.
 */
void set_mode(enum mode m);

/**
 * @brief Gets the current WiFi operating mode.
 *
 * @return The current operating mode.
 *
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
 * @throws std::system_error on failure.
 */
[[nodiscard]] enum mode get_mode();
#endif

/**
 * @brief Sets the WiFi operating mode.
 *
 * @param m The operating mode to set.
 * @return Success, or an error.
 */
result<void> try_set_mode(enum mode m);

/**
 * @brief Gets the current WiFi operating mode.
 *
 * @return The current operating mode, or an error.
 */
[[nodiscard]] result<enum mode> try_get_mode();

// =============================================================================
// Free function API — STA config
// =============================================================================

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
/**
 * @brief Sets the WiFi station configuration.
 *
 * @param cfg The station configuration.
 *
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
 * @throws std::system_error on failure.
 */
void set_sta_config(const sta_config& cfg);

/**
 * @brief Gets the current WiFi station configuration.
 *
 * @return The current station configuration.
 *
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
 * @throws std::system_error on failure.
 */
[[nodiscard]] sta_config get_sta_config();
#endif

/**
 * @brief Sets the WiFi station configuration.
 *
 * @param cfg The station configuration.
 * @return Success, or an error.
 */
[[nodiscard]] result<void> try_set_sta_config(const sta_config& cfg);

/**
 * @brief Gets the current WiFi station configuration.
 *
 * @return The current station configuration, or an error.
 */
[[nodiscard]] result<sta_config> try_get_sta_config();

// =============================================================================
// Free function API — AP config
// =============================================================================

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
/**
 * @brief Sets the WiFi access point configuration.
 *
 * @param cfg The access point configuration.
 *
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
 * @throws std::system_error on failure.
 */
void set_ap_config(const ap_config& cfg);

/**
 * @brief Gets the current WiFi access point configuration.
 *
 * @return The current access point configuration.
 *
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
 * @throws std::system_error on failure.
 */
[[nodiscard]] ap_config get_ap_config();

/**
 * @brief Deauthenticates a station from the soft-AP.
 *
 * @param aid Association ID of the station to deauthenticate (0 = all).
 *
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
 * @throws std::system_error on failure.
 */
void deauth_sta(uint16_t aid);

/**
 * @brief Gets the list of stations connected to the soft-AP.
 *
 * @return A vector of connected station information.
 *
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
 * @throws std::system_error on failure.
 */
[[nodiscard]] std::vector<sta_info> get_sta_list();

/**
 * @brief Gets the association ID for a station connected to the soft-AP.
 *
 * @param mac MAC address of the station.
 * @return The association ID.
 *
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
 * @throws std::system_error on failure.
 */
[[nodiscard]] uint16_t ap_get_sta_aid(mac_address mac);
#endif

/**
 * @brief Sets the WiFi access point configuration.
 *
 * @param cfg The access point configuration.
 * @return Success, or an error.
 */
[[nodiscard]] result<void> try_set_ap_config(const ap_config& cfg);

/**
 * @brief Gets the current WiFi access point configuration.
 *
 * @return The current access point configuration, or an error.
 */
[[nodiscard]] result<ap_config> try_get_ap_config();

/**
 * @brief Deauthenticates a station from the soft-AP.
 *
 * @param aid Association ID of the station to deauthenticate (0 = all).
 * @return Success, or an error.
 */
result<void> try_deauth_sta(uint16_t aid);

/**
 * @brief Gets the list of stations connected to the soft-AP.
 *
 * @return A vector of connected station information, or an error.
 */
[[nodiscard]] result<std::vector<sta_info>> try_get_sta_list();

/**
 * @brief Gets the association ID for a station connected to the soft-AP.
 *
 * @param mac MAC address of the station.
 * @return The association ID, or an error.
 */
[[nodiscard]] result<uint16_t> try_ap_get_sta_aid(mac_address mac);

// =============================================================================
// Free function API — Connect / disconnect
// =============================================================================

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
/**
 * @brief Connects to the configured access point.
 *
 * The WiFi subsystem must be started before connecting. Connection progress
 * is reported via WiFi and IP events.
 *
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
 * @throws std::system_error on failure.
 */
void connect();

/**
 * @brief Disconnects from the current access point.
 *
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
 * @throws std::system_error on failure.
 */
void disconnect();

/**
 * @brief Clears the fast-connect data stored in memory.
 *
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
 * @throws std::system_error on failure.
 */
void clear_fast_connect();

/**
 * @brief Gets the association ID assigned by the AP in station mode.
 *
 * @return The association ID.
 *
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
 * @throws std::system_error on failure.
 */
[[nodiscard]] uint16_t sta_get_aid();

/**
 * @brief Gets the PHY mode negotiated with the connected AP.
 *
 * @return The negotiated PHY mode.
 *
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
 * @throws std::system_error on failure.
 */
[[nodiscard]] enum phy_mode get_negotiated_phymode();
#endif

/**
 * @brief Connects to the configured access point.
 *
 * The WiFi subsystem must be started before connecting. Connection progress
 * is reported via WiFi and IP events.
 *
 * @return Success, or an error.
 */
result<void> try_connect();

/**
 * @brief Disconnects from the current access point.
 *
 * @return Success, or an error.
 */
result<void> try_disconnect();

/**
 * @brief Clears the fast-connect data stored in memory.
 *
 * @return Success, or an error.
 */
result<void> try_clear_fast_connect();

/**
 * @brief Gets the association ID assigned by the AP in station mode.
 *
 * @return The association ID, or an error.
 */
[[nodiscard]] result<uint16_t> try_sta_get_aid();

/**
 * @brief Gets the PHY mode negotiated with the connected AP.
 *
 * @return The negotiated PHY mode, or an error.
 */
[[nodiscard]] result<enum phy_mode> try_get_negotiated_phymode();

// =============================================================================
// Free function API — Scanning
// =============================================================================

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
/**
 * @brief Performs a blocking scan for access points.
 *
 * Starts a scan and waits for it to complete before returning results.
 *
 * @param cfg Scan configuration. Defaults to scanning all channels.
 * @return A vector of discovered access points sorted by RSSI.
 *
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
 * @throws std::system_error on failure.
 */
[[nodiscard]] std::vector<ap_record> scan(const scan_config& cfg = {});

/**
 * @brief Starts a non-blocking scan for access points.
 *
 * Initiates a scan that runs asynchronously. Listen for the
 * wifi::event_id::scan_done event, then call scan_get_results() to
 * retrieve the results.
 *
 * @param cfg Scan configuration. Defaults to scanning all channels.
 *
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
 * @throws std::system_error on failure.
 */
void scan_start(const scan_config& cfg = {});

/**
 * @brief Retrieves results from a completed scan.
 *
 * Call this after receiving the wifi::event_id::scan_done event.
 *
 * @return A vector of discovered access points.
 *
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
 * @throws std::system_error on failure.
 */
[[nodiscard]] std::vector<ap_record> scan_get_results();

/**
 * @brief Stops an in-progress scan.
 *
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
 * @throws std::system_error on failure.
 */
void scan_stop();

/**
 * @brief Gets the number of APs found in the last scan.
 *
 * @return The number of APs found.
 *
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
 * @throws std::system_error on failure.
 */
[[nodiscard]] uint16_t scan_get_ap_num();

/**
 * @brief Clears the AP list stored from a previous scan.
 *
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
 * @throws std::system_error on failure.
 */
void clear_ap_list();

/**
 * @brief Sets the default scan timing parameters.
 *
 * @param params The scan timing parameters.
 *
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
 * @throws std::system_error on failure.
 */
void set_scan_parameters(const scan_default_params& params);

/**
 * @brief Gets the current default scan timing parameters.
 *
 * @return The current scan timing parameters.
 *
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
 * @throws std::system_error on failure.
 */
[[nodiscard]] scan_default_params get_scan_parameters();
#endif

/**
 * @brief Performs a blocking scan for access points.
 *
 * Starts a scan and waits for it to complete before returning results.
 *
 * @param cfg Scan configuration. Defaults to scanning all channels.
 * @return A vector of discovered access points sorted by RSSI, or an error.
 */
[[nodiscard]] result<std::vector<ap_record>> try_scan(const scan_config& cfg = {});

/**
 * @brief Starts a non-blocking scan for access points.
 *
 * Initiates a scan that runs asynchronously. Listen for the
 * wifi::event_id::scan_done event, then call try_scan_get_results() to
 * retrieve the results.
 *
 * @param cfg Scan configuration. Defaults to scanning all channels.
 * @return Success, or an error.
 */
[[nodiscard]] result<void> try_scan_start(const scan_config& cfg = {});

/**
 * @brief Retrieves results from a completed scan.
 *
 * Call this after receiving the wifi::event_id::scan_done event.
 *
 * @return A vector of discovered access points, or an error.
 */
[[nodiscard]] result<std::vector<ap_record>> try_scan_get_results();

/**
 * @brief Stops an in-progress scan.
 *
 * @return Success, or an error.
 */
result<void> try_scan_stop();

/**
 * @brief Gets the number of APs found in the last scan.
 *
 * @return The number of APs found, or an error.
 */
[[nodiscard]] result<uint16_t> try_scan_get_ap_num();

/**
 * @brief Clears the AP list stored from a previous scan.
 *
 * @return Success, or an error.
 */
result<void> try_clear_ap_list();

/**
 * @brief Sets the default scan timing parameters.
 *
 * @param params The scan timing parameters.
 * @return Success, or an error.
 */
[[nodiscard]] result<void> try_set_scan_parameters(const scan_default_params& params);

/**
 * @brief Gets the current default scan timing parameters.
 *
 * @return The current scan timing parameters, or an error.
 */
[[nodiscard]] result<scan_default_params> try_get_scan_parameters();

// =============================================================================
// Free function API — Power save
// =============================================================================

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
/**
 * @brief Sets the power save mode.
 *
 * @param ps The power save mode.
 *
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
 * @throws std::system_error on failure.
 */
void set_power_save(enum power_save ps);

/**
 * @brief Gets the current power save mode.
 *
 * @return The current power save mode.
 *
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
 * @throws std::system_error on failure.
 */
[[nodiscard]] enum power_save get_power_save();
#endif

/**
 * @brief Sets the power save mode.
 *
 * @param ps The power save mode.
 * @return Success, or an error.
 */
result<void> try_set_power_save(enum power_save ps);

/**
 * @brief Gets the current power save mode.
 *
 * @return The current power save mode, or an error.
 */
[[nodiscard]] result<enum power_save> try_get_power_save();

// =============================================================================
// Free function API — Bandwidth
// =============================================================================

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
/**
 * @brief Sets the channel bandwidth for the specified interface.
 *
 * @param iface The WiFi interface.
 * @param bw The bandwidth.
 *
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
 * @throws std::system_error on failure.
 */
void set_bandwidth(enum interface iface, enum bandwidth bw);

/**
 * @brief Gets the current channel bandwidth for the specified interface.
 *
 * @param iface The WiFi interface.
 * @return The current bandwidth.
 *
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
 * @throws std::system_error on failure.
 */
[[nodiscard]] enum bandwidth get_bandwidth(enum interface iface);

/**
 * @brief Sets bandwidths for dual-band operation on the specified interface.
 *
 * @param iface The WiFi interface.
 * @param bw The bandwidth configuration for both bands.
 *
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
 * @throws std::system_error on failure.
 */
void set_bandwidths(enum interface iface, const bandwidths_config& bw);

/**
 * @brief Gets bandwidths for dual-band operation on the specified interface.
 *
 * @param iface The WiFi interface.
 * @return The bandwidth configuration for both bands.
 *
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
 * @throws std::system_error on failure.
 */
[[nodiscard]] bandwidths_config get_bandwidths(enum interface iface);
#endif

/**
 * @brief Sets the channel bandwidth for the specified interface.
 *
 * @param iface The WiFi interface.
 * @param bw The bandwidth.
 * @return Success, or an error.
 */
result<void> try_set_bandwidth(enum interface iface, enum bandwidth bw);

/**
 * @brief Gets the current channel bandwidth for the specified interface.
 *
 * @param iface The WiFi interface.
 * @return The current bandwidth, or an error.
 */
[[nodiscard]] result<enum bandwidth> try_get_bandwidth(enum interface iface);

/**
 * @brief Sets bandwidths for dual-band operation on the specified interface.
 *
 * @param iface The WiFi interface.
 * @param bw The bandwidth configuration for both bands.
 * @return Success, or an error.
 */
[[nodiscard]] result<void> try_set_bandwidths(enum interface iface, const bandwidths_config& bw);

/**
 * @brief Gets bandwidths for dual-band operation on the specified interface.
 *
 * @param iface The WiFi interface.
 * @return The bandwidth configuration for both bands, or an error.
 */
[[nodiscard]] result<bandwidths_config> try_get_bandwidths(enum interface iface);

// =============================================================================
// Free function API — MAC
// =============================================================================

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
/**
 * @brief Sets the MAC address for the specified interface.
 *
 * @param iface The WiFi interface.
 * @param mac The MAC address as a 6-byte array.
 *
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
 * @throws std::system_error on failure.
 */
void set_mac(enum interface iface, mac_address mac);

/**
 * @brief Gets the MAC address for the specified interface.
 *
 * @param iface The WiFi interface.
 * @return The MAC address.
 *
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
 * @throws std::system_error on failure.
 */
[[nodiscard]] mac_address get_mac(enum interface iface);
#endif

/**
 * @brief Sets the MAC address for the specified interface.
 *
 * @param iface The WiFi interface.
 * @param mac The MAC address.
 * @return Success, or an error.
 */
[[nodiscard]] result<void> try_set_mac(enum interface iface, mac_address mac);

/**
 * @brief Gets the MAC address for the specified interface.
 *
 * @param iface The WiFi interface.
 * @return The MAC address, or an error.
 */
[[nodiscard]] result<mac_address> try_get_mac(enum interface iface);

// =============================================================================
// Free function API — AP info
// =============================================================================

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
/**
 * @brief Gets information about the currently connected access point.
 *
 * @return The AP information.
 *
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
 * @throws std::system_error on failure.
 */
[[nodiscard]] ap_record get_ap_info();
#endif

/**
 * @brief Gets information about the currently connected access point.
 *
 * @return The AP information, or an error.
 */
[[nodiscard]] result<ap_record> try_get_ap_info();

// =============================================================================
// Free function API — Channel
// =============================================================================

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
/**
 * @brief Sets the primary and secondary channel.
 *
 * @param primary The primary channel number.
 * @param second The secondary channel position.
 *
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
 * @throws std::system_error on failure.
 */
void set_channel(uint8_t primary, enum second_channel second);

/**
 * @brief Gets the current primary and secondary channel.
 *
 * @return The channel information.
 *
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
 * @throws std::system_error on failure.
 */
[[nodiscard]] channel_info get_channel();
#endif

/**
 * @brief Sets the primary and secondary channel.
 *
 * @param primary The primary channel number.
 * @param second The secondary channel position.
 * @return Success, or an error.
 */
[[nodiscard]] result<void> try_set_channel(uint8_t primary, enum second_channel second);

/**
 * @brief Gets the current primary and secondary channel.
 *
 * @return The channel information, or an error.
 */
[[nodiscard]] result<channel_info> try_get_channel();

// =============================================================================
// Free function API — Country
// =============================================================================

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
/**
 * @brief Sets the WiFi country configuration.
 *
 * @param cfg The country configuration.
 *
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
 * @throws std::system_error on failure.
 */
void set_country(const country_config& cfg);

/**
 * @brief Gets the current WiFi country configuration.
 *
 * @return The country configuration.
 *
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
 * @throws std::system_error on failure.
 */
[[nodiscard]] country_config get_country();

/**
 * @brief Sets the country code.
 *
 * @param cc Two-character country code string.
 * @param ieee80211d_enabled Whether to enable IEEE 802.11d.
 *
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
 * @throws std::system_error on failure.
 */
void set_country_code(std::string_view cc, bool ieee80211d_enabled);

/**
 * @brief Gets the current country code.
 *
 * @return The current country code string.
 *
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
 * @throws std::system_error on failure.
 */
[[nodiscard]] std::string get_country_code();
#endif

/**
 * @brief Sets the WiFi country configuration.
 *
 * @param cfg The country configuration.
 * @return Success, or an error.
 */
[[nodiscard]] result<void> try_set_country(const country_config& cfg);

/**
 * @brief Gets the current WiFi country configuration.
 *
 * @return The country configuration, or an error.
 */
[[nodiscard]] result<country_config> try_get_country();

/**
 * @brief Sets the country code.
 *
 * @param cc Two-character country code string.
 * @param ieee80211d_enabled Whether to enable IEEE 802.11d.
 * @return Success, or an error.
 */
[[nodiscard]] result<void> try_set_country_code(std::string_view cc, bool ieee80211d_enabled);

/**
 * @brief Gets the current country code.
 *
 * @return The current country code string, or an error.
 */
[[nodiscard]] result<std::string> try_get_country_code();

// =============================================================================
// Free function API — TX power
// =============================================================================

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
/**
 * @brief Sets the maximum transmit power.
 *
 * @param power Maximum TX power in 0.25 dBm units (range: [8, 84]).
 *
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
 * @throws std::system_error on failure.
 */
void set_max_tx_power(int8_t power);

/**
 * @brief Gets the current maximum transmit power.
 *
 * @return The maximum TX power in 0.25 dBm units.
 *
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
 * @throws std::system_error on failure.
 */
[[nodiscard]] int8_t get_max_tx_power();
#endif

/**
 * @brief Sets the maximum transmit power.
 *
 * @param power Maximum TX power in 0.25 dBm units (range: [8, 84]).
 * @return Success, or an error.
 */
[[nodiscard]] result<void> try_set_max_tx_power(int8_t power);

/**
 * @brief Gets the current maximum transmit power.
 *
 * @return The maximum TX power in 0.25 dBm units, or an error.
 */
[[nodiscard]] result<int8_t> try_get_max_tx_power();

// =============================================================================
// Free function API — RSSI
// =============================================================================

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
/**
 * @brief Sets the RSSI threshold for the sta_bss_rssi_low event.
 *
 * @param rssi The RSSI threshold value (dBm).
 *
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
 * @throws std::system_error on failure.
 */
void set_rssi_threshold(int32_t rssi);

/**
 * @brief Gets the current RSSI of the connected AP.
 *
 * @return The RSSI value (dBm).
 *
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
 * @throws std::system_error on failure.
 */
[[nodiscard]] int get_rssi();
#endif

/**
 * @brief Sets the RSSI threshold for the sta_bss_rssi_low event.
 *
 * @param rssi The RSSI threshold value (dBm).
 * @return Success, or an error.
 */
[[nodiscard]] result<void> try_set_rssi_threshold(int32_t rssi);

/**
 * @brief Gets the current RSSI of the connected AP.
 *
 * @return The RSSI value (dBm), or an error.
 */
[[nodiscard]] result<int> try_get_rssi();

// =============================================================================
// Free function API — Protocol
// =============================================================================

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
/**
 * @brief Sets the enabled WiFi protocols for the specified interface.
 *
 * @param iface The WiFi interface.
 * @param protos The protocol flags to enable.
 *
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
 * @throws std::system_error on failure.
 */
void set_protocol(enum interface iface, flags<protocol> protos);

/**
 * @brief Gets the enabled WiFi protocols for the specified interface.
 *
 * @param iface The WiFi interface.
 * @return The enabled protocol flags.
 *
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
 * @throws std::system_error on failure.
 */
[[nodiscard]] flags<protocol> get_protocol(enum interface iface);

/**
 * @brief Sets the enabled WiFi protocols for dual-band operation.
 *
 * @param iface The WiFi interface.
 * @param cfg The protocol configuration for both bands.
 *
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
 * @throws std::system_error on failure.
 */
void set_protocols(enum interface iface, const protocols_config& cfg);

/**
 * @brief Gets the enabled WiFi protocols for dual-band operation.
 *
 * @param iface The WiFi interface.
 * @return The protocol configuration for both bands.
 *
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
 * @throws std::system_error on failure.
 */
[[nodiscard]] protocols_config get_protocols(enum interface iface);
#endif

/**
 * @brief Sets the enabled WiFi protocols for the specified interface.
 *
 * @param iface The WiFi interface.
 * @param protos The protocol flags to enable.
 * @return Success, or an error.
 */
[[nodiscard]] result<void> try_set_protocol(enum interface iface, flags<protocol> protos);

/**
 * @brief Gets the enabled WiFi protocols for the specified interface.
 *
 * @param iface The WiFi interface.
 * @return The enabled protocol flags, or an error.
 */
[[nodiscard]] result<flags<protocol>> try_get_protocol(enum interface iface);

/**
 * @brief Sets the enabled WiFi protocols for dual-band operation.
 *
 * @param iface The WiFi interface.
 * @param cfg The protocol configuration for both bands.
 * @return Success, or an error.
 */
[[nodiscard]] result<void> try_set_protocols(enum interface iface, const protocols_config& cfg);

/**
 * @brief Gets the enabled WiFi protocols for dual-band operation.
 *
 * @param iface The WiFi interface.
 * @return The protocol configuration for both bands, or an error.
 */
[[nodiscard]] result<protocols_config> try_get_protocols(enum interface iface);

// =============================================================================
// Free function API — Band
// =============================================================================

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
/**
 * @brief Sets the WiFi band.
 *
 * @param b The frequency band.
 *
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
 * @throws std::system_error on failure.
 */
void set_band(enum band b);

/**
 * @brief Gets the current WiFi band.
 *
 * @return The current frequency band.
 *
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
 * @throws std::system_error on failure.
 */
[[nodiscard]] enum band get_band();

/**
 * @brief Sets the WiFi band mode.
 *
 * @param m The band mode.
 *
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
 * @throws std::system_error on failure.
 */
void set_band_mode(enum band_mode m);

/**
 * @brief Gets the current WiFi band mode.
 *
 * @return The current band mode.
 *
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
 * @throws std::system_error on failure.
 */
[[nodiscard]] enum band_mode get_band_mode();
#endif

/**
 * @brief Sets the WiFi band.
 *
 * @param b The frequency band.
 * @return Success, or an error.
 */
result<void> try_set_band(enum band b);

/**
 * @brief Gets the current WiFi band.
 *
 * @return The current frequency band, or an error.
 */
[[nodiscard]] result<enum band> try_get_band();

/**
 * @brief Sets the WiFi band mode.
 *
 * @param m The band mode.
 * @return Success, or an error.
 */
result<void> try_set_band_mode(enum band_mode m);

/**
 * @brief Gets the current WiFi band mode.
 *
 * @return The current band mode, or an error.
 */
[[nodiscard]] result<enum band_mode> try_get_band_mode();

// =============================================================================
// Free function API — Storage
// =============================================================================

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
/**
 * @brief Sets the WiFi configuration storage location.
 *
 * @param s The storage location (flash or RAM).
 *
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
 * @throws std::system_error on failure.
 */
void set_storage(enum storage s);
#endif

/**
 * @brief Sets the WiFi configuration storage location.
 *
 * @param s The storage location (flash or RAM).
 * @return Success, or an error.
 */
result<void> try_set_storage(enum storage s);

// =============================================================================
// Free function API — Inactive time
// =============================================================================

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
/**
 * @brief Sets the inactive time before a station is deauthenticated.
 *
 * @param iface The WiFi interface.
 * @param sec Inactive time in seconds.
 *
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
 * @throws std::system_error on failure.
 */
void set_inactive_time(enum interface iface, uint16_t sec);

/**
 * @brief Gets the inactive time before a station is deauthenticated.
 *
 * @param iface The WiFi interface.
 * @return The inactive time in seconds.
 *
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
 * @throws std::system_error on failure.
 */
[[nodiscard]] uint16_t get_inactive_time(enum interface iface);
#endif

/**
 * @brief Sets the inactive time before a station is deauthenticated.
 *
 * @param iface The WiFi interface.
 * @param sec Inactive time in seconds.
 * @return Success, or an error.
 */
[[nodiscard]] result<void> try_set_inactive_time(enum interface iface, uint16_t sec);

/**
 * @brief Gets the inactive time before a station is deauthenticated.
 *
 * @param iface The WiFi interface.
 * @return The inactive time in seconds, or an error.
 */
[[nodiscard]] result<uint16_t> try_get_inactive_time(enum interface iface);

// =============================================================================
// Free function API — Event mask
// =============================================================================

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
/**
 * @brief Sets the WiFi event mask.
 *
 * Controls which events are sent to the event loop.
 *
 * @param mask Event mask flags indicating which events to suppress.
 *
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
 * @throws std::system_error on failure.
 */
void set_event_mask(flags<event_mask> mask);

/**
 * @brief Gets the current WiFi event mask.
 *
 * @return The current event mask.
 *
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
 * @throws std::system_error on failure.
 */
[[nodiscard]] flags<event_mask> get_event_mask();
#endif

/**
 * @brief Sets the WiFi event mask.
 *
 * Controls which events are sent to the event loop.
 *
 * @param mask Event mask flags indicating which events to suppress.
 * @return Success, or an error.
 */
result<void> try_set_event_mask(flags<event_mask> mask);

/**
 * @brief Gets the current WiFi event mask.
 *
 * @return The current event mask, or an error.
 */
[[nodiscard]] result<flags<event_mask>> try_get_event_mask();

// =============================================================================
// Free function API — Force wakeup
// =============================================================================

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
/**
 * @brief Acquires a WiFi wakeup lock.
 *
 * Prevents the WiFi modem from entering sleep while the lock is held.
 *
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
 * @throws std::system_error on failure.
 */
void force_wakeup_acquire();

/**
 * @brief Releases a WiFi wakeup lock.
 *
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
 * @throws std::system_error on failure.
 */
void force_wakeup_release();
#endif

/**
 * @brief Acquires a WiFi wakeup lock.
 *
 * Prevents the WiFi modem from entering sleep while the lock is held.
 *
 * @return Success, or an error.
 */
result<void> try_force_wakeup_acquire();

/**
 * @brief Releases a WiFi wakeup lock.
 *
 * @return Success, or an error.
 */
result<void> try_force_wakeup_release();

// =============================================================================
// Free function API — Promiscuous mode
// =============================================================================

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
/**
 * @brief Enables or disables promiscuous mode.
 *
 * @param en True to enable, false to disable.
 *
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
 * @throws std::system_error on failure.
 */
void set_promiscuous(bool en);

/**
 * @brief Gets whether promiscuous mode is enabled.
 *
 * @return True if promiscuous mode is enabled.
 *
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
 * @throws std::system_error on failure.
 */
[[nodiscard]] bool get_promiscuous();

/**
 * @brief Sets the promiscuous mode receive callback.
 *
 * The callback receives a buffer pointer and a packet type integer
 * (cast to promiscuous_pkt_type). See ESP-IDF documentation for buffer format.
 *
 * @param cb Callback function pointer, or nullptr to clear.
 *
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
 * @throws std::system_error on failure.
 */
void set_promiscuous_rx_cb(void (*cb)(void*, int));

/**
 * @brief Sets the promiscuous mode packet type filter.
 *
 * @param filter The filter flags to apply.
 *
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
 * @throws std::system_error on failure.
 */
void set_promiscuous_filter(flags<promiscuous_filter> filter);

/**
 * @brief Gets the current promiscuous mode packet type filter.
 *
 * @return The current filter flags.
 *
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
 * @throws std::system_error on failure.
 */
[[nodiscard]] flags<promiscuous_filter> get_promiscuous_filter();

/**
 * @brief Sets the promiscuous mode control frame sub-type filter.
 *
 * @param filter The control frame filter flags to apply.
 *
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
 * @throws std::system_error on failure.
 */
void set_promiscuous_ctrl_filter(flags<promiscuous_ctrl_filter> filter);

/**
 * @brief Gets the current promiscuous mode control frame sub-type filter.
 *
 * @return The current control frame filter flags.
 *
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
 * @throws std::system_error on failure.
 */
[[nodiscard]] flags<promiscuous_ctrl_filter> get_promiscuous_ctrl_filter();
#endif

/**
 * @brief Enables or disables promiscuous mode.
 *
 * @param en True to enable, false to disable.
 * @return Success, or an error.
 */
result<void> try_set_promiscuous(bool en);

/**
 * @brief Gets whether promiscuous mode is enabled.
 *
 * @return True if promiscuous mode is enabled, or an error.
 */
[[nodiscard]] result<bool> try_get_promiscuous();

/**
 * @brief Sets the promiscuous mode receive callback.
 *
 * The callback receives a buffer pointer and a packet type integer
 * (cast to promiscuous_pkt_type). See ESP-IDF documentation for buffer format.
 *
 * @param cb Callback function pointer, or nullptr to clear.
 * @return Success, or an error.
 */
result<void> try_set_promiscuous_rx_cb(void (*cb)(void*, int));

/**
 * @brief Sets the promiscuous mode packet type filter.
 *
 * @param filter The filter flags to apply.
 * @return Success, or an error.
 */
result<void> try_set_promiscuous_filter(flags<promiscuous_filter> filter);

/**
 * @brief Gets the current promiscuous mode packet type filter.
 *
 * @return The current filter flags, or an error.
 */
[[nodiscard]] result<flags<promiscuous_filter>> try_get_promiscuous_filter();

/**
 * @brief Sets the promiscuous mode control frame sub-type filter.
 *
 * @param filter The control frame filter flags to apply.
 * @return Success, or an error.
 */
result<void> try_set_promiscuous_ctrl_filter(flags<promiscuous_ctrl_filter> filter);

/**
 * @brief Gets the current promiscuous mode control frame sub-type filter.
 *
 * @return The current control frame filter flags, or an error.
 */
[[nodiscard]] result<flags<promiscuous_ctrl_filter>> try_get_promiscuous_ctrl_filter();

// =============================================================================
// Free function API — Raw 802.11
// =============================================================================

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
/**
 * @brief Transmits a raw 802.11 frame.
 *
 * @param iface The WiFi interface to transmit on.
 * @param buffer The raw frame data.
 * @param en_sys_seq Whether to use system-managed sequence numbers.
 *
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
 * @throws std::system_error on failure.
 */
void tx_80211(enum interface iface, std::span<const uint8_t> buffer, bool en_sys_seq);

/**
 * @brief Registers a callback for 802.11 TX completion.
 *
 * The callback receives a pointer to TX info (cast from esp_wifi_80211_tx_info_t).
 *
 * @param cb Callback function pointer.
 *
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
 * @throws std::system_error on failure.
 */
void register_80211_tx_cb(void (*cb)(const void*));
#endif

/**
 * @brief Transmits a raw 802.11 frame.
 *
 * @param iface The WiFi interface to transmit on.
 * @param buffer The raw frame data.
 * @param en_sys_seq Whether to use system-managed sequence numbers.
 * @return Success, or an error.
 */
[[nodiscard]] result<void> try_tx_80211(enum interface iface, std::span<const uint8_t> buffer, bool en_sys_seq);

/**
 * @brief Registers a callback for 802.11 TX completion.
 *
 * The callback receives a pointer to TX info (cast from esp_wifi_80211_tx_info_t).
 *
 * @param cb Callback function pointer.
 * @return Success, or an error.
 */
result<void> try_register_80211_tx_cb(void (*cb)(const void*));

// =============================================================================
// Free function API — Vendor IE
// =============================================================================

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
/**
 * @brief Sets a vendor-specific information element.
 *
 * @param enable Whether to enable or disable the vendor IE.
 * @param type The frame type for the vendor IE.
 * @param id The vendor IE index.
 * @param vnd_ie Pointer to the vendor IE data.
 *
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
 * @throws std::system_error on failure.
 */
void set_vendor_ie(bool enable, enum vendor_ie_type type, enum vendor_ie_id id, const void* vnd_ie);

/**
 * @brief Registers a callback for received vendor-specific IEs.
 *
 * @param cb Callback function: (ctx, type, source_mac, vendor_ie_data, rssi).
 * @param ctx User context pointer passed to the callback.
 *
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
 * @throws std::system_error on failure.
 */
void set_vendor_ie_cb(void (*cb)(void*, int, const uint8_t*, const void*, int), void* ctx);
#endif

/**
 * @brief Sets a vendor-specific information element.
 *
 * @param enable Whether to enable or disable the vendor IE.
 * @param type The frame type for the vendor IE.
 * @param id The vendor IE index.
 * @param vnd_ie Pointer to the vendor IE data.
 * @return Success, or an error.
 */
[[nodiscard]] result<void>
try_set_vendor_ie(bool enable, enum vendor_ie_type type, enum vendor_ie_id id, const void* vnd_ie);

/**
 * @brief Registers a callback for received vendor-specific IEs.
 *
 * @param cb Callback function: (ctx, type, source_mac, vendor_ie_data, rssi).
 * @param ctx User context pointer passed to the callback.
 * @return Success, or an error.
 */
result<void> try_set_vendor_ie_cb(void (*cb)(void*, int, const uint8_t*, const void*, int), void* ctx);

// =============================================================================
// Free function API — CSI
// =============================================================================

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
/**
 * @brief Enables or disables CSI (Channel State Information) collection.
 *
 * @param en True to enable, false to disable.
 *
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
 * @throws std::system_error on failure.
 */
void set_csi(bool en);

/**
 * @brief Sets the CSI configuration.
 *
 * @param cfg The CSI configuration.
 *
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
 * @throws std::system_error on failure.
 */
void set_csi_config(const csi_config& cfg);

/**
 * @brief Gets the current CSI configuration.
 *
 * @return The current CSI configuration.
 *
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
 * @throws std::system_error on failure.
 */
[[nodiscard]] csi_config get_csi_config();

/**
 * @brief Registers a callback for CSI data reception.
 *
 * @param cb Callback function: (ctx, csi_data).
 * @param ctx User context pointer passed to the callback.
 *
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
 * @throws std::system_error on failure.
 */
void set_csi_rx_cb(void (*cb)(void*, void*), void* ctx);
#endif

/**
 * @brief Enables or disables CSI (Channel State Information) collection.
 *
 * @param en True to enable, false to disable.
 * @return Success, or an error.
 */
result<void> try_set_csi(bool en);

/**
 * @brief Sets the CSI configuration.
 *
 * @param cfg The CSI configuration.
 * @return Success, or an error.
 */
result<void> try_set_csi_config(const csi_config& cfg);

/**
 * @brief Gets the current CSI configuration.
 *
 * @return The current CSI configuration, or an error.
 */
[[nodiscard]] result<csi_config> try_get_csi_config();

/**
 * @brief Registers a callback for CSI data reception.
 *
 * @param cb Callback function: (ctx, csi_data).
 * @param ctx User context pointer passed to the callback.
 * @return Success, or an error.
 */
result<void> try_set_csi_rx_cb(void (*cb)(void*, void*), void* ctx);

// =============================================================================
// Free function API — FTM
// =============================================================================

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
/**
 * @brief Initiates an FTM (Fine Timing Measurement) session.
 *
 * @param cfg The FTM initiator configuration.
 *
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
 * @throws std::system_error on failure.
 */
void ftm_initiate_session(const ftm_initiator_config& cfg);

/**
 * @brief Ends the current FTM session.
 *
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
 * @throws std::system_error on failure.
 */
void ftm_end_session();

/**
 * @brief Sets the FTM responder offset.
 *
 * @param offset The offset value in nanoseconds.
 *
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
 * @throws std::system_error on failure.
 */
void ftm_resp_set_offset(int16_t offset);

/**
 * @brief Retrieves the FTM measurement report entries.
 *
 * Only valid when ftm_initiator_config::use_get_report_api is true.
 *
 * @param max_entries Maximum number of entries to retrieve.
 * @return A vector of FTM report entries.
 *
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
 * @throws std::system_error on failure.
 */
[[nodiscard]] std::vector<ftm_report_entry> ftm_get_report(size_t max_entries);
#endif

/**
 * @brief Initiates an FTM (Fine Timing Measurement) session.
 *
 * @param cfg The FTM initiator configuration.
 * @return Success, or an error.
 */
[[nodiscard]] result<void> try_ftm_initiate_session(const ftm_initiator_config& cfg);

/**
 * @brief Ends the current FTM session.
 *
 * @return Success, or an error.
 */
result<void> try_ftm_end_session();

/**
 * @brief Sets the FTM responder offset.
 *
 * @param offset The offset value in nanoseconds.
 * @return Success, or an error.
 */
[[nodiscard]] result<void> try_ftm_resp_set_offset(int16_t offset);

/**
 * @brief Retrieves the FTM measurement report entries.
 *
 * Only valid when ftm_initiator_config::use_get_report_api is true.
 *
 * @param max_entries Maximum number of entries to retrieve.
 * @return A vector of FTM report entries, or an error.
 */
[[nodiscard]] result<std::vector<ftm_report_entry>> try_ftm_get_report(size_t max_entries);

// =============================================================================
// Free function API — Miscellaneous
// =============================================================================

/**
 * @brief Gets the TSF (Timing Synchronization Function) time for the specified interface.
 *
 * This function does not return an error; it always returns a timestamp.
 *
 * @param iface The WiFi interface.
 * @return The TSF time in microseconds.
 */
[[nodiscard]] int64_t get_tsf_time(enum interface iface);

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
/**
 * @brief Dumps WiFi statistics for the specified modules.
 *
 * @param modules Flags indicating which modules to dump statistics for.
 *
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
 * @throws std::system_error on failure.
 */
void statis_dump(flags<statis_module> modules);

/**
 * @brief Enables or disables 802.11b rate for the specified interface.
 *
 * @param iface The WiFi interface.
 * @param disable True to disable 11b rate, false to enable.
 *
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
 * @throws std::system_error on failure.
 */
void config_11b_rate(enum interface iface, bool disable);

/**
 * @brief Sets the 802.11 TX rate for the specified interface.
 *
 * @param iface The WiFi interface.
 * @param rate The PHY transmission rate.
 *
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
 * @throws std::system_error on failure.
 */
void config_80211_tx_rate(enum interface iface, enum phy_rate rate);

/**
 * @brief Disables PMF (Protected Management Frames) for the specified interface.
 *
 * @param iface The WiFi interface.
 *
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
 * @throws std::system_error on failure.
 */
void disable_pmf_config(enum interface iface);

/**
 * @brief Enables or disables dynamic CS (Carrier Sense).
 *
 * @param enabled True to enable, false to disable.
 *
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
 * @throws std::system_error on failure.
 */
void set_dynamic_cs(bool enabled);

/**
 * @brief Sets the connectionless module wake interval.
 *
 * @param interval The wake interval value.
 *
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
 * @throws std::system_error on failure.
 */
void connectionless_module_set_wake_interval(uint16_t interval);
#endif

/**
 * @brief Dumps WiFi statistics for the specified modules.
 *
 * @param modules Flags indicating which modules to dump statistics for.
 * @return Success, or an error.
 */
[[nodiscard]] result<void> try_statis_dump(flags<statis_module> modules);

/**
 * @brief Enables or disables 802.11b rate for the specified interface.
 *
 * @param iface The WiFi interface.
 * @param disable True to disable 11b rate, false to enable.
 * @return Success, or an error.
 */
[[nodiscard]] result<void> try_config_11b_rate(enum interface iface, bool disable);

/**
 * @brief Sets the 802.11 TX rate for the specified interface.
 *
 * @param iface The WiFi interface.
 * @param rate The PHY transmission rate.
 * @return Success, or an error.
 */
[[nodiscard]] result<void> try_config_80211_tx_rate(enum interface iface, enum phy_rate rate);

/**
 * @brief Disables PMF (Protected Management Frames) for the specified interface.
 *
 * @param iface The WiFi interface.
 * @return Success, or an error.
 */
[[nodiscard]] result<void> try_disable_pmf_config(enum interface iface);

/**
 * @brief Enables or disables dynamic CS (Carrier Sense).
 *
 * @param enabled True to enable, false to disable.
 * @return Success, or an error.
 */
[[nodiscard]] result<void> try_set_dynamic_cs(bool enabled);

/**
 * @brief Sets the connectionless module wake interval.
 *
 * @param interval The wake interval value.
 * @return Success, or an error.
 */
[[nodiscard]] result<void> try_connectionless_module_set_wake_interval(uint16_t interval);

// =============================================================================
// Inline definitions — exception-based API
// =============================================================================

/** @cond INTERNAL */

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS

// Lifecycle
inline void init(const init_config& cfg) {
    unwrap(try_init(cfg));
}
inline void deinit() {
    unwrap(try_deinit());
}
inline void start() {
    unwrap(try_start());
}
inline void stop() {
    unwrap(try_stop());
}
inline void restore() {
    unwrap(try_restore());
}

// Mode
inline void set_mode(enum mode m) {
    unwrap(try_set_mode(m));
}
inline enum mode get_mode() {
    return unwrap(try_get_mode());
}

// STA config
inline void set_sta_config(const sta_config& cfg) {
    unwrap(try_set_sta_config(cfg));
}
inline sta_config get_sta_config() {
    return unwrap(try_get_sta_config());
}

// AP config
inline void set_ap_config(const ap_config& cfg) {
    unwrap(try_set_ap_config(cfg));
}
inline ap_config get_ap_config() {
    return unwrap(try_get_ap_config());
}
inline void deauth_sta(uint16_t aid) {
    unwrap(try_deauth_sta(aid));
}
inline std::vector<sta_info> get_sta_list() {
    return unwrap(try_get_sta_list());
}
inline uint16_t ap_get_sta_aid(mac_address mac) {
    return unwrap(try_ap_get_sta_aid(mac));
}

// Connect / disconnect
inline void connect() {
    unwrap(try_connect());
}
inline void disconnect() {
    unwrap(try_disconnect());
}
inline void clear_fast_connect() {
    unwrap(try_clear_fast_connect());
}
inline uint16_t sta_get_aid() {
    return unwrap(try_sta_get_aid());
}
inline enum phy_mode get_negotiated_phymode() {
    return unwrap(try_get_negotiated_phymode());
}

// Scanning
inline std::vector<ap_record> scan(const scan_config& cfg) {
    return unwrap(try_scan(cfg));
}
inline void scan_start(const scan_config& cfg) {
    unwrap(try_scan_start(cfg));
}
inline std::vector<ap_record> scan_get_results() {
    return unwrap(try_scan_get_results());
}
inline void scan_stop() {
    unwrap(try_scan_stop());
}
inline uint16_t scan_get_ap_num() {
    return unwrap(try_scan_get_ap_num());
}
inline void clear_ap_list() {
    unwrap(try_clear_ap_list());
}
inline void set_scan_parameters(const scan_default_params& params) {
    unwrap(try_set_scan_parameters(params));
}
inline scan_default_params get_scan_parameters() {
    return unwrap(try_get_scan_parameters());
}

// Power save
inline void set_power_save(enum power_save ps) {
    unwrap(try_set_power_save(ps));
}
inline enum power_save get_power_save() {
    return unwrap(try_get_power_save());
}

// Bandwidth
inline void set_bandwidth(enum interface iface, enum bandwidth bw) {
    unwrap(try_set_bandwidth(iface, bw));
}
inline enum bandwidth get_bandwidth(enum interface iface) {
    return unwrap(try_get_bandwidth(iface));
}
inline void set_bandwidths(enum interface iface, const bandwidths_config& bw) {
    unwrap(try_set_bandwidths(iface, bw));
}
inline bandwidths_config get_bandwidths(enum interface iface) {
    return unwrap(try_get_bandwidths(iface));
}

// MAC
inline void set_mac(enum interface iface, mac_address mac) {
    unwrap(try_set_mac(iface, mac));
}
inline mac_address get_mac(enum interface iface) {
    return unwrap(try_get_mac(iface));
}

// AP info
inline ap_record get_ap_info() {
    return unwrap(try_get_ap_info());
}

// Channel
inline void set_channel(uint8_t primary, enum second_channel second) {
    unwrap(try_set_channel(primary, second));
}
inline channel_info get_channel() {
    return unwrap(try_get_channel());
}

// Country
inline void set_country(const country_config& cfg) {
    unwrap(try_set_country(cfg));
}
inline country_config get_country() {
    return unwrap(try_get_country());
}
inline void set_country_code(std::string_view cc, bool ieee80211d_enabled) {
    unwrap(try_set_country_code(cc, ieee80211d_enabled));
}
inline std::string get_country_code() {
    return unwrap(try_get_country_code());
}

// TX power
inline void set_max_tx_power(int8_t power) {
    unwrap(try_set_max_tx_power(power));
}
inline int8_t get_max_tx_power() {
    return unwrap(try_get_max_tx_power());
}

// RSSI
inline void set_rssi_threshold(int32_t rssi) {
    unwrap(try_set_rssi_threshold(rssi));
}
inline int get_rssi() {
    return unwrap(try_get_rssi());
}

// Protocol
inline void set_protocol(enum interface iface, flags<protocol> protos) {
    unwrap(try_set_protocol(iface, protos));
}
inline flags<protocol> get_protocol(enum interface iface) {
    return unwrap(try_get_protocol(iface));
}
inline void set_protocols(enum interface iface, const protocols_config& cfg) {
    unwrap(try_set_protocols(iface, cfg));
}
inline protocols_config get_protocols(enum interface iface) {
    return unwrap(try_get_protocols(iface));
}

// Band
inline void set_band(enum band b) {
    unwrap(try_set_band(b));
}
inline enum band get_band() {
    return unwrap(try_get_band());
}
inline void set_band_mode(enum band_mode m) {
    unwrap(try_set_band_mode(m));
}
inline enum band_mode get_band_mode() {
    return unwrap(try_get_band_mode());
}

// Storage
inline void set_storage(enum storage s) {
    unwrap(try_set_storage(s));
}

// Inactive time
inline void set_inactive_time(enum interface iface, uint16_t sec) {
    unwrap(try_set_inactive_time(iface, sec));
}
inline uint16_t get_inactive_time(enum interface iface) {
    return unwrap(try_get_inactive_time(iface));
}

// Event mask
inline void set_event_mask(flags<event_mask> mask) {
    unwrap(try_set_event_mask(mask));
}
inline flags<event_mask> get_event_mask() {
    return unwrap(try_get_event_mask());
}

// Force wakeup
inline void force_wakeup_acquire() {
    unwrap(try_force_wakeup_acquire());
}
inline void force_wakeup_release() {
    unwrap(try_force_wakeup_release());
}

// Promiscuous mode
inline void set_promiscuous(bool en) {
    unwrap(try_set_promiscuous(en));
}
inline bool get_promiscuous() {
    return unwrap(try_get_promiscuous());
}
inline void set_promiscuous_rx_cb(void (*cb)(void*, int)) {
    unwrap(try_set_promiscuous_rx_cb(cb));
}
inline void set_promiscuous_filter(flags<promiscuous_filter> filter) {
    unwrap(try_set_promiscuous_filter(filter));
}
inline flags<promiscuous_filter> get_promiscuous_filter() {
    return unwrap(try_get_promiscuous_filter());
}
inline void set_promiscuous_ctrl_filter(flags<promiscuous_ctrl_filter> filter) {
    unwrap(try_set_promiscuous_ctrl_filter(filter));
}
inline flags<promiscuous_ctrl_filter> get_promiscuous_ctrl_filter() {
    return unwrap(try_get_promiscuous_ctrl_filter());
}

// Raw 802.11
inline void tx_80211(enum interface iface, std::span<const uint8_t> buffer, bool en_sys_seq) {
    unwrap(try_tx_80211(iface, buffer, en_sys_seq));
}
inline void register_80211_tx_cb(void (*cb)(const void*)) {
    unwrap(try_register_80211_tx_cb(cb));
}

// Vendor IE
inline void set_vendor_ie(bool enable, enum vendor_ie_type type, enum vendor_ie_id id, const void* vnd_ie) {
    unwrap(try_set_vendor_ie(enable, type, id, vnd_ie));
}
inline void set_vendor_ie_cb(void (*cb)(void*, int, const uint8_t*, const void*, int), void* ctx) {
    unwrap(try_set_vendor_ie_cb(cb, ctx));
}

// CSI
inline void set_csi(bool en) {
    unwrap(try_set_csi(en));
}
inline void set_csi_config(const csi_config& cfg) {
    unwrap(try_set_csi_config(cfg));
}
inline csi_config get_csi_config() {
    return unwrap(try_get_csi_config());
}
inline void set_csi_rx_cb(void (*cb)(void*, void*), void* ctx) {
    unwrap(try_set_csi_rx_cb(cb, ctx));
}

// FTM
inline void ftm_initiate_session(const ftm_initiator_config& cfg) {
    unwrap(try_ftm_initiate_session(cfg));
}
inline void ftm_end_session() {
    unwrap(try_ftm_end_session());
}
inline void ftm_resp_set_offset(int16_t offset) {
    unwrap(try_ftm_resp_set_offset(offset));
}
inline std::vector<ftm_report_entry> ftm_get_report(size_t max_entries) {
    return unwrap(try_ftm_get_report(max_entries));
}

// Miscellaneous
inline void statis_dump(flags<statis_module> modules) {
    unwrap(try_statis_dump(modules));
}
inline void config_11b_rate(enum interface iface, bool disable) {
    unwrap(try_config_11b_rate(iface, disable));
}
inline void config_80211_tx_rate(enum interface iface, enum phy_rate rate) {
    unwrap(try_config_80211_tx_rate(iface, rate));
}
inline void disable_pmf_config(enum interface iface) {
    unwrap(try_disable_pmf_config(iface));
}
inline void set_dynamic_cs(bool enabled) {
    unwrap(try_set_dynamic_cs(enabled));
}
inline void connectionless_module_set_wake_interval(uint16_t interval) {
    unwrap(try_connectionless_module_set_wake_interval(interval));
}

#endif // CONFIG_COMPILER_CXX_EXCEPTIONS

/** @endcond */

} // namespace idfxx::wifi

namespace idfxx {

/**
 * @brief Returns a reference to the WiFi error category singleton.
 *
 * @return Reference to the singleton wifi::error_category instance.
 */
[[nodiscard]] const wifi::error_category& wifi_category() noexcept;

/**
 * @headerfile <idfxx/wifi>
 * @brief Creates an unexpected error from an ESP-IDF error code, mapping to WiFi error codes where possible.
 *
 * Converts the ESP-IDF error code to a WiFi-specific error code if a mapping exists,
 * otherwise falls back to the default IDFXX error category.
 *
 * @param e The ESP-IDF error code.
 * @return An unexpected value suitable for returning from result-returning functions.
 */
[[nodiscard]] std::unexpected<std::error_code> wifi_error(esp_err_t e);

// =============================================================================
// String conversion
// =============================================================================

/**
 * @headerfile <idfxx/wifi>
 * @brief Returns a string representation of a WiFi operating mode.
 *
 * @param m The operating mode to convert.
 * @return "null", "sta", "ap", "ap_sta", or "unknown(N)" for unrecognized values.
 */
[[nodiscard]] std::string to_string(wifi::mode m);

/**
 * @headerfile <idfxx/wifi>
 * @brief Returns a string representation of a WiFi authentication mode.
 *
 * @param m The authentication mode to convert.
 * @return "OPEN", "WPA2_PSK", etc., or "unknown(N)" for unrecognized values.
 */
[[nodiscard]] std::string to_string(wifi::auth_mode m);

/**
 * @headerfile <idfxx/wifi>
 * @brief Returns a string representation of a WiFi cipher type.
 *
 * @param c The cipher type to convert.
 * @return "NONE", "CCMP", etc., or "unknown(N)" for unrecognized values.
 */
[[nodiscard]] std::string to_string(wifi::cipher_type c);

/**
 * @headerfile <idfxx/wifi>
 * @brief Returns a string representation of a WiFi disconnect reason.
 *
 * @param r The disconnect reason to convert.
 * @return A human-readable reason string, or "unknown(N)" for unrecognized values.
 */
[[nodiscard]] std::string to_string(wifi::disconnect_reason r);

} // namespace idfxx

namespace idfxx::wifi {

/**
 * @headerfile <idfxx/wifi>
 * @brief Creates an error code from an idfxx::wifi::errc value.
 *
 * @param e The WiFi error code enumerator.
 * @return The corresponding std::error_code.
 */
[[nodiscard]] inline std::error_code make_error_code(errc e) noexcept {
    return {std::to_underlying(e), wifi_category()};
}

} // namespace idfxx::wifi

/** @cond INTERNAL */
namespace std {
template<>
struct is_error_code_enum<idfxx::wifi::errc> : true_type {};
} // namespace std
/** @endcond */

#include "sdkconfig.h"
#ifdef CONFIG_IDFXX_STD_FORMAT
/** @cond INTERNAL */
#include <algorithm>
#include <format>
namespace std {

template<>
struct formatter<idfxx::wifi::mode> {
    constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }

    template<typename FormatContext>
    auto format(idfxx::wifi::mode m, FormatContext& ctx) const {
        auto s = idfxx::to_string(m);
        return std::copy(s.begin(), s.end(), ctx.out());
    }
};

template<>
struct formatter<idfxx::wifi::auth_mode> {
    constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }

    template<typename FormatContext>
    auto format(idfxx::wifi::auth_mode m, FormatContext& ctx) const {
        auto s = idfxx::to_string(m);
        return std::copy(s.begin(), s.end(), ctx.out());
    }
};

template<>
struct formatter<idfxx::wifi::cipher_type> {
    constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }

    template<typename FormatContext>
    auto format(idfxx::wifi::cipher_type c, FormatContext& ctx) const {
        auto s = idfxx::to_string(c);
        return std::copy(s.begin(), s.end(), ctx.out());
    }
};

template<>
struct formatter<idfxx::wifi::disconnect_reason> {
    constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }

    template<typename FormatContext>
    auto format(idfxx::wifi::disconnect_reason r, FormatContext& ctx) const {
        auto s = idfxx::to_string(r);
        return std::copy(s.begin(), s.end(), ctx.out());
    }
};

} // namespace std
/** @endcond */
#endif // CONFIG_IDFXX_STD_FORMAT

/** @} */ // end of idfxx_wifi
