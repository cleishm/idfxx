# Tests for idfxx_rtc_pcf8563
# Note: These tests require ESP-IDF and should be run on hardware or in the ESP-IDF test framework

set(IDFXX_RTC_PCF8563_TEST_SOURCES
    pcf8563_test.cpp
)

# When building as part of an ESP-IDF project with the Unity test framework,
# these files will be compiled and linked into the test application.
