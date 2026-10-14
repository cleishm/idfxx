# Console REPL

Runs an interactive command prompt over the serial console with `idfxx::console`.

## What it demonstrates

- Registering commands with `idfxx::console::register_command()`: `hello`, which takes an optional name, and `count`, which keeps a counter between calls
- Adding the built-in `help` command with `idfxx::console::register_help_command()`
- Starting a REPL on the default console UART with `idfxx::console::repl`, which runs in its own task

The example uses the exception-based API (`CONFIG_COMPILER_CXX_EXCEPTIONS=y` in `sdkconfig.defaults`).

## Hardware required

- Any ESP32-series development board

## How to use

Create a project from this example:

```bash
idf.py create-project-from-example "cleishm/idfxx_console:console_repl"
cd console_repl
```

Then build and flash it, then watch the serial output:

```bash
idf.py set-target esp32
idf.py -p PORT flash monitor
```

Replace `esp32` with your target and `PORT` with your board's serial port. Press `Ctrl-]` to exit the monitor.

Once the prompt appears, type `help` to list the commands, then try `hello`, `hello Alice` and `count`.
