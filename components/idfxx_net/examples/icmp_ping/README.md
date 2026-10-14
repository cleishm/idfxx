# ICMP Ping

Sends a single ping (ICMP echo request) over a raw socket and logs the reply and round-trip time.

## What it demonstrates

- Joining a Wi-Fi network with `idfxx::wifi`, and waiting for an IP address with an `idfxx::event_group`
- Parsing the target address with `idfxx::net::resolve_one()` and the `numeric_host` option
- Opening a raw ICMP socket with `idfxx::net::raw_socket`, and setting a receive timeout with `set_recv_timeout()`
- Sending a hand-built echo request with `send_to()`
- Receiving the reply and its sender with `recv_from()`, and skipping the IP header that raw IPv4 sockets
  deliver in front of the ICMP message

The example uses the exception-based API (`CONFIG_COMPILER_CXX_EXCEPTIONS=y` in `sdkconfig.defaults`).

## Hardware required

- Any ESP32-series development board with Wi-Fi
- A 2.4 GHz Wi-Fi access point using WPA2 or stronger security, with internet access

The ESP32-H2 has no Wi-Fi, so it can't run this example.

## Configuration

Set `WIFI_SSID` and `WIFI_PASSWORD` in `main/main.cpp` to your network's name and password. The example
pings `8.8.8.8`; to ping a different IPv4 address, change `TARGET_HOST` in the same file.

## How to use

Create a project from this example:

```bash
idf.py create-project-from-example "cleishm/idfxx_net:icmp_ping"
cd icmp_ping
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
