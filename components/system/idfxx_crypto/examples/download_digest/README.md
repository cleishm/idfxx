# Download Digest

Joins a Wi-Fi network, downloads a file over HTTPS, and logs its SHA-256 digest, hashing the file as
it arrives.

## What it demonstrates

- Joining a Wi-Fi network and waiting for an IP address with `idfxx::wifi::connect_sta()`
- Creating an `idfxx::http::client` for a URL, and checking the server's certificate against the
  ESP-IDF certificate bundle with the `crt_bundle_attach` option
- Streaming the response body with `open()`, `fetch_headers()` and `read()`, after checking with
  `status_code()` that the server sent the file (status 200) rather than an error page
- Hashing the body piece by piece with `idfxx::crypto::hash::sha256`'s `update()`, then getting the
  digest with `finish()`, so the file never has to fit in memory
- Logging the digest as hex text with `idfxx::hex`

The example uses the exception-based API (`CONFIG_COMPILER_CXX_EXCEPTIONS=y` in
`sdkconfig.defaults`).

## Hardware required

- Any ESP32-series development board with Wi-Fi
- A 2.4 GHz Wi-Fi access point using WPA2 or stronger security, with internet access

The ESP32-H2 has no Wi-Fi, so it can't run this example.

## Configuration

Set `WIFI_SSID` and `WIFI_PASSWORD` in `main/main.cpp` to your network's name and password. To
download a different file, change `DOWNLOAD_URL` in the same file. Redirects aren't followed, so use
the file's final URL.

## How to use

Create a project from this example:

```bash
idf.py create-project-from-example "cleishm/idfxx_crypto:download_digest"
cd download_digest
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

To check the result, compute the digest of the same file on your computer and compare it with the
one the example logs:

```bash
curl -s https://httpbin.org/image/png | shasum -a 256
```
