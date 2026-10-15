// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#include <idfxx/console>

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <map>
#include <mutex>
#include <string>
#include <utility>

namespace {
const char* TAG = "idfxx::console";
}

namespace idfxx::console {
namespace {

// Context stored for each registered command
struct command_context {
    handler callback; // null for raw function pointer commands
    std::string name; // stored copy (must live until deregister/deinit)
    std::string help; // stored copy
    std::string hint; // stored copy
};

// Global storage for command contexts
struct command_storage {
    std::mutex mutex;
    std::map<std::string, std::unique_ptr<command_context>, std::less<>> contexts;
};

command_storage storage;

// Trampoline function that ESP-IDF calls for handler-based commands
int command_trampoline(void* context, int argc, char** argv) {
    auto* ctx = static_cast<command_context*>(context);
    // An exception (already logged) fails the command
    auto res = invoke_guarded(TAG, "Command handler", [&] { return ctx->callback(argc, argv); });
    return res.value_or(1);
}

} // namespace

// =============================================================================
// Command registration
// =============================================================================

result<void> try_register_command(const command& cmd, handler callback) {
    if (cmd.name->empty() || cmd.name->find(' ') != std::string_view::npos) {
        ESP_LOGD(TAG, "Field 'name' has an invalid value");
        return error(errc::invalid_arg);
    }

    auto ctx = std::make_unique<command_context>();
    ctx->callback = std::move(callback);
    ctx->name = std::string{*cmd.name};
    ctx->help = std::string{cmd.help};
    ctx->hint = std::string{cmd.hint};

    esp_console_cmd_t idf_cmd{};
    idf_cmd.command = ctx->name.c_str();
    idf_cmd.help = ctx->help.empty() ? nullptr : ctx->help.c_str();
    idf_cmd.hint = ctx->hint.empty() ? nullptr : ctx->hint.c_str();
    idf_cmd.func = nullptr;
    idf_cmd.argtable = cmd.argtable;
    idf_cmd.func_w_context = command_trampoline;
    idf_cmd.context = ctx.get();

    auto err = esp_console_cmd_register(&idf_cmd);
    if (err != ESP_OK) {
        return error(err);
    }

    std::lock_guard lock(storage.mutex);
    storage.contexts[ctx->name] = std::move(ctx);
    return {};
}

result<void> try_deregister_command(std::string_view name) {
    std::string name_str{name};
    auto err = esp_console_cmd_deregister(name_str.c_str());
    if (err != ESP_OK) {
        return error(err);
    }

    std::lock_guard lock(storage.mutex);
    storage.contexts.erase(name_str);
    return {};
}

result<void> try_register_help_command() {
    return wrap(esp_console_register_help_command());
}

result<void> try_deregister_help_command() {
    return wrap(esp_console_deregister_help_command());
}

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
void register_command(const command& cmd, handler callback) {
    unwrap(try_register_command(cmd, std::move(callback)));
}

void deregister_command(std::string_view name) {
    unwrap(try_deregister_command(name));
}

void register_help_command() {
    unwrap(try_register_help_command());
}

void deregister_help_command() {
    unwrap(try_deregister_help_command());
}
#endif

// =============================================================================
// Low-level console (non-REPL)
// =============================================================================

result<void> try_init(const config& cfg) {
    esp_console_config_t idf_cfg{};
    idf_cfg.max_cmdline_length = cfg.max_cmdline_length;
    idf_cfg.max_cmdline_args = cfg.max_cmdline_args;
    idf_cfg.heap_alloc_caps = MALLOC_CAP_DEFAULT;
    idf_cfg.hint_color = cfg.hint_color;
    idf_cfg.hint_bold = cfg.hint_bold ? 1 : 0;

    return wrap(esp_console_init(&idf_cfg));
}

result<void> try_deinit() {
    return wrap(esp_console_deinit());
}

result<int> try_run(std::string_view cmdline) {
    // esp_console_run needs a mutable copy (it modifies the string in place)
    std::string cmdline_str{cmdline};
    int ret = 0;
    auto err = esp_console_run(cmdline_str.c_str(), &ret);
    if (err != ESP_OK) {
        return error(err);
    }
    return ret;
}

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
void init(const config& cfg) {
    unwrap(try_init(cfg));
}

void deinit() {
    unwrap(try_deinit());
}

int run(std::string_view cmdline) {
    return unwrap(try_run(cmdline));
}
#endif

// =============================================================================
// REPL config translation
// =============================================================================

result<esp_console_repl_config_t> repl::_translate_config(const config& cfg) {
    if (cfg.max_cmdline_length == 0u) {
        return error(errc::invalid_arg);
    }

    esp_console_repl_config_t idf_cfg = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    idf_cfg.max_history_len = static_cast<uint32_t>(cfg.max_history_len);
    idf_cfg.task_stack_size = static_cast<uint32_t>(cfg.task_stack_size);
    idf_cfg.task_priority = static_cast<uint32_t>(cfg.priority.value());
    idf_cfg.task_core_id = cfg.core_affinity.has_value() ? static_cast<BaseType_t>(*cfg.core_affinity) : tskNO_AFFINITY;
    if (cfg.max_cmdline_length) {
        idf_cfg.max_cmdline_length = *cfg.max_cmdline_length;
    }
    // Note: history_save_path and prompt need null-terminated strings that live
    // long enough for the esp_console_new_repl_* call. They are set in the
    // factory methods using temporary std::string objects.
    return idf_cfg;
}

// =============================================================================
// REPL UART backend
// =============================================================================

#if CONFIG_ESP_CONSOLE_UART_DEFAULT || CONFIG_ESP_CONSOLE_UART_CUSTOM

result<repl> repl::make(const config& cfg, const uart_config& uart) {
    if ((uart.tx_gpio && !uart.tx_gpio->is_connected()) || (uart.rx_gpio && !uart.rx_gpio->is_connected())) {
        return error(errc::invalid_arg);
    }

    auto translated = _translate_config(cfg);
    if (!translated) {
        return error(translated.error());
    }
    auto& idf_cfg = *translated;

    // Temporary strings for null termination (ESP-IDF copies them internally)
    auto history_path = cfg.history_save_path.string();
    std::string prompt{cfg.prompt.value_or(std::string_view{})};
    idf_cfg.history_save_path = history_path.empty() ? nullptr : history_path.c_str();
    idf_cfg.prompt = cfg.prompt ? prompt.c_str() : nullptr;

    esp_console_dev_uart_config_t dev_cfg{};
    dev_cfg.channel = static_cast<int>(uart.port);
    dev_cfg.baud_rate = uart.baud_rate;
    dev_cfg.tx_gpio_num = uart.tx_gpio ? uart.tx_gpio->num() : -1; // -1 keeps the port's default pin
    dev_cfg.rx_gpio_num = uart.rx_gpio ? uart.rx_gpio->num() : -1;

    esp_console_repl_t* handle = nullptr;
    auto err = esp_console_new_repl_uart(&dev_cfg, &idf_cfg, &handle);
    if (err != ESP_OK) {
        return error(err);
    }

    err = esp_console_start_repl(handle);
    if (err != ESP_OK) {
        handle->del(handle);
        return error(err);
    }

    return repl(handle);
}

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
repl::repl(const config& cfg, const uart_config& uart)
    : repl(unwrap(make(cfg, uart))) {}
#endif

#endif // UART

// =============================================================================
// REPL USB CDC backend
// =============================================================================

#if CONFIG_ESP_CONSOLE_USB_CDC

result<repl> repl::make(const config& cfg, usb_cdc_t /*transport*/) {
    auto translated = _translate_config(cfg);
    if (!translated) {
        return error(translated.error());
    }
    auto& idf_cfg = *translated;

    auto history_path = cfg.history_save_path.string();
    std::string prompt{cfg.prompt.value_or(std::string_view{})};
    idf_cfg.history_save_path = history_path.empty() ? nullptr : history_path.c_str();
    idf_cfg.prompt = cfg.prompt ? prompt.c_str() : nullptr;

    esp_console_dev_usb_cdc_config_t dev_cfg = ESP_CONSOLE_DEV_CDC_CONFIG_DEFAULT();

    esp_console_repl_t* handle = nullptr;
    auto err = esp_console_new_repl_usb_cdc(&dev_cfg, &idf_cfg, &handle);
    if (err != ESP_OK) {
        return error(err);
    }

    err = esp_console_start_repl(handle);
    if (err != ESP_OK) {
        handle->del(handle);
        return error(err);
    }

    return repl(handle);
}

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
repl::repl(const config& cfg, usb_cdc_t transport)
    : repl(unwrap(make(cfg, transport))) {}
#endif

#endif // USB CDC

// =============================================================================
// REPL USB Serial JTAG backend
// =============================================================================

#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG

result<repl> repl::make(const config& cfg, usb_serial_jtag_t /*transport*/) {
    auto translated = _translate_config(cfg);
    if (!translated) {
        return error(translated.error());
    }
    auto& idf_cfg = *translated;

    auto history_path = cfg.history_save_path.string();
    std::string prompt{cfg.prompt.value_or(std::string_view{})};
    idf_cfg.history_save_path = history_path.empty() ? nullptr : history_path.c_str();
    idf_cfg.prompt = cfg.prompt ? prompt.c_str() : nullptr;

    esp_console_dev_usb_serial_jtag_config_t dev_cfg = ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();

    esp_console_repl_t* handle = nullptr;
    auto err = esp_console_new_repl_usb_serial_jtag(&dev_cfg, &idf_cfg, &handle);
    if (err != ESP_OK) {
        return error(err);
    }

    err = esp_console_start_repl(handle);
    if (err != ESP_OK) {
        handle->del(handle);
        return error(err);
    }

    return repl(handle);
}

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
repl::repl(const config& cfg, usb_serial_jtag_t transport)
    : repl(unwrap(make(cfg, transport))) {}
#endif

#endif // USB Serial JTAG

// =============================================================================
// REPL stdio backend
// =============================================================================

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 1, 0)

