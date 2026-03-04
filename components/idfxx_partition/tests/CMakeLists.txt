# Tests for idfxx_partition
# Note: These tests require ESP-IDF and should be run on hardware or in the ESP-IDF test framework

# Test source files
set(IDFXX_PARTITION_TEST_SOURCES
    partition_test.cpp
)

# When building as part of an ESP-IDF project with the Unity test framework,
# these files will be compiled and linked into the test application.
# See ESP-IDF documentation for running unit tests:
# https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/unit-tests.html
