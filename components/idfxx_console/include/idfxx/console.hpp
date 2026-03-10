// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

/**
 * @headerfile <idfxx/console>
 * @file console.hpp
 * @brief Interactive console REPL and command management.
 *
 * @defgroup idfxx_console Console Component
 * @brief Interactive console with REPL and command registration.
 *
 * Provides an interactive command-line console for ESP32 devices, supporting
 * UART, USB CDC, and USB Serial JTAG backends. Commands can be registered
 * with C++ callbacks (including lambdas with captures) or raw function pointers.
 *
 * The console uses a global command registry -- commands registered via the
 * free functions in this namespace are available across all backends.
 *
 * Depends on @ref idfxx_core for error handling.
 * @{
 */

#include <idfxx/cpu>
#include <idfxx/error>
#include <idfxx/gpio>
#include <idfxx/uart>

#include <esp_console.h>
#include <filesystem>
#include <functional>
#include <optional>
#include <string_view>
#include <utility>

namespace idfxx::console {

// =============================================================================
// Command types
// =============================================================================

/**
 * @headerfile <idfxx/console>
 * @brief Command handler callback type.
 *
 * Called when a registered command is executed. The callback receives the
 * argument count and argument vector, and returns an integer status code
 * (0 for success).
 */
using handler = std::move_only_function<int(int argc, char** argv)>;

/**
 * @headerfile <idfxx/console>
 * @brief Command descriptor for registration.
 *
 * Describes a command to register with the console. The @c name field is
 * required and must not contain spaces. Other fields are optional.
 */
struct command {
    std::string_view name;      ///< Command name (required, no spaces)
    std::string_view help = {}; ///< Help text (empty = hidden from help)
    std::string_view hint = {}; ///< Hint text (empty = auto-generate from argtable)
    void* argtable = nullptr;   ///< Optional argtable3 pointer for argument parsing
};

// =============================================================================
// Command registration (free functions)
// =============================================================================

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
/**
 * @brief Registers a command with a callback handler.
 *
 * The callback can be any callable, including lambdas with captures
 * and raw function pointers.
 * The callback and command strings are stored internally and remain valid
 * until the command is deregistered or the console is deinitialized.
 *
 * @code
 * int counter = 0;
 * idfxx::console::register_command(
 *     {.name = "count", .help = "Increment counter"},
 *     [&counter](int argc, char** argv) {
 *         ++counter;
 *         std::printf("Count: %d\n", counter);
 *         return 0;
 *     });
 * @endcode
 *
 * @param cmd Command descriptor.
 * @param callback Function to call when the command is executed.
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
 * @throws std::system_error on failure.
 */
void register_command(const command& cmd, handler callback);

/**
 * @brief Deregisters a previously registered command.
 *
 * @param name The command name to deregister.
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
 * @throws std::system_error on failure.
 */
void deregister_command(std::string_view name);

/**
 * @brief Registers the built-in 'help' command.
 *
 * The help command lists all registered commands with their help text.
 *
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
 * @throws std::system_error on failure.
 */
void register_help_command();

/**
 * @brief Deregisters the built-in 'help' command.
 *
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
 * @throws std::system_error on failure.
 */
void deregister_help_command();
#endif

/**
 * @brief Registers a command with a callback handler.
 *
 * The callback can be any callable, including lambdas with captures
 * and raw function pointers.
 * The callback and command strings are stored internally and remain valid
 * until the command is deregistered or the console is deinitialized.
 *
 * @param cmd Command descriptor.
 * @param callback Function to call when the command is executed.
 * @return Success, or an error.
 * @retval invalid_arg If the command name is empty or contains spaces.
 */
[[nodiscard]] result<void> try_register_command(const command& cmd, handler callback);

/**
 * @brief Deregisters a previously registered command.
 *
 * @param name The command name to deregister.
 * @return Success, or an error.
 * @retval invalid_arg If the command is not registered.
 */
result<void> try_deregister_command(std::string_view name);

/**
 * @brief Registers the built-in 'help' command.
 *
 * The help command lists all registered commands with their help text.
 *
 * @return Success, or an error.
 * @retval invalid_state If the console is not initialized.
 */
[[nodiscard]] result<void> try_register_help_command();

/**
 * @brief Deregisters the built-in 'help' command.
 *
 * @return Success, or an error.
 */
result<void> try_deregister_help_command();

// =============================================================================
// Low-level console (non-REPL use)
// =============================================================================

/**
 * @headerfile <idfxx/console>
 * @brief Configuration for low-level console initialization.
 *
 * Used with init() / try_init() for non-REPL console usage.
 */
struct config {
    size_t max_cmdline_length = 256; ///< Maximum command line length in bytes
    size_t max_cmdline_args = 32;    ///< Maximum number of command line arguments
    int hint_color = 39;             ///< ASCII color code for hint text
    bool hint_bold = false;          ///< Whether to display hints in bold
};

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
/**
 * @brief Initializes the console module.
 *
 * Call this before using command registration or run() in non-REPL mode.
 * Not needed when using the REPL, which initializes the console internally.
 *
 * @param cfg Console configuration.
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
 * @throws std::system_error on failure.
 */
void init(const config& cfg = {});

/**
 * @brief Deinitializes the console module.
 *
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
 * @throws std::system_error on failure.
 */
void deinit();

/**
 * @brief Runs a command line string.
 *
 * Parses and executes the given command line. The console must be
 * initialized first via init() or by creating a REPL.
 *
 * @code
 * idfxx::console::init();
 * idfxx::console::register_command(
 *     {.name = "hello", .help = "Say hello"},
 *     [](int, char**) { std::printf("Hello!\n"); return 0; });
 * int ret = idfxx::console::run("hello");
 * @endcode
 *
 * @param cmdline The command line string to execute.
 * @return The command's return code.
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
 * @throws std::system_error if the command is not found or the console
 *         is not initialized.
 */
int run(std::string_view cmdline);
#endif

/**
 * @brief Initializes the console module.
 *
 * Call this before using command registration or try_run() in non-REPL mode.
 * Not needed when using the REPL, which initializes the console internally.
 *
 * @param cfg Console configuration.
 * @return Success, or an error.
 * @retval invalid_state If already initialized.
 * @retval invalid_arg If the configuration is invalid.
 */
[[nodiscard]] result<void> try_init(const config& cfg = {});

/**
 * @brief Deinitializes the console module.
 *
 * @return Success, or an error.
 * @retval invalid_state If not initialized.
 */
result<void> try_deinit();

/**
 * @brief Runs a command line string.
 *
 * Parses and executes the given command line. The console must be
 * initialized first via try_init() or by creating a REPL.
 *
 * @param cmdline The command line string to execute.
 * @return The command's return code, or an error.
 * @retval invalid_arg If the command line is empty or only whitespace.
 * @retval not_found If the command is not registered.
 * @retval invalid_state If the console is not initialized.
 */
[[nodiscard]] result<int> try_run(std::string_view cmdline);

// =============================================================================
// REPL
// =============================================================================

/**
 * @headerfile <idfxx/console>
 * @brief Interactive REPL (Read-Eval-Print Loop) console.
 *
 * Provides an interactive command-line interface over UART, USB CDC, or
 * USB Serial JTAG. The REPL runs in a background FreeRTOS task and processes
 * commands from the global command registry.
 *
 * This type is non-copyable and move-only. Result-returning methods on a
 * moved-from object return errc::invalid_state.
 *
 * @code
 * // Register commands
 * idfxx::console::register_command(
 *     {.name = "hello", .help = "Say hello"},
 *     [](int, char**) { std::printf("Hello!\n"); return 0; });
 * idfxx::console::register_help_command();
 *
 * // Create UART REPL (starts accepting input immediately)
 * idfxx::console::repl console({}, {});
 * @endcode
 */
class repl {
public:
    /**
     * @brief REPL configuration parameters.
     */
    struct config {
        size_t max_history_len = 32;                         ///< Maximum command history length
        std::filesystem::path history_save_path = {};        ///< File path for history persistence (empty = no save)
        size_t task_stack_size = 4096;                       ///< REPL task stack size in bytes
        task_priority priority = 2;                          ///< REPL task priority
        std::optional<core_id> core_affinity = std::nullopt; ///< Core affinity (nullopt = any core)
        std::string_view prompt = {};                        ///< Prompt string (empty = default "esp> ")
        size_t max_cmdline_length = 0;                       ///< Maximum command line length (0 = default 256)
    };

