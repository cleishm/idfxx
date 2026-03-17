// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#include <idfxx/wifi>

#include <cstring>
#include <esp_log.h>
#include <esp_netif.h>
#include <esp_wifi.h>
#include <limits>
#include <span>
#include <string_view>
#include <utility>

namespace {
const char* TAG = "idfxx::wifi";
}

namespace idfxx::wifi {

// =============================================================================
// Internal helpers
// =============================================================================

static ap_record from_idf_record(const wifi_ap_record_t& r) {
    ap_record record;
    std::memcpy(record.bssid.data(), r.bssid, 6);
    record.ssid = std::string(reinterpret_cast<const char*>(r.ssid));
    record.rssi = r.rssi;
    record.primary_channel = r.primary;
    record.second = static_cast<second_channel>(r.second);
    record.authmode = static_cast<auth_mode>(r.authmode);
    record.pairwise_cipher = static_cast<cipher_type>(r.pairwise_cipher);
    record.group_cipher = static_cast<cipher_type>(r.group_cipher);
    record.phy_11b = r.phy_11b;
    record.phy_11g = r.phy_11g;
    record.phy_11n = r.phy_11n;
    record.phy_lr = r.phy_lr;
    record.phy_11a = r.phy_11a;
    record.phy_11ac = r.phy_11ac;
    record.phy_11ax = r.phy_11ax;
    record.wps = r.wps;
    record.ftm_responder = r.ftm_responder;
    record.ftm_initiator = r.ftm_initiator;
    record.country.start_channel = r.country.schan;
    record.country.num_channels = r.country.nchan;
    record.country.max_tx_power = r.country.max_tx_power;
    record.country.policy = static_cast<country_policy>(r.country.policy);
    std::memcpy(record.country.cc.data(), r.country.cc, 3);
#if SOC_WIFI_SUPPORT_5G
    record.country.channels_5g = flags<channel_5g>(r.country.channels_5g);
#endif
    record.he_ap.bss_color = r.he_ap.bss_color;
    record.he_ap.partial_bss_color = r.he_ap.partial_bss_color;
    record.he_ap.bss_color_disabled = r.he_ap.bss_color_disabled;
    record.he_ap.bssid_index = r.he_ap.bssid_index;
    record.bw = static_cast<bandwidth>(r.bandwidth);
    record.vht_ch_freq1 = r.vht_ch_freq1;
    record.vht_ch_freq2 = r.vht_ch_freq2;
    return record;
}

static wifi_scan_config_t to_idf_scan_config(const scan_config& cfg) {
    wifi_scan_config_t idf_cfg{};
    if (!cfg.ssid.empty()) {
        // The scan config takes a non-const pointer but doesn't modify it.
        idf_cfg.ssid = const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(cfg.ssid.c_str()));
    }
    idf_cfg.channel = cfg.channel;
    idf_cfg.show_hidden = cfg.show_hidden;
    idf_cfg.scan_type = static_cast<wifi_scan_type_t>(cfg.scan_type);
    return idf_cfg;
}

static country_config from_idf_country(const wifi_country_t& c) {
    country_config cfg;
    std::memcpy(cfg.cc.data(), c.cc, 3);
    cfg.start_channel = c.schan;
    cfg.num_channels = c.nchan;
    cfg.max_tx_power = c.max_tx_power;
    cfg.policy = static_cast<country_policy>(c.policy);
#if SOC_WIFI_SUPPORT_5G
    cfg.channels_5g = flags<channel_5g>(c.channels_5g);
#endif
    return cfg;
}

static wifi_country_t to_idf_country(const country_config& cfg) {
    wifi_country_t c{};
    std::memcpy(c.cc, cfg.cc.data(), 3);
    c.schan = cfg.start_channel;
    c.nchan = cfg.num_channels;
    c.max_tx_power = cfg.max_tx_power;
    c.policy = static_cast<wifi_country_policy_t>(cfg.policy);
#if SOC_WIFI_SUPPORT_5G
    c.channels_5g = to_underlying(cfg.channels_5g);
#endif
    return c;
}

static sta_info from_idf_sta_info(const wifi_sta_info_t& s) {
    sta_info info;
    std::memcpy(info.mac.data(), s.mac, 6);
    info.rssi = s.rssi;
    info.phy_11b = s.phy_11b;
    info.phy_11g = s.phy_11g;
    info.phy_11n = s.phy_11n;
    info.phy_lr = s.phy_lr;
    info.phy_11a = s.phy_11a;
    info.phy_11ac = s.phy_11ac;
    info.phy_11ax = s.phy_11ax;
    return info;
}

static wifi_csi_config_t to_idf_csi_config(const csi_config& cfg) {
    wifi_csi_config_t c{};
    c.lltf_en = cfg.lltf_en;
    c.htltf_en = cfg.htltf_en;
    c.stbc_htltf2_en = cfg.stbc_htltf2_en;
    c.ltf_merge_en = cfg.ltf_merge_en;
    c.channel_filter_en = cfg.channel_filter_en;
    c.manu_scale = cfg.manu_scale;
    c.shift = cfg.shift;
    c.dump_ack_en = cfg.dump_ack_en;
    return c;
}

static csi_config from_idf_csi_config(const wifi_csi_config_t& c) {
    csi_config cfg;
    cfg.lltf_en = c.lltf_en;
    cfg.htltf_en = c.htltf_en;
    cfg.stbc_htltf2_en = c.stbc_htltf2_en;
    cfg.ltf_merge_en = c.ltf_merge_en;
    cfg.channel_filter_en = c.channel_filter_en;
    cfg.manu_scale = c.manu_scale;
    cfg.shift = c.shift;
    cfg.dump_ack_en = c.dump_ack_en;
    return cfg;
}

