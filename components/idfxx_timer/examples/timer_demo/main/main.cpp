// SPDX-License-Identifier: Apache-2.0

#include <idfxx/log>
#include <idfxx/sched>
#include <idfxx/timer>

#include <atomic>
#include <chrono>

using namespace std::chrono_literals;

static constexpr idfxx::log::logger logger{"example"};

extern "C" void app_main() {
    // --- Timer clock ---
    logger.info("=== Timer Clock ===");
    auto now = idfxx::timer::clock::now();
    logger.info("Timer clock time: {} us since boot", now.time_since_epoch().count());

    // --- One-shot timer ---
    logger.info("=== One-Shot Timer ===");
    std::atomic<bool> one_shot_fired{false};
    idfxx::timer one_shot({.name = "one_shot"}, [&one_shot_fired]() { one_shot_fired = true; });
    one_shot.start_once(200ms);
    logger.info("One-shot started, active={}", one_shot.is_active());
    idfxx::delay(500ms);
    logger.info("One-shot fired={}, active={}", one_shot_fired.load(), one_shot.is_active());

    // --- Periodic timer ---
    logger.info("=== Periodic Timer ===");
    std::atomic<int> tick_count{0};
    idfxx::timer periodic({.name = "periodic"}, [&tick_count]() { tick_count++; });
    periodic.start_periodic(500ms);
    logger.info("Periodic started, period={}us", periodic.period().count());

    for (int i = 0; i < 5; ++i) {
        idfxx::delay(500ms);
        logger.info("Tick count: {}", tick_count.load());
    }

    periodic.stop();
    logger.info("Periodic stopped, active={}", periodic.is_active());

    // --- Restart demo ---
    logger.info("=== Restart ===");
    std::atomic<bool> restart_fired{false};
    idfxx::timer restart_timer({.name = "restart"}, [&restart_fired]() { restart_fired = true; });
    restart_timer.start_once(5s);
    logger.info("Started with 5s timeout, active={}", restart_timer.is_active());

    restart_timer.restart(500ms);
    logger.info("Restarted with 500ms timeout");
    idfxx::delay(1s);
    logger.info("Restart fired={}", restart_fired.load());

    // --- Static start_once ---
    logger.info("=== Static start_once ===");
    std::atomic<bool> static_fired{false};
    auto static_timer =
        idfxx::timer::start_once({.name = "static_once"}, 300ms, [&static_fired]() { static_fired = true; });
    idfxx::delay(500ms);
    logger.info("Static one-shot fired={}", static_fired.load());

    // --- Static start_periodic ---
    logger.info("=== Static start_periodic ===");
    std::atomic<int> static_count{0};
    auto static_periodic =
        idfxx::timer::start_periodic({.name = "static_periodic"}, 200ms, [&static_count]() { static_count++; });
    idfxx::delay(1s);
    static_periodic.stop();
    logger.info("Static periodic ticks: {}", static_count.load());

    logger.info("Done!");
}
