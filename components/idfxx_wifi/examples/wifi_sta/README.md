# Wi-Fi Station

Joins a Wi-Fi network, logs the IP address it gets, and reconnects whenever the connection drops.

## What it demonstrates

- Creating the default event loop with `idfxx::event_loop::create_system()`, initializing the network stack
  with `idfxx::netif::init()`, and creating the station interface with `idfxx::wifi::make_sta_netif()`
- Handling the `idfxx::wifi::sta_connected` event, and printing the network's SSID, channel and security mode
- Handling the `idfxx::wifi::sta_disconnected` event, and reconnecting with `idfxx::wifi::try_connect()`
- Handling the `idfxx::netif::sta_got_ip4` event, and printing the IP address
- Joining the network with `idfxx::wifi::init()`, `set_roles()`, `set_sta_config()`, `start()` and `connect()`

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
idf.py create-project-from-example "cleishm/idfxx_wifi:wifi_sta"
cd wifi_sta
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