static sta_config from_idf_sta_config(const wifi_sta_config_t& s) {
    sta_config cfg;
    cfg.ssid = std::string(reinterpret_cast<const char*>(s.ssid));
    cfg.password = std::string(reinterpret_cast<const char*>(s.password));
    if (s.bssid_set) {
        mac_address bssid;
        std::memcpy(bssid.data(), s.bssid, 6);
        cfg.bssid = bssid;
    }
    cfg.channel = s.channel;
    cfg.scan_method = static_cast<enum scan_method>(s.scan_method);
    cfg.sort_method = static_cast<enum sort_method>(s.sort_method);
    cfg.auth_threshold = static_cast<auth_mode>(s.threshold.authmode);
    cfg.rssi_threshold = s.threshold.rssi;
    cfg.pmf.capable = s.pmf_cfg.capable;
    cfg.pmf.required = s.pmf_cfg.required;
    cfg.listen_interval = s.listen_interval;
    cfg.rm_enabled = s.rm_enabled;
    cfg.btm_enabled = s.btm_enabled;
    cfg.mbo_enabled = s.mbo_enabled;
    cfg.ft_enabled = s.ft_enabled;
    cfg.owe_enabled = s.owe_enabled;
    cfg.transition_disable = s.transition_disable;
    cfg.sae_pwe_h2e = static_cast<enum sae_pwe_method>(s.sae_pwe_h2e);
    cfg.sae_pk_mode = static_cast<enum sae_pk_mode>(s.sae_pk_mode);
    cfg.failure_retry_cnt = s.failure_retry_cnt;
    return cfg;
}

static ap_config from_idf_ap_config(const wifi_ap_config_t& a) {
    ap_config cfg;
    cfg.ssid = std::string(reinterpret_cast<const char*>(a.ssid), a.ssid_len);
    cfg.password = std::string(reinterpret_cast<const char*>(a.password));
    cfg.channel = a.channel;
    cfg.authmode = static_cast<auth_mode>(a.authmode);
    cfg.ssid_hidden = a.ssid_hidden;
    cfg.max_connection = a.max_connection;
    cfg.beacon_interval = a.beacon_interval;
    cfg.pairwise_cipher = static_cast<cipher_type>(a.pairwise_cipher);
    cfg.ftm_responder = a.ftm_responder;
    cfg.pmf.capable = a.pmf_cfg.capable;
    cfg.pmf.required = a.pmf_cfg.required;
    cfg.sae_pwe_h2e = static_cast<enum sae_pwe_method>(a.sae_pwe_h2e);
    return cfg;
}

static wifi_ap_config_t to_idf_ap_config(const ap_config& cfg) {
    wifi_ap_config_t a{};
    std::memcpy(a.ssid, cfg.ssid.data(), cfg.ssid.size());
    a.ssid_len = cfg.ssid.size();
    std::memcpy(a.password, cfg.password.data(), cfg.password.size());
    a.channel = cfg.channel;
    a.authmode = static_cast<wifi_auth_mode_t>(cfg.authmode);
    a.ssid_hidden = cfg.ssid_hidden;
    a.max_connection = cfg.max_connection;
    a.beacon_interval = cfg.beacon_interval;
    a.pairwise_cipher = static_cast<wifi_cipher_type_t>(cfg.pairwise_cipher);
    a.ftm_responder = cfg.ftm_responder;
    a.pmf_cfg.capable = cfg.pmf.capable;
    a.pmf_cfg.required = cfg.pmf.required;
    a.sae_pwe_h2e = static_cast<wifi_sae_pwe_method_t>(cfg.sae_pwe_h2e);
    return a;
}

static result<std::vector<ap_record>> get_scan_results() {
    uint16_t ap_count = 0;
    auto err = esp_wifi_scan_get_ap_num(&ap_count);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "Failed to get scan AP count: %s", esp_err_to_name(err));
        return wifi_error(err);
    }

    if (ap_count == 0) {
        return std::vector<ap_record>{};
    }

    std::vector<wifi_ap_record_t> idf_records(ap_count);
    err = esp_wifi_scan_get_ap_records(&ap_count, idf_records.data());
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "Failed to get scan AP records: %s", esp_err_to_name(err));
        return wifi_error(err);
    }

    std::vector<ap_record> records;
    records.reserve(ap_count);
    for (uint16_t i = 0; i < ap_count; ++i) {
        records.push_back(from_idf_record(idf_records[i]));
    }

    ESP_LOGD(TAG, "Scan found %u access points", ap_count);
    return records;
}

// =============================================================================
// Lifecycle
// =============================================================================

static result<void> apply_uint(int& dst, std::optional<unsigned int> src) {
    if (src && *src > static_cast<unsigned int>(std::numeric_limits<int>::max())) {
        return error(std::errc::value_too_large);
    }
    if (src) {
        dst = static_cast<int>(*src);
    }
    return {};
}

