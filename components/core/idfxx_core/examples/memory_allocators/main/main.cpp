// SPDX-License-Identifier: Apache-2.0

#include <idfxx/log>
#include <idfxx/memory>

#include <vector>

static constexpr idfxx::log::logger logger{"example"};

extern "C" void app_main() {
    namespace memory = idfxx::memory;

    // DRAM allocator with std::vector
    logger.info("=== DRAM Allocator ===");
    std::vector<uint32_t, idfxx::dram_allocator<uint32_t>> dram_vec;
    dram_vec.push_back(0xDEADBEEF);
    dram_vec.push_back(0xCAFEBABE);
    logger.info("DRAM vector: [{:#010x}, {:#010x}]", dram_vec[0], dram_vec[1]);
    logger.info("DRAM vector size: {}, capacity: {}", dram_vec.size(), dram_vec.capacity());

    // DMA allocator for DMA-capable buffers
    logger.info("=== DMA Allocator ===");
    std::vector<uint8_t, idfxx::dma_allocator<uint8_t>> dma_buf(64, 0xFF);
    logger.info("DMA buffer size: {} bytes", dma_buf.size());

    // Memory capability flags
    logger.info("=== Memory Capability Flags ===");
    auto caps_flags = memory::capabilities::internal | memory::capabilities::access_8bit;
    logger.info("Combined caps: {}", caps_flags);
    logger.info("Contains internal? {}", caps_flags.contains(memory::capabilities::internal));
    logger.info("Contains DMA?      {}", caps_flags.contains(memory::capabilities::dma));
    logger.info("Empty?             {}", caps_flags.empty());

    // Set difference: remove a flag
    auto without_8bit = caps_flags - memory::capabilities::access_8bit;
    logger.info("After removing access_8bit: {}", without_8bit);

    // Heap queries by capability
    logger.info("=== Heap Info by Capability ===");
    auto dram_total = memory::total_size(memory::capabilities::dram);
    auto dram_free = memory::free_size(memory::capabilities::dram);
    logger.info("DRAM total: {} bytes, free: {} bytes", dram_total, dram_free);

    // Detailed heap statistics
    logger.info("=== Detailed Heap Statistics ===");
    auto hi = memory::get_info(memory::capabilities::dram);
    logger.info("Total free:      {} bytes", hi.total_free_bytes);
    logger.info("Total allocated: {} bytes", hi.total_allocated_bytes);
    logger.info("Largest block:   {} bytes", hi.largest_free_block);
    logger.info("Min free (hwm):  {} bytes", hi.minimum_free_bytes);
    logger.info("Allocated blocks: {}", hi.allocated_blocks);
    logger.info("Free blocks:      {}", hi.free_blocks);

    // Heap integrity check
    logger.info("=== Heap Integrity ===");
    bool ok = memory::check_integrity();
    logger.info("Heap integrity: {}", ok ? "OK" : "CORRUPT");
}
