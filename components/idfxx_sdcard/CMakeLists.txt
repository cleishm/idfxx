# The SDMMC pin definitions live in esp_hal_sd from ESP-IDF 6.0, and in soc before that.
set(_priv_requires esp_driver_sdspi esp_driver_sdmmc)
if(IDF_VERSION_MAJOR GREATER_EQUAL 6)
    list(APPEND _priv_requires esp_hal_sd)
endif()

idf_component_register(
    SRCS "src/sdcard.cpp"
    INCLUDE_DIRS "include"
    REQUIRES sdmmc
    PRIV_REQUIRES ${_priv_requires}
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_23)
set_target_properties(${COMPONENT_LIB} PROPERTIES CXX_EXTENSIONS OFF)

# Register test sources for the central test app
file(GLOB _test_sources "${CMAKE_CURRENT_SOURCE_DIR}/tests/*_test.cpp")
if(_test_sources)
    set_property(GLOBAL APPEND PROPERTY IDFXX_TEST_SOURCES ${_test_sources})
endif()
