idf_component_register(
    SRCS "src/adc.cpp"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES esp_adc
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_23)
set_target_properties(${COMPONENT_LIB} PROPERTIES CXX_EXTENSIONS OFF)

# Register test sources for the central test app.
# esp_adc's boot-time analog calibration hangs under QEMU (which does not
# emulate the ADC peripheral), so skip registration there — the tests are
# error-path only and run on real hardware.
file(GLOB _test_sources "${CMAKE_CURRENT_SOURCE_DIR}/tests/*_test.cpp")
if(_test_sources AND NOT CONFIG_IDF_TARGET_QEMU)
    set_property(GLOBAL APPEND PROPERTY IDFXX_TEST_SOURCES ${_test_sources})
endif()
