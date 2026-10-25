# The driver requires PSRAM, and its header refuses to compile without
# CONFIG_SPIRAM. Build the driver (and register its tests) only when PSRAM is
# enabled, so that merely depending on this component doesn't break a build
# that never uses the panel.
if(CONFIG_SPIRAM)
    set(srcs "src/el133uf1.cpp")
endif()

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS "include"
)

if(srcs)
    target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_23)
    set_target_properties(${COMPONENT_LIB} PROPERTIES CXX_EXTENSIONS OFF)
else()
    target_compile_features(${COMPONENT_LIB} INTERFACE cxx_std_23)
endif()

# Register test sources for the central test app
file(GLOB _test_sources "${CMAKE_CURRENT_SOURCE_DIR}/tests/*_test.cpp")
if(_test_sources AND CONFIG_SPIRAM)
    set_property(GLOBAL APPEND PROPERTY IDFXX_TEST_SOURCES ${_test_sources})
endif()
