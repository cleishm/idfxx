# Link options for the test app while these tests are in it (see tests/CMakeLists.txt).
# update_failure_test.cpp defines the wrappers, which inject failures into the crypto library's
# update functions.
set(test_link_options
    "-Wl,--wrap=psa_hash_update"
    "-Wl,--wrap=psa_mac_update"
)