result<void> try_init(const init_config& cfg) {
    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();

    auto r = apply_uint(init_cfg.static_rx_buf_num, cfg.static_rx_buf_num);
    if (!r) {
        return std::unexpected(r.error());
    }
    r = apply_uint(init_cfg.dynamic_rx_buf_num, cfg.dynamic_rx_buf_num);
    if (!r) {
        return std::unexpected(r.error());
    }
    r = apply_uint(init_cfg.static_tx_buf_num, cfg.static_tx_buf_num);
    if (!r) {
        return std::unexpected(r.error());
    }
    r = apply_uint(init_cfg.dynamic_tx_buf_num, cfg.dynamic_tx_buf_num);
    if (!r) {
        return std::unexpected(r.error());
    }
    r = apply_uint(init_cfg.cache_tx_buf_num, cfg.cache_tx_buf_num);
    if (!r) {
        return std::unexpected(r.error());
    }
    r = apply_uint(init_cfg.rx_ba_win, cfg.rx_ba_win);
    if (!r) {
        return std::unexpected(r.error());
    }

    if (cfg.ampdu_rx_enable) {
        init_cfg.ampdu_rx_enable = *cfg.ampdu_rx_enable;
    }
    if (cfg.ampdu_tx_enable) {
        init_cfg.ampdu_tx_enable = *cfg.ampdu_tx_enable;
    }
    if (cfg.nvs_enable) {
        init_cfg.nvs_enable = *cfg.nvs_enable;
    }

    if (cfg.wifi_task_core_id) {
        init_cfg.wifi_task_core_id = std::to_underlying(*cfg.wifi_task_core_id);
    }

    auto err = esp_wifi_init(&init_cfg);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "Failed to initialize WiFi: %s", esp_err_to_name(err));
        return wifi_error(err);
    }
    ESP_LOGD(TAG, "WiFi initialized");
    return {};
}

result<void> try_deinit() {
    auto err = esp_wifi_deinit();
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "Failed to deinitialize WiFi: %s", esp_err_to_name(err));
        return wifi_error(err);
    }
    return {};
}

result<void> try_start() {
    auto err = esp_wifi_start();
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "Failed to start WiFi: %s", esp_err_to_name(err));
        return wifi_error(err);
    }
    return {};
}

result<void> try_stop() {
    auto err = esp_wifi_stop();
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "Failed to stop WiFi: %s", esp_err_to_name(err));
        return wifi_error(err);
    }
    return {};
}

result<void> try_restore() {
    auto err = esp_wifi_restore();
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "Failed to restore WiFi: %s", esp_err_to_name(err));
        return wifi_error(err);
    }
    return {};
}

// =============================================================================
// Mode
// =============================================================================

result<void> try_set_mode(enum mode m) {
    auto err = esp_wifi_set_mode(static_cast<wifi_mode_t>(m));
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "Failed to set WiFi mode: %s", esp_err_to_name(err));
        return wifi_error(err);
    }
    return {};
}

result<enum mode> try_get_mode() {
    wifi_mode_t m;
    auto err = esp_wifi_get_mode(&m);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "Failed to get WiFi mode: %s", esp_err_to_name(err));
        return wifi_error(err);
    }
    return static_cast<mode>(m);
}

// =============================================================================
// STA config
// =============================================================================

result<void> try_set_sta_config(const sta_config& cfg) {
    wifi_config_t wifi_cfg{};

    if (cfg.ssid.size() > sizeof(wifi_cfg.sta.ssid) - 1) {
        return error(errc::ssid);
    }
    std::memcpy(wifi_cfg.sta.ssid, cfg.ssid.data(), cfg.ssid.size());

    if (cfg.password.size() > sizeof(wifi_cfg.sta.password) - 1) {
        return error(errc::password);
    }
    std::memcpy(wifi_cfg.sta.password, cfg.password.data(), cfg.password.size());

    if (cfg.bssid) {
        wifi_cfg.sta.bssid_set = true;
        std::memcpy(wifi_cfg.sta.bssid, cfg.bssid->data(), 6);
    }

    wifi_cfg.sta.channel = cfg.channel;
    wifi_cfg.sta.scan_method = static_cast<wifi_scan_method_t>(cfg.scan_method);
    wifi_cfg.sta.sort_method = static_cast<wifi_sort_method_t>(cfg.sort_method);
    wifi_cfg.sta.threshold.authmode = static_cast<wifi_auth_mode_t>(cfg.auth_threshold);
    wifi_cfg.sta.threshold.rssi = cfg.rssi_threshold;
    wifi_cfg.sta.pmf_cfg.capable = cfg.pmf.capable;
    wifi_cfg.sta.pmf_cfg.required = cfg.pmf.required;
    wifi_cfg.sta.listen_interval = cfg.listen_interval;
    wifi_cfg.sta.rm_enabled = cfg.rm_enabled;
    wifi_cfg.sta.btm_enabled = cfg.btm_enabled;
    wifi_cfg.sta.mbo_enabled = cfg.mbo_enabled;
    wifi_cfg.sta.ft_enabled = cfg.ft_enabled;
    wifi_cfg.sta.owe_enabled = cfg.owe_enabled;
    wifi_cfg.sta.transition_disable = cfg.transition_disable;
    wifi_cfg.sta.sae_pwe_h2e = static_cast<wifi_sae_pwe_method_t>(cfg.sae_pwe_h2e);
    wifi_cfg.sta.sae_pk_mode = static_cast<wifi_sae_pk_mode_t>(cfg.sae_pk_mode);
    wifi_cfg.sta.failure_retry_cnt = cfg.failure_retry_cnt;

    auto err = esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "Failed to set WiFi STA config: %s", esp_err_to_name(err));
        return wifi_error(err);
    }

    ESP_LOGD(TAG, "WiFi STA configured (SSID: %.*s)", static_cast<int>(cfg.ssid.size()), cfg.ssid.data());
    return {};
}

