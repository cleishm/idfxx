# The components that these tests use beyond idfxx_persistent_log's own dependencies, for the
# filtered test app (see tests/CMakeLists.txt). The tests use spi_flash for the log's cache check,
# which the capture tests replace under QEMU and then restore.
set(test_requires spi_flash)
