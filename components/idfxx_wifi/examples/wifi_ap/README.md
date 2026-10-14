# Wi-Fi Access Point

Starts a Wi-Fi access point and logs stations as they connect and disconnect.

## What it demonstrates

- Creating the default event loop with `idfxx::event_loop::create_system()`, initializing the network stack
  with `idfxx::netif::init()`, and creating the access point interface with `idfxx::wifi::make_ap_netif()`
- Handling the `idfxx::wifi::ap_sta_connected` and `ap_sta_disconnected` events
- Starting the access point with `idfxx::wifi::init()`, `set_roles()`, `set_ap_config()` and `start()`
- Listing the connected stations and their signal strength with `idfxx::wifi::get_sta_list()`

The example uses the exception-based API (`CONFIG_COMPILER_CXX_EXCEPTIONS=y` in `sdkconfig.defaults`).

## Hardware required

- Any ESP32-series development board with Wi-Fi
- A phone or computer to connect to the access point

The ESP32-H2 has no Wi-Fi, so it can't run this example.

## How to use

Create a project from this example:

```bash
idf.py create-project-from-example "cleishm/idfxx_wifi:wifi_ap"
cd wifi_ap
```

Then build and flash it, then watch the serial output:

```bash
idf.py set-target esp32
idf.py -p PORT flash monitor
```

Replace `esp32` with your target and `PORT` with your board's serial port. Press `Ctrl-]` to exit the monitor.

Connect your phone or computer to the network `idfxx_ap` with the password `password123`. The example logs
each connection and disconnection, and every 10 seconds lists the connected stations. The access point uses
WPA2-PSK on channel 6 and accepts up to 4 stations; to change these settings, edit the `set_ap_config()`
call in `main/main.cpp`.