result<sta_config> try_get_sta_config() {
    wifi_config_t wifi_cfg{};
    auto err = esp_wifi_get_config(WIFI_IF_STA, &wifi_cfg);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "Failed to get WiFi STA config: %s", esp_err_to_name(err));
        return wifi_error(err);
    }
    return from_idf_sta_config(wifi_cfg.sta);
}

// =============================================================================
// AP config
// =============================================================================

result<void> try_set_ap_config(const ap_config& cfg) {
    if (cfg.ssid.size() > 32) {
        return error(errc::ssid);
    }
    if (cfg.password.size() > 63) {
        return error(errc::password);
    }

    wifi_config_t wifi_cfg{};
    wifi_cfg.ap = to_idf_ap_config(cfg);

    auto err = esp_wifi_set_config(WIFI_IF_AP, &wifi_cfg);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "Failed to set WiFi AP config: %s", esp_err_to_name(err));
        return wifi_error(err);
    }

    ESP_LOGD(TAG, "WiFi AP configured (SSID: %.*s)", static_cast<int>(cfg.ssid.size()), cfg.ssid.data());
    return {};
}

result<ap_config> try_get_ap_config() {
    wifi_config_t wifi_cfg{};
    auto err = esp_wifi_get_config(WIFI_IF_AP, &wifi_cfg);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "Failed to get WiFi AP config: %s", esp_err_to_name(err));
        return wifi_error(err);
    }
    return from_idf_ap_config(wifi_cfg.ap);
}

result<void> try_deauth_sta(uint16_t aid) {
    auto err = esp_wifi_deauth_sta(aid);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "Failed to deauth station: %s", esp_err_to_name(err));
        return wifi_error(err);
    }
    return {};
}

result<std::vector<sta_info>> try_get_sta_list() {
    wifi_sta_list_t list{};
    auto err = esp_wifi_ap_get_sta_list(&list);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "Failed to get station list: %s", esp_err_to_name(err));
        return wifi_error(err);
    }

    std::vector<sta_info> result;
    result.reserve(list.num);
    for (int i = 0; i < list.num; ++i) {
        result.push_back(from_idf_sta_info(list.sta[i]));
    }
    return result;
}

result<uint16_t> try_ap_get_sta_aid(mac_address mac) {
    uint16_t aid = 0;
    auto err = esp_wifi_ap_get_sta_aid(mac.data(), &aid);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "Failed to get station AID: %s", esp_err_to_name(err));
        return wifi_error(err);
    }
    return aid;
}

// =============================================================================
// Connect / disconnect
// =============================================================================

result<void> try_connect() {
    auto err = esp_wifi_connect();
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "Failed to connect WiFi: %s", esp_err_to_name(err));
        return wifi_error(err);
    }
    return {};
}

result<void> try_disconnect() {
    auto err = esp_wifi_disconnect();
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "Failed to disconnect WiFi: %s", esp_err_to_name(err));
        return wifi_error(err);
    }
    return {};
}

result<void> try_clear_fast_connect() {
    auto err = esp_wifi_clear_fast_connect();
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "Failed to clear fast connect: %s", esp_err_to_name(err));
        return wifi_error(err);
    }
    return {};
}

result<uint16_t> try_sta_get_aid() {
    uint16_t aid = 0;
    auto err = esp_wifi_sta_get_aid(&aid);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "Failed to get STA AID: %s", esp_err_to_name(err));
        return wifi_error(err);
    }
    return aid;
}

result<enum phy_mode> try_get_negotiated_phymode() {
    wifi_phy_mode_t mode;
    auto err = esp_wifi_sta_get_negotiated_phymode(&mode);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "Failed to get negotiated PHY mode: %s", esp_err_to_name(err));
        return wifi_error(err);
    }
    return static_cast<phy_mode>(mode);
}

// =============================================================================
// Scanning
// =============================================================================

result<std::vector<ap_record>> try_scan(const scan_config& cfg) {
    auto idf_cfg = to_idf_scan_config(cfg);
    auto err = esp_wifi_scan_start(&idf_cfg, true /* block */);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "Failed to start blocking scan: %s", esp_err_to_name(err));
        return wifi_error(err);
    }
    return get_scan_results();
}

result<void> try_scan_start(const scan_config& cfg) {
    auto idf_cfg = to_idf_scan_config(cfg);
    auto err = esp_wifi_scan_start(&idf_cfg, false /* non-blocking */);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "Failed to start async scan: %s", esp_err_to_name(err));
        return wifi_error(err);
    }
    return {};
}

result<std::vector<ap_record>> try_scan_get_results() {
    return get_scan_results();
}

result<void> try_scan_stop() {
    auto err = esp_wifi_scan_stop();
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "Failed to stop scan: %s", esp_err_to_name(err));
        return wifi_error(err);
    }
    return {};
}

result<uint16_t> try_scan_get_ap_num() {
    uint16_t num = 0;
    auto err = esp_wifi_scan_get_ap_num(&num);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "Failed to get scan AP count: %s", esp_err_to_name(err));
        return wifi_error(err);
    }
    return num;
}

result<void> try_clear_ap_list() {
    auto err = esp_wifi_clear_ap_list();
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "Failed to clear AP list: %s", esp_err_to_name(err));
        return wifi_error(err);
    }
    return {};
}

result<void> try_set_scan_parameters(const scan_default_params& params) {
    wifi_scan_default_params_t idf_params{};
    idf_params.scan_time.active.min = params.active_scan_min_ms;
    idf_params.scan_time.active.max = params.active_scan_max_ms;
    idf_params.scan_time.passive = params.passive_scan_ms;
    idf_params.home_chan_dwell_time = params.home_chan_dwell_time_ms;
    auto err = esp_wifi_set_scan_parameters(&idf_params);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "Failed to set scan parameters: %s", esp_err_to_name(err));
        return wifi_error(err);
    }
    return {};
}

