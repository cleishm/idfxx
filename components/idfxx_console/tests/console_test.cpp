// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Unit tests for idfxx console
// Uses ESP-IDF Unity test framework with compile-time static_asserts

#include <idfxx/console>
#include <unity.h>

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
    TEST_ASSERT_EQUAL(2, cfg.task_priority);
    TEST_ASSERT_FALSE(cfg.core_affinity.has_value());
    TEST_ASSERT_EQUAL(0, cfg.max_cmdline_length);
}

#if CONFIG_ESP_CONSOLE_UART_DEFAULT || CONFIG_ESP_CONSOLE_UART_CUSTOM
TEST_CASE("console uart_config default values", "[idfxx][console]") {
    repl::uart_config cfg{};
    TEST_ASSERT_EQUAL(static_cast<int>(repl::default_uart_port), static_cast<int>(cfg.port));
    TEST_ASSERT_EQUAL(CONFIG_ESP_CONSOLE_UART_BAUDRATE, cfg.baud_rate);
    TEST_ASSERT_EQUAL(-1, cfg.tx_gpio.num());
    TEST_ASSERT_EQUAL(-1, cfg.rx_gpio.num());
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

#endif // CONFIG_COMPILER_CXX_EXCEPTIONS
