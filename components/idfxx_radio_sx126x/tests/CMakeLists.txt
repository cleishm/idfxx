# Tests for idfxx_radio_sx126x
# Note: These tests require ESP-IDF and should be run on hardware or in the ESP-IDF test framework

set(IDFXX_RADIO_SX126X_TEST_SOURCES
    sx126x_test.cpp
)

# When building as part of an ESP-IDF project with the Unity test framework,
# these files will be compiled and linked into the test application.
