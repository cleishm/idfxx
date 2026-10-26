# Block Sink

Shows both sides of `idfxx::image::sink`: a producer that delivers an image block by block, as an image decoder does, and three consumers that receive the blocks.

## What it demonstrates

- A producer function template constrained on `idfxx::image::sink`, delivering a generated test pattern as tiles (edge tiles are smaller) and stopping at the first error from the sink
- A lambda sink that composites blocks into an `idfxx::image::buffer<>` at an offset, rejecting blocks that don't fit with `idfxx::errc::invalid_size`
- A function object sink that gathers statistics (block count, pixel count and average colour)
- A sink that stops the producer early by returning an error, which the producer returns to its caller
- Checking sinks at compile time with `static_assert(idfxx::image::sink<...>)`
- Printing an image as brightness-based ASCII art

The example uses the exception-based API (`CONFIG_COMPILER_CXX_EXCEPTIONS=y` in `sdkconfig.defaults`) for its own errors; the sinks and the producer return `idfxx::result<void>`, as `idfxx::image::sink` requires.

## Hardware required

- Any ESP32-series development board

The output is printed to the serial monitor; nothing needs to be connected.

## How to use

Create a project from this example:

```bash
idf.py create-project-from-example "cleishm/idfxx_image:block_sink"
cd block_sink
```

Then build and flash it, then watch the serial output:

```bash
idf.py set-target esp32
idf.py -p PORT flash monitor
```

Replace `esp32` with your target and `PORT` with your board's serial port. Press `Ctrl-]` to exit the monitor.
