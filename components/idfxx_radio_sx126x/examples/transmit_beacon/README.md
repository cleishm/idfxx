# Transmit Beacon

Sends a numbered LoRa beacon every two seconds.

## What it demonstrates

- Installing the GPIO interrupt service with `idfxx::gpio::install_isr_service()`, which the radio
  driver needs
- Setting up an SPI bus with `idfxx::spi::master_bus`
- Creating an `idfxx::radio::sx126x` from a config with its control pins and TCXO settings
- Setting the frequency, transmit power and LoRa modulation (SF9, 125 kHz bandwidth, 4/5 coding rate)
  with `configure()`
- Sending a packet with `transmit()`, which waits for the packet to go out and sizes its own timeout
  from the packet's time on air
- Catching `std::system_error` to log radio errors

The example uses the exception-based API (`CONFIG_COMPILER_CXX_EXCEPTIONS=y` in `sdkconfig.defaults`).

## Hardware required

- A Heltec WiFi LoRa 32 V3 (ESP32-S3 with an SX1262), or an ESP32-S3 board wired to an SX1262 module
- An antenna for the radio's frequency band, attached before you run the example; transmitting
  without one can damage the radio

The pins in `main/main.cpp` match the Heltec WiFi LoRa 32 V3. For another board, wire the module as
follows, or change the `PIN_*` constants at the top of `main/main.cpp`:

| SX1262 pin   | ESP32-S3 GPIO |
| ------------ | ------------- |
| MOSI         | GPIO 10       |
| MISO         | GPIO 11       |
| SCK          | GPIO 9        |
| NSS (CS)     | GPIO 8        |
| BUSY         | GPIO 13       |
| DIO1         | GPIO 14       |
| NRESET       | GPIO 12       |

The example also expects the module to use DIO2 to switch its antenna between transmit and receive,
and to run from a TCXO powered by DIO3 at 1.8 V, as on the Heltec board. If your module uses a
crystal, remove the `.tcxo` setting; if its TCXO needs another voltage, change `1800_mV`. For an
SX1261 or SX1268, change `chip_variant::sx1262`.

> [!NOTE]
> These pins don't suit other chips. On the ESP32, GPIO 9–11 are connected to the SPI flash; on the
> ESP32-C3, GPIO 12–14 are; on the ESP32-C6, GPIO 12 and 13 are USB-JTAG pins; and GPIO 8 and 9 are
> strapping pins on the ESP32-C3, ESP32-C6 and ESP32-H2. Choose other pins on these chips.

## Configuration

Before building, set the radio frequency to one that's allowed in your region and that your module and
antenna are built for. In `main/main.cpp`, change the `FREQUENCY` constant from `915_MHz` (for example
to `868_MHz` in Europe). The `radio.configure()` call sets the transmit power (`output_power = 14_dBm`).

## How to use

Create a project from this example:

```bash
idf.py create-project-from-example "cleishm/idfxx_radio_sx126x:transmit_beacon"
cd transmit_beacon
```

Set the target, then edit `main/main.cpp` as described under Configuration:

```bash
idf.py set-target esp32s3
```

Then build and flash it, then watch the serial output:

```bash
idf.py -p PORT flash monitor
```

Replace `PORT` with your board's serial port. Press `Ctrl-]` to exit the monitor.

The board sends `beacon 0000`, `beacon 0001` and so on every two seconds. To see the beacons arrive,
run the [receive_log](../receive_log) example on a second board with the same frequency.
