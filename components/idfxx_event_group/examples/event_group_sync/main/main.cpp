// SPDX-License-Identifier: Apache-2.0

#include <idfxx/event_group>
#include <idfxx/log>
#include <idfxx/sched>
#include <idfxx/task>

#include <chrono>
#include <cstdint>

using namespace std::chrono_literals;
using idfxx::operator|;

static constexpr idfxx::log::logger logger{"example"};

// Define flag enum for event group bits
enum class sync_flag : uint32_t {
    data_ready = 1u << 0,
    processing_done = 1u << 1,
    task_a_ready = 1u << 2,
    task_b_ready = 1u << 3,
};
template<>
inline constexpr bool idfxx::enable_flags_operators<sync_flag> = true;

extern "C" void app_main() {
    // --- Basic set/clear/get ---
    logger.info("=== Basic Set / Clear / Get ===");
    idfxx::event_group<sync_flag> eg;

    eg.set(sync_flag::data_ready);
    logger.info("After set data_ready: {}", idfxx::to_underlying(eg.get()));

    eg.set(sync_flag::processing_done);
    logger.info("After set processing_done: {}", idfxx::to_underlying(eg.get()));

    eg.clear(sync_flag::data_ready);
    logger.info("After clear data_ready: {}", idfxx::to_underlying(eg.get()));

    eg.clear(sync_flag::processing_done);

    // --- Wait with wait_mode::any ---
    logger.info("=== Wait (any) ===");
    idfxx::task setter_any({.name = "setter_any", .stack_size = 4096}, [&eg](idfxx::task::self& self) {
        idfxx::delay(200ms);
        logger.info("[setter_any] setting data_ready");
        eg.set(sync_flag::data_ready);
    });
    auto bits = eg.wait(sync_flag::data_ready | sync_flag::processing_done, idfxx::wait_mode::any);
    logger.info("Wait(any) returned: {}", idfxx::to_underlying(bits));
    setter_any.join(1s);

    // --- Wait with wait_mode::all ---
    logger.info("=== Wait (all) ===");
    idfxx::task setter_all({.name = "setter_all", .stack_size = 4096}, [&eg](idfxx::task::self& self) {
        idfxx::delay(200ms);
        logger.info("[setter_all] setting data_ready");
        eg.set(sync_flag::data_ready);
        idfxx::delay(200ms);
        logger.info("[setter_all] setting processing_done");
        eg.set(sync_flag::processing_done);
    });
    bits = eg.wait(sync_flag::data_ready | sync_flag::processing_done, idfxx::wait_mode::all);
    logger.info("Wait(all) returned: {}", idfxx::to_underlying(bits));
    setter_all.join(1s);

    // --- Sync rendezvous ---
    logger.info("=== Sync Rendezvous ===");
    idfxx::event_group<sync_flag> rendezvous;
    auto both_ready = sync_flag::task_a_ready | sync_flag::task_b_ready;

    idfxx::task task_a({.name = "task_a", .stack_size = 4096}, [&rendezvous, both_ready](idfxx::task::self&) {
        logger.info("[task_a] doing work...");
        idfxx::delay(300ms);
        logger.info("[task_a] syncing");
        auto result = rendezvous.sync(sync_flag::task_a_ready, both_ready);
        logger.info("[task_a] sync complete: {}", idfxx::to_underlying(result));
    });

    idfxx::task task_b({.name = "task_b", .stack_size = 4096}, [&rendezvous, both_ready](idfxx::task::self&) {
        logger.info("[task_b] doing work...");
        idfxx::delay(500ms);
        logger.info("[task_b] syncing");
        auto result = rendezvous.sync(sync_flag::task_b_ready, both_ready);
        logger.info("[task_b] sync complete: {}", idfxx::to_underlying(result));
    });

    task_a.join(3s);
    task_b.join(3s);

    logger.info("Done!");
}
