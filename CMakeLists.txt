cmake_minimum_required(VERSION 3.16)

# Components are in area directories, components/<area>/<name>. ESP-IDF looks for components only
# directly inside each EXTRA_COMPONENT_DIRS entry, so every area directory is listed. An area
# directory is a directory that holds a component.
file(GLOB _idfxx_component_files CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/components/*/*/CMakeLists.txt")
set(_idfxx_area_dirs)
foreach(_file ${_idfxx_component_files})
    get_filename_component(_dir "${_file}" DIRECTORY)
    get_filename_component(_dir "${_dir}" DIRECTORY)
    list(APPEND _idfxx_area_dirs "${_dir}")
endforeach()
list(REMOVE_DUPLICATES _idfxx_area_dirs)
set(EXTRA_COMPONENT_DIRS ${_idfxx_area_dirs} "${CMAKE_SOURCE_DIR}/tests")

# idfxx_wifi is restricted to chips that have WiFi (see its idf_component.yml).
# Exclude it from the build on other targets, or the component manager rejects
# the whole project. Before project() runs, IDF_TARGET is set in the cache
# (set-target) or in the environment (idf.py).
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
        file(GLOB _component_dir LIST_DIRECTORIES true
            "${CMAKE_SOURCE_DIR}/components/*/${_component}")
        if(NOT _component_dir)
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
# removed, so that the check runs again and tests/CMakeLists.txt reads the current files.
file(GLOB _idfxx_test_files CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/components/*/*/tests/*_test.cpp")
file(GLOB _idfxx_test_cmake_files CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/components/*/*/tests/requires.cmake"
    "${CMAKE_SOURCE_DIR}/components/*/*/tests/link_options.cmake")
foreach(_file ${_idfxx_test_files})
    # <component dir>/tests/<file>: the component is the directory's name
    get_filename_component(_component "${_file}" DIRECTORY)
    get_filename_component(_component "${_component}" DIRECTORY)
    get_filename_component(_component "${_component}" NAME)
    if(NOT _component IN_LIST _idfxx_grouped)
        message(FATAL_ERROR "tests/groups.cmake: ${_component} has tests but is in no group")
    endif()
endforeach()

# The tests that the app holds:
#   -D IDFXX_TEST_GROUP=network               one group's tests (the Justfile and CI build each)
#   -D IDFXX_TESTS="idfxx_dht;idfxx_gpio"     only those components' tests (`just test` uses this)
#   neither                                   every group's tests
# With a selection, the build includes only the tests component and the
# components that it requires, instead of every component. tests/CMakeLists.txt
# reads the list as a build property, because IDF's requirements pass cannot
# read ordinary variables.
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

# Config fragments and partition tables are in config/. IDF looks for
# sdkconfig.defaults in the project root by default, so SDKCONFIG_DEFAULTS is set
# to point at config/. IDF still finds the target-specific
# config/sdkconfig.defaults.<target> beside it. An explicit SDKCONFIG_DEFAULTS
# (-D or environment) replaces this setting, so pass the base fragment too when
# you add another fragment on top of it. The generated sdkconfig is in the build
# directory, so that each build dir has its own config.
if(NOT DEFINED SDKCONFIG_DEFAULTS AND NOT DEFINED ENV{SDKCONFIG_DEFAULTS})
    set(SDKCONFIG_DEFAULTS "${CMAKE_SOURCE_DIR}/config/sdkconfig.defaults")
endif()
if(NOT DEFINED SDKCONFIG)
    set(SDKCONFIG "${CMAKE_BINARY_DIR}/sdkconfig")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
idf_build_set_property(IDFXX_TESTS "${_idfxx_tests}")
project(idfxx)

# Formatting. Different versions of clang-format lay out code differently. So the format targets
# run only with the major version that CI checks with, which is esp-clang from CI's ESP-IDF
# (.github/workflows/build.yml). Update this version when CI's ESP-IDF changes.
set(IDFXX_CLANG_FORMAT_MAJOR 21)

function(_idfxx_clang_format_major out)
    execute_process(
        COMMAND ${CLANG_FORMAT_EXECUTABLE} --version
        OUTPUT_VARIABLE _version
        ERROR_QUIET
    )
    string(REGEX MATCH "version ([0-9]+)" _match "${_version}")
    set(${out} "${CMAKE_MATCH_1}" PARENT_SCOPE)
endfunction()

find_program(CLANG_FORMAT_EXECUTABLE NAMES clang-format)
if(CLANG_FORMAT_EXECUTABLE)
    _idfxx_clang_format_major(_clang_format_major)
    if(NOT _clang_format_major STREQUAL IDFXX_CLANG_FORMAT_MAJOR)
        # The cached path may be from another ESP-IDF. Look again on the current PATH.
        unset(CLANG_FORMAT_EXECUTABLE CACHE)
        find_program(CLANG_FORMAT_EXECUTABLE NAMES clang-format)
        if(CLANG_FORMAT_EXECUTABLE)
            _idfxx_clang_format_major(_clang_format_major)
        endif()
    endif()
endif()

if(CLANG_FORMAT_EXECUTABLE AND NOT _clang_format_major STREQUAL IDFXX_CLANG_FORMAT_MAJOR)
    string(CONCAT _clang_format_error
        "clang-format ${CLANG_FORMAT_EXECUTABLE} is version ${_clang_format_major}, but code is"
        " formatted with version ${IDFXX_CLANG_FORMAT_MAJOR} (esp-clang from CI's ESP-IDF)."
        " Activate that ESP-IDF and reconfigure this build directory."
    )
    foreach(_target format-check format)
        add_custom_target(${_target}
            COMMAND ${CMAKE_COMMAND} -E echo "${_clang_format_error}"
            COMMAND ${CMAKE_COMMAND} -E false
            VERBATIM
        )
    endforeach()
    message(STATUS "clang-format version ${_clang_format_major}: format targets fail")
elseif(CLANG_FORMAT_EXECUTABLE)
    file(GLOB_RECURSE ALL_SOURCE_FILES
        ${CMAKE_SOURCE_DIR}/components/*/*/include/*.h
        ${CMAKE_SOURCE_DIR}/components/*/*/include/*.hpp
        ${CMAKE_SOURCE_DIR}/components/*/*/src/*.h
        ${CMAKE_SOURCE_DIR}/components/*/*/src/*.hpp
        ${CMAKE_SOURCE_DIR}/components/*/*/src/*.c
        ${CMAKE_SOURCE_DIR}/components/*/*/src/*.cpp
        ${CMAKE_SOURCE_DIR}/components/*/*/examples/*/main/*.h
        ${CMAKE_SOURCE_DIR}/components/*/*/examples/*/main/*.hpp
        ${CMAKE_SOURCE_DIR}/components/*/*/examples/*/main/*.c
        ${CMAKE_SOURCE_DIR}/components/*/*/examples/*/main/*.cpp
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

# docs/Doxyfile lists each area directory in STRIP_FROM_INC_PATH and INCLUDE_PATH, so the docs show
# include paths from the component directory. The docs target fails while an area is missing from
# those lists.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/docs/Doxyfile")
file(READ "${CMAKE_SOURCE_DIR}/docs/Doxyfile" _doxyfile)
set(_idfxx_unlisted_areas)
foreach(_dir ${_idfxx_area_dirs})
    get_filename_component(_area "${_dir}" NAME)
    string(REGEX MATCHALL "\\.\\./components/${_area}[ \n]" _listed "${_doxyfile}")
    list(LENGTH _listed _count)
    if(NOT _count EQUAL 2)
        list(APPEND _idfxx_unlisted_areas ${_area})
    endif()
endforeach()

if(DOXYGEN_FOUND AND _idfxx_unlisted_areas)
    string(REPLACE ";" ", " _areas "${_idfxx_unlisted_areas}")
    string(CONCAT _docs_error
        "docs/Doxyfile: add ../components/<area> for ${_areas} to STRIP_FROM_INC_PATH and"
        " INCLUDE_PATH"
    )
    add_custom_target(docs
        COMMAND ${CMAKE_COMMAND} -E echo "${_docs_error}"
        COMMAND ${CMAKE_COMMAND} -E false
        VERBATIM
    )
    message(STATUS "${_docs_error}: docs target fails")
elseif(DOXYGEN_FOUND)
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
