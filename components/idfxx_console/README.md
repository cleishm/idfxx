# idfxx_console

Interactive console REPL and command management for ESP32.

## Features

- **REPL (Read-Eval-Print Loop)** over UART, USB CDC, or USB Serial JTAG
- **Command registration** with C++ callbacks (lambdas, captures) or raw function pointers
- **Global command registry** shared across all console backends
- **Built-in help command** with automatic registration
- **Low-level console API** for non-REPL command dispatch
- **RAII lifecycle management** for REPL instances

## Requirements

- ESP-IDF 5.5 or later
- C++23 compiler

## Installation

### ESP-IDF Component Manager

Add to your project's `idf_component.yml`:

```yaml
dependencies:
  idfxx_console:
    version: "^0.9.0"
```

Or add `idfxx_console` to the `REQUIRES` list in your component's `CMakeLists.txt`.

## Usage

### Exception-based API

If `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled:

```cpp
#include <idfxx/console>
#include <cstdio>

extern "C" void app_main() {
    using namespace idfxx::console;

    try {
        // Register commands
        register_command(
            {.name = "hello", .help = "Say hello"},
            [](int, char**) { std::printf("Hello!\n"); return 0; });

        register_help_command();

        // Create UART REPL (starts accepting input immediately)
        repl console({}, uart_config{});

    } catch (const std::system_error& e) {
        std::printf("Console error: %s\n", e.what());
    }
}
```

### Result-based API

If `CONFIG_COMPILER_CXX_EXCEPTIONS` is *not* enabled:

```cpp
#include <idfxx/console>
#include <cstdio>

extern "C" void app_main() {
    using namespace idfxx::console;

    // Register commands
    auto r = try_register_command(
        {.name = "hello", .help = "Say hello"},
        [](int, char**) { std::printf("Hello!\n"); return 0; });
    if (!r) {
        std::printf("Failed to register command\n");
        return;
    }

    try_register_help_command();

    // Create UART REPL (starts accepting input immediately)
    auto console = repl::make({}, repl::uart_config{});
    if (!console) {
        std::printf("Failed to create REPL\n");
        return;
    }
}
```

### Low-level Console (Non-REPL)

For programmatic command dispatch without an interactive REPL:

```cpp
#include <idfxx/console>

// Initialize the console
idfxx::console::init();

// Register commands
idfxx::console::register_command(
    {.name = "process", .help = "Process data"},
    [](int argc, char** argv) {
        // Process command arguments
        return 0;
    });

// Run commands programmatically
int ret = idfxx::console::run("process arg1 arg2");

// Clean up
idfxx::console::deinit();
```

## API Overview

### Command Registration

- `register_command(cmd, callback)` / `try_register_command(cmd, callback)` - Register with callback (lambdas, function pointers, etc.)
- `deregister_command(name)` / `try_deregister_command(name)` - Remove a command
- `register_help_command()` / `try_register_help_command()` - Add built-in help
- `deregister_help_command()` / `try_deregister_help_command()` - Remove built-in help

### REPL

- `repl(config, uart_config)` / `repl::make(config, uart_config)` - Create and start UART REPL
- `repl(config, usb_cdc_config)` / `repl::make(config, usb_cdc_config)` - Create and start USB CDC REPL
- `repl(config, usb_serial_jtag_config)` / `repl::make(config, usb_serial_jtag_config)` - Create and start USB Serial JTAG REPL

### Low-level Console

- `init(config)` / `try_init(config)` - Initialize console module
- `deinit()` / `try_deinit()` - Deinitialize console module
- `run(cmdline)` / `try_run(cmdline)` - Execute a command line string

## Error Handling

Console operations use error codes from `idfxx::errc`:

- `invalid_state` - Console not initialized
- `invalid_arg` - Invalid command name or empty command line
- `not_found` - Command not registered

All `try_*` methods return `idfxx::result<T>`. Exception-based methods (without `try_` prefix) throw `std::system_error` when `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled.

## Important Notes

- **Global command registry**: Commands are registered globally. All REPLs and `run()` calls share the same registry.
- **String lifetime**: Command names, help text, and hint strings are copied internally and remain valid until deregistration.
- **REPL backends**: Only one backend type is available at a time, determined by the ESP-IDF console configuration (`CONFIG_ESP_CONSOLE_UART_DEFAULT`, `CONFIG_ESP_CONSOLE_USB_CDC`, etc.).
- **Non-copyable/move-only**: The `repl` class is non-copyable and move-only.
- **Automatic cleanup**: The `repl` destructor stops the REPL task and releases resources.

## License

Apache License 2.0 - see [LICENSE](LICENSE) for details.
