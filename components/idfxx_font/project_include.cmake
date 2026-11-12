# SPDX-License-Identifier: Apache-2.0
#
# idfxx_font_generate(): build-time generation of idfxx::font::proportional_font data from an
# outline font (TrueType/OpenType) with scripts/ttf_to_c.py.
#
# ESP-IDF includes this file at project level, so the function is available to every component
# whose requirements include idfxx_font (the main component requires all components by default).
# Call it after idf_component_register():
#
#   idf_component_register(SRCS "main.cpp" PRIV_REQUIRES idfxx_font)
#   idfxx_font_generate(
#       NAME <identifier prefix>      # fonts are named <NAME>_<size>, e.g. inter_48
#       FONT <path>                   # relative paths resolve against CMAKE_CURRENT_SOURCE_DIR
#       SIZES <px>...                 # em sizes in pixels
#       [CHARSET ascii|latin1|cp1252] # default cp1252
#       [CHARS <text>]                # extra characters
#       [RANGES <spec>]               # extra code points, e.g. "0x100-0x17F,0x2190"
#       [FACE_INDEX <n>]              # face in a .ttc collection, default 0
#       [NAMESPACE <ns>]              # default fonts
#       [RENDER mono|threshold]       # default mono
#       [THRESHOLD <1-255>]           # threshold mode only, default 128
#       [REPLACEMENT <char>|U+XXXX|none]
#   )
#
# This generates <NAME>_<size>.cpp for each size plus a header <NAMESPACE>/<NAME>.hpp declaring
# every font (a nested namespace a::b gives a/b/<NAME>.hpp), adds the sources to the component, and
# puts the generated directory on the component's (public) include path:
#
#   #include "fonts/inter.hpp"
#   idfxx::gfx::draw_text(canvas, fonts::inter_48, x, y, "Hello");
#
# Each font is regenerated when the font file or the generator changes. Fonts the application
# never references are dropped at link time.
#
# The generator needs freetype-py. By default it runs with the ESP-IDF Python; set the cache
# variable IDFXX_FONT_PYTHON to use another interpreter, e.g.
#   idf.py -DIDFXX_FONT_PYTHON=/path/to/python reconfigure

set(IDFXX_FONT_PYTHON "" CACHE FILEPATH
    "Python interpreter (with freetype-py) for idfxx_font_generate(); empty = ESP-IDF Python")

