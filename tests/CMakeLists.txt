# Collect test sources registered by each component via the IDFXX_TEST_SOURCES property
get_property(IDFXX_TEST_SOURCES GLOBAL PROPERTY IDFXX_TEST_SOURCES)

set(IDFXX_TEST_REQUIRES
    unity idfxx_core idfxx_crypto esp_mm idfxx_hw_support idfxx_gpio idfxx_nvs idfxx_partition idfxx_ota idfxx_i2c idfxx_spi idfxx_sdcard esp_driver_sdspi idfxx_fatfs idfxx_panel_io idfxx_lcd
    idfxx_lcd_ili9341 idfxx_lcd_ssd1306 idfxx_lcd_touch idfxx_lcd_touch_stmpe610 idfxx_onewire idfxx_ds18x20
    idfxx_timer idfxx_event
    idfxx_event_group idfxx_task idfxx_queue idfxx_log idfxx_http idfxx_http_client idfxx_http_server
    idfxx_https_server idfxx_console idfxx_rotary_encoder idfxx_button idfxx_pwm idfxx_net idfxx_netif idfxx_sleep
    esp_netif idfxx_dht esp_driver_rmt idfxx_radio idfxx_radio_sx126x idfxx_font idfxx_font_spleen idfxx_font_inter idfxx_gfx
    idfxx_epaper idfxx_epaper_ssd1680 idfxx_epaper_uc8179 idfxx_epaper_el133uf1 idfxx_image idfxx_jpeg idfxx_lcd_image idfxx_epaper_image
    idfxx_rtc idfxx_rtc_pcf8563
)

# idfxx_adc pulls in esp_adc, whose boot-time analog calibration hangs under
# QEMU (which does not emulate the ADC peripheral). Keep it out of the QEMU link;
# its tests are also skipped there (see components/idfxx_adc/CMakeLists.txt).
# QEMU is a build config rather than a target, so — unlike idfxx_wifi below — it
# can't be excluded via a top-level EXCLUDE_COMPONENTS, which is resolved before
# sdkconfig (and thus CONFIG_IDF_TARGET_QEMU) is known.
if(NOT CONFIG_IDF_TARGET_QEMU)
    list(APPEND IDFXX_TEST_REQUIRES idfxx_adc)
endif()

# idfxx_wifi is only available on Wi-Fi-capable targets (it is excluded from
# the build on other targets; see the top-level CMakeLists.txt).
idf_build_get_property(idfxx_test_target IDF_TARGET)
set(IDFXX_WIFI_TARGETS esp32 esp32s2 esp32s3 esp32c2 esp32c3 esp32c5 esp32c6 esp32c61)
if(idfxx_test_target IN_LIST IDFXX_WIFI_TARGETS)
    list(APPEND IDFXX_TEST_REQUIRES idfxx_wifi)
endif()

# Filtered test app (IDFXX_TESTS, see the top-level CMakeLists.txt): require only
# the selected components, plus whatever their tests use beyond the components'
# own dependencies, declared in components/<name>/tests/requires.cmake. This has
# to happen before idf_component_register: in IDF's requirements pass, the code
# after it never runs.
idf_build_get_property(idfxx_tests IDFXX_TESTS)
if(idfxx_tests)
    set(IDFXX_TEST_REQUIRES unity idfxx_core idfxx_log esp_netif ${idfxx_tests})
    foreach(_component ${idfxx_tests})
        set(_requires_file "${CMAKE_CURRENT_LIST_DIR}/../components/${_component}/tests/requires.cmake")
        if(EXISTS "${_requires_file}")
            set(test_requires)
            include("${_requires_file}")
            list(APPEND IDFXX_TEST_REQUIRES ${test_requires})
        endif()
    endforeach()
endif()

idf_component_register(
    SRCS "main.cpp"
    INCLUDE_DIRS "."
    REQUIRES ${IDFXX_TEST_REQUIRES}
    WHOLE_ARCHIVE
)

# Add test sources via target_sources to avoid ESP-IDF 6.0 component validation warnings.
# The validation only checks idf_component_register(SRCS ...), not target_sources.
if(idfxx_tests)
    # Only the selected components' tests; their dependencies are in the build
    # but their tests are not. Tests also see the component's private
    # requirements, as they exercise its internals.
    set(_selected_sources)
    foreach(_component ${idfxx_tests})
        foreach(_source ${IDFXX_TEST_SOURCES})
            if(_source MATCHES "/components/${_component}/tests/")
                list(APPEND _selected_sources ${_source})
            endif()
        endforeach()
        idf_component_get_property(_priv ${_component} PRIV_REQUIRES)
        foreach(_req ${_priv})
            idf_component_get_property(_lib ${_req} COMPONENT_LIB)
            target_link_libraries(${COMPONENT_LIB} PRIVATE ${_lib})
        endforeach()
    endforeach()
    set(IDFXX_TEST_SOURCES ${_selected_sources})
endif()
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
# build, which a filtered test app may leave out.
if(COMMAND fatfs_create_rawflash_image)
    fatfs_create_rawflash_image(fatimg "${CMAKE_CURRENT_SOURCE_DIR}/../components/idfxx_fatfs/tests/fatimg" FLASH_IN_PROJECT)
endif()
