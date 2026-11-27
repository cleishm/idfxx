# Advertise and Query

Joins a Wi-Fi network, answers as `<hostname>.local`, advertises a web service, and finds the other
web services on the network.

## What it demonstrates

- Initializing NVS with `idfxx::nvs::flash::init()`, which Wi-Fi needs, and erasing it if it is full
  or has a newer format
- Joining a network with `idfxx::wifi::connect_sta()`, after creating the system event loop,
  initializing the network stack and creating the station interface
- Listening for `idfxx::mdns::hostname_changed` on the system event loop
- Starting mDNS with `idfxx::mdns::init()` and claiming a hostname with
  `idfxx::mdns::set_hostname()`
- Advertising an `_http._tcp` service with a TXT record, using `idfxx::mdns::service`
- Following other `_http._tcp` services as they appear and go away, with `idfxx::mdns::browser`
- Asking for `_http._tcp` services once with `idfxx::mdns::query_ptr()`, and logging each answer's
  instance name, hostname, port, addresses and TXT items

The example uses the exception-based API (`CONFIG_COMPILER_CXX_EXCEPTIONS=y` in
`sdkconfig.defaults`).

## Hardware required

- Any ESP32-series development board with Wi-Fi
- A 2.4 GHz Wi-Fi access point using WPA2 or stronger security
- Optionally, other devices on the same network advertising `_http._tcp` services (many printers,
  NAS boxes and routers do), or a computer running `dns-sd -R test _http._tcp local 8080` (macOS) or
  `avahi-publish -s test _http._tcp 8080` (Linux)

The ESP32-H2 has no Wi-Fi, so it can't run this example.

## Configuration

Set `WIFI_SSID` and `WIFI_PASSWORD` in `main/main.cpp` to your network's name and password, and
optionally `HOSTNAME` to the name the board answers for.

## How to use

Create a project from this example:

```bash
idf.py create-project-from-example "cleishm/idfxx_mdns:advertise_query"
cd advertise_query
```

Set the target:

```bash
idf.py set-target esp32
```

Then set your Wi-Fi credentials in `main/main.cpp` (see [Configuration](#configuration)), build and
flash it, and watch the serial output:

```bash
idf.py -p PORT flash monitor
```

Replace `esp32` with your target and `PORT` with your board's serial port. Press `Ctrl-]` to exit
the monitor.

From a computer on the same network, `ping idfxx-mdns.local` reaches the board, and
`dns-sd -B _http._tcp` (macOS) or `avahi-browse -r _http._tcp` (Linux) lists its service.
