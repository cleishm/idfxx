# Simple HTTP Server

Runs an HTTP server with a JSON status endpoint, a greeting that reads a query parameter, and an endpoint
that echoes the request body.

## What it demonstrates

- Joining a Wi-Fi network with `idfxx::wifi`, and reconnecting with `idfxx::wifi::try_connect()` when the
  connection drops
- Starting an `idfxx::http::server` on port 80
- Registering GET handlers with `on_get()`: `/api/status` returns a JSON request count, and `/hello` reads
  the `name` query parameter with `query_param()`
- Registering a POST handler with `on_post()`: `/echo` reads the request body with `recv_body()` and sends
  it back
- Setting the response type with `set_content_type()` and sending the response with `send()`

The example uses the exception-based API (`CONFIG_COMPILER_CXX_EXCEPTIONS=y` in `sdkconfig.defaults`).

## Hardware required

- Any ESP32-series development board with Wi-Fi
- A 2.4 GHz Wi-Fi access point using WPA2 or stronger security
- A computer on the same network, with a web browser or `curl`

The ESP32-H2 has no Wi-Fi, so it can't run this example.

## Configuration

Set `WIFI_SSID` and `WIFI_PASSWORD` in `main/main.cpp` to your network's name and password.

## How to use

Create a project from this example:

```bash
idf.py create-project-from-example "cleishm/idfxx_http_server:simple_server"
cd simple_server
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

Once the board joins the network, it logs its IP address. Open `http://<IP>/hello?name=esp32` in a
browser, or try the endpoints with `curl`:

```bash
curl http://<IP>/api/status
curl "http://<IP>/hello?name=esp32"
curl -d "some text" http://<IP>/echo
```
