# Netconn Echo Server

A TCP echo server built on the `idfxx::net::netconn` API, which sends received data back one segment at a
time without copying it.

Most applications should use the socket API instead; the `tcp_echo_server` example is the equivalent
server built on `idfxx::net::listener` and `stream_socket`.

## What it demonstrates

- Joining a Wi-Fi network with `idfxx::wifi`, and waiting for an IP address with an `idfxx::event_group`
- Binding and listening on a port with `idfxx::net::netconn::listener`
- Accepting a connection as a `netconn::stream_channel`
- Receiving data with `recv()`, which returns a `netconn::buffer` that isn't open once the client closes
  the connection
- Walking the buffer's segments with `segments()`, and sending each one back with `send()`

The example uses the exception-based API (`CONFIG_COMPILER_CXX_EXCEPTIONS=y` in `sdkconfig.defaults`).

## Hardware required

- Any ESP32-series development board with Wi-Fi
- A 2.4 GHz Wi-Fi access point using WPA2 or stronger security
- A computer on the same network, with `nc` (netcat)

The ESP32-H2 has no Wi-Fi, so it can't run this example.

## Configuration

Set `WIFI_SSID` and `WIFI_PASSWORD` in `main/main.cpp` to your network's name and password.

## How to use

Create a project from this example:

```bash
idf.py create-project-from-example "cleishm/idfxx_net:netconn_echo"
cd netconn_echo
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

Once the board joins the network, it logs its IP address. From the other computer, connect to port 8081:

```bash
nc <IP> 8081
```

Each line you type is echoed back. The server handles one client at a time.
