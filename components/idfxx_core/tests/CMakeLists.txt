# Tests for idfxx_core
# Note: These tests require ESP-IDF and should be run on hardware or in the ESP-IDF test framework

# Test source files
set(IDFXX_CORE_TEST_SOURCES
    cpu_test.cpp
    error_test.cpp
    chrono_test.cpp
    flags_test.cpp
    memory_dma_allocator_test.cpp
    memory_dram_allocator_test.cpp
    memory_spiram_allocator_test.cpp
)

# When building as part of an ESP-IDF project with the Unity test framework,
# these files will be compiled and linked into the test application.
# See ESP-IDF documentation for running unit tests:
# https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/unit-tests.html
