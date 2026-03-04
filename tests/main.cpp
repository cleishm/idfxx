#include <unity.h>
#include <unity_test_runner.h>
#include <idfxx/error>
#include <idfxx/chrono>
#include <idfxx/memory>
#include <idfxx/gpio>
#include <idfxx/nvs>
#include <idfxx/spi/master>

#include <idfxx/log>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

extern "C" void app_main()
{
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

    // Halt after tests complete - CI will kill QEMU when it sees the sentinel
    while (true) {
        vTaskDelay(portMAX_DELAY);
    }
#endif
}
