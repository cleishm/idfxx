// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Unit tests for idfxx console
// Uses ESP-IDF Unity test framework with compile-time static_asserts

#include <idfxx/console>
#include <unity.h>

#include <stdexcept>
#include <system_error>
#include <type_traits>

using namespace idfxx;
using namespace idfxx::console;

// =============================================================================
// Compile-time tests (static_assert)
// These verify correctness at compile time - if this file compiles, they pass.
// =============================================================================

// repl is not default constructible
static_assert(!std::is_default_constructible_v<repl>);

// repl is not copyable
static_assert(!std::is_copy_constructible_v<repl>);
static_assert(!std::is_copy_assignable_v<repl>);

// repl is move-only
static_assert(std::is_move_constructible_v<repl>);
static_assert(std::is_move_assignable_v<repl>);

// =============================================================================
// Runtime tests (Unity TEST_CASE)
// Uses low-level init/run API to avoid REPL task conflicts in tests.
// =============================================================================

namespace {

// Helper: initialize console, run test body, deinitialize
struct console_fixture {
    console_fixture() { TEST_ASSERT_TRUE(try_init().has_value()); }

    ~console_fixture() {
        auto r = try_deinit();
        TEST_ASSERT_TRUE(r.has_value());
    }
};

} // namespace

TEST_CASE("console register and run command with lambda handler", "[idfxx][console]") {
    console_fixture fixture;

    int called_with_argc = -1;
    auto r = try_register_command(
        {.name = "testcmd", .help = "Test command"},
        [&called_with_argc](int argc, char** /*argv*/) {
            called_with_argc = argc;
            return 42;
        }
    );
    TEST_ASSERT_TRUE(r.has_value());

    auto run_result = try_run("testcmd");
    TEST_ASSERT_TRUE(run_result.has_value());
    TEST_ASSERT_EQUAL(42, *run_result);
    TEST_ASSERT_EQUAL(1, called_with_argc); // just the command name

    TEST_ASSERT_TRUE(try_deregister_command("testcmd").has_value());
}

TEST_CASE("console register and run command with non-capturing lambda", "[idfxx][console]") {
    console_fixture fixture;

    static int called = 0;
    called = 0;

    auto r = try_register_command(
        {.name = "noncap", .help = "Non-capturing lambda"},
        [](int /*argc*/, char** /*argv*/) -> int {
            called = 1;
            return 7;
        }
    );
    TEST_ASSERT_TRUE(r.has_value());

    auto run_result = try_run("noncap");
    TEST_ASSERT_TRUE(run_result.has_value());
    TEST_ASSERT_EQUAL(7, *run_result);
    TEST_ASSERT_EQUAL(1, called);

    TEST_ASSERT_TRUE(try_deregister_command("noncap").has_value());
}

TEST_CASE("console register help command", "[idfxx][console]") {
    console_fixture fixture;

    auto r = try_register_help_command();
    TEST_ASSERT_TRUE(r.has_value());

    // Running 'help' should succeed
    auto run_result = try_run("help");
    TEST_ASSERT_TRUE(run_result.has_value());
    TEST_ASSERT_EQUAL(0, *run_result);

    TEST_ASSERT_TRUE(try_deregister_help_command().has_value());
}

TEST_CASE("console deregister command then run returns not_found", "[idfxx][console]") {
    console_fixture fixture;

    auto r = try_register_command(
        {.name = "ephemeral", .help = "Will be removed"}, [](int, char**) { return 0; }
    );
    TEST_ASSERT_TRUE(r.has_value());

    // Should work before deregistration
    auto run_result = try_run("ephemeral");
    TEST_ASSERT_TRUE(run_result.has_value());

    // Deregister
    TEST_ASSERT_TRUE(try_deregister_command("ephemeral").has_value());

    // Should fail after deregistration
    auto run_result2 = try_run("ephemeral");
    TEST_ASSERT_FALSE(run_result2.has_value());
    TEST_ASSERT_EQUAL(std::to_underlying(errc::not_found), run_result2.error().value());
}

TEST_CASE("console run unknown command returns not_found", "[idfxx][console]") {
    console_fixture fixture;

    auto run_result = try_run("nonexistent_command");
    TEST_ASSERT_FALSE(run_result.has_value());
    TEST_ASSERT_EQUAL(std::to_underlying(errc::not_found), run_result.error().value());
}

TEST_CASE("console run empty string returns invalid_arg", "[idfxx][console]") {
    console_fixture fixture;

    auto run_result = try_run("");
    TEST_ASSERT_FALSE(run_result.has_value());
    TEST_ASSERT_EQUAL(std::to_underlying(errc::invalid_arg), run_result.error().value());
}

