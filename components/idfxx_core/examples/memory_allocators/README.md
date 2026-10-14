# Memory Allocators

Shows how to place standard containers in specific memory and query the heap with `idfxx::memory`.

## What it demonstrates

- Storing a `std::vector` in internal DRAM with `idfxx::dram_allocator`
- Allocating a DMA-capable buffer with `idfxx::dma_allocator`
- Combining, testing and removing `idfxx::memory::capabilities` flags
- Reading total and free heap size by capability with `total_size()` and `free_size()`
- Reading detailed heap statistics with `get_info()`
- Checking heap integrity with `check_integrity()`

The example uses the exception-based API (`CONFIG_COMPILER_CXX_EXCEPTIONS=y` in `sdkconfig.defaults`).

## Hardware required

- Any ESP32-series development board

## How to use

Create a project from this example:

```bash
idf.py create-project-from-example "cleishm/idfxx_core:memory_allocators"
cd memory_allocators
```

Then build and flash it, then watch the serial output:

```bash
idf.py set-target esp32
idf.py -p PORT flash monitor
```

Replace `esp32` with your target and `PORT` with your board's serial port. Press `Ctrl-]` to exit the monitor.
