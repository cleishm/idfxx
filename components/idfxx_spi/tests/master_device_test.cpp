// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#include "idfxx/future"
#include "idfxx/spi/master"
#include "unity.h"

#include <driver/spi_master.h>
#include <type_traits>
#include <vector>

using namespace idfxx;
using namespace idfxx::spi;
using namespace frequency_literals;

// =============================================================================
// Compile-time tests (static_assert)
// =============================================================================

static_assert(!std::is_default_constructible_v<master_device>);
static_assert(!std::is_copy_constructible_v<master_device>);
static_assert(!std::is_copy_assignable_v<master_device>);
static_assert(std::is_move_constructible_v<master_device>);
static_assert(std::is_move_assignable_v<master_device>);

static_assert(std::is_aggregate_v<master_device::config>);
static_assert(std::is_default_constructible_v<master_device::config>);

static_assert(std::is_default_constructible_v<transaction>);
static_assert(std::is_copy_constructible_v<transaction>);
static_assert(std::is_copy_assignable_v<transaction>);
static_assert(std::is_move_constructible_v<transaction>);
static_assert(std::is_move_assignable_v<transaction>);
static_assert(std::is_trivially_copy_constructible_v<transaction>);
static_assert(std::is_trivially_move_constructible_v<transaction>);

static_assert(std::to_underlying(device_flags::txbit_lsbfirst) == SPI_DEVICE_TXBIT_LSBFIRST);
static_assert(std::to_underlying(device_flags::rxbit_lsbfirst) == SPI_DEVICE_RXBIT_LSBFIRST);
static_assert(std::to_underlying(device_flags::bit_lsbfirst) == SPI_DEVICE_BIT_LSBFIRST);
static_assert(std::to_underlying(device_flags::three_wire) == SPI_DEVICE_3WIRE);
static_assert(std::to_underlying(device_flags::positive_cs) == SPI_DEVICE_POSITIVE_CS);
static_assert(std::to_underlying(device_flags::halfduplex) == SPI_DEVICE_HALFDUPLEX);
static_assert(std::to_underlying(device_flags::clk_as_cs) == SPI_DEVICE_CLK_AS_CS);
static_assert(std::to_underlying(device_flags::no_dummy) == SPI_DEVICE_NO_DUMMY);
static_assert(std::to_underlying(device_flags::ddrclk) == SPI_DEVICE_DDRCLK);
static_assert(std::to_underlying(device_flags::no_return_result) == SPI_DEVICE_NO_RETURN_RESULT);

static_assert(std::to_underlying(trans_flags::mode_dio) == SPI_TRANS_MODE_DIO);
static_assert(std::to_underlying(trans_flags::mode_qio) == SPI_TRANS_MODE_QIO);
static_assert(std::to_underlying(trans_flags::mode_oct) == SPI_TRANS_MODE_OCT);
static_assert(std::to_underlying(trans_flags::use_rxdata) == SPI_TRANS_USE_RXDATA);
static_assert(std::to_underlying(trans_flags::use_txdata) == SPI_TRANS_USE_TXDATA);
static_assert(std::to_underlying(trans_flags::multiline_addr) == SPI_TRANS_MULTILINE_ADDR);
static_assert(std::to_underlying(trans_flags::multiline_cmd) == SPI_TRANS_MULTILINE_CMD);
static_assert(std::to_underlying(trans_flags::variable_cmd) == SPI_TRANS_VARIABLE_CMD);
static_assert(std::to_underlying(trans_flags::variable_addr) == SPI_TRANS_VARIABLE_ADDR);
static_assert(std::to_underlying(trans_flags::variable_dummy) == SPI_TRANS_VARIABLE_DUMMY);
static_assert(std::to_underlying(trans_flags::cs_keep_active) == SPI_TRANS_CS_KEEP_ACTIVE);
static_assert(std::to_underlying(trans_flags::dma_buffer_align_manual) == SPI_TRANS_DMA_BUFFER_ALIGN_MANUAL);
static_assert(std::to_underlying(trans_flags::dma_use_psram) == SPI_TRANS_DMA_USE_PSRAM);