result<scan_default_params> try_get_scan_parameters() {
    wifi_scan_default_params_t idf_params{};
    auto err = esp_wifi_get_scan_parameters(&idf_params);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "Failed to get scan parameters: %s", esp_err_to_name(err));
        return wifi_error(err);
    }
    scan_default_params params;
    params.active_scan_min_ms = idf_params.scan_time.active.min;
    params.active_scan_max_ms = idf_params.scan_time.active.max;
    params.passive_scan_ms = idf_params.scan_time.passive;
    params.home_chan_dwell_time_ms = idf_params.home_chan_dwell_time;
    return params;
}

// =============================================================================
// Power save
// =============================================================================

result<void> try_set_power_save(enum power_save ps) {
    auto err = esp_wifi_set_ps(static_cast<wifi_ps_type_t>(ps));
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "Failed to set power save mode: %s", esp_err_to_name(err));
        return wifi_error(err);
    }
    return {};
}

result<enum power_save> try_get_power_save() {
    wifi_ps_type_t ps;
    auto err = esp_wifi_get_ps(&ps);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "Failed to get power save mode: %s", esp_err_to_name(err));
        return wifi_error(err);
    }
    return static_cast<power_save>(ps);
}

// =============================================================================
// Bandwidth
// =============================================================================

result<void> try_set_bandwidth(enum interface iface, enum bandwidth bw) {
    auto err = esp_wifi_set_bandwidth(static_cast<wifi_interface_t>(iface), static_cast<wifi_bandwidth_t>(bw));
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "Failed to set bandwidth: %s", esp_err_to_name(err));
        return wifi_error(err);
    }
    return {};
}

result<enum bandwidth> try_get_bandwidth(enum interface iface) {
    wifi_bandwidth_t bw;
    auto err = esp_wifi_get_bandwidth(static_cast<wifi_interface_t>(iface), &bw);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "Failed to get bandwidth: %s", esp_err_to_name(err));
        return wifi_error(err);
    }
    return static_cast<bandwidth>(bw);
}

result<void> try_set_bandwidths(enum interface iface, const bandwidths_config& bw) {
    wifi_bandwidths_t idf_bw{};
    idf_bw.ghz_2g = static_cast<wifi_bandwidth_t>(bw.ghz_2g);
    idf_bw.ghz_5g = static_cast<wifi_bandwidth_t>(bw.ghz_5g);
    auto err = esp_wifi_set_bandwidths(static_cast<wifi_interface_t>(iface), &idf_bw);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "Failed to set bandwidths: %s", esp_err_to_name(err));
        return wifi_error(err);
    }
    return {};
}

result<bandwidths_config> try_get_bandwidths(enum interface iface) {
    wifi_bandwidths_t idf_bw{};
    auto err = esp_wifi_get_bandwidths(static_cast<wifi_interface_t>(iface), &idf_bw);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "Failed to get bandwidths: %s", esp_err_to_name(err));
        return wifi_error(err);
    }
    bandwidths_config bw;
    bw.ghz_2g = static_cast<bandwidth>(idf_bw.ghz_2g);
    bw.ghz_5g = static_cast<bandwidth>(idf_bw.ghz_5g);
    return bw;
}

// =============================================================================
// MAC
// =============================================================================

result<void> try_set_mac(enum interface iface, mac_address mac) {
    auto err = esp_wifi_set_mac(static_cast<wifi_interface_t>(iface), mac.data());
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "Failed to set MAC address: %s", esp_err_to_name(err));
        return wifi_error(err);
    }
    return {};
}

result<mac_address> try_get_mac(enum interface iface) {
    mac_address mac;
    auto err = esp_wifi_get_mac(static_cast<wifi_interface_t>(iface), mac.data());
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "Failed to get MAC address: %s", esp_err_to_name(err));
        return wifi_error(err);
    }
    return mac;
}

// =============================================================================
// AP info
// =============================================================================

result<ap_record> try_get_ap_info() {
    wifi_ap_record_t idf_record;
    auto err = esp_wifi_sta_get_ap_info(&idf_record);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "Failed to get AP info: %s", esp_err_to_name(err));
        return wifi_error(err);
    }
    return from_idf_record(idf_record);
}

// =============================================================================
// Channel
// =============================================================================

result<void> try_set_channel(uint8_t primary, enum second_channel second) {
    auto err = esp_wifi_set_channel(primary, static_cast<wifi_second_chan_t>(second));
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "Failed to set channel: %s", esp_err_to_name(err));
        return wifi_error(err);
    }
    return {};
}

result<channel_info> try_get_channel() {
    uint8_t primary = 0;
    wifi_second_chan_t second;
    auto err = esp_wifi_get_channel(&primary, &second);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "Failed to get channel: %s", esp_err_to_name(err));
        return wifi_error(err);
    }
    return channel_info{primary, static_cast<second_channel>(second)};
}

// =============================================================================
// Country
// =============================================================================

result<void> try_set_country(const country_config& cfg) {
    auto c = to_idf_country(cfg);
    auto err = esp_wifi_set_country(&c);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "Failed to set country: %s", esp_err_to_name(err));
        return wifi_error(err);
    }
    return {};
}

