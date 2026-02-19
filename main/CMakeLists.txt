# Collect test sources from all components
file(GLOB IDFXX_TEST_SOURCES
    "${CMAKE_SOURCE_DIR}/components/*/tests/*_test.cpp"
)

idf_component_register(
    SRCS "main.cpp" ${IDFXX_TEST_SOURCES}
    INCLUDE_DIRS "."
    REQUIRES unity idfxx_core idfxx_hw_support idfxx_gpio idfxx_nvs idfxx_i2c idfxx_spi idfxx_lcd idfxx_lcd_ili9341 idfxx_lcd_touch idfxx_lcd_touch_stmpe610 idfxx_ds18x20 idfxx_timer idfxx_event idfxx_event_group idfxx_task idfxx_queue idfxx_log
    WHOLE_ARCHIVE
)

target_compile_options(${COMPONENT_LIB} PRIVATE -std=gnu++23)
