set(srcs "src/entry.cpp" "src/fault_store.cpp" "src/flash_log.cpp" "src/log_line.cpp"
         "src/sector_store.cpp" "src/service.cpp")
if(CONFIG_IDFXX_STD_FORMAT)
    list(APPEND srcs "src/capture.cpp")
endif()

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS "include"
    PRIV_REQUIRES esp_app_format esp_partition esp_rom esp_timer log
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_23)
set_target_properties(${COMPONENT_LIB} PROPERTIES CXX_EXTENSIONS OFF)

# start() reads the core dump from flash, if the app stores one there in ELF format (see
# service.cpp).
if(CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH)
    idf_component_optional_requires(PRIVATE espcoredump)
endif()

# Register test sources for the test app
file(GLOB _test_sources "${CMAKE_CURRENT_SOURCE_DIR}/tests/*_test.cpp")
if(_test_sources)
    set_property(GLOBAL APPEND PROPERTY IDFXX_TEST_SOURCES ${_test_sources})
endif()
