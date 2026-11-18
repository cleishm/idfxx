# Tests for idfxx_radio
# Note: These tests require ESP-IDF and should be run on hardware or in the ESP-IDF test framework

set(IDFXX_RADIO_TEST_SOURCES
    radio_test.cpp
)

# When building as part of an ESP-IDF project with the Unity test framework,
# these files will be compiled and linked into the test application.
