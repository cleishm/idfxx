cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS "${CMAKE_SOURCE_DIR}/components" "${CMAKE_SOURCE_DIR}/tests")

# idfxx_wifi is restricted to Wi-Fi-capable chips (see its idf_component.yml);
# exclude it from the build on other targets or the component manager rejects
# the whole project. IDF_TARGET arrives via the cache (set-target) or the
# environment (idf.py) before project() runs.
if(NOT DEFINED IDF_TARGET AND DEFINED ENV{IDF_TARGET})
    set(IDF_TARGET $ENV{IDF_TARGET})
endif()
set(IDFXX_WIFI_TARGETS esp32 esp32s2 esp32s3 esp32c2 esp32c3 esp32c5 esp32c6 esp32c61)
if(IDF_TARGET AND NOT IDF_TARGET IN_LIST IDFXX_WIFI_TARGETS)
    set(EXCLUDE_COMPONENTS idfxx_wifi)
endif()

if(NOT DEFINED ENV{IDF_PATH} OR "$ENV{IDF_PATH}" STREQUAL "")
    message(FATAL_ERROR "IDF_PATH is not set. Source ESP-IDF export.sh before configuring.")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(idfxx)

# Formatting
find_program(CLANG_FORMAT_EXECUTABLE NAMES clang-format)

if(CLANG_FORMAT_EXECUTABLE)
    file(GLOB_RECURSE ALL_SOURCE_FILES
        ${CMAKE_SOURCE_DIR}/components/*/include/*.h
        ${CMAKE_SOURCE_DIR}/components/*/include/*.hpp
        ${CMAKE_SOURCE_DIR}/components/*/src/*.cpp
        ${CMAKE_SOURCE_DIR}/components/*/examples/*/main/*.cpp
    )

    add_custom_target(format-check
        COMMAND ${CLANG_FORMAT_EXECUTABLE} --dry-run --Werror ${ALL_SOURCE_FILES}
        COMMENT "Checking code formatting"
        VERBATIM
    )

    add_custom_target(format
        COMMAND ${CLANG_FORMAT_EXECUTABLE} -i ${ALL_SOURCE_FILES}
        COMMENT "Applying code formatting"
        VERBATIM
    )

    message(STATUS "clang-format found: format targets available")
else()
    message(WARNING "clang-format not found: format targets unavailable")
endif()

# Documentation
find_package(Doxygen)

if(DOXYGEN_FOUND)
    add_custom_target(docs
        COMMAND ${DOXYGEN_EXECUTABLE} ${CMAKE_SOURCE_DIR}/docs/Doxyfile
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/docs
        COMMENT "Generating API documentation with Doxygen"
        VERBATIM
    )
    message(STATUS "Doxygen found: documentation can be built with 'make docs'")
else()
    message(WARNING "Doxygen not found: documentation cannot be built")
endif()
