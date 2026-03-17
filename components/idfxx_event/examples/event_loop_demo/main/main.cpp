// SPDX-License-Identifier: Apache-2.0

#include <idfxx/event>
#include <idfxx/log>
#include <idfxx/sched>

#include <chrono>
#include <cstdint>

using namespace std::chrono_literals;

static constexpr idfxx::log::logger logger{"example"};

// Define event IDs
enum class app_event_id { started, data_received, stopped };

// Define the event base
IDFXX_EVENT_DEFINE_BASE(app_events, app_event_id);

// Define event data type (trivially copyable types work automatically)
struct app_data {
    int value;
};

// Define typed events
inline constexpr idfxx::event<app_event_id> started{app_event_id::started};
inline constexpr idfxx::event<app_event_id, app_data> data_received{app_event_id::data_received};
inline constexpr idfxx::event<app_event_id> stopped{app_event_id::stopped};

extern "C" void app_main() {
    // --- Event loop with dedicated task ---
    logger.info("=== Event Loop with Task ===");
    idfxx::event_loop loop({.name = "evt_task", .stack_size = 4096, .priority = 5});

    // Register listener for a specific event (type-safe, no void* casting needed)
    auto handle = loop.listener_add(started, []() { logger.info("Listener: got 'started' event"); });

    // Register wildcard listener for any event from the base
    auto any_handle = loop.listener_add(app_events, [](idfxx::event_base<app_event_id>, app_event_id id, void* data) {
        logger.info("Any-listener: got event id={}", static_cast<int32_t>(id));
        if (data) {
            int value = *static_cast<const int*>(data);
            logger.info("Any-listener: event data={}", value);
        }
    });

    // Post events (type-safe)
    loop.post(started);
    loop.post(data_received, app_data{42});
    loop.post(stopped);
    idfxx::delay(200ms);

    // Remove listeners manually
    loop.listener_remove(handle);
    loop.listener_remove(any_handle);

    // --- RAII listener with unique_listener_handle ---
    logger.info("=== RAII Listener ===");
    {
        idfxx::event_loop::unique_listener_handle raii_handle{loop.listener_add(started, []() {
            logger.info("RAII listener: got event");
        })};
        loop.post(started);
        idfxx::delay(100ms);
        logger.info("RAII handle valid: {}", static_cast<bool>(raii_handle));
    } // listener automatically removed here
    logger.info("RAII listener removed on scope exit");

    // --- User event loop with manual dispatch ---
    logger.info("=== User Event Loop (manual dispatch) ===");
    idfxx::user_event_loop user_loop(16);

    // Event listener receives data directly
    user_loop.listener_add(data_received, [](const app_data& data) {
        logger.info("User loop listener: data={}", data.value);
    });

    user_loop.post(data_received, app_data{99});
    user_loop.run(100ms);

    // --- System event loop ---
    logger.info("=== System Event Loop ===");
    idfxx::event_loop::create_system();
    auto& sys = idfxx::event_loop::system();

    sys.listener_add(started, []() { logger.info("System loop listener: got event"); });
    sys.post(started);
    idfxx::delay(100ms);

    idfxx::event_loop::destroy_system();
    logger.info("System loop destroyed");

    logger.info("Done!");
}
