# SNTP Time Sync

Joins a Wi-Fi network, sets the system clock from internet time servers, and prints the current time every
10 seconds.

## What it demonstrates

- Creating the default event loop with `idfxx::event_loop::create_system()`, initializing the network stack
  with `idfxx::netif::init()`, and creating the Wi-Fi station interface with `idfxx::wifi::make_sta_netif()`
- Configuring SNTP with `idfxx::netif::sntp::init()` before connecting, using the servers `pool.ntp.org`
  and `time.google.com`
- Logging the IP address from the `idfxx::netif::sta_got_ip4` event, and reconnecting with
  `idfxx::wifi::try_connect()` when the `sta_disconnected` event arrives
- Joining the network with `idfxx::wifi`
- Waiting up to 30 seconds for the first time sync with `idfxx::netif::sntp::sync_wait()`
- Printing the time every 10 seconds, pausing with `idfxx::delay()`

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
idf.py create-project-from-example "cleishm/idfxx_netif:sntp_sync"
cd sntp_sync
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

The time is printed in UTC.
