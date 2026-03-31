idf_component_register(
    INCLUDE_DIRS "include"
    REQUIRES freertos
)

target_compile_features(${COMPONENT_LIB} INTERFACE cxx_std_23)
set_target_properties(${COMPONENT_LIB} PROPERTIES CXX_EXTENSIONS OFF)

# Register test sources for the central test app
file(GLOB _test_sources "${CMAKE_CURRENT_SOURCE_DIR}/tests/*_test.cpp")
if(_test_sources)
    set_property(GLOBAL APPEND PROPERTY IDFXX_TEST_SOURCES ${_test_sources})
endif()
