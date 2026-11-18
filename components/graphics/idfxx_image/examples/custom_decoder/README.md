# Custom Decoder

Makes a new kind of image source work with the renderer by implementing `idfxx::image::decoder`,
then scales its picture to fit a small screen and prints it as ASCII art.

The same decoder also works with anything else that takes an `idfxx::image::decoder`, such as the
writers in `idfxx_lcd_image` and `idfxx_epaper_image`.

## What it demonstrates

- Implementing `info()` and `try_decode()` to model `idfxx::image::decoder`, and checking that with
  `static_assert`
- Delivering an image to an `idfxx::image::sink` as full-width rows, from the top row down
- Fitting the picture to a screen with `idfxx::image::render()`, `idfxx::image::placement` and
  `idfxx::image::render_config`
- Writing a sink that receives the output as it is produced, so the picture is never held in memory
- Stopping a decode early by returning an error from a sink, which `idfxx::image::try_render()`
  returns to its caller

The example uses the exception-based `render()` for its main path, and the result-based
`try_render()` where it needs to look at the error. Sinks and decoders return `idfxx::result<void>`
either way.

## Hardware required

- Any ESP32-series development board

The output is printed to the serial monitor; nothing needs to be connected.

## How to use

Create a project from this example:

```bash
idf.py create-project-from-example "cleishm/idfxx_image:custom_decoder"
cd custom_decoder
```

Then build and flash it, then watch the serial output:

```bash
idf.py set-target esp32
idf.py -p PORT flash monitor
```

Replace `esp32` with your target and `PORT` with your board's serial port. Press `Ctrl-]` to exit
the monitor.
