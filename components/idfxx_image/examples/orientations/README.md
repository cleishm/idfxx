# Orientations

Shows a small in-memory photo the right way up on a panel that is mounted rotated, and prints the
results as ASCII art.

To decode an image straight to a display, use `idfxx::image::render()` instead, which applies these
orientations for you.

## What it demonstrates

- Drawing into an `idfxx::image::buffer<>` of `idfxx::image::rgb888` pixels with `operator[]`,
  `row()` and `subview()`
- Combining a photo's EXIF orientation and the panel's rotation into one transform with
  `image::compose()`
- Getting the result as a new image with `image::oriented()`, with an explicit allocator (here
  `idfxx::dram_allocator`)
- Writing the result into a region of a larger image with `image::orient()`, sized with
  `image::oriented_size()`
- Finding the photo pixel under a screen position with `image::source_point()`
- Finding where a photo pixel landed on screen with `image::inverse()` and `image::source_point()`

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

Replace `esp32` with your target and `PORT` with your board's serial port. Press `Ctrl-]` to exit
the monitor.
