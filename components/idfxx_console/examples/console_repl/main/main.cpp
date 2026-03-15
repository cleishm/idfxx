// SPDX-License-Identifier: Apache-2.0

#include <idfxx/console>
#include <idfxx/log>
#include <idfxx/sched>

#include <chrono>
#include <cstdio>

using namespace std::chrono_literals;

static constexpr idfxx::log::logger logger{"example"};

extern "C" void app_main() {
    logger.info("=== Console REPL Example ===");

    // --- Register commands ---
    logger.info("Registering commands...");

    idfxx::console::register_command(
        {.name = "hello", .help = "Print a greeting. Usage: hello [name]"},
        [](int argc, char** argv) {
            if (argc > 1) {
                std::printf("Hello, %s!\n", argv[1]);
            } else {
                std::printf("Hello, world!\n");
            }
            return 0;
        }
    );
    logger.info("Registered 'hello' command");

    int counter = 0;
    idfxx::console::register_command(
        {.name = "count", .help = "Increment and display a counter"},
        [&counter](int, char**) {
            ++counter;
            std::printf("Count: %d\n", counter);
            return 0;
        }
    );
    logger.info("Registered 'count' command");

    idfxx::console::register_help_command();
    logger.info("Registered 'help' command");

    // --- Start REPL ---
    logger.info("Starting UART REPL...");
    idfxx::console::repl console({}, {});
    logger.info("REPL started. Type 'help' to see available commands.");

    // --- Block forever (REPL runs in a background task) ---
    while (true) {
        idfxx::delay(1h);
    }
}
