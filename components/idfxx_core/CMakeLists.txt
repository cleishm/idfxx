set(core_requires freertos esp_system esp_hw_support)
if(IDF_VERSION_MAJOR GREATER_EQUAL 6)
    list(APPEND core_requires esp_hal_uart)
endif()

idf_component_register(
    SRCS "src/error.cpp" "src/net.cpp" "src/sched.cpp" "src/system.cpp" "src/app.cpp"
    INCLUDE_DIRS "include"
    REQUIRES ${core_requires}
    PRIV_REQUIRES esp_app_format
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_23)
set_target_properties(${COMPONENT_LIB} PROPERTIES CXX_EXTENSIONS OFF)

# Register test sources for the central test app
file(GLOB _test_sources "${CMAKE_CURRENT_SOURCE_DIR}/tests/*_test.cpp")
if(_test_sources)
    set_property(GLOBAL APPEND PROPERTY IDFXX_TEST_SOURCES ${_test_sources})
endif()
