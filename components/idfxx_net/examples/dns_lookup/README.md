# DNS Lookup

Joins a Wi-Fi network and resolves host names to addresses with the `idfxx::net` resolver.

## What it demonstrates

- Joining a Wi-Fi network with `idfxx::wifi`, and waiting for an IP address with an `idfxx::event_group`
- Resolving every address of a host with `idfxx::net::resolve()`, and printing the returned endpoints
- Limiting results to one address family with the `family` option
- Parsing a literal IP address without a DNS query, using the `numeric_host` option
- Getting only the first match with `idfxx::net::resolve_one()`
- Visiting each result without building a list, using `idfxx::net::resolve_each()`

The example uses the exception-based API (`CONFIG_COMPILER_CXX_EXCEPTIONS=y` in `sdkconfig.defaults`).

## Hardware required

- Any ESP32-series development board with Wi-Fi
- A 2.4 GHz Wi-Fi access point using WPA2 or stronger security, with internet access

The ESP32-H2 has no Wi-Fi, so it can't run this example.

## Configuration

Set `WIFI_SSID` and `WIFI_PASSWORD` in `main/main.cpp` to your network's name and password. To look up
other hosts, change the `HOSTS` list in the same file.

## How to use

Create a project from this example:

```bash
idf.py create-project-from-example "cleishm/idfxx_net:dns_lookup"
cd dns_lookup
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
