# The logger needs std::format: without CONFIG_IDFXX_STD_FORMAT nothing is compiled, so that an
# app can turn formatting off while a component it uses still lists idfxx_log. Including
# <idfxx/log> then stops the build with an error saying so.
if(CONFIG_IDFXX_STD_FORMAT)
    set(_srcs "src/log.cpp")
else()
    set(_srcs "")
endif()

idf_component_register(
    SRCS ${_srcs}
    INCLUDE_DIRS "include"
    REQUIRES log
)

if(_srcs)
    target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_23)
    set_target_properties(${COMPONENT_LIB} PROPERTIES CXX_EXTENSIONS OFF)

    # Register test sources for the central test app
    file(GLOB _test_sources "${CMAKE_CURRENT_SOURCE_DIR}/tests/*_test.cpp")
    if(_test_sources)
        set_property(GLOBAL APPEND PROPERTY IDFXX_TEST_SOURCES ${_test_sources})
    endif()
else()
    target_compile_features(${COMPONENT_LIB} INTERFACE cxx_std_23)
endif()