// =============================================================================
// Runtime tests (Unity TEST_CASE)
// =============================================================================

TEST_CASE("master_device::config default initialization", "[idfxx][spi]") {
    master_device::config cfg{};
    TEST_ASSERT_EQUAL(0, cfg.command_bits);
    TEST_ASSERT_EQUAL(0, cfg.address_bits);
    TEST_ASSERT_EQUAL(0, cfg.dummy_bits);
    TEST_ASSERT_EQUAL(0, cfg.mode);
    TEST_ASSERT_EQUAL(0, cfg.duty_cycle_pos);
    TEST_ASSERT_EQUAL(0, cfg.cs_ena_pretrans);
    TEST_ASSERT_EQUAL(0, cfg.cs_ena_posttrans);
    TEST_ASSERT_EQUAL(0, cfg.clock_speed.count());
    TEST_ASSERT_EQUAL(0, cfg.input_delay.count());
    TEST_ASSERT_FALSE(cfg.cs.is_connected());
    TEST_ASSERT_TRUE(cfg.flags.empty());
    TEST_ASSERT_EQUAL(1, cfg.queue_size);
}

TEST_CASE("transaction default initialization", "[idfxx][spi]") {
    transaction trans{};
    TEST_ASSERT_TRUE(trans.flags.empty());
    TEST_ASSERT_EQUAL(0, trans.cmd);
    TEST_ASSERT_EQUAL(0, trans.addr);
    TEST_ASSERT_TRUE(trans.tx_buffer.empty());
    TEST_ASSERT_TRUE(trans.rx_buffer.empty());
    TEST_ASSERT_EQUAL(0, trans.length);
    TEST_ASSERT_EQUAL(0, trans.rx_length);
    TEST_ASSERT_TRUE(trans.override_freq == 0_Hz);
    TEST_ASSERT_EQUAL(0, trans.command_bits);
    TEST_ASSERT_EQUAL(0, trans.address_bits);
    TEST_ASSERT_EQUAL(0, trans.dummy_bits);
}

// =============================================================================
// Hardware-dependent tests
// These tests interact with actual SPI hardware
// =============================================================================

namespace {

master_bus make_test_bus() {
    bus_config cfg{};
    cfg.mosi = gpio_11;
    cfg.miso = gpio_13;
    cfg.sclk = gpio_12;
    cfg.max_transfer_sz = 4096;
    auto r = master_bus::make(host_device::spi2, dma_chan::ch_auto, cfg);
    TEST_ASSERT_TRUE(r.has_value());
    return std::move(*r);
}

master_device::config default_device_config() {
    master_device::config cfg{};
    cfg.clock_speed = 1000000_Hz;
    cfg.cs = gpio_10;
    return cfg;
}

} // namespace

TEST_CASE("master_device::make creates device on bus", "[idfxx][spi]") {
    auto bus = make_test_bus();
    auto dev_cfg = default_device_config();
    dev_cfg.queue_size = 1;

    auto device = master_device::make(bus, dev_cfg);
    TEST_ASSERT_TRUE(device.has_value());
    TEST_ASSERT_NOT_NULL(device->idf_handle());
}

TEST_CASE("master_device actual_frequency returns non-zero", "[idfxx][spi]") {
    auto bus = make_test_bus();
    auto device = master_device::make(bus, default_device_config());
    TEST_ASSERT_TRUE(device.has_value());

    auto freq = device->frequency();
    TEST_ASSERT_GREATER_THAN(0, freq.count());
}

