# Hardware Info

Prints the chip model, revision, features and MAC addresses.

## What it demonstrates

- Reading chip information with `idfxx::chip_info::get()`: model, core count and revision
- Checking chip features such as Wi-Fi, Bluetooth, IEEE 802.15.4 and embedded flash or PSRAM with `features()` and `idfxx::chip_feature`
- Reading the base MAC address with `idfxx::base_mac_address()`
- Reading the MAC address for each interface with `idfxx::read_mac()` and `idfxx::mac_type`, skipping the Wi-Fi and Bluetooth addresses on chips without them (`SOC_WIFI_SUPPORTED`, `CONFIG_ESP_MAC_ADDR_UNIVERSE_BT`)
- Printing MAC addresses directly, and reading their individual bytes with `bytes()`

The example uses the exception-based API (`CONFIG_COMPILER_CXX_EXCEPTIONS=y` in `sdkconfig.defaults`).

## Hardware required

- Any ESP32-series development board

## How to use

Create a project from this example:

```bash
idf.py create-project-from-example "cleishm/idfxx_hw_support:hardware_info"
cd hardware_info
```

Then build and flash it, then watch the serial output:

```bash
idf.py set-target esp32
idf.py -p PORT flash monitor
```

Replace `esp32` with your target and `PORT` with your board's serial port. Press `Ctrl-]` to exit the monitor.