function(idfxx_font_generate)
    set(script "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/scripts/ttf_to_c.py")
    set(one_value NAME FONT CHARSET CHARS RANGES FACE_INDEX NAMESPACE RENDER THRESHOLD REPLACEMENT)
    cmake_parse_arguments(PARSE_ARGV 0 arg "" "${one_value}" "SIZES")

    if(arg_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "idfxx_font_generate: unexpected arguments: ${arg_UNPARSED_ARGUMENTS}")
    endif()
    foreach(required NAME FONT SIZES)
        if(NOT arg_${required})
            message(FATAL_ERROR "idfxx_font_generate: ${required} is required")
        endif()
    endforeach()
    if(NOT arg_NAME MATCHES "^[A-Za-z_][A-Za-z0-9_]*$")
        message(FATAL_ERROR "idfxx_font_generate: NAME '${arg_NAME}' is not a C++ identifier")
    endif()
    if(NOT arg_NAMESPACE)
        set(arg_NAMESPACE fonts)
    endif()
    if(NOT arg_NAMESPACE MATCHES "^[A-Za-z_][A-Za-z0-9_]*(::[A-Za-z_][A-Za-z0-9_]*)*$")
        message(FATAL_ERROR "idfxx_font_generate: NAMESPACE '${arg_NAMESPACE}' is not valid")
    endif()
    if(NOT COMPONENT_LIB OR NOT TARGET ${COMPONENT_LIB})
        message(FATAL_ERROR "idfxx_font_generate: call it after idf_component_register()")
    endif()
    get_target_property(lib_type ${COMPONENT_LIB} TYPE)
    if(lib_type STREQUAL "INTERFACE_LIBRARY")
        message(FATAL_ERROR
            "idfxx_font_generate: component ${COMPONENT_NAME} has no sources to add fonts to")
    endif()

    cmake_path(ABSOLUTE_PATH arg_FONT BASE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        NORMALIZE OUTPUT_VARIABLE font)
    if(NOT EXISTS "${font}")
        message(FATAL_ERROR "idfxx_font_generate: font file not found: ${font}")
    endif()

    # Resolve the interpreter and check, once per configure, that it can import freetype.
    set(python "${IDFXX_FONT_PYTHON}")
    if(NOT python)
        idf_build_get_property(python PYTHON)
    endif()
    get_property(checked GLOBAL PROPERTY IDFXX_FONT_PYTHON_CHECKED)
    if(NOT checked STREQUAL python)
        execute_process(
            COMMAND "${python}" -c "import freetype"
            RESULT_VARIABLE result
            OUTPUT_QUIET ERROR_QUIET)
        if(NOT result EQUAL 0)
            message(FATAL_ERROR
                "idfxx_font_generate: the Python interpreter\n"
                "  ${python}\n"
                "cannot import freetype (the freetype-py package). Install it with\n"
                "  ${python} -m pip install freetype-py\n"
                "or point IDFXX_FONT_PYTHON at a Python that has it:\n"
                "  idf.py -DIDFXX_FONT_PYTHON=/path/to/python reconfigure")
        endif()
        set_property(GLOBAL PROPERTY IDFXX_FONT_PYTHON_CHECKED "${python}")
    endif()

    set(out_dir "${CMAKE_CURRENT_BINARY_DIR}/idfxx_font")

    set(options --namespace "${arg_NAMESPACE}")
    foreach(opt CHARSET CHARS RANGES FACE_INDEX RENDER THRESHOLD REPLACEMENT)
        if(DEFINED arg_${opt})
            string(TOLOWER "${opt}" flag)
            string(REPLACE "_" "-" flag "${flag}")
            list(APPEND options "--${flag}" "${arg_${opt}}")
        endif()
    endforeach()

    cmake_path(GET font FILENAME font_file)
    string(REPLACE "::" "/" header_dir "${arg_NAMESPACE}")
    set(header_name "${header_dir}/${arg_NAME}.hpp")
    set(header "${out_dir}/${header_name}")
    file(MAKE_DIRECTORY "${out_dir}/${header_dir}")
    set(declarations "")
    set(sources "")
    foreach(size IN LISTS arg_SIZES)
        if(NOT size MATCHES "^[1-9][0-9]*$")
            message(FATAL_ERROR "idfxx_font_generate: SIZES value '${size}' is not a pixel size")
        endif()
        set(ident "${arg_NAME}_${size}")
        string(APPEND declarations "extern const idfxx::font::proportional_font ${ident};\n")
        set(source "${out_dir}/${ident}.cpp")
        add_custom_command(
            OUTPUT "${source}"
            COMMAND "${python}" "${script}" "${font}" "${ident}" --size "${size}"
                -o "${source}" --header "${header_name}" ${options}
            DEPENDS "${font}" "${script}"
            COMMENT "Generating font ${ident} from ${font_file}"
            VERBATIM)
        list(APPEND sources "${source}")
    endforeach()

    # The header depends only on the arguments, so it is written at configure time, and only
    # rewritten when its content changes. Values are substituted as-is, never rescanned.
    file(CONFIGURE OUTPUT "${header}" CONTENT [[
// Generated by idfxx_font_generate() from @font_file@ - DO NOT EDIT.
//
// Proportional bitmap fonts rendered from @font_file@, one per pixel size.

#pragma once

#include <idfxx/font.hpp>

namespace @arg_NAMESPACE@ {

@declarations@
} // namespace @arg_NAMESPACE@
]] @ONLY)

    target_sources(${COMPONENT_LIB} PRIVATE ${sources})
    target_include_directories(${COMPONENT_LIB} PUBLIC "${out_dir}")
endfunction()
