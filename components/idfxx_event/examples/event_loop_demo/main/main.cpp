// SPDX-License-Identifier: Apache-2.0

#include <idfxx/event>
#include <idfxx/log>
#include <idfxx/sched>

#include <chrono>
#include <cstdint>

using namespace std::chrono_literals;

static constexpr idfxx::log::logger logger{"example"};

// Define event IDs
enum class app_event : int32_t { started, data_received, stopped };

// Define the event base
IDFXX_EVENT_DEFINE_BASE(app_events, app_event);

extern "C" void app_main() {
    // --- Event loop with dedicated task ---
    logger.info("=== Event Loop with Task ===");
    idfxx::event_loop loop({.name = "evt_task", .stack_size = 4096, .priority = 5});

    // Register listener for a specific event
    auto handle =
        loop.listener_add(app_events, app_event::started, [](idfxx::event_base<app_event>, app_event id, void*) {
            logger.info("Listener: got 'started' event (id={})", static_cast<int32_t>(id));
        });

    // Register listener for any event from the base
    auto any_handle = loop.listener_add(app_events, [](idfxx::event_base<app_event>, app_event id, void* data) {
        logger.info("Any-listener: got event id={}", static_cast<int32_t>(id));
        if (data) {
            int value = *static_cast<const int*>(data);
            logger.info("Any-listener: event data={}", value);
        }
    });

    // Post events
    loop.post(app_events, app_event::started);

    int payload = 42;
    loop.post(app_events, app_event::data_received, &payload, sizeof(payload));

    loop.post(app_events, app_event::stopped);
    idfxx::delay(200ms);

    // Remove listeners manually
    loop.listener_remove(handle);
    loop.listener_remove(any_handle);

    // --- RAII listener with unique_listener_handle ---
    logger.info("=== RAII Listener ===");
    {
        idfxx::event_loop::unique_listener_handle raii_handle{loop.listener_add(
            app_events,
            app_event::started,
            [](idfxx::event_base<app_event>, app_event, void*) { logger.info("RAII listener: got event"); }
        )};
        loop.post(app_events, app_event::started);
        idfxx::delay(100ms);
        logger.info("RAII handle valid: {}", static_cast<bool>(raii_handle));
    } // listener automatically removed here
    logger.info("RAII listener removed on scope exit");

    // --- User event loop with manual dispatch ---
    logger.info("=== User Event Loop (manual dispatch) ===");
    idfxx::user_event_loop user_loop(16);

    user_loop
        .listener_add(app_events, app_event::data_received, [](idfxx::event_base<app_event>, app_event, void* data) {
            int value = *static_cast<const int*>(data);
            logger.info("User loop listener: data={}", value);
        });

    int value = 99;
    user_loop.post(app_events, app_event::data_received, &value, sizeof(value));
    user_loop.run(100ms);

    // --- System event loop ---
    logger.info("=== System Event Loop ===");
    idfxx::event_loop::create_system();
    auto& sys = idfxx::event_loop::system();

    sys.listener_add(app_events, app_event::started, [](idfxx::event_base<app_event>, app_event, void*) {
        logger.info("System loop listener: got event");
    });
    sys.post(app_events, app_event::started);
    idfxx::delay(100ms);

    idfxx::event_loop::destroy_system();
    logger.info("System loop destroyed");

    logger.info("Done!");
}