TEST_CASE("console lambda with captures works", "[idfxx][console]") {
    console_fixture fixture;

    std::string captured_value = "hello";
    auto r = try_register_command(
        {.name = "capture_test"},
        [&captured_value](int, char**) {
            captured_value = "modified";
            return 0;
        }
    );
    TEST_ASSERT_TRUE(r.has_value());

    auto run_result = try_run("capture_test");
    TEST_ASSERT_TRUE(run_result.has_value());
    TEST_ASSERT_EQUAL_STRING("modified", captured_value.c_str());

    TEST_ASSERT_TRUE(try_deregister_command("capture_test").has_value());
}

TEST_CASE("console command receives arguments", "[idfxx][console]") {
    console_fixture fixture;

    static int received_argc = 0;
    static char received_arg1[64] = {};
    received_argc = 0;
    received_arg1[0] = '\0';

    auto r = try_register_command(
        {.name = "argtest"},
        [](int argc, char** argv) -> int {
            received_argc = argc;
            if (argc > 1) {
                snprintf(received_arg1, sizeof(received_arg1), "%s", argv[1]);
            }
            return 0;
        }
    );
    TEST_ASSERT_TRUE(r.has_value());

    auto run_result = try_run("argtest myarg");
    TEST_ASSERT_TRUE(run_result.has_value());
    TEST_ASSERT_EQUAL(2, received_argc);
    TEST_ASSERT_EQUAL_STRING("myarg", received_arg1);

    TEST_ASSERT_TRUE(try_deregister_command("argtest").has_value());
}

TEST_CASE("console config default values", "[idfxx][console]") {
    console::config cfg{};
    TEST_ASSERT_EQUAL(256, cfg.max_cmdline_length);
    TEST_ASSERT_EQUAL(32, cfg.max_cmdline_args);
    TEST_ASSERT_EQUAL(39, cfg.hint_color);
    TEST_ASSERT_FALSE(cfg.hint_bold);
}

TEST_CASE("console repl config default values", "[idfxx][console]") {
    repl::config cfg{};
    TEST_ASSERT_EQUAL(32, cfg.max_history_len);
    TEST_ASSERT_EQUAL(4096, cfg.task_stack_size);
    TEST_ASSERT_EQUAL(2, cfg.priority.value());
    TEST_ASSERT_FALSE(cfg.core_affinity.has_value());
    TEST_ASSERT_FALSE(cfg.prompt.has_value());
    TEST_ASSERT_FALSE(cfg.max_cmdline_length.has_value());
}

namespace {

// A transport tag must not be constructible from `{}`, so that `repl({}, {})`
// unambiguously selects the UART transport.
template<typename T>
concept direct_constructible_from_empty_braces = requires { T{}; };

template<typename T>
concept convertible_from_empty_braces = requires(void (*fn)(T)) { fn({}); };

} // namespace

#if CONFIG_ESP_CONSOLE_UART_DEFAULT || CONFIG_ESP_CONSOLE_UART_CUSTOM
// `{}` selects the UART transport
#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
static_assert(std::is_constructible_v<repl, const repl::config&, const repl::uart_config&>);
#endif
static_assert(std::is_same_v<decltype(repl::make({}, {})), result<repl>>);
#endif // UART

#if CONFIG_ESP_CONSOLE_USB_CDC
static_assert(std::is_empty_v<repl::usb_cdc_t>);
static_assert(!std::is_default_constructible_v<repl::usb_cdc_t>);
static_assert(!direct_constructible_from_empty_braces<repl::usb_cdc_t>);
static_assert(!convertible_from_empty_braces<repl::usb_cdc_t>);
static_assert(std::is_same_v<decltype(repl::usb_cdc), const repl::usb_cdc_t>);
#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
static_assert(std::is_constructible_v<repl, const repl::config&, repl::usb_cdc_t>);
#endif
static_assert(std::is_same_v<decltype(repl::make(repl::config{}, repl::usb_cdc)), result<repl>>);
#endif // USB CDC

#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
static_assert(std::is_empty_v<repl::usb_serial_jtag_t>);
static_assert(!std::is_default_constructible_v<repl::usb_serial_jtag_t>);
static_assert(!direct_constructible_from_empty_braces<repl::usb_serial_jtag_t>);
static_assert(!convertible_from_empty_braces<repl::usb_serial_jtag_t>);
static_assert(std::is_same_v<decltype(repl::usb_serial_jtag), const repl::usb_serial_jtag_t>);
#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
static_assert(std::is_constructible_v<repl, const repl::config&, repl::usb_serial_jtag_t>);
#endif
static_assert(std::is_same_v<decltype(repl::make(repl::config{}, repl::usb_serial_jtag)), result<repl>>);
#endif // USB Serial JTAG

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 1, 0)
static_assert(std::is_empty_v<repl::stdio_t>);
static_assert(!std::is_default_constructible_v<repl::stdio_t>);
static_assert(!direct_constructible_from_empty_braces<repl::stdio_t>);
static_assert(!convertible_from_empty_braces<repl::stdio_t>);
static_assert(std::is_same_v<decltype(repl::stdio), const repl::stdio_t>);
#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
static_assert(std::is_constructible_v<repl, const repl::config&, repl::stdio_t>);
#endif
static_assert(std::is_same_v<decltype(repl::make(repl::config{}, repl::stdio)), result<repl>>);
#endif // stdio

