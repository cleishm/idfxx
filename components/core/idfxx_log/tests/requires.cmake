# The components that these tests use beyond idfxx_log's own dependencies, for
# the filtered test app (see tests/CMakeLists.txt). The tests use spi_flash's
# cache check, which qemu_log_cache.hpp replaces under QEMU.
set(test_requires spi_flash)
