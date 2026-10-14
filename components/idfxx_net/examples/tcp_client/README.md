# TCP Client

Connects to `example.com` over TCP, sends a plain HTTP request, and logs the response.

## What it demonstrates

- Joining a Wi-Fi network with `idfxx::wifi`, and waiting for an IP address with an `idfxx::event_group`
- Resolving a host name and connecting in one step with the `idfxx::net::stream_socket` (host, port)
  constructor, with the `no_delay` option
- Reading the connected address with `peer_endpoint()`, and setting a receive timeout with `set_recv_timeout()`
- Sending the whole request with `send_all()`
- Reading the response with `recv()` until the server closes the connection

The example uses the exception-based API (`CONFIG_COMPILER_CXX_EXCEPTIONS=y` in `sdkconfig.defaults`).

## Hardware required

- Any ESP32-series development board with Wi-Fi
- A 2.4 GHz Wi-Fi access point using WPA2 or stronger security, with internet access

The ESP32-H2 has no Wi-Fi, so it can't run this example.

## Configuration

Set `WIFI_SSID` and `WIFI_PASSWORD` in `main/main.cpp` to your network's name and password.

## How to use

Create a project from this example:

```bash
idf.py create-project-from-example "cleishm/idfxx_net:tcp_client"
cd tcp_client
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
