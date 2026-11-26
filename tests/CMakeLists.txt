# Collect test sources registered by each component via the IDFXX_TEST_SOURCES property
get_property(IDFXX_TEST_SOURCES GLOBAL PROPERTY IDFXX_TEST_SOURCES)

# Components whose tests the app runs: the components of one group (tests/groups.cmake), the
# components named in IDFXX_TESTS, or the components of every group (see the top-level
# CMakeLists.txt). The app requires them. It also requires the components that their tests use
# beyond the components' own dependencies. Each component declares these extra components in
# components/<area>/<name>/tests/requires.cmake. This has to happen before idf_component_register,
# because in IDF's requirements pass, the code after it never runs.
idf_build_get_property(idfxx_tests IDFXX_TESTS)

# idfxx_wifi is only available on targets that have Wi-Fi. On other targets, it is excluded from the
# build (see the top-level CMakeLists.txt), so it and its tests are left out of the app.
# Requirements can't depend on sdkconfig, because IDF's requirements pass runs before sdkconfig is
# generated. So tests that depend on the config are left out by the component that registers them
# instead. An example is idfxx_adc's tests, which are skipped under QEMU.
idf_build_get_property(idfxx_test_target IDF_TARGET)
set(IDFXX_WIFI_TARGETS esp32 esp32s2 esp32s3 esp32c2 esp32c3 esp32c5 esp32c6 esp32c61)
set(_unavailable)
if(NOT idfxx_test_target IN_LIST IDFXX_WIFI_TARGETS)
    list(APPEND _unavailable idfxx_wifi)
    list(REMOVE_ITEM idfxx_tests ${_unavailable})
endif()

set(IDFXX_TEST_REQUIRES unity idfxx_core idfxx_log esp_netif ${idfxx_tests})
foreach(_component ${idfxx_tests})
    file(GLOB _requires_file
        "${CMAKE_CURRENT_LIST_DIR}/../components/*/${_component}/tests/requires.cmake")
    if(_requires_file)
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
# Add only the selected components' tests, in the order that the components registered them. The
# dependencies of these components are in the build, but their tests are not. Tests are also linked
# against the component's private requirements, because they exercise its internals.
set(_selected_sources)
foreach(_source ${IDFXX_TEST_SOURCES})
    # <component dir>/tests/<file>: the component is the directory's name
    get_filename_component(_component "${_source}" DIRECTORY)
    get_filename_component(_component "${_component}" DIRECTORY)
    get_filename_component(_component "${_component}" NAME)
    if(_component IN_LIST idfxx_tests)
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

# Link options that tests need, such as --wrap to inject failures. A tests directory declares them
# in components/<area>/<name>/tests/link_options.cmake (set(test_link_options ...)). The options
# apply only while that directory's tests are in the app, because the tests define the symbols that
# the options refer to.
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

# Raw FAT image for the idfxx_fatfs image tests. `idf.py flash` writes it to the fatimg partition,
# and the merged QEMU flash image includes it. fatfs_create_rawflash_image is only defined when the
# fatfs component is in the build. A group app or a filtered test app may not include the fatfs
# component.
if(COMMAND fatfs_create_rawflash_image)
    file(GLOB _fatimg LIST_DIRECTORIES true
        "${CMAKE_CURRENT_SOURCE_DIR}/../components/*/idfxx_fatfs/tests/fatimg")
    fatfs_create_rawflash_image(fatimg "${_fatimg}" FLASH_IN_PROJECT)
endif()
