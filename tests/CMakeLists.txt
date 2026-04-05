# Collect test sources registered by each component via the IDFXX_TEST_SOURCES property
get_property(IDFXX_TEST_SOURCES GLOBAL PROPERTY IDFXX_TEST_SOURCES)

idf_component_register(
    SRCS "main.cpp"
    INCLUDE_DIRS "."
    REQUIRES unity idfxx_core idfxx_hw_support idfxx_gpio idfxx_nvs idfxx_partition idfxx_ota idfxx_i2c idfxx_spi idfxx_lcd idfxx_lcd_ili9341 idfxx_lcd_touch idfxx_lcd_touch_stmpe610 idfxx_onewire idfxx_ds18x20 idfxx_timer idfxx_event idfxx_event_group idfxx_task idfxx_queue idfxx_log idfxx_http idfxx_http_client idfxx_http_server idfxx_https_server idfxx_console idfxx_rotary_encoder idfxx_button idfxx_wifi idfxx_pwm esp_netif
    WHOLE_ARCHIVE
)

# Add test sources via target_sources to avoid ESP-IDF 6.0 component validation warnings.
# The validation only checks idf_component_register(SRCS ...), not target_sources.
target_sources(${COMPONENT_LIB} PRIVATE ${IDFXX_TEST_SOURCES})

target_compile_options(${COMPONENT_LIB} PRIVATE -std=gnu++23)
