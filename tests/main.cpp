#include <unity.h>
#include <unity_test_runner.h>

#include <idfxx/log>

#include <esp_netif.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "test_cleanup.hpp"

// Unity calls this after each test, including one that failed an assertion.
extern "C" void tearDown()
{
    idfxx::test::run_cleanups();
}

extern "C" void app_main()
{
    ESP_ERROR_CHECK(esp_netif_init());

    UNITY_BEGIN();

#ifdef CONFIG_IDF_TARGET_QEMU
    // Exclude hardware-dependent tests when running in QEMU
    // Tests tagged with [hw] require physical hardware and precise timing
    unity_run_tests_by_tag("[hw]", true);  // true = invert (exclude tests with [hw] tag)
#else
    unity_run_all_tests();
#endif

    UNITY_END();

#ifdef CONFIG_IDF_TARGET_QEMU
    // Print sentinel for CI to detect test completion and exit QEMU early
    idfxx::log::info("test", "### TESTS COMPLETE ###");

    // Halt after the tests complete. CI kills QEMU when it finds the sentinel in the output.
    while (true) {
        vTaskDelay(portMAX_DELAY);
    }
#else
    // When a test puts the chip in light sleep, the chip disconnects from the
    // USB bus. A USB-Serial-JTAG console then loses its connection, and
    // everything printed after that point is lost, including the summary above.
    // Re-print the result periodically, so that a monitor that reconnects later
    // still shows the outcome.
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        idfxx::log::info(
            "test", "{} Tests {} Failures {} Ignored - {}", Unity.NumberOfTests, Unity.TestFailures,
            Unity.TestIgnores, Unity.TestFailures == 0 ? "OK" : "FAIL"
        );
    }
#endif
}