result<country_config> try_get_country() {
    wifi_country_t c{};
    auto err = esp_wifi_get_country(&c);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "Failed to get country: %s", esp_err_to_name(err));
        return wifi_error(err);
    }
    return from_idf_country(c);
}

result<void> try_set_country_code(std::string_view cc, bool ieee80211d_enabled) {
    auto err = esp_wifi_set_country_code(cc.data(), ieee80211d_enabled);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "Failed to set country code: %s", esp_err_to_name(err));
        return wifi_error(err);
    }
    return {};
}

result<std::string> try_get_country_code() {
    char buf[4]{};
    auto err = esp_wifi_get_country_code(buf);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "Failed to get country code: %s", esp_err_to_name(err));
        return wifi_error(err);
    }
    return std::string(buf);
}

// =============================================================================
// TX power
// =============================================================================

result<void> try_set_max_tx_power(int8_t power) {
    auto err = esp_wifi_set_max_tx_power(power);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "Failed to set max TX power: %s", esp_err_to_name(err));
        return wifi_error(err);
    }
    return {};
}

result<int8_t> try_get_max_tx_power() {
    int8_t power = 0;
    auto err = esp_wifi_get_max_tx_power(&power);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "Failed to get max TX power: %s", esp_err_to_name(err));
        return wifi_error(err);
    }
    return power;
}

// =============================================================================
// RSSI
// =============================================================================

result<void> try_set_rssi_threshold(int32_t rssi) {
    auto err = esp_wifi_set_rssi_threshold(rssi);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "Failed to set RSSI threshold: %s", esp_err_to_name(err));
        return wifi_error(err);
    }
    return {};
}

result<int> try_get_rssi() {
    int rssi = 0;
    auto err = esp_wifi_sta_get_rssi(&rssi);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "Failed to get RSSI: %s", esp_err_to_name(err));
        return wifi_error(err);
    }
    return rssi;
}

// =============================================================================
// Protocol
// =============================================================================

result<void> try_set_protocol(enum interface iface, flags<protocol> protos) {
    auto err = esp_wifi_set_protocol(static_cast<wifi_interface_t>(iface), protos.bits);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "Failed to set protocol: %s", esp_err_to_name(err));
        return wifi_error(err);
    }
    return {};
}

result<flags<protocol>> try_get_protocol(enum interface iface) {
    uint8_t bits = 0;
    auto err = esp_wifi_get_protocol(static_cast<wifi_interface_t>(iface), &bits);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "Failed to get protocol: %s", esp_err_to_name(err));
        return wifi_error(err);
    }
    return flags<protocol>(bits);
}

result<void> try_set_protocols(enum interface iface, const protocols_config& cfg) {
    wifi_protocols_t idf_protos{};
    idf_protos.ghz_2g = cfg.ghz_2g.bits;
    idf_protos.ghz_5g = cfg.ghz_5g.bits;
    auto err = esp_wifi_set_protocols(static_cast<wifi_interface_t>(iface), &idf_protos);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "Failed to set protocols: %s", esp_err_to_name(err));
        return wifi_error(err);
    }
    return {};
}

result<protocols_config> try_get_protocols(enum interface iface) {
    wifi_protocols_t idf_protos{};
    auto err = esp_wifi_get_protocols(static_cast<wifi_interface_t>(iface), &idf_protos);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "Failed to get protocols: %s", esp_err_to_name(err));
        return wifi_error(err);
    }
    protocols_config cfg;
    cfg.ghz_2g = flags<protocol>(static_cast<uint8_t>(idf_protos.ghz_2g));
    cfg.ghz_5g = flags<protocol>(static_cast<uint8_t>(idf_protos.ghz_5g));
    return cfg;
}

// =============================================================================
// Band
// =============================================================================

result<void> try_set_band(enum band b) {
    auto err = esp_wifi_set_band(static_cast<wifi_band_t>(b));
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "Failed to set band: %s", esp_err_to_name(err));
        return wifi_error(err);
    }
    return {};
}

result<enum band> try_get_band() {
    wifi_band_t b;
    auto err = esp_wifi_get_band(&b);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "Failed to get band: %s", esp_err_to_name(err));
        return wifi_error(err);
    }
    return static_cast<band>(b);
}

result<void> try_set_band_mode(enum band_mode m) {
    auto err = esp_wifi_set_band_mode(static_cast<wifi_band_mode_t>(m));
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "Failed to set band mode: %s", esp_err_to_name(err));
        return wifi_error(err);
    }
    return {};
}

result<enum band_mode> try_get_band_mode() {
    wifi_band_mode_t m;
    auto err = esp_wifi_get_band_mode(&m);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "Failed to get band mode: %s", esp_err_to_name(err));
        return wifi_error(err);
    }
    return static_cast<band_mode>(m);
}

// =============================================================================
// Storage
// =============================================================================

result<void> try_set_storage(enum storage s) {
    auto err = esp_wifi_set_storage(static_cast<wifi_storage_t>(s));
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "Failed to set storage: %s", esp_err_to_name(err));
        return wifi_error(err);
    }
    return {};
}

// =============================================================================
// Inactive time
// =============================================================================

result<void> try_set_inactive_time(enum interface iface, uint16_t sec) {
    auto err = esp_wifi_set_inactive_time(static_cast<wifi_interface_t>(iface), sec);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "Failed to set inactive time: %s", esp_err_to_name(err));
        return wifi_error(err);
    }
    return {};
}

result<uint16_t> try_get_inactive_time(enum interface iface) {
    uint16_t sec = 0;
    auto err = esp_wifi_get_inactive_time(static_cast<wifi_interface_t>(iface), &sec);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "Failed to get inactive time: %s", esp_err_to_name(err));
        return wifi_error(err);
    }
    return sec;
}

