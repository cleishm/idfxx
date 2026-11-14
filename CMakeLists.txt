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

# The test suite is split into groups, one Unity app each (tests/groups.cmake).
# Every component with tests must be in exactly one group.
include("${CMAKE_SOURCE_DIR}/tests/groups.cmake")
set(_idfxx_grouped)
foreach(_group ${IDFXX_TEST_GROUPS})
    foreach(_component ${IDFXX_TEST_GROUP_${_group}})
        if(NOT IS_DIRECTORY "${CMAKE_SOURCE_DIR}/components/${_component}")
            message(FATAL_ERROR
                "tests/groups.cmake: group ${_group} names ${_component}, which is not a component")
        endif()
        if(_component IN_LIST _idfxx_grouped)
            message(FATAL_ERROR "tests/groups.cmake: ${_component} is in more than one group")
        endif()
        list(APPEND _idfxx_grouped ${_component})
    endforeach()
endforeach()
# CONFIGURE_DEPENDS makes a build reconfigure when test files or test requirements are added or
# removed, so that the check reruns and tests/CMakeLists.txt sees them.
file(GLOB _idfxx_test_files CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/components/*/tests/*_test.cpp")
file(GLOB _idfxx_test_cmake_files CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/components/*/tests/requires.cmake"
    "${CMAKE_SOURCE_DIR}/components/*/tests/link_options.cmake")
foreach(_file ${_idfxx_test_files})
    string(REGEX REPLACE "^.*/components/([^/]+)/tests/[^/]+$" "\\1" _component "${_file}")
    if(NOT _component IN_LIST _idfxx_grouped)
        message(FATAL_ERROR "tests/groups.cmake: ${_component} has tests but is in no group")
    endif()
endforeach()

# Which components' tests the app holds:
#   -D IDFXX_TEST_GROUP=network               one group's (the Justfile and CI build each group)
#   -D IDFXX_TESTS="idfxx_dht;idfxx_gpio"     just those components' (`just test` drives it)
#   neither                                   every group's
# A selection builds only the tests component and the components it requires,
# instead of every component. tests/CMakeLists.txt reads the list as a build
# property, since IDF's requirements pass doesn't see ordinary variables.
if(IDFXX_TEST_GROUP)
    if(IDFXX_TESTS)
        message(FATAL_ERROR
            "Set IDFXX_TEST_GROUP or IDFXX_TESTS, not both (use a fresh build directory)")
    endif()
    if(NOT IDFXX_TEST_GROUP IN_LIST IDFXX_TEST_GROUPS)
        string(REPLACE ";" ", " _groups "${IDFXX_TEST_GROUPS}")
        message(FATAL_ERROR "Unknown IDFXX_TEST_GROUP '${IDFXX_TEST_GROUP}' (groups: ${_groups})")
    endif()
    set(_idfxx_tests ${IDFXX_TEST_GROUP_${IDFXX_TEST_GROUP}})
elseif(IDFXX_TESTS)
    set(_idfxx_tests ${IDFXX_TESTS})
else()
    set(_idfxx_tests ${_idfxx_grouped})
endif()
if(IDFXX_TEST_GROUP OR IDFXX_TESTS)
    set(COMPONENTS tests)
endif()

# Config fragments and partition tables live in config/, so IDF's root-level
# sdkconfig.defaults lookup needs pointing there; the target-specific
# config/sdkconfig.defaults.<target> is still picked up beside it. An explicit
# SDKCONFIG_DEFAULTS (-D or environment) replaces this, so pass the base
# fragment too when layering one on top. The generated sdkconfig lives in the
# build directory, keeping each build dir's config separate.
if(NOT DEFINED SDKCONFIG_DEFAULTS AND NOT DEFINED ENV{SDKCONFIG_DEFAULTS})
    set(SDKCONFIG_DEFAULTS "${CMAKE_SOURCE_DIR}/config/sdkconfig.defaults")
endif()
if(NOT DEFINED SDKCONFIG)
    set(SDKCONFIG "${CMAKE_BINARY_DIR}/sdkconfig")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
idf_build_set_property(IDFXX_TESTS "${_idfxx_tests}")
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
