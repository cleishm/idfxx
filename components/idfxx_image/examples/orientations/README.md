# Orientations

Applies the eight EXIF image orientations to a small pixel image and prints the results as ASCII art.

## What it demonstrates

- Building an `idfxx::image::buffer<>` of `idfxx::image::rgb888` pixels, and reading and writing pixels with `operator[]`
- Reading a row at a time with `row()`
- Drawing into a region of a larger image with `subview()`, and passing mutable views as read-only views
- Drawing all eight orientations onto one "contact sheet" with `image::orient()`, sizing each cell with `image::swaps_axes()`
- Getting a new image with `image::oriented()`, optionally with an explicit allocator (here `idfxx::dram_allocator`)
- Fusing an EXIF orientation and a display rotation into one transform with `image::compose()`
- Undoing an orientation with `image::inverse()`
- Finding which source pixel appears at a displayed position with `image::source_point()`

Image operations cannot fail, so the example has no error handling.

## Hardware required

- Any ESP32-series development board

The output is printed to the serial monitor; nothing needs to be connected.

## How to use

Create a project from this example:

```bash
idf.py create-project-from-example "cleishm/idfxx_image:orientations"
cd orientations
```

Then build and flash it, then watch the serial output:

```bash
idf.py set-target esp32
idf.py -p PORT flash monitor
```

Replace `esp32` with your target and `PORT` with your board's serial port. Press `Ctrl-]` to exit the monitor.