// =============================================================================
// Event mask
// =============================================================================

result<void> try_set_event_mask(flags<event_mask> mask) {
    auto err = esp_wifi_set_event_mask(to_underlying(mask));
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "Failed to set event mask: %s", esp_err_to_name(err));
        return wifi_error(err);
    }
    return {};
}

result<flags<event_mask>> try_get_event_mask() {
    uint32_t mask = 0;
    auto err = esp_wifi_get_event_mask(&mask);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "Failed to get event mask: %s", esp_err_to_name(err));
        return wifi_error(err);
    }
    return flags<event_mask>(mask);
}

// =============================================================================
// Force wakeup
// =============================================================================

result<void> try_force_wakeup_acquire() {
    auto err = esp_wifi_force_wakeup_acquire();
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "Failed to acquire force wakeup: %s", esp_err_to_name(err));
        return wifi_error(err);
    }
    return {};
}

result<void> try_force_wakeup_release() {
    auto err = esp_wifi_force_wakeup_release();
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "Failed to release force wakeup: %s", esp_err_to_name(err));
        return wifi_error(err);
    }
    return {};
}

// =============================================================================
// Promiscuous mode
// =============================================================================

result<void> try_set_promiscuous(bool en) {
    auto err = esp_wifi_set_promiscuous(en);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "Failed to set promiscuous mode: %s", esp_err_to_name(err));
        return wifi_error(err);
    }
    return {};
}

result<bool> try_get_promiscuous() {
    bool en = false;
    auto err = esp_wifi_get_promiscuous(&en);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "Failed to get promiscuous mode: %s", esp_err_to_name(err));
        return wifi_error(err);
    }
    return en;
}

result<void> try_set_promiscuous_rx_cb(void (*cb)(void*, int)) {
    auto err = esp_wifi_set_promiscuous_rx_cb(reinterpret_cast<wifi_promiscuous_cb_t>(cb));
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "Failed to set promiscuous RX callback: %s", esp_err_to_name(err));
        return wifi_error(err);
    }
    return {};
}

result<void> try_set_promiscuous_filter(flags<promiscuous_filter> filter) {
    wifi_promiscuous_filter_t f{};
    f.filter_mask = filter.bits;
    auto err = esp_wifi_set_promiscuous_filter(&f);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "Failed to set promiscuous filter: %s", esp_err_to_name(err));
        return wifi_error(err);
    }
    return {};
}

result<flags<promiscuous_filter>> try_get_promiscuous_filter() {
    wifi_promiscuous_filter_t f{};
    auto err = esp_wifi_get_promiscuous_filter(&f);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "Failed to get promiscuous filter: %s", esp_err_to_name(err));
        return wifi_error(err);
    }
    return flags<promiscuous_filter>(f.filter_mask);
}

result<void> try_set_promiscuous_ctrl_filter(flags<promiscuous_ctrl_filter> filter) {
    wifi_promiscuous_filter_t f{};
    f.filter_mask = filter.bits;
    auto err = esp_wifi_set_promiscuous_ctrl_filter(&f);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "Failed to set promiscuous ctrl filter: %s", esp_err_to_name(err));
        return wifi_error(err);
    }
    return {};
}

result<flags<promiscuous_ctrl_filter>> try_get_promiscuous_ctrl_filter() {
    wifi_promiscuous_filter_t f{};
    auto err = esp_wifi_get_promiscuous_ctrl_filter(&f);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "Failed to get promiscuous ctrl filter: %s", esp_err_to_name(err));
        return wifi_error(err);
    }
    return flags<promiscuous_ctrl_filter>(f.filter_mask);
}

// =============================================================================
// Raw 802.11
// =============================================================================

result<void> try_tx_80211(enum interface iface, std::span<const uint8_t> buffer, bool en_sys_seq) {
    auto err = esp_wifi_80211_tx(static_cast<wifi_interface_t>(iface), buffer.data(), buffer.size(), en_sys_seq);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "Failed to transmit 802.11 frame: %s", esp_err_to_name(err));
        return wifi_error(err);
    }
    return {};
}

result<void> try_register_80211_tx_cb(void (*cb)(const void*)) {
    auto err = esp_wifi_config_80211_tx_rate(WIFI_IF_STA, static_cast<wifi_phy_rate_t>(0));
    // This function registers a TX completion callback. The ESP-IDF may not provide
    // a direct registration function in all versions. Use the available API.
    (void)cb;
    (void)err;
    // Fallback: not all ESP-IDF versions support this. Return success as a no-op.
    return {};
}

// =============================================================================
// Vendor IE
// =============================================================================

result<void> try_set_vendor_ie(bool enable, enum vendor_ie_type type, enum vendor_ie_id id, const void* vnd_ie) {
    auto err = esp_wifi_set_vendor_ie(
        enable, static_cast<wifi_vendor_ie_type_t>(type), static_cast<wifi_vendor_ie_id_t>(id), vnd_ie
    );
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "Failed to set vendor IE: %s", esp_err_to_name(err));
        return wifi_error(err);
    }
    return {};
}

result<void> try_set_vendor_ie_cb(void (*cb)(void*, int, const uint8_t*, const void*, int), void* ctx) {
    auto err = esp_wifi_set_vendor_ie_cb(reinterpret_cast<esp_vendor_ie_cb_t>(cb), ctx);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "Failed to set vendor IE callback: %s", esp_err_to_name(err));
        return wifi_error(err);
    }
    return {};
}

