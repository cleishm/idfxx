idf_component_register(
    SRCS "src/error.cpp" "src/renderer.cpp" "src/resampler.cpp"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "src"
    REQUIRES idfxx_core
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_23)
set_target_properties(${COMPONENT_LIB} PROPERTIES CXX_EXTENSIONS OFF)

# Resampling coefficients must round after each operation as written, so the
# output is the same on every target: no fused multiply-add.
target_compile_options(${COMPONENT_LIB} PRIVATE -ffp-contract=off)

# Register test sources for the central test app
file(GLOB _test_sources "${CMAKE_CURRENT_SOURCE_DIR}/tests/*_test.cpp")
if(_test_sources)
    set_property(GLOBAL APPEND PROPERTY IDFXX_TEST_SOURCES ${_test_sources})
endif()