    // =========================================================================
    // UART backend
    // =========================================================================

#if CONFIG_ESP_CONSOLE_UART_DEFAULT || CONFIG_ESP_CONSOLE_UART_CUSTOM || defined(__DOXYGEN__)
    /// @brief The default UART port, as configured by `CONFIG_ESP_CONSOLE_UART_NUM`.
    static constexpr uart_port default_uart_port = static_cast<uart_port>(CONFIG_ESP_CONSOLE_UART_NUM);

    /**
     * @brief UART-specific configuration.
     */
    struct uart_config {
        uart_port port = default_uart_port;               ///< UART port
        int baud_rate = CONFIG_ESP_CONSOLE_UART_BAUDRATE; ///< Communication baud rate
        gpio tx_gpio = gpio::nc();                        ///< TX GPIO (nc = default pin)
        gpio rx_gpio = gpio::nc();                        ///< RX GPIO (nc = default pin)
    };

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Creates and starts a REPL over UART.
     *
     * The REPL starts accepting input immediately upon construction.
     *
     * @code
     * idfxx::console::repl console({}, {});
     * @endcode
     *
     * @param cfg REPL configuration.
     * @param uart UART-specific configuration.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
     * @throws std::system_error on failure.
     */
    [[nodiscard]] explicit repl(const config& cfg, const uart_config& uart);
#endif

