# Custom Error Pages

Runs an HTTP server that replaces the default "404 Not Found" and "405 Method Not Allowed" responses with
its own pages.

## What it demonstrates

- Joining a Wi-Fi network with `idfxx::wifi`, and reconnecting with `idfxx::wifi::try_connect()` when the
  connection drops
- Starting an `idfxx::http::server` on port 80 with `wildcard_uri_match` enabled
- Registering custom error handlers with `on_error()` for `status_code::not_found` and
  `status_code::method_not_allowed`, which also cover errors the server detects itself, such as an unknown
  URL or the wrong method
- Matching `/items/*` with a wildcard route, and reading the item name from `uri()`
- Sending the custom 404 page for an unknown item with `handle_error()`

The example uses the exception-based API (`CONFIG_COMPILER_CXX_EXCEPTIONS=y` in `sdkconfig.defaults`).

## Hardware required

- Any ESP32-series development board with Wi-Fi
- A 2.4 GHz Wi-Fi access point using WPA2 or stronger security
- A computer on the same network, with `curl`

The ESP32-H2 has no Wi-Fi, so it can't run this example.

## Configuration

Set `WIFI_SSID` and `WIFI_PASSWORD` in `main/main.cpp` to your network's name and password.

## How to use

Create a project from this example:

```bash
idf.py create-project-from-example "cleishm/idfxx_http_server:error_pages"
cd error_pages
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

Once the board joins the network, it logs its IP address. From the other computer, try:

```bash
curl -i http://<IP>/items/apple      # found
curl -i http://<IP>/items/durian     # unknown item: custom 404 page
curl -i http://<IP>/missing          # unknown URL: same 404 page
curl -i -X POST http://<IP>/         # wrong method: custom 405 response
```
