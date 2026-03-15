// SPDX-License-Identifier: Apache-2.0

#include <idfxx/error>
#include <idfxx/log>

static constexpr idfxx::log::logger logger{"example"};

// A function that returns a result with a value
idfxx::result<int> try_read_sensor(bool simulate_error) {
    if (simulate_error) {
        return idfxx::error(idfxx::errc::timeout);
    }
    return 42;
}

// A function that returns a void result
idfxx::result<void> try_initialize(bool simulate_error) {
    if (simulate_error) {
        return idfxx::error(idfxx::errc::invalid_state);
    }
    return {};
}

// Demonstrates error propagation between result types
idfxx::result<int> try_read_after_init(bool init_error) {
    auto r = try_initialize(init_error);
    if (!r) {
        // Propagate the error to a different result type
        return idfxx::error(r.error());
    }
    return try_read_sensor(false);
}

extern "C" void app_main() {
    // --- Checking results ---
    logger.info("=== Checking Results ===");

    // Successful result with a value
    auto r1 = try_read_sensor(false);
    if (r1) {
        logger.info("Sensor value: {}", *r1);
    }

    // Failed result
    auto r2 = try_read_sensor(true);
    if (!r2) {
        logger.info("Sensor error: {}", r2.error().message());
    }

    // Void result (success)
    auto r3 = try_initialize(false);
    if (r3) {
        logger.info("Initialization succeeded");
    }

    // --- Error propagation ---
    logger.info("=== Error Propagation ===");

    auto r4 = try_read_after_init(false);
    if (r4) {
        logger.info("Read after init: {}", *r4);
    }

    auto r5 = try_read_after_init(true);
    if (!r5) {
        logger.info("Propagated error: {}", r5.error().message());
    }

    // --- abort_on_error ---
    logger.info("=== abort_on_error ===");

    // Plain abort_on_error — aborts if the result is an error
    idfxx::abort_on_error(try_initialize(false));
    logger.info("Initialization succeeded (would have aborted on failure)");

    // abort_on_error with a callback for logging before abort
    idfxx::abort_on_error(try_initialize(false), [&](std::error_code ec) { logger.error("Fatal: {}", ec.message()); });
    logger.info("Done!");
}
