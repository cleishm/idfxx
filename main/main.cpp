#include <unity.h>
#include <idfxx/error>
#include <idfxx/chrono>
#include <idfxx/memory>
#include <idfxx/gpio>
#include <idfxx/nvs>
#include <idfxx/spi/master>

extern "C" void app_main()
{
    UNITY_BEGIN();

#ifdef CONFIG_IDF_TARGET_QEMU
    // Exclude hardware-dependent tests when running in QEMU
    // Tests tagged with [hw] require physical hardware (GPIO pins, NVS flash, SPI)
    unity_set_exclude_tag("[hw]");
#endif

    unity_run_all_tests();
    UNITY_END();

#ifdef CONFIG_IDF_TARGET_QEMU
    // Exit cleanly for QEMU (prevents hang after test completion)
    esp_restart();
#endif
}
