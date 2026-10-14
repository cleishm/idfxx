# UDP Echo Server

A UDP server that sends each datagram it receives back to its sender.

## What it demonstrates

- Joining a Wi-Fi network with `idfxx::wifi`, and waiting for an IP address with an `idfxx::event_group`
- Opening an IPv4 `idfxx::net::datagram_socket` and binding it to a port with `bind()`
- Receiving datagrams and their sender with `recv_from()`, and detecting ones too large for the buffer
  with `truncated`
- Replying to the sender with `send_to()`

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
idf.py create-project-from-example "cleishm/idfxx_net:udp_echo"
cd udp_echo
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

Once the board joins the network, it logs its IP address. From the other computer, send a datagram to
port 5005:

```bash
echo hi | nc -u <IP> 5005
```

`nc` prints the echoed reply.
