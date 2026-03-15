// SPDX-License-Identifier: Apache-2.0

#include <idfxx/log>
#include <idfxx/queue>
#include <idfxx/sched>
#include <idfxx/task>

#include <chrono>
#include <system_error>

using namespace std::chrono_literals;

static constexpr idfxx::log::logger logger{"example"};

struct sensor_reading {
    int sensor_id;
    float value;
};

extern "C" void app_main() {
    // --- Basic send/receive ---
    logger.info("=== Basic Send / Receive ===");
    idfxx::queue<int> q(5);
    logger.info("Queue created: size={}, available={}", q.size(), q.available());

    q.send(10);
    q.send(20);
    q.send(30);
    logger.info("After 3 sends: size={}, available={}", q.size(), q.available());

    logger.info("Received: {}", q.receive());
    logger.info("Received: {}", q.receive());
    logger.info("Received: {}", q.receive());
    logger.info("After receives: empty={}", q.empty());

    // --- send_to_front priority ---
    logger.info("=== Send to Front ===");
    q.send(1);
    q.send(2);
    q.send_to_front(99);
    logger.info("Receive (should be 99): {}", q.receive());
    logger.info("Receive (should be 1): {}", q.receive());
    logger.info("Receive (should be 2): {}", q.receive());

    // --- Peek ---
    logger.info("=== Peek ===");
    q.send(42);
    logger.info("Peek: {}", q.peek());
    logger.info("Size after peek (still 1): {}", q.size());
    q.receive(); // consume it

    // --- Overwrite (mailbox pattern) ---
    logger.info("=== Overwrite (Mailbox) ===");
    idfxx::queue<int> mailbox(1);
    mailbox.overwrite(100);
    logger.info("Mailbox value: {}", mailbox.peek());
    mailbox.overwrite(200);
    logger.info("After overwrite: {}", mailbox.receive());

    // --- Producer-consumer with sensor_reading ---
    logger.info("=== Producer-Consumer ===");
    idfxx::queue<sensor_reading> data_queue(10);

    idfxx::task producer({.name = "producer", .stack_size = 4096}, [&data_queue](idfxx::task::self& self) {
        for (int i = 0; i < 5 && !self.stop_requested(); ++i) {
            sensor_reading reading{.sensor_id = i, .value = 20.0f + static_cast<float>(i) * 0.5f};
            data_queue.send(reading);
            logger.info("[producer] sent sensor_id={}, value={:.1f}", reading.sensor_id, reading.value);
            idfxx::delay(200ms);
        }
    });

    idfxx::task consumer({.name = "consumer", .stack_size = 4096}, [&data_queue](idfxx::task::self& self) {
        while (!self.stop_requested()) {
            auto result = data_queue.try_receive(1s);
            if (result) {
                auto& r = *result;
                logger.info("[consumer] got sensor_id={}, value={:.1f}", r.sensor_id, r.value);
            } else {
                logger.info("[consumer] receive timed out");
                break;
            }
        }
    });

    producer.join(5s);
    consumer.request_stop();
    consumer.join(5s);

    // --- Queue state ---
    logger.info("=== Queue State ===");
    idfxx::queue<int> state_q(3);
    state_q.send(1);
    state_q.send(2);
    state_q.send(3);
    logger.info("size={}, available={}, full={}", state_q.size(), state_q.available(), state_q.full());

    state_q.reset();
    logger.info("After reset: size={}, empty={}", state_q.size(), state_q.empty());

    logger.info("Done!");
}
