# Test groups: the central test suite is split into one Unity app per group, so that each app fits
# the ota_0 partition. Every component with tests (components/<name>/tests/*_test.cpp) belongs to
# exactly one group; configuring fails otherwise (see the top-level CMakeLists.txt).
#
# Select a group's app with -D IDFXX_TEST_GROUP=<group> (the Justfile recipes and CI build each
# group in its own build directory). Without a selection the app holds every group's tests.
#
# When adding a component with tests, append it to the group whose components its tests resemble
# most; put it in a new group only when an app no longer fits its partition.

# The Justfile and CI (.github/workflows/build.yml) read the group names from this line, so keep it
# on one line.
set(IDFXX_TEST_GROUPS system network display)

# Kernel, peripherals, buses, storage and the drivers built on them
set(IDFXX_TEST_GROUP_system
    idfxx_core
    idfxx_log
    idfxx_task
    idfxx_queue
    idfxx_event
    idfxx_event_group
    idfxx_timer
    idfxx_gpio
    idfxx_pwm
    idfxx_adc
    idfxx_button
    idfxx_rotary_encoder
    idfxx_sleep
    idfxx_hw_support
    idfxx_console
    idfxx_nvs
    idfxx_partition
    idfxx_ota
    idfxx_dht
    idfxx_onewire
    idfxx_ds18x20
    idfxx_i2c
    idfxx_spi
    idfxx_rtc
    idfxx_rtc_pcf8563
    idfxx_sdcard
    idfxx_fatfs
    idfxx_radio
    idfxx_radio_sx126x
)

# Networking, HTTP and cryptography
set(IDFXX_TEST_GROUP_network
    idfxx_net
    idfxx_netif
    idfxx_wifi
    idfxx_http
    idfxx_http_client
    idfxx_http_server
    idfxx_https_server
    idfxx_crypto
)

# Displays, graphics, fonts and images
set(IDFXX_TEST_GROUP_display
    idfxx_panel_io
    idfxx_lcd
    idfxx_lcd_ili9341
    idfxx_lcd_ssd1306
    idfxx_lcd_touch
    idfxx_lcd_touch_stmpe610
    idfxx_epaper
    idfxx_epaper_ssd1680
    idfxx_epaper_uc8179
    idfxx_epaper_el133uf1
    idfxx_gfx
    idfxx_font
    idfxx_font_spleen
    idfxx_font_inter
    idfxx_image
    idfxx_jpeg
    idfxx_lcd_image
    idfxx_epaper_image
)
