set(srcs "src/error.cpp" "src/hash.cpp")
# Keys and MACs need the mbedTLS threading layer (<idfxx/crypto/key> is a compile error without it),
# so that an app that only hashes builds without it
if(CONFIG_MBEDTLS_THREADING_C)
    list(APPEND srcs "src/key.cpp" "src/mac.cpp")
endif()

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS "include"
    REQUIRES mbedtls
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_23)
set_target_properties(${COMPONENT_LIB} PROPERTIES CXX_EXTENSIONS OFF)

# Register test sources for the central test app
file(GLOB _test_sources "${CMAKE_CURRENT_SOURCE_DIR}/tests/*_test.cpp")
if(NOT CONFIG_MBEDTLS_THREADING_C)
    list(FILTER _test_sources EXCLUDE REGEX "/(key|mac)_test\\.cpp$")
endif()
if(_test_sources)
    set_property(GLOBAL APPEND PROPERTY IDFXX_TEST_SOURCES ${_test_sources})
endif()
