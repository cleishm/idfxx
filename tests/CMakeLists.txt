# Collect test sources registered by each component via the IDFXX_TEST_SOURCES property
get_property(IDFXX_TEST_SOURCES GLOBAL PROPERTY IDFXX_TEST_SOURCES)

set(IDFXX_TEST_REQUIRES
    unity idfxx_core idfxx_hw_support idfxx_gpio idfxx_nvs idfxx_partition idfxx_ota idfxx_i2c idfxx_spi idfxx_lcd
    idfxx_lcd_ili9341 idfxx_lcd_ssd1306 idfxx_lcd_touch idfxx_lcd_touch_stmpe610 idfxx_onewire idfxx_ds18x20
    idfxx_timer idfxx_event
    idfxx_event_group idfxx_task idfxx_queue idfxx_log idfxx_http idfxx_http_client idfxx_http_server
    idfxx_https_server idfxx_console idfxx_rotary_encoder idfxx_button idfxx_pwm idfxx_net idfxx_netif idfxx_sleep
    esp_netif idfxx_dht esp_driver_rmt idfxx_radio idfxx_radio_sx126x
)

# idfxx_adc pulls in esp_adc, whose boot-time analog calibration hangs under
# QEMU (which does not emulate the ADC peripheral). Keep it out of the QEMU link;
# its tests are also skipped there (see components/idfxx_adc/CMakeLists.txt).
# QEMU is a build config rather than a target, so — unlike idfxx_wifi below — it
# can't be excluded via a top-level EXCLUDE_COMPONENTS, which is resolved before
# sdkconfig (and thus CONFIG_IDF_TARGET_QEMU) is known.
if(NOT CONFIG_IDF_TARGET_QEMU)
    list(APPEND IDFXX_TEST_REQUIRES idfxx_adc)
endif()

# idfxx_wifi is only available on Wi-Fi-capable targets (it is excluded from
# the build on other targets; see the top-level CMakeLists.txt).
idf_build_get_property(idfxx_test_target IDF_TARGET)
set(IDFXX_WIFI_TARGETS esp32 esp32s2 esp32s3 esp32c2 esp32c3 esp32c5 esp32c6 esp32c61)
if(idfxx_test_target IN_LIST IDFXX_WIFI_TARGETS)
    list(APPEND IDFXX_TEST_REQUIRES idfxx_wifi)
endif()

idf_component_register(
    SRCS "main.cpp"
    INCLUDE_DIRS "."
    REQUIRES ${IDFXX_TEST_REQUIRES}
    WHOLE_ARCHIVE
)

# Add test sources via target_sources to avoid ESP-IDF 6.0 component validation warnings.
# The validation only checks idf_component_register(SRCS ...), not target_sources.
target_sources(${COMPONENT_LIB} PRIVATE ${IDFXX_TEST_SOURCES})

target_compile_options(${COMPONENT_LIB} PRIVATE -std=gnu++23)
