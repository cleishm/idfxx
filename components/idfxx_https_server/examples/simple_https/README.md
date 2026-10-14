# Simple HTTPS Server

Runs an HTTPS server on port 443, using a self-signed certificate built into the firmware.

## What it demonstrates

- Joining a Wi-Fi network with `idfxx::wifi`, and reconnecting with `idfxx::wifi::try_connect()` when the
  connection drops
- Starting an `idfxx::http::ssl_server` on port 443, with the certificate and private key passed as
  `server_cert` and `private_key`
- Registering GET handlers for `/api/status` and `/hello` with `on_get()`, the same way as for a plain
  `idfxx::http::server`

The example uses the exception-based API (`CONFIG_COMPILER_CXX_EXCEPTIONS=y` in `sdkconfig.defaults`).

## Hardware required

- Any ESP32-series development board with Wi-Fi
- A 2.4 GHz Wi-Fi access point using WPA2 or stronger security
- A computer on the same network, with `curl`

The ESP32-H2 has no Wi-Fi, so it can't run this example.

## Configuration

Set `WIFI_SSID` and `WIFI_PASSWORD` in `main/main.cpp` to your network's name and password.

The certificate and key in `main/certs/` (`server.crt` and `server.key`, in PEM format) are self-signed
and for demonstration only. Replace them with your own before using the server for anything real.

## How to use

Create a project from this example:

```bash
idf.py create-project-from-example "cleishm/idfxx_https_server:simple_https"
cd simple_https
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

Once the board joins the network, it logs its IP address. From the other computer, try the endpoints with
`curl`. Because the certificate is self-signed, pass `--insecure` to skip certificate checks:

```bash
curl --insecure https://<IP>/hello
curl --insecure https://<IP>/api/status
```