TEST_CASE("master_device move semantics", "[idfxx][spi]") {
    auto bus = make_test_bus();
    auto dev_cfg = default_device_config();

    auto result = master_device::make(bus, dev_cfg);
    TEST_ASSERT_TRUE(result.has_value());

    // Move construct
    master_device moved(std::move(*result));
    TEST_ASSERT_NOT_NULL(moved.idf_handle());
    TEST_ASSERT_NULL(result->idf_handle());

    // Move assign
    auto result2 = master_device::make(bus, dev_cfg);
    TEST_ASSERT_TRUE(result2.has_value());
    moved = std::move(*result2);
    TEST_ASSERT_NOT_NULL(moved.idf_handle());
}

TEST_CASE("master_device basic transmit", "[idfxx][spi]") {
    auto bus = make_test_bus();
    auto device = master_device::make(bus, default_device_config());
    TEST_ASSERT_TRUE(device.has_value());

    uint8_t data[] = {0x01, 0x02, 0x03, 0x04};
    auto res = device->try_transmit(std::span<const uint8_t>(data));
    TEST_ASSERT_TRUE(res.has_value());
}

TEST_CASE("master_device rejects transaction length exceeding buffer", "[idfxx][spi]") {
    auto bus = make_test_bus();
    auto device = master_device::make(bus, default_device_config());
    TEST_ASSERT_TRUE(device.has_value());

    uint8_t data[2] = {0x00, 0x00};
    transaction trans{};
    trans.tx_buffer = data;
    trans.length = 17; // 3 bytes worth — exceeds 16-bit buffer capacity

    auto res = device->try_transmit(trans);
    TEST_ASSERT_FALSE(res.has_value());
}

TEST_CASE("master_device queue_trans/wait per-transaction", "[idfxx][spi]") {
    auto bus = make_test_bus();
    auto dev_cfg = default_device_config();
    dev_cfg.queue_size = 2;

    auto device = master_device::make(bus, dev_cfg);
    TEST_ASSERT_TRUE(device.has_value());

    uint8_t d1[] = {0x11};
    uint8_t d2[] = {0x22};

    transaction t1{};
    t1.tx_buffer = d1;
    t1.length = sizeof(d1) * 8;

    transaction t2{};
    t2.tx_buffer = d2;
    t2.length = sizeof(d2) * 8;

    auto f1 = device->try_queue_trans(t1);
    TEST_ASSERT_TRUE(f1.has_value());
    TEST_ASSERT_TRUE(f1->valid());

    auto f2 = device->try_queue_trans(t2);
    TEST_ASSERT_TRUE(f2.has_value());
    TEST_ASSERT_TRUE(f2->valid());

    // Wait for the second transaction first — cooperative retrieval should
    // drain t1's completion and mark it done along the way.
    TEST_ASSERT_TRUE(f2->try_wait().has_value());
    TEST_ASSERT_TRUE(f2->done());
    TEST_ASSERT_TRUE(f1->try_wait().has_value()); // should return immediately
    TEST_ASSERT_TRUE(f1->done());

    // Regression for the old pool wrap-around bug: a third queue_trans after
    // draining the queue must still succeed.
    uint8_t d3[] = {0x33};
    transaction t3{};
    t3.tx_buffer = d3;
    t3.length = sizeof(d3) * 8;
    auto f3 = device->try_queue_trans(t3);
    TEST_ASSERT_TRUE(f3.has_value());
    TEST_ASSERT_TRUE(f3->try_wait().has_value());
}

