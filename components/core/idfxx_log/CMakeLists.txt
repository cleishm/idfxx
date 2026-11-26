# The logger needs std::format. Without CONFIG_IDFXX_STD_FORMAT nothing is compiled, so that an
# app can turn formatting off while a component that it uses still lists idfxx_log. Including
# <idfxx/log> then stops the build with an error that says the option must be enabled.
if(CONFIG_IDFXX_STD_FORMAT)
    set(_srcs "src/log.cpp" "src/sink.cpp")
    if(CONFIG_IDFXX_LOG_ALL_LINES)
        list(APPEND _srcs "src/all_lines.cpp")
    endif()
else()
    set(_srcs "")
endif()

idf_component_register(
    SRCS ${_srcs}
    INCLUDE_DIRS "include"
    REQUIRES idfxx_core log
    PRIV_REQUIRES esp_common freertos
)

if(_srcs)
    target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_23)
    set_target_properties(${COMPONENT_LIB} PROPERTIES CXX_EXTENSIONS OFF)

    # A sink added with all_lines receives ESP-IDF's log lines because idfxx_log wraps esp_log and
    # esp_log_va at link time. The wrappers are also marked as undefined symbols, so that they are
    # linked regardless of which functions the app calls.
    if(CONFIG_IDFXX_LOG_ALL_LINES)
        target_link_libraries(
            ${COMPONENT_LIB}
            INTERFACE "-Wl,--wrap=esp_log" "-Wl,--wrap=esp_log_va" "-Wl,-u,__wrap_esp_log"
                      "-Wl,-u,__wrap_esp_log_va"
        )
    endif()

    # Register test sources for the central test app
    file(GLOB _test_sources "${CMAKE_CURRENT_SOURCE_DIR}/tests/*_test.cpp")
    if(_test_sources)
        set_property(GLOBAL APPEND PROPERTY IDFXX_TEST_SOURCES ${_test_sources})
    endif()
else()
    target_compile_features(${COMPONENT_LIB} INTERFACE cxx_std_23)
endif()