result<repl> repl::make(const config& cfg, stdio_t /*transport*/) {
    auto translated = _translate_config(cfg);
    if (!translated) {
        return error(translated.error());
    }
    auto& idf_cfg = *translated;

    auto history_path = cfg.history_save_path.string();
    std::string prompt{cfg.prompt.value_or(std::string_view{})};
    idf_cfg.history_save_path = history_path.empty() ? nullptr : history_path.c_str();
    idf_cfg.prompt = cfg.prompt ? prompt.c_str() : nullptr;

    esp_console_repl_t* handle = nullptr;
    auto err = esp_console_new_repl_stdio(&idf_cfg, &handle);
    if (err != ESP_OK) {
        return error(err);
    }

    err = esp_console_start_repl(handle);
    if (err != ESP_OK) {
        handle->del(handle);
        return error(err);
    }

    return repl(handle);
}

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
repl::repl(const config& cfg, stdio_t transport)
    : repl(unwrap(make(cfg, transport))) {}
#endif

#endif // Stdio

// =============================================================================
// REPL lifecycle
// =============================================================================

repl::repl(repl&& other) noexcept
    : _handle(std::exchange(other._handle, nullptr)) {}

repl& repl::operator=(repl&& other) noexcept {
    if (this != &other) {
        _stop_and_delete();
        _handle = std::exchange(other._handle, nullptr);
    }
    return *this;
}

repl::~repl() {
    _stop_and_delete();
}

void repl::_stop_and_delete() noexcept {
    if (_handle != nullptr) {
        auto err = esp_console_stop_repl(_handle);
        if (err != ESP_OK) {
            auto msg = make_error_code(err).message();
            ESP_LOGE(TAG, "Failed to stop REPL: %s", msg.c_str());
        }
        err = _handle->del(_handle);
        if (err != ESP_OK) {
            auto msg = make_error_code(err).message();
            ESP_LOGE(TAG, "Failed to delete REPL: %s", msg.c_str());
        }
    }
}

} // namespace idfxx::console
