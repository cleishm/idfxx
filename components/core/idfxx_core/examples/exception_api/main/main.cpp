// SPDX-License-Identifier: Apache-2.0

#include <idfxx/error>
#include <idfxx/log>

static constexpr idfxx::log::logger logger{"example"};

// A helper that returns a result — used to demonstrate converting results to exceptions
idfxx::result<int> try_read_sensor(bool simulate_error) {
    if (simulate_error) {
        return idfxx::error(idfxx::errc::timeout);
    }
    return 42;
}

// Throws std::system_error directly from an error code
void initialize(bool simulate_error) {
    if (simulate_error) {
        throw std::system_error(make_error_code(idfxx::errc::invalid_state));
    }
}

extern "C" void app_main() {
    // --- Basic try/catch ---
    logger.info("=== Basic try/catch ===");

    try {
        initialize(true);
    } catch (const std::system_error& e) {
        logger.info("Caught: {}", e.what());
        logger.info("Error code: {} ({})", e.code().value(), e.code().message());
    }

    // --- Converting a failed result to an exception ---
    logger.info("=== Result to Exception ===");

    try {
        auto r = try_read_sensor(true);
        if (!r) {
            throw std::system_error(r.error());
        }
    } catch (const std::system_error& e) {
        logger.info("Caught from result: {}", e.what());
    }

    // --- Nested try/catch for different error types ---
    logger.info("=== Nested try/catch ===");

    try {
        initialize(false); // succeeds

        try {
            auto r = try_read_sensor(true);
            if (!r) {
                throw std::system_error(r.error());
            }
        } catch (const std::system_error& e) {
            logger.info("Inner catch (sensor error): {}", e.what());
        }

        initialize(true); // throws
    } catch (const std::system_error& e) {
        logger.info("Outer catch (init error): {}", e.what());
    }

    logger.info("Done!");
}
