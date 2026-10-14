# HTTP GET

Joins a Wi-Fi network, fetches `https://httpbin.org/get` over HTTPS, and logs the response.

## What it demonstrates

- Joining a Wi-Fi network with `idfxx::wifi`, and waiting for an IP address with an `idfxx::event_group`
- Creating an `idfxx::http::client` for a URL, and checking the server's certificate against the ESP-IDF
  certificate bundle with the `crt_bundle_attach` option
- Collecting the response body from `on_data` events in an `on_event` callback
- Sending the request with `perform()`
- Reading the response status with `status_code()` and the `Content-Type` header with `get_header()`

The example uses the exception-based API (`CONFIG_COMPILER_CXX_EXCEPTIONS=y` in `sdkconfig.defaults`).

## Hardware required

- Any ESP32-series development board with Wi-Fi
- A 2.4 GHz Wi-Fi access point using WPA2 or stronger security, with internet access

The ESP32-H2 has no Wi-Fi, so it can't run this example.

## Configuration

Set `WIFI_SSID` and `WIFI_PASSWORD` in `main/main.cpp` to your network's name and password. To fetch a
different URL, change `REQUEST_URL` in the same file.

## How to use

Create a project from this example:

```bash
idf.py create-project-from-example "cleishm/idfxx_http_client:http_get"
cd http_get
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
