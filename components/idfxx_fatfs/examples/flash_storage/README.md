# Flash Storage

Keeps a boot counter in a file on a FAT filesystem in a flash partition, so that the count survives
resets and power cycles.

## What it demonstrates

- A partition table with a `storage` partition (type `data`, subtype `fat`) for the filesystem, in
  `partitions.csv`
- Mounting the partition by its label at `/data` with `idfxx::fatfs::volume`, and formatting it on
  the first boot (`format_if_missing`). A volume on flash spreads its writes across the partition
  (wear leveling), so the same file can be rewritten on every boot.
- Finishing or discarding a save that a reset interrupted with `recover_file()`, then reading the
  file with `std::ifstream`
- Saving the new count with `replace_file()`, so that a reset or power loss while saving leaves
  either the old count or the new
- Reporting the volume's capacity and free space with `size()` and `free_size()`
- Unmounting explicitly with `unmount()`, which saves the wear-leveling state and reports any error
- Catching the `std::system_error` thrown when the partition can't be mounted

The example uses the exception-based API (`CONFIG_COMPILER_CXX_EXCEPTIONS=y` in
`sdkconfig.defaults`).

## Hardware required

- Any ESP32-series development board with at least 4 MB of flash

The partition table in `partitions.csv` gives the app 1 MB and the `storage` partition 1 MB. To
change the filesystem's size, change the size of `storage`; `sdkconfig.defaults` selects the
partition table and the 4 MB flash size.

## How to use

Create a project from this example:

```bash
idf.py create-project-from-example "cleishm/idfxx_fatfs:flash_storage"
cd flash_storage
```

Then build and flash it, then watch the serial output:

```bash
idf.py set-target esp32
idf.py -p PORT flash monitor
```

Replace `esp32` with your target and `PORT` with your board's serial port. Press the board's reset
button to see the count go up, and `Ctrl-]` to exit the monitor. `idf.py erase-flash` resets the
count, by erasing the partition.
