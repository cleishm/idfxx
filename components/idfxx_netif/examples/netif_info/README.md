# Network Interface Info

Joins a Wi-Fi network and, once it has an IP address, prints the station interface's addresses and settings.

## What it demonstrates

- Creating the default event loop with `idfxx::event_loop::create_system()`, and initializing the network
  stack with `idfxx::netif::init()`
- Creating the Wi-Fi station interface with `idfxx::wifi::make_sta_netif()`
- Handling the `idfxx::netif::sta_got_ip4` event, and printing its IP address, netmask, gateway and
  `changed` flag
- Querying the interface with `key()`, `description()`, `is_up()`, `get_route_priority()`,
  `get_ipv4_info()`, `get_hostname()`, `get_mac()` and `is_dhcp_client_running()`
- Listing the main, backup and fallback DNS servers with `get_dns()`
- Joining the network with `idfxx::wifi`

The example uses the exception-based API (`CONFIG_COMPILER_CXX_EXCEPTIONS=y` in `sdkconfig.defaults`).

## Hardware required

- Any ESP32-series development board with Wi-Fi
- A 2.4 GHz Wi-Fi access point using WPA2 or stronger security

The ESP32-H2 has no Wi-Fi, so it can't run this example.

## Configuration

Set `WIFI_SSID` and `WIFI_PASSWORD` in `main/main.cpp` to your network's name and password.

## How to use

Create a project from this example:

```bash
idf.py create-project-from-example "cleishm/idfxx_netif:netif_info"
cd netif_info
```

Set the target:

```bash
idf.py set-target esp32
```

Then set your Wi-Fi credentials in `main/main.cpp` (see [Configuration](#configuration)), build and flash
it, and watch the serial output:

```bash
idf.py -p PORT flash monitor
```

Replace `esp32` with your target and `PORT` with your board's serial port. Press `Ctrl-]` to exit the monitor.