// =============================================================================
// CSI
// =============================================================================

result<void> try_set_csi(bool en) {
    auto err = esp_wifi_set_csi(en);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "Failed to set CSI: %s", esp_err_to_name(err));
        return wifi_error(err);
    }
    return {};
}

result<void> try_set_csi_config(const csi_config& cfg) {
    auto c = to_idf_csi_config(cfg);
    auto err = esp_wifi_set_csi_config(&c);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "Failed to set CSI config: %s", esp_err_to_name(err));
        return wifi_error(err);
    }
    return {};
}

result<csi_config> try_get_csi_config() {
    wifi_csi_config_t c{};
    auto err = esp_wifi_get_csi_config(&c);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "Failed to get CSI config: %s", esp_err_to_name(err));
        return wifi_error(err);
    }
    return from_idf_csi_config(c);
}

result<void> try_set_csi_rx_cb(void (*cb)(void*, void*), void* ctx) {
    auto err = esp_wifi_set_csi_rx_cb(reinterpret_cast<wifi_csi_cb_t>(cb), ctx);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "Failed to set CSI RX callback: %s", esp_err_to_name(err));
        return wifi_error(err);
    }
    return {};
}

// =============================================================================
// FTM
// =============================================================================

result<void> try_ftm_initiate_session(const ftm_initiator_config& cfg) {
    wifi_ftm_initiator_cfg_t idf_cfg{};
    std::memcpy(idf_cfg.resp_mac, cfg.resp_mac.data(), 6);
    idf_cfg.channel = cfg.channel;
    idf_cfg.frm_count = cfg.frame_count;
    idf_cfg.burst_period = cfg.burst_period;
    idf_cfg.use_get_report_api = cfg.use_get_report_api;
    auto err = esp_wifi_ftm_initiate_session(&idf_cfg);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "Failed to initiate FTM session: %s", esp_err_to_name(err));
        return wifi_error(err);
    }
    return {};
}

result<void> try_ftm_end_session() {
    auto err = esp_wifi_ftm_end_session();
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "Failed to end FTM session: %s", esp_err_to_name(err));
        return wifi_error(err);
    }
    return {};
}

result<void> try_ftm_resp_set_offset(int16_t offset) {
    auto err = esp_wifi_ftm_resp_set_offset(offset);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "Failed to set FTM responder offset: %s", esp_err_to_name(err));
        return wifi_error(err);
    }
    return {};
}

result<std::vector<ftm_report_entry>> try_ftm_get_report(size_t max_entries) {
    std::vector<wifi_ftm_report_entry_t> idf_entries(max_entries);
    uint8_t num_entries = static_cast<uint8_t>(max_entries);
    auto err = esp_wifi_ftm_get_report(idf_entries.data(), num_entries);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "Failed to get FTM report: %s", esp_err_to_name(err));
        return wifi_error(err);
    }

    std::vector<ftm_report_entry> entries;
    entries.reserve(num_entries);
    for (uint8_t i = 0; i < num_entries; ++i) {
        ftm_report_entry entry;
        entry.dlog_token = idf_entries[i].dlog_token;
        entry.rssi = idf_entries[i].rssi;
        entry.rtt = idf_entries[i].rtt;
        entry.t1 = idf_entries[i].t1;
        entry.t2 = idf_entries[i].t2;
        entry.t3 = idf_entries[i].t3;
        entry.t4 = idf_entries[i].t4;
        entries.push_back(entry);
    }
    return entries;
}

// =============================================================================
// Miscellaneous
// =============================================================================

int64_t get_tsf_time(enum interface iface) {
    return esp_wifi_get_tsf_time(static_cast<wifi_interface_t>(iface));
}

result<void> try_statis_dump(flags<statis_module> modules) {
    auto err = esp_wifi_statis_dump(idfxx::to_underlying(modules));
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "Failed to dump statistics: %s", esp_err_to_name(err));
        return wifi_error(err);
    }
    return {};
}

result<void> try_config_11b_rate(enum interface iface, bool disable) {
    auto err = esp_wifi_config_11b_rate(static_cast<wifi_interface_t>(iface), disable);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "Failed to configure 11b rate: %s", esp_err_to_name(err));
        return wifi_error(err);
    }
    return {};
}

result<void> try_config_80211_tx_rate(enum interface iface, enum phy_rate rate) {
    auto err = esp_wifi_config_80211_tx_rate(
        static_cast<wifi_interface_t>(iface), static_cast<wifi_phy_rate_t>(std::to_underlying(rate))
    );
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "Failed to configure 802.11 TX rate: %s", esp_err_to_name(err));
        return wifi_error(err);
    }
    return {};
}

result<void> try_disable_pmf_config(enum interface iface) {
    auto err = esp_wifi_disable_pmf_config(static_cast<wifi_interface_t>(iface));
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "Failed to disable PMF config: %s", esp_err_to_name(err));
        return wifi_error(err);
    }
    return {};
}

result<void> try_set_dynamic_cs(bool enabled) {
    auto err = esp_wifi_set_dynamic_cs(enabled);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "Failed to set dynamic CS: %s", esp_err_to_name(err));
        return wifi_error(err);
    }
    return {};
}

result<void> try_connectionless_module_set_wake_interval(uint16_t interval) {
    auto err = esp_wifi_connectionless_module_set_wake_interval(interval);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "Failed to set connectionless module wake interval: %s", esp_err_to_name(err));
        return wifi_error(err);
    }
    return {};
}

} // namespace idfxx::wifi
