# Collect test sources registered by each component via the IDFXX_TEST_SOURCES property
get_property(IDFXX_TEST_SOURCES GLOBAL PROPERTY IDFXX_TEST_SOURCES)

# Components whose tests the app runs: a group's (tests/groups.cmake), the ones named in
# IDFXX_TESTS, or every group's (see the top-level CMakeLists.txt). The app requires them, plus
# whatever their tests use beyond the components' own dependencies, declared in
# components/<name>/tests/requires.cmake. This has to happen before idf_component_register: in
# IDF's requirements pass, the code after it never runs.
idf_build_get_property(idfxx_tests IDFXX_TESTS)

# idfxx_wifi is only available on Wi-Fi-capable targets (it is excluded from the build on other
# targets; see the top-level CMakeLists.txt), so it and its tests are left out there. Requirements
# can't depend on sdkconfig (IDF's requirements pass runs before it is generated), so tests that
# depend on the config, such as idfxx_adc's (skipped under QEMU), are left out by the component
# that registers them instead.
idf_build_get_property(idfxx_test_target IDF_TARGET)
set(IDFXX_WIFI_TARGETS esp32 esp32s2 esp32s3 esp32c2 esp32c3 esp32c5 esp32c6 esp32c61)
set(_unavailable)
if(NOT idfxx_test_target IN_LIST IDFXX_WIFI_TARGETS)
    list(APPEND _unavailable idfxx_wifi)
    list(REMOVE_ITEM idfxx_tests ${_unavailable})
endif()

set(IDFXX_TEST_REQUIRES unity idfxx_core idfxx_log esp_netif ${idfxx_tests})
foreach(_component ${idfxx_tests})
    set(_requires_file "${CMAKE_CURRENT_LIST_DIR}/../components/${_component}/tests/requires.cmake")
    if(EXISTS "${_requires_file}")
        set(test_requires)
        include("${_requires_file}")
        list(APPEND IDFXX_TEST_REQUIRES ${test_requires})
    endif()
endforeach()
list(REMOVE_DUPLICATES IDFXX_TEST_REQUIRES)
if(_unavailable)
    list(REMOVE_ITEM IDFXX_TEST_REQUIRES ${_unavailable})
endif()

idf_component_register(
    SRCS "main.cpp"
    INCLUDE_DIRS "."
    REQUIRES ${IDFXX_TEST_REQUIRES}
    WHOLE_ARCHIVE
)

# Add test sources via target_sources to avoid ESP-IDF 6.0 component validation warnings.
# The validation only checks idf_component_register(SRCS ...), not target_sources.
# Only the selected components' tests, in the order the components registered them; their
# dependencies are in the build but their tests are not. Tests also see the component's private
# requirements, as they exercise its internals.
set(_selected_sources)
foreach(_source ${IDFXX_TEST_SOURCES})
    if(_source MATCHES "/components/([^/]+)/tests/[^/]+$" AND CMAKE_MATCH_1 IN_LIST idfxx_tests)
        list(APPEND _selected_sources ${_source})
    endif()
endforeach()
set(IDFXX_TEST_SOURCES ${_selected_sources})
foreach(_component ${idfxx_tests})
    idf_component_get_property(_priv ${_component} PRIV_REQUIRES)
    foreach(_req ${_priv})
        idf_component_get_property(_lib ${_req} COMPONENT_LIB)
        target_link_libraries(${COMPONENT_LIB} PRIVATE ${_lib})
    endforeach()
endforeach()
target_sources(${COMPONENT_LIB} PRIVATE ${IDFXX_TEST_SOURCES})

# Link options that tests need, such as --wrap to inject failures, declared in
# components/<name>/tests/link_options.cmake (set(test_link_options ...)). Applied only while that
# directory's tests are in the app, as they define the symbols the options refer to.
set(_test_dirs)
foreach(_source ${IDFXX_TEST_SOURCES})
    get_filename_component(_dir "${_source}" DIRECTORY)
    list(APPEND _test_dirs "${_dir}")
endforeach()
list(REMOVE_DUPLICATES _test_dirs)
foreach(_dir ${_test_dirs})
    if(EXISTS "${_dir}/link_options.cmake")
        set(test_link_options)
        include("${_dir}/link_options.cmake")
        target_link_options(${COMPONENT_LIB} INTERFACE ${test_link_options})
    endif()
endforeach()

target_compile_options(${COMPONENT_LIB} PRIVATE -std=gnu++23)

# Raw FAT image for the idfxx_fatfs image tests, written to the fatimg partition by `idf.py flash`
# (and included in the merged QEMU flash image). Only defined when the fatfs component is in the
# build, which a group or filtered test app may leave out.
if(COMMAND fatfs_create_rawflash_image)
    fatfs_create_rawflash_image(fatimg "${CMAKE_CURRENT_SOURCE_DIR}/../components/idfxx_fatfs/tests/fatimg" FLASH_IN_PROJECT)
endif()
