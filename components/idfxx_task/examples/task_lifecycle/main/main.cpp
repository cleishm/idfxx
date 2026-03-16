// SPDX-License-Identifier: Apache-2.0

#include <idfxx/log>
#include <idfxx/sched>
#include <idfxx/task>

#include <chrono>

using namespace std::chrono_literals;

static constexpr idfxx::log::logger logger{"example"};

extern "C" void app_main() {
    // --- Current task info ---
    logger.info("=== Current Task Info ===");
    logger.info("Current task name: {}", idfxx::task::current_name());

    // --- Create a worker task ---
    logger.info("=== Worker Task ===");
    idfxx::task worker({.name = "worker", .stack_size = 4096, .priority = 5}, [](idfxx::task::self& self) {
        auto& log = logger;
        log.info("[worker] started: name={}, priority={}", self.name(), self.priority());
        int count = 0;
        while (!self.stop_requested()) {
            self.wait();
            if (self.stop_requested()) {
                break;
            }
            ++count;
            log.info("[worker] notified (count={})", count);
        }
        log.info("[worker] stop requested, exiting (stack HWM={})", self.stack_high_water_mark());
    });

    logger.info("Worker name: {}", worker.name());
    logger.info("Worker priority: {}", worker.priority());

    // --- Notify the worker several times ---
    logger.info("=== Notify ===");
    for (int i = 0; i < 3; ++i) {
        worker.notify();
        idfxx::delay(100ms);
    }

    // --- Change priority ---
    logger.info("=== Priority Change ===");
    worker.set_priority(10);
    logger.info("Worker priority after change: {}", worker.priority());

    // --- Suspend and resume ---
    logger.info("=== Suspend / Resume ===");
    worker.suspend();
    logger.info("Worker suspended");
    idfxx::delay(500ms);
    worker.resume();
    logger.info("Worker resumed");
    idfxx::delay(100ms);

    // --- Stack high water mark ---
    logger.info("=== Stack Info ===");
    logger.info("Worker stack HWM: {}", worker.stack_high_water_mark());

    // --- Request stop and join ---
    logger.info("=== Stop and Join ===");
    worker.request_stop();
    worker.notify();
    worker.join(5s);
    logger.info("Worker joined (completed={})", worker.is_completed());

    // --- Fire-and-forget task ---
    logger.info("=== Spawn (fire-and-forget) ===");
    idfxx::task::spawn({.name = "oneshot", .stack_size = 4096}, [](idfxx::task::self& self) {
        logger.info("[oneshot] running and finishing immediately");
    });
    idfxx::delay(500ms);

    logger.info("Done!");
}
