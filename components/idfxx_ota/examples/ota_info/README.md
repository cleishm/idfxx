# OTA Info

Prints the device's OTA partitions, the running app's description, partition image states, and rollback
status.

## What it demonstrates

- Finding the running partition with `idfxx::ota::running_partition()`, and printing its label, address
  and size
- Reading the app's version, project name, ESP-IDF version, and build date and time with
  `idfxx::ota::partition_description()`
- Finding the boot partition with `idfxx::ota::boot_partition()`, and the partition the next update would
  be written to with `idfxx::ota::next_update_partition()`
- Counting the OTA app partitions with `idfxx::ota::app_partition_count()`
- Reading image states with `idfxx::ota::partition_state()`, and with `try_partition_state()` for the next
  update partition, which may have no recorded state
- Checking whether a rollback is possible with `idfxx::ota::rollback_possible()`, and looking up the last
  invalid partition with `try_last_invalid_partition()`

The example uses the exception-based API (`CONFIG_COMPILER_CXX_EXCEPTIONS=y` in `sdkconfig.defaults`).

## Hardware required

- Any ESP32-series development board with at least 4 MB of flash

The example uses ESP-IDF's "factory app, two OTA definitions" partition table
(`CONFIG_PARTITION_TABLE_TWO_OTA=y` in `sdkconfig.defaults`), which needs 4 MB of flash.

## How to use

Create a project from this example:

```bash
idf.py create-project-from-example "cleishm/idfxx_ota:ota_info"
cd ota_info
```

Then build and flash it, then watch the serial output:

```bash
idf.py set-target esp32
idf.py -p PORT flash monitor
```

Replace `esp32` with your target and `PORT` with your board's serial port. Press `Ctrl-]` to exit the monitor.