TEST_CASE("master_device transaction vector storage and queueing", "[idfxx][spi]") {
    auto bus = make_test_bus();
    auto dev_cfg = default_device_config();
    dev_cfg.queue_size = 4;

    auto device = master_device::make(bus, dev_cfg);
    TEST_ASSERT_TRUE(device.has_value());

    // Build a vector of transactions; moving the vector around must be a no-op
    // on the elements' addresses from queue_trans's perspective, because the
    // transaction contents are copied into slots at submission time.
    uint8_t data[3][2] = {{0x01, 0x02}, {0x03, 0x04}, {0x05, 0x06}};

    std::vector<transaction> batch;
    for (size_t i = 0; i < 3; ++i) {
        transaction t{};
        t.tx_buffer = std::span<const uint8_t>(data[i]);
        t.length = sizeof(data[i]) * 8;
        batch.push_back(t);
    }

    // Move the vector (reallocation-safe — transactions are value types now).
    auto batch_moved = std::move(batch);

    std::vector<idfxx::future<void>> futures;
    for (auto& t : batch_moved) {
        auto f = device->try_queue_trans(t);
        TEST_ASSERT_TRUE(f.has_value());
        futures.push_back(std::move(*f));
    }

    for (auto& f : futures) {
        TEST_ASSERT_TRUE(f.try_wait().has_value());
    }
}

TEST_CASE("master_device future copy shares completion", "[idfxx][spi]") {
    auto bus = make_test_bus();
    auto dev_cfg = default_device_config();
    dev_cfg.queue_size = 1;

    auto device = master_device::make(bus, dev_cfg);
    TEST_ASSERT_TRUE(device.has_value());

    uint8_t data[] = {0xA5};
    transaction t{};
    t.tx_buffer = data;
    t.length = sizeof(data) * 8;

    auto f_res = device->try_queue_trans(t);
    TEST_ASSERT_TRUE(f_res.has_value());
    auto f1 = std::move(*f_res);
    auto f2 = f1; // copy — shares the slot
    TEST_ASSERT_TRUE(f1.valid());
    TEST_ASSERT_TRUE(f2.valid());

    // Waiting on either one completes the transaction for both.
    TEST_ASSERT_TRUE(f1.try_wait().has_value());
    TEST_ASSERT_TRUE(f1.done());
    TEST_ASSERT_TRUE(f2.done());
}

TEST_CASE("master_device dropped future slot is eventually reclaimed", "[idfxx][spi]") {
    auto bus = make_test_bus();
    auto dev_cfg = default_device_config();
    dev_cfg.queue_size = 2;

    auto device = master_device::make(bus, dev_cfg);
    TEST_ASSERT_TRUE(device.has_value());

    uint8_t d1[] = {0x10};
    uint8_t d2[] = {0x20};

    transaction t1{};
    t1.tx_buffer = d1;
    t1.length = sizeof(d1) * 8;

    transaction t2{};
    t2.tx_buffer = d2;
    t2.length = sizeof(d2) * 8;

    // Queue two transactions, immediately drop the first future. The second
    // future remains so we can wait on it and, in the process, cooperatively
    // drain and reclaim the dropped future's slot too.
    {
        auto dropped = device->try_queue_trans(t1);
        TEST_ASSERT_TRUE(dropped.has_value());
    } // first future dropped here

    auto f2 = device->try_queue_trans(t2);
    TEST_ASSERT_TRUE(f2.has_value());

    TEST_ASSERT_TRUE(f2->try_wait().has_value());

    // Both slots should now be available. Queue (queue_size + 2) more
    // transactions back-to-back with wait() between them to confirm no
    // deadlock and no slot exhaustion.
    for (int i = 0; i < 4; ++i) {
        uint8_t byte = static_cast<uint8_t>(0x30 + i);
        transaction t{};
        t.tx_buffer = std::span<const uint8_t>(&byte, 1);
        t.length = 8;
        auto f = device->try_queue_trans(t);
        TEST_ASSERT_TRUE(f.has_value());
        TEST_ASSERT_TRUE(f->try_wait().has_value());
    }
}

TEST_CASE("master_device destructor releases device", "[idfxx][spi]") {
    auto bus = make_test_bus();
    auto dev_cfg = default_device_config();

    {
        auto device = master_device::make(bus, dev_cfg);
        TEST_ASSERT_TRUE(device.has_value());
    }

    // Should be able to create again after destruction
    auto device2 = master_device::make(bus, dev_cfg);
    TEST_ASSERT_TRUE(device2.has_value());
}
