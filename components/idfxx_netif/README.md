# idfxx_netif

Type-safe network interface management for ESP32.

📚 **[Full API Documentation](https://cleishm.github.io/idfxx/group__idfxx__netif.html)**

## Features

- RAII network interface lifecycle management with move-only ownership
- IPv4 and IPv6 address value types with string conversion and formatting
- DHCP client and server control
- DNS server configuration
- SNTP time synchronization client
- IP event integration (got IP, lost IP, AP station IP assigned, IPv6)
- Network interface flags, hostname, MAC address, and route priority
- NAPT (Network Address Port Translation) support
- Domain-specific error codes with `std::error_code` integration

## Requirements

- ESP-IDF 5.5 or later
- C++23 compiler
- System event loop must be created before use

## Installation

### ESP-IDF Component Manager

Add to your project's `idf_component.yml`:

```yaml
dependencies:
  idfxx_netif:
    version: "^1.0.0"
```

Or add `idfxx_netif` to the `REQUIRES` list in your component's `CMakeLists.txt`.

## Usage

### Basic Network Interface Setup

If `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled:

```cpp
#include <idfxx/netif>
#include <idfxx/event>
#include <idfxx/log>
#include <idfxx/wifi>

// Initialize prerequisites (call once at startup)
idfxx::event_loop::create_system();
idfxx::netif::init();
auto sta_netif = idfxx::wifi::create_default_sta_netif();

// Register IP event handlers
auto& loop = idfxx::event_loop::system();
loop.listener_add(idfxx::netif::sta_got_ip4,
    [](const idfxx::netif::ip4_event_data& info) {
        idfxx::log::info("netif", "Got IP: {}", info.ip4.ip);
    });

// Query interface properties
auto hostname = sta_netif.get_hostname();
auto ip = sta_netif.get_ip_info();
```

### Result-based API

If `CONFIG_COMPILER_CXX_EXCEPTIONS` is *not* enabled:

```cpp
#include <idfxx/netif>

// Initialize
if (auto r = idfxx::netif::try_init(); !r) {
    // handle error
    return;
}

// Query IP info
if (auto r = sta_netif.try_get_ip_info(); r) {
    auto ip_str = idfxx::to_string(r->ip);
}
```

### SNTP Time Synchronization

```cpp
#include <idfxx/netif>

idfxx::netif::sntp::init({
    .servers = {"pool.ntp.org", "time.google.com"},
});

// Wait for time sync (blocking)
if (idfxx::netif::sntp::sync_wait(std::chrono::seconds(10))) {
    // Time is synchronized
}

// Cleanup
idfxx::netif::sntp::deinit();
```

## API Overview

The component provides two API styles:
- **Exception-based API** (requires `CONFIG_COMPILER_CXX_EXCEPTIONS`) — methods throw `std::system_error` on error
- **Result-based API** (always available) — methods prefixed with `try_*` return `result<T>`

### IP Address Types (from `idfxx::net` in `idfxx_core`)

- `net::ip4_addr` — IPv4 address value type with construction from octets and string conversion
- `net::ip6_addr` — IPv6 address value type with zone ID support
- `net::ip4_info` — IPv4 address, netmask, and gateway aggregate
- `net::ip6_info` — IPv6 address aggregate
- `dns_info` — DNS server address (uses `net::ip4_addr`)

### Subsystem Lifecycle

`init()`, `deinit()`

### Network Interface (interface class)

**Status**: `is_up()`, `key()`, `description()`, `get_route_priority()` / `set_route_priority()`, `get_flags()`, `get_event_id()`

**IDF Interop**: `idf_handle()`, `idf_index()`, `idf_name()` (lwIP only)

**MAC**: `get_mac()` / `set_mac()`

**Hostname**: `get_hostname()` / `set_hostname()`

**IPv4**: `get_ip4_info()` / `set_ip4_info()`

**IPv6**: `create_ip6_linklocal()`, `get_ip6_linklocal()`, `get_ip6_global()`, `get_all_ip6()`, `get_all_preferred_ip6()`

**DHCP Client**: `dhcp_client_start()`, `dhcp_client_stop()`, `is_dhcp_client_running()`

**DHCP Server**: `dhcp_server_start()`, `dhcp_server_stop()`, `is_dhcp_server_running()`

**DNS**: `set_dns()` / `get_dns()`

**NAPT**: `napt_enable()`

### Discovery

`get_nr_of_ifs()`, `get_default()`, `set_default()`, `find_by_key()`

### SNTP

`sntp::init()`, `sntp::start()`, `sntp::deinit()`, `sntp::sync_wait()`

## Error Handling

The `idfxx::netif::errc` enum provides netif-specific error codes:

- `invalid_params` — Invalid parameters
- `if_not_ready` — Interface not ready
- `dhcpc_start_failed` — DHCP client start failed
- `dhcp_already_started` — DHCP already started
- `dhcp_already_stopped` — DHCP already stopped
- `no_mem` — Out of memory
- `dhcp_not_stopped` — DHCP not stopped
- `driver_attach_failed` — Driver attach failed
- `init_failed` — Initialization failed
- `dns_not_configured` — DNS not configured
- `mld6_failed` — MLD6 operation failed
- `ip6_addr_failed` — IPv6 address operation failed
- `dhcps_start_failed` — DHCP server start failed
- `tx_failed` — Transmit failed

## Events

IP events are available via the idfxx event loop using type-safe typed events:

- `netif::ip_events` — IP event base

Typed event constants pair an event ID with its data type:

```cpp
// Events with data — callback receives const reference
loop.listener_add(netif::sta_got_ip4,
    [](const netif::ip4_event_data& info) { ... });

// Events without data — callback takes no arguments
loop.listener_add(netif::sta_lost_ip4, []() { ... });
```

Available events: `sta_got_ip4`, `sta_lost_ip4`, `ap_sta_ip4_assigned`, `got_ip6`, `eth_got_ip4`, `eth_lost_ip4`, `ppp_got_ip4`, `ppp_lost_ip4`.

## Important Notes

- **Dual API Pattern**: Component provides both result-based (`try_*`) and exception-based APIs. Exception-based methods require `CONFIG_COMPILER_CXX_EXCEPTIONS`.
- **RAII Ownership**: The `interface` class owns its network interface handle. Move-only semantics prevent accidental copies.
- **Prerequisites**: Create the system event loop before using netif functions. Call `init()` before creating any network interfaces.
- **IP Address Byte Order**: `ip4_addr` stores addresses in network byte order.

## License

Apache License 2.0 - see [LICENSE](LICENSE) for details.
