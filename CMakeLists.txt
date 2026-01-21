cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS "${CMAKE_SOURCE_DIR}/components")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(idfxx)

# Formatting
find_program(CLANG_FORMAT_EXECUTABLE NAMES clang-format)

if(CLANG_FORMAT_EXECUTABLE)
    file(GLOB_RECURSE ALL_SOURCE_FILES
        ${CMAKE_SOURCE_DIR}/components/*/include/*.h
        ${CMAKE_SOURCE_DIR}/components/*/include/*.hpp
        ${CMAKE_SOURCE_DIR}/components/*/src/*.cpp
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
