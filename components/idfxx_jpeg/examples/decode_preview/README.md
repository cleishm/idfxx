# Decode preview

Decodes a JPEG photo and prints a colour preview of it in the serial monitor, with no display or other hardware.

## What it demonstrates

- Reading a JPEG's size, EXIF orientation and coding from its headers with `idfxx::jpeg::decoder`, over an `idfxx::memory_source`
- Decoding at a reduced scale with `scale_to_cover()`, swapping the target size for an image whose EXIF orientation turns it a quarter turn
- Decoding into an `idfxx::image::buffer<>`, then turning the image upright with `idfxx::image::oriented()`
- Printing the pixels as 24-bit ANSI colour text
- Decoding row by row through a sink, so only one row of the image is in memory at a time
- Handling bad data with the result-based API (`decoder::make()` and `try_decode()`): not a JPEG, truncated data, a progressive JPEG over its memory limit, and a destination of the wrong size

The example uses the exception-based API (`CONFIG_COMPILER_CXX_EXCEPTIONS=y` in `sdkconfig.defaults`), except for the error handling section, which uses the result-based API.

## Hardware required

- Any ESP32-series development board; no PSRAM is needed

The serial terminal must support 24-bit colour and UTF-8. Most do, and `idf.py monitor` passes both through.

## How to use

Create a project from this example:

```bash
idf.py create-project-from-example "cleishm/idfxx_jpeg:decode_preview"
cd decode_preview
```

Then build and flash it, then watch the serial output:

```bash
idf.py set-target esp32
idf.py -p PORT flash monitor
```

Replace `esp32` with your target and `PORT` with your board's serial port. Press `Ctrl-]` to exit the monitor.
