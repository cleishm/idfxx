# Webhook Verify

Joins a Wi-Fi network and receives webhooks over HTTP, accepting a request only if its body is
signed with HMAC-SHA-256 under a secret shared with the sender.

Each request carries its signature in an `X-Signature: sha256=<hex>` header, in the style of GitHub
webhooks. The board authenticates the body as it arrives, so it never has to fit in memory, and
answers `200 OK` if the signature matches, or `401 Unauthorized` if the header is missing or
malformed or the signature doesn't match.

## What it demonstrates

- Joining a Wi-Fi network and waiting for an IP address with `idfxx::wifi::connect_sta()`
- Importing the shared secret as a key with `idfxx::crypto::mac::hmac_sha256::import_key()`
- Starting an `idfxx::http::server` and registering a POST handler with `on_post()`
- Reading the `X-Signature` header with `header()`, and the body piece by piece with `read()`
- Authenticating the body with an `idfxx::crypto::mac::hmac_sha256` created from the key, adding
  each piece with `update()`, then checking the sender's signature with `verify()`, which compares
  the tags in constant time
- Responding with `send()`, or with `send_error()` when a request is rejected
- Catching a failure to read or authenticate the body, and answering with `handle_error()`:
  `408 Request Timeout` if the body stopped arriving, `400 Bad Request` if it ended early, or
  `500 Internal Server Error`

The example uses the exception-based API (`CONFIG_COMPILER_CXX_EXCEPTIONS=y` in
`sdkconfig.defaults`).

## Hardware required

- Any ESP32-series development board with Wi-Fi
- A 2.4 GHz Wi-Fi access point using WPA2 or stronger security
- A computer on the same network, with `curl` and `openssl`

The ESP32-H2 has no Wi-Fi, so it can't run this example.

## Configuration

Set `WIFI_SSID` and `WIFI_PASSWORD` in `main/main.cpp` to your network's name and password, and
`WEBHOOK_SECRET` to the secret shared with the senders (it must not be empty). Building a secret
into the firmware keeps the example short; a real application should provision it per device
instead, for example into encrypted NVS.

## How to use

Create a project from this example:

```bash
idf.py create-project-from-example "cleishm/idfxx_crypto:webhook_verify"
cd webhook_verify
```

Set the target:

```bash
idf.py set-target esp32
```

Then set your Wi-Fi credentials and the secret in `main/main.cpp` (see
[Configuration](#configuration)), build and flash it, and watch the serial output:

```bash
idf.py -p PORT flash monitor
```

Replace `esp32` with your target and `PORT` with your board's serial port. Press `Ctrl-]` to exit
the monitor.

Once the board joins the network, it logs the address to send webhooks to. From a computer on the
same network, sign a payload with the secret and send it, replacing `<IP>` with the board's address:

```bash
SECRET=change-me
echo '{"event":"ping"}' > payload.json
SIG=$(openssl dgst -sha256 -hmac "$SECRET" -r payload.json | cut -d' ' -f1)
curl -i --data-binary @payload.json -H "X-Signature: sha256=$SIG" http://<IP>/hook
```

The board answers `200 OK` and logs the size of the webhook. Use `--data-binary` rather than `-d`,
which strips newlines from the file, so the board would receive a different body from the one that
was signed.

To see a request rejected, sign it with a different secret, or leave out the `X-Signature` header;
the board answers `401 Unauthorized`:

```bash
SIG=$(openssl dgst -sha256 -hmac wrong-secret -r payload.json | cut -d' ' -f1)
curl -i --data-binary @payload.json -H "X-Signature: sha256=$SIG" http://<IP>/hook
```
