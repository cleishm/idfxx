# idfxx_wifi

Type-safe WiFi management for ESP32.

📚 **[Full API Documentation](https://cleishm.github.io/idfxx/group__idfxx__wifi.html)**

## Features

- WiFi lifecycle management via namespace-level free functions
- Station (STA) mode with connection management and power save
- Access point (AP) mode with station tracking and deauthentication
- AP scanning with blocking and non-blocking modes
- Type-safe enumerations for roles, authentication, bandwidth, power save, and protocols
- Event integration for connection, disconnection, IP assignment, scan completion, and more
- Domain-specific error codes with `std::error_code` integration
- Advanced features: promiscuous mode, CSI, FTM, raw 802.11 TX, vendor IE, country/regulatory

## Requirements

- ESP-IDF 5.5 or later
- C++23 compiler
- System event loop must be created before use

## Installation

### ESP-IDF Component Manager

Add to your project's `idf_component.yml`:

```yaml
dependencies:
  idfxx_wifi:
    version: "^0.9.0"
```

Or add `idfxx_wifi` to the `REQUIRES` list in your component's `CMakeLists.txt`.

## Usage

### Basic Station Example

If `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled:

```cpp
#include <idfxx/wifi>
#include <idfxx/netif>
#include <idfxx/event>
#include <idfxx/log>

// Initialize prerequisites (call once at startup)
idfxx::event_loop::create_system();
idfxx::netif::init();
auto sta_netif = idfxx::wifi::create_default_sta_netif();

// Register event handlers
auto& loop = idfxx::event_loop::system();
loop.listener_add(idfxx::wifi::sta_connected,
    [](const idfxx::wifi::connected_event_data& info) {
        idfxx::log::info("wifi", "Connected to {}", info.ssid);
    });
loop.listener_add(idfxx::netif::sta_got_ip4,
    [](const idfxx::netif::ip4_event_data& info) {
        idfxx::log::info("wifi", "Got IP address");
    });

try {
    // Initialize with default Kconfig settings
    idfxx::wifi::init();

    // Or customize initialization parameters
    // idfxx::wifi::init({.static_rx_buf_num = 8, .nvs_enable = false});

    idfxx::wifi::set_roles(idfxx::wifi::role::sta);
    idfxx::wifi::set_sta_config({
        .ssid = "MyNetwork",
        .password = "secret123",
        .auth_threshold = idfxx::wifi::auth_mode::wpa2_psk,
    });
    idfxx::wifi::start();
    idfxx::wifi::connect();

    // ... application code ...

} catch (const std::system_error& e) {
    idfxx::log::error("wifi", "WiFi error: {}", e.what());
}
```

### Basic Access Point Example

```cpp
#include <idfxx/wifi>
#include <idfxx/netif>
#include <idfxx/event>
#include <idfxx/log>

// Initialize prerequisites (call once at startup)
idfxx::event_loop::create_system();
idfxx::netif::init();
auto ap_netif = idfxx::wifi::create_default_ap_netif();

// Register AP event handlers
auto& loop = idfxx::event_loop::system();
loop.listener_add(idfxx::wifi::ap_sta_connected,
    [](const idfxx::wifi::ap_sta_connected_event_data& info) {
        idfxx::log::info("wifi", "Station connected (AID: {})", info.aid);
    });
loop.listener_add(idfxx::wifi::ap_sta_disconnected,
    [](const idfxx::wifi::ap_sta_disconnected_event_data& info) {
        idfxx::log::info("wifi", "Station disconnected (AID: {})", info.aid);
    });

try {
    idfxx::wifi::init();
    idfxx::wifi::set_roles(idfxx::wifi::role::ap);
    idfxx::wifi::set_ap_config({
        .ssid = "MyAccessPoint",
        .password = "ap_password",
        .channel = 6,
        .authmode = idfxx::wifi::auth_mode::wpa2_psk,
        .max_connection = 4,
    });
    idfxx::wifi::start();

    // Query connected stations
    auto stations = idfxx::wifi::get_sta_list();
    for (const auto& sta : stations) {
        idfxx::log::info("wifi", "Station RSSI: {}", sta.rssi);
    }

} catch (const std::system_error& e) {
    idfxx::log::error("wifi", "WiFi error: {}", e.what());
}
```

### Result-based API

If `CONFIG_COMPILER_CXX_EXCEPTIONS` is *not* enabled, the result-based API must be used:

```cpp
#include <idfxx/wifi>
#include <idfxx/netif>
#include <idfxx/log>

// Initialize prerequisites
idfxx::event_loop::try_create_system();
idfxx::netif::try_init();
auto netif = idfxx::wifi::try_create_default_sta_netif();

// Initialize and configure
if (auto r = idfxx::wifi::try_init(); !r) {
    idfxx::log::error("wifi", "Failed to init: {}", r.error().message());
    return;
}
if (auto r = idfxx::wifi::try_set_roles(idfxx::wifi::role::sta); !r) {
    idfxx::log::error("wifi", "Failed to set roles: {}", r.error().message());
    return;
}
if (auto r = idfxx::wifi::try_set_sta_config({
        .ssid = "MyNetwork",
        .password = "secret123",
    }); !r) {
    idfxx::log::error("wifi", "Failed to configure: {}", r.error().message());
    return;
}

// Start and connect
if (auto r = idfxx::wifi::try_start(); !r) {
    idfxx::log::error("wifi", "Failed to start: {}", r.error().message());
    return;
}
if (auto r = idfxx::wifi::try_connect(); !r) {
    idfxx::log::error("wifi", "Failed to connect: {}", r.error().message());
    return;
}
```

### Scanning for Access Points

```cpp
idfxx::wifi::init();
idfxx::wifi::set_roles(idfxx::wifi::role::sta);
idfxx::wifi::set_sta_config({.ssid = "MyNetwork", .password = "secret123"});
idfxx::wifi::start();

// Blocking scan (waits for results)
auto aps = idfxx::wifi::scan();
for (const auto& ap : aps) {
    idfxx::log::info("wifi", "Found: {} (RSSI: {})", ap.ssid, ap.rssi);
}

// Non-blocking scan (use with event loop)
idfxx::wifi::scan_start({.channel = 6, .show_hidden = true});
// Listen for wifi::event_id::scan_done, then call:
// auto results = idfxx::wifi::scan_get_results();
```

## API Overview

The component provides two API styles:
- **Exception-based API** (requires `CONFIG_COMPILER_CXX_EXCEPTIONS`) — functions throw `std::system_error` on error
- **Result-based API** (always available) — functions prefixed with `try_*` return `result<T>`

All functions listed below have a corresponding `try_*` variant that returns `result<T>`.

### Lifecycle

`init(init_config)`, `deinit()`, `start()`, `stop()`, `restore()`

### Roles & Configuration

`set_roles(flags<role>)` / `get_roles()`, `set_sta_config(sta_config)` / `get_sta_config()`, `set_ap_config(ap_config)` / `get_ap_config()`

### Connection

`connect()`, `disconnect()`, `deauth_sta(aid)`, `get_sta_list()`, `ap_get_sta_aid(mac)`, `sta_get_aid()`, `get_negotiated_phymode()`, `get_ap_info()`

### Scanning

`scan(scan_config)` (blocking), `scan_start(scan_config)` / `scan_get_results()` (non-blocking), `scan_stop()`, `scan_get_ap_num()`, `clear_ap_list()`, `set_scan_parameters(scan_default_params)` / `get_scan_parameters()`

### Network Configuration

`set_power_save(power_save)` / `get_power_save()`, `set_bandwidth(role, bandwidth)` / `get_bandwidth(role)`, `set_mac(role, mac)` / `get_mac(role)`, `set_channel(primary, second)` / `get_channel()`, `set_max_tx_power(int8_t)` / `get_max_tx_power()`, `set_rssi_threshold(int32_t)` / `get_rssi()`, `set_country(country_config)` / `get_country()`, `set_country_code(string_view, ieee80211d_enabled)` / `get_country_code()`, `set_protocol(role, flags<protocol>)` / `get_protocol(role)`, `set_band(band)` / `get_band()`, `set_storage(storage)`

### Advanced Features

- **Promiscuous mode**: `set_promiscuous(bool)`, `set_promiscuous_rx_cb(callback)`, `set_promiscuous_filter(flags)` / `set_promiscuous_ctrl_filter(flags)`
- **CSI**: `set_csi(bool)`, `set_csi_config(csi_config)`, `set_csi_rx_cb(callback, ctx)` (fields of `csi_config` vary by target: Wi-Fi 4/5 vs Wi-Fi 6 HE)
- **FTM**: `ftm_initiate_session(config)`, `ftm_end_session()`, `ftm_resp_set_offset(int16_t)`, `ftm_get_report(max_entries)`
- **Raw 802.11**: `tx_80211(role, span, en_sys_seq)`, `register_80211_tx_cb(callback)`
- **Vendor IE**: `set_vendor_ie(enable, type, id, data)`, `set_vendor_ie_cb(callback, ctx)`

See the [full API documentation](https://cleishm.github.io/idfxx/group__idfxx__wifi.html) for complete details.

## Error Handling

The `idfxx::wifi::errc` enum provides WiFi-specific error codes:

- `not_init` - WiFi driver not initialized
- `not_started` - WiFi driver not started
- `not_stopped` - WiFi driver not stopped
- `if_error` - WiFi interface error
- `mode` - WiFi mode error
- `state` - WiFi internal state error
- `conn` - WiFi internal control block of station error
- `nvs` - WiFi internal NVS module error
- `mac` - MAC address is invalid
- `ssid` - Invalid SSID
- `password` - Invalid password
- `timeout` - Operation timed out
- `wake_fail` - WiFi is in sleep state and wakeup failed
- `would_block` - The caller would block
- `not_connect` - Station not connected
- `post` - Failed to post event to WiFi task
- `init_state` - Invalid WiFi state when init/deinit is called
- `stop_state` - WiFi stop in progress
- `not_assoc` - WiFi connection not associated
- `tx_disallow` - WiFi TX is disallowed
- `twt_full` - No available TWT flow ID
- `twt_setup_timeout` - TWT setup response timeout
- `twt_setup_txfail` - TWT setup frame TX failed
- `twt_setup_reject` - TWT setup request was rejected by AP
- `discard` - Frame discarded
- `roc_in_progress` - Remain-on-channel operation in progress

## Events

WiFi and IP events are available via the idfxx event loop using type-safe typed events:

- `wifi::events` — WiFi event base (station start/stop, connect/disconnect, scan, AP events, FTM, TWT, NAN)
- `netif::ip_events` — IP event base (got IP, lost IP, AP station IP assigned, got IPv6) — defined in `idfxx_netif`

Typed event constants (e.g., `wifi::sta_connected`, `netif::sta_got_ip4`) pair an event ID with its data type, enabling type-safe listener registration where callbacks receive the correct data type directly:

```cpp
// WiFi events with data - callback receives const reference
loop.listener_add(wifi::sta_connected,
    [](const wifi::connected_event_data& info) { ... });

// WiFi events without data - callback takes no arguments
loop.listener_add(wifi::sta_start, []() { ... });

// IP events (from idfxx_netif)
loop.listener_add(netif::sta_got_ip4,
    [](const netif::ip4_event_data& info) { ... });
```

## Important Notes

- **Dual API Pattern**: Component provides both result-based (`try_*`) and exception-based APIs. Exception-based functions require `CONFIG_COMPILER_CXX_EXCEPTIONS`.
- **Procedural API**: WiFi is managed through namespace-level free functions rather than class instances, reflecting the global nature of the WiFi subsystem.
- **Prerequisites**: Create the system event loop before using WiFi functions. Most applications will also need to initialize `idfxx::netif::init()` and create the appropriate default netif (`idfxx::wifi::create_default_sta_netif()` for STA, `idfxx::wifi::create_default_ap_netif()` for AP).
- **No auto-reconnect**: Reconnection logic should be implemented via event handlers.
- **TX power units**: TX power values are in 0.25 dBm units (e.g., 60 = 15 dBm). Hardware may clamp the value to its supported range.
- **Callback context**: Promiscuous mode, CSI, and FTM callbacks run in the WiFi task context. Keep processing minimal or defer work to another task.
- **FTM support**: Requires both initiator and responder to support FTM. Check `ap_record::ftm_responder` and `ap_record::ftm_initiator` fields from scan results.

## License

Apache License 2.0 - see [LICENSE](LICENSE) for details.