    /**
     * @brief Creates and starts a REPL over UART.
     *
     * The REPL starts accepting input immediately upon construction.
     *
     * @param cfg REPL configuration.
     * @param uart UART-specific configuration.
     * @return The new running REPL, or an error.
     */
    [[nodiscard]] static result<repl> make(const config& cfg, const uart_config& uart);
#endif // UART

    // =========================================================================
    // USB CDC backend
    // =========================================================================

#if CONFIG_ESP_CONSOLE_USB_CDC || (defined(__DOXYGEN__) && SOC_USB_OTG_SUPPORTED)
    /**
     * @brief USB CDC-specific configuration (reserved for future use).
     */
    struct usb_cdc_config {};

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Creates and starts a REPL over USB CDC.
     *
     * The REPL starts accepting input immediately upon construction.
     *
     * @param cfg REPL configuration.
     * @param usb_cdc USB CDC-specific configuration.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
     * @throws std::system_error on failure.
     */
    [[nodiscard]] explicit repl(const config& cfg, const usb_cdc_config& usb_cdc);
#endif

    /**
     * @brief Creates and starts a REPL over USB CDC.
     *
     * The REPL starts accepting input immediately upon construction.
     *
     * @param cfg REPL configuration.
     * @param usb_cdc USB CDC-specific configuration.
     * @return The new running REPL, or an error.
     */
    [[nodiscard]] static result<repl> make(const config& cfg, const usb_cdc_config& usb_cdc);
#endif // USB CDC

    // =========================================================================
    // USB Serial JTAG backend
    // =========================================================================

#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG || (defined(__DOXYGEN__) && SOC_USB_SERIAL_JTAG_SUPPORTED)
    /**
     * @brief USB Serial JTAG-specific configuration (reserved for future use).
     */
    struct usb_serial_jtag_config {};

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Creates and starts a REPL over USB Serial JTAG.
     *
     * The REPL starts accepting input immediately upon construction.
     *
     * @param cfg REPL configuration.
     * @param usb_jtag USB Serial JTAG-specific configuration.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
     * @throws std::system_error on failure.
     */
    [[nodiscard]] explicit repl(const config& cfg, const usb_serial_jtag_config& usb_jtag);
#endif

    /**
     * @brief Creates and starts a REPL over USB Serial JTAG.
     *
     * The REPL starts accepting input immediately upon construction.
     *
     * @param cfg REPL configuration.
     * @param usb_jtag USB Serial JTAG-specific configuration.
     * @return The new running REPL, or an error.
     */
    [[nodiscard]] static result<repl> make(const config& cfg, const usb_serial_jtag_config& usb_jtag);
#endif // USB Serial JTAG

    // =========================================================================
    // Lifecycle
    // =========================================================================

    /**
     * @brief Destroys the REPL.
     *
     * Stops the REPL task and releases all resources.
     */
    ~repl();

    repl(const repl&) = delete;
    repl& operator=(const repl&) = delete;

    /** @brief Move constructor. Transfers ownership of the REPL. */
    repl(repl&& other) noexcept;

    /** @brief Move assignment. Transfers ownership of the REPL. */
    repl& operator=(repl&& other) noexcept;

    /**
     * @brief Returns the underlying ESP-IDF REPL handle.
     * @return The esp_console_repl_t pointer, or nullptr if moved-from.
     */
    [[nodiscard]] esp_console_repl_t* idf_handle() const noexcept { return _handle; }

private:
    explicit repl(esp_console_repl_t* handle)
        : _handle(handle) {}

    void _stop_and_delete() noexcept;

    static esp_console_repl_config_t _translate_config(const config& cfg);

    esp_console_repl_t* _handle = nullptr;
};

/** @} */ // end of idfxx_console

} // namespace idfxx::console