#if CONFIG_ESP_CONSOLE_UART_DEFAULT || CONFIG_ESP_CONSOLE_UART_CUSTOM
TEST_CASE("console uart_config default values", "[idfxx][console]") {
    repl::uart_config cfg{};
    TEST_ASSERT_EQUAL(static_cast<int>(repl::default_uart_port), static_cast<int>(cfg.port));
    TEST_ASSERT_EQUAL(CONFIG_ESP_CONSOLE_UART_BAUDRATE, cfg.baud_rate);
    TEST_ASSERT_FALSE(cfg.tx_gpio.has_value());
    TEST_ASSERT_FALSE(cfg.rx_gpio.has_value());
}

TEST_CASE("console repl make rejects invalid config", "[idfxx][console]") {
    auto zero_cmdline = repl::make({.max_cmdline_length = 0}, {});
    TEST_ASSERT_FALSE(zero_cmdline.has_value());
    TEST_ASSERT_EQUAL(std::to_underlying(errc::invalid_arg), zero_cmdline.error().value());

    auto nc_tx = repl::make({}, {.tx_gpio = idfxx::gpio::nc()});
    TEST_ASSERT_FALSE(nc_tx.has_value());
    TEST_ASSERT_EQUAL(std::to_underlying(errc::invalid_arg), nc_tx.error().value());
}
#endif // UART

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS

TEST_CASE("console register_command with exception API", "[idfxx][console]") {
    console_fixture fixture;

    bool called = false;
    register_command({.name = "exc_test", .help = "Exception test"}, [&called](int, char**) {
        called = true;
        return 0;
    });

    int ret = run("exc_test");
    TEST_ASSERT_EQUAL(0, ret);
    TEST_ASSERT_TRUE(called);

    deregister_command("exc_test");
}

TEST_CASE("console run throws on unknown command", "[idfxx][console]") {
    console_fixture fixture;

    bool threw = false;
    try {
        run("nonexistent_exc_cmd");
    } catch (const std::system_error& e) {
        threw = true;
        TEST_ASSERT_EQUAL(std::to_underlying(errc::not_found), e.code().value());
    }
    TEST_ASSERT_TRUE(threw);
}

TEST_CASE("console command fails with status 1 when its handler throws", "[idfxx][console][exceptions]") {
    console_fixture fixture;

    int calls = 0;
    auto r = try_register_command({.name = "throw_syserr"}, [&calls](int, char**) -> int {
        ++calls;
        throw std::system_error(make_error_code(errc::invalid_state));
    });
    TEST_ASSERT_TRUE(r.has_value());
    r = try_register_command({.name = "throw_exc"}, [&calls](int, char**) -> int {
        ++calls;
        throw std::runtime_error("boom");
    });
    TEST_ASSERT_TRUE(r.has_value());
    r = try_register_command({.name = "throw_int"}, [&calls](int, char**) -> int {
        ++calls;
        throw 42;
    });
    TEST_ASSERT_TRUE(r.has_value());
    r = try_register_command({.name = "ok_cmd"}, [](int, char**) { return 7; });
    TEST_ASSERT_TRUE(r.has_value());

    for (const char* cmd : {"throw_syserr", "throw_exc", "throw_int"}) {
        auto run_result = try_run(cmd);
        TEST_ASSERT_TRUE(run_result.has_value());
        TEST_ASSERT_EQUAL(1, *run_result);
    }
    TEST_ASSERT_EQUAL(3, calls);

    // Other commands still run normally
    auto ok_result = try_run("ok_cmd");
    TEST_ASSERT_TRUE(ok_result.has_value());
    TEST_ASSERT_EQUAL(7, *ok_result);

    TEST_ASSERT_TRUE(try_deregister_command("throw_syserr").has_value());
    TEST_ASSERT_TRUE(try_deregister_command("throw_exc").has_value());
    TEST_ASSERT_TRUE(try_deregister_command("throw_int").has_value());
    TEST_ASSERT_TRUE(try_deregister_command("ok_cmd").has_value());
}

#endif // CONFIG_COMPILER_CXX_EXCEPTIONS
