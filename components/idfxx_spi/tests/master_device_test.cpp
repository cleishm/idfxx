// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#include "idfxx/future"
#include "idfxx/spi/master"
#include "unity.h"

#include <array>
#include <concepts>
#include <cstddef>
#include <driver/spi_master.h>
#include <initializer_list>
#include <mutex>
#include <optional>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

using namespace idfxx;
using namespace idfxx::spi;
using namespace quanta::frequency_literals;

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
static_assert(std::is_same_v<decltype(transaction::length), std::optional<size_t>>);
static_assert(std::is_same_v<decltype(transaction::rx_length), std::optional<size_t>>);

// Byte buffers of either element type, and literal byte lists, are accepted
static_assert(requires(master_device& d, std::array<std::byte, 2> b, std::array<uint8_t, 2> u) {
    { d.try_transmit(b) } -> std::same_as<result<void>>;
    { d.try_transmit(u) } -> std::same_as<result<void>>;
    { d.try_transmit({0x01, 0x02}) } -> std::same_as<result<void>>;
    { d.try_polling_transmit({0x01}) } -> std::same_as<result<void>>;
    { d.try_receive(b) } -> std::same_as<result<void>>;
    { d.try_polling_receive(u) } -> std::same_as<result<void>>;
    { d.try_transfer(u, b) } -> std::same_as<result<void>>;
    { d.try_polling_transfer(b, u) } -> std::same_as<result<void>>;
    // Literal tx lists, including ones that could read as {pointer, length} or an empty view,
    // resolve to the initializer_list forms without ambiguity
    { d.try_transfer({0x9F, 0, 0, 0}, u) } -> std::same_as<result<void>>;
    { d.try_transfer({0, 0}, u) } -> std::same_as<result<void>>;
    { d.try_transfer({}, b) } -> std::same_as<result<void>>;
    { d.try_polling_transfer({0x05, 0x00}, b) } -> std::same_as<result<void>>;
});
#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
static_assert(requires(master_device& d, std::array<uint8_t, 2> u) {
    d.transfer({0x9F, 0x00}, u);
    d.polling_transfer({0x05, 0x00}, u);
});
#endif
// Fixed-size receives need at least one byte
template<typename D, size_t N>
concept receivable_n = requires(D& d) { d.template try_receive<N>(); };
template<typename D, size_t N>
concept polling_receivable_n = requires(D& d) { d.template try_polling_receive<N>(); };
static_assert(receivable_n<master_device, 1>);
static_assert(!receivable_n<master_device, 0>);
static_assert(polling_receivable_n<master_device, 1>);
static_assert(!polling_receivable_n<master_device, 0>);
static_assert(std::is_same_v<decltype(std::declval<master_device&>().try_receive<4>()),
                             result<std::array<uint8_t, 4>>>);
static_assert(std::is_same_v<decltype(std::declval<master_device&>().try_polling_receive<3>()),
                             result<std::array<uint8_t, 3>>>);
#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
static_assert(std::is_same_v<decltype(std::declval<master_device&>().receive<4>()),
                             std::array<uint8_t, 4>>);
static_assert(std::is_same_v<decltype(std::declval<master_device&>().polling_receive<3>()),
                             std::array<uint8_t, 3>>);
#endif
// Receive buffers must be writable
template<typename B>
concept receivable_into = requires(master_device& d, B& b) { d.try_receive(b); };
static_assert(receivable_into<std::array<uint8_t, 2>>);
static_assert(!receivable_into<const std::array<uint8_t, 2>>);

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
static_assert(std::to_underlying(trans_flags::multiline_addr) == SPI_TRANS_MULTILINE_ADDR);
static_assert(std::to_underlying(trans_flags::multiline_cmd) == SPI_TRANS_MULTILINE_CMD);
static_assert(std::to_underlying(trans_flags::variable_cmd) == SPI_TRANS_VARIABLE_CMD);
static_assert(std::to_underlying(trans_flags::variable_addr) == SPI_TRANS_VARIABLE_ADDR);
static_assert(std::to_underlying(trans_flags::variable_dummy) == SPI_TRANS_VARIABLE_DUMMY);
static_assert(std::to_underlying(trans_flags::cs_keep_active) == SPI_TRANS_CS_KEEP_ACTIVE);
static_assert(std::to_underlying(trans_flags::dma_buffer_align_manual) == SPI_TRANS_DMA_BUFFER_ALIGN_MANUAL);
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
static_assert(std::to_underlying(trans_flags::dma_use_psram) == SPI_TRANS_DMA_USE_PSRAM);
#endif
// SPI_TRANS_USE_RXDATA / SPI_TRANS_USE_TXDATA are deliberately not exposed: they select the
// driver's inline tx_data/rx_data storage, which a transaction cannot provide or return
template<typename E>
concept has_use_rxdata = requires { E::use_rxdata; };
template<typename E>
concept has_use_txdata = requires { E::use_txdata; };
static_assert(!has_use_rxdata<trans_flags>);
static_assert(!has_use_txdata<trans_flags>);

// =============================================================================
// Runtime tests (Unity TEST_CASE)
// =============================================================================

TEST_CASE("master_device::config default initialization", "[idfxx][spi]") {
    master_device::config cfg{};
    TEST_ASSERT_EQUAL(0, cfg.command_bits);
    TEST_ASSERT_EQUAL(0, cfg.address_bits);
    TEST_ASSERT_EQUAL(0, cfg.dummy_bits);
    TEST_ASSERT_EQUAL(0, cfg.mode);
    TEST_ASSERT_FALSE(cfg.duty_cycle_pos.has_value());
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
    TEST_ASSERT_FALSE(trans.length.has_value());
    TEST_ASSERT_FALSE(trans.rx_length.has_value());
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

TEST_CASE("master_device::make with zero duty cycle fails", "[idfxx][spi]") {
    auto bus = make_test_bus();
    auto dev_cfg = default_device_config();
    dev_cfg.duty_cycle_pos = 0;

    auto device = master_device::make(bus, dev_cfg);
    TEST_ASSERT_FALSE(device.has_value());
    TEST_ASSERT_TRUE(device.error() == idfxx::errc::invalid_arg);
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

TEST_CASE("master_device basic transmit", "[idfxx][spi][hw]") {
    auto bus = make_test_bus();
    auto device = master_device::make(bus, default_device_config());
    TEST_ASSERT_TRUE(device.has_value());

    uint8_t data[] = {0x01, 0x02, 0x03, 0x04};
    auto res = device->try_transmit(data);
    TEST_ASSERT_TRUE(res.has_value());
}

TEST_CASE("master_device transmit accepts literal bytes", "[idfxx][spi][hw]") {
    auto bus = make_test_bus();
    auto device = master_device::make(bus, default_device_config());
    TEST_ASSERT_TRUE(device.has_value());

    TEST_ASSERT_TRUE(device->try_transmit({0x9F}).has_value());
    TEST_ASSERT_TRUE(device->try_transmit({0x02, 0x00, 0x10, 0xAA}).has_value());
    TEST_ASSERT_TRUE(device->try_polling_transmit({0x06}).has_value());
}

TEST_CASE("master_device receive and transfer with byte buffers", "[idfxx][spi][hw]") {
    auto bus = make_test_bus();
    auto device = master_device::make(bus, default_device_config());
    TEST_ASSERT_TRUE(device.has_value());

    std::array<std::byte, 4> rx{};
    TEST_ASSERT_TRUE(device->try_receive(rx).has_value());
    TEST_ASSERT_TRUE(device->try_polling_receive(rx).has_value());

    std::array<uint8_t, 4> tx = {0x01, 0x02, 0x03, 0x04};
    TEST_ASSERT_TRUE(device->try_transfer(tx, rx).has_value());
    TEST_ASSERT_TRUE(device->try_polling_transfer(tx, rx).has_value());
}

TEST_CASE("master_device receive fixed-size array", "[idfxx][spi][hw]") {
    auto bus = make_test_bus();
    auto device = master_device::make(bus, default_device_config());
    TEST_ASSERT_TRUE(device.has_value());

    auto r = device->try_receive<3>();
    TEST_ASSERT_TRUE(r.has_value());
    TEST_ASSERT_EQUAL(3, r->size());

    auto pr = device->try_polling_receive<2>();
    TEST_ASSERT_TRUE(pr.has_value());
    TEST_ASSERT_EQUAL(2, pr->size());

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    auto data = device->receive<4>();
    TEST_ASSERT_EQUAL(4, data.size());
    auto polled = device->polling_receive<4>();
    TEST_ASSERT_EQUAL(4, polled.size());
#endif
}

TEST_CASE("master_device rejects rx_length exceeding buffer", "[idfxx][spi][hw]") {
    auto bus = make_test_bus();
    auto device = master_device::make(bus, default_device_config());
    TEST_ASSERT_TRUE(device.has_value());

    std::array<std::byte, 1> rx{};
    transaction trans{};
    trans.rx_buffer = rx;
    trans.length = 16; // 2 bytes worth — exceeds 8-bit rx_buffer capacity

    auto res = device->try_transmit(trans);
    TEST_ASSERT_FALSE(res.has_value());
    TEST_ASSERT_TRUE(res.error() == idfxx::errc::invalid_arg);
}

TEST_CASE("master_device rejects transaction length exceeding buffer", "[idfxx][spi][hw]") {
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

TEST_CASE("master_device rejects explicit rx_length exceeding buffer", "[idfxx][spi]") {
    auto bus = make_test_bus();
    auto device = master_device::make(bus, default_device_config());
    TEST_ASSERT_TRUE(device.has_value());

    uint8_t tx[2] = {0x00, 0x00};
    std::array<std::byte, 1> rx{};
    transaction trans{};
    trans.tx_buffer = tx;
    trans.rx_buffer = rx;
    trans.rx_length = 16; // exceeds 8-bit rx_buffer capacity

    auto res = device->try_transmit(trans);
    TEST_ASSERT_FALSE(res.has_value());
    TEST_ASSERT_TRUE(res.error() == idfxx::errc::invalid_arg);
}

TEST_CASE("master_device derives transaction length from tx_buffer", "[idfxx][spi][hw]") {
    auto bus = make_test_bus();
    auto device = master_device::make(bus, default_device_config());
    TEST_ASSERT_TRUE(device.has_value());

    uint8_t tx[4] = {0x01, 0x02, 0x03, 0x04};
    std::array<std::byte, 4> rx{};
    transaction trans{};
    trans.tx_buffer = tx;
    TEST_ASSERT_TRUE(device->try_transmit(trans).has_value());
    TEST_ASSERT_TRUE(device->try_polling_transmit(trans).has_value());

    trans.rx_buffer = rx;
    TEST_ASSERT_TRUE(device->try_transmit(trans).has_value());

    // An explicit length shorter than the buffers still applies
    trans.rx_buffer = {rx.data(), 2};
    trans.length = 16;
    TEST_ASSERT_TRUE(device->try_transmit(trans).has_value());
}

TEST_CASE("master_device rejects explicit zero rx_length in full duplex", "[idfxx][spi]") {
    auto bus = make_test_bus();
    auto device = master_device::make(bus, default_device_config());
    TEST_ASSERT_TRUE(device.has_value());

    uint8_t tx[2] = {0x01, 0x02};
    std::array<std::byte, 2> rx{};
    transaction trans{};
    trans.tx_buffer = tx;
    trans.rx_buffer = rx;
    trans.rx_length = 0; // the driver would read this as "same as length"

    auto res = device->try_transmit(trans);
    TEST_ASSERT_FALSE(res.has_value());
    TEST_ASSERT_TRUE(res.error() == idfxx::errc::invalid_arg);

    auto polled = device->try_polling_transmit(trans);
    TEST_ASSERT_FALSE(polled.has_value());
    TEST_ASSERT_TRUE(polled.error() == idfxx::errc::invalid_arg);

    auto queued = device->try_queue_trans(trans);
    TEST_ASSERT_FALSE(queued.has_value());
    TEST_ASSERT_TRUE(queued.error() == idfxx::errc::invalid_arg);
}

TEST_CASE("master_device derived length is checked against rx_buffer", "[idfxx][spi]") {
    auto bus = make_test_bus();
    auto device = master_device::make(bus, default_device_config());
    TEST_ASSERT_TRUE(device.has_value());

    // The derived 32-bit length (and default rx_length) exceeds a 2-byte rx_buffer
    uint8_t tx[4] = {0x01, 0x02, 0x03, 0x04};
    std::array<std::byte, 2> rx{};
    transaction trans{};
    trans.tx_buffer = tx;
    trans.rx_buffer = rx;

    auto res = device->try_transmit(trans);
    TEST_ASSERT_FALSE(res.has_value());
    TEST_ASSERT_TRUE(res.error() == idfxx::errc::invalid_arg);

    auto queued = device->try_queue_trans(trans);
    TEST_ASSERT_FALSE(queued.has_value());
    TEST_ASSERT_TRUE(queued.error() == idfxx::errc::invalid_arg);
}

TEST_CASE("master_device derives receive-only length from rx_buffer", "[idfxx][spi][hw]") {
    auto bus = make_test_bus();
    auto device = master_device::make(bus, default_device_config());
    TEST_ASSERT_TRUE(device.has_value());

    std::array<uint8_t, 3> rx{};
    transaction trans{};
    trans.rx_buffer = rx;
    TEST_ASSERT_TRUE(device->try_transmit(trans).has_value());
    TEST_ASSERT_TRUE(device->try_polling_transmit(trans).has_value());

    auto f = device->try_queue_trans(trans);
    TEST_ASSERT_TRUE(f.has_value());
    TEST_ASSERT_TRUE(f->try_wait().has_value());
}

TEST_CASE("master_device half-duplex receive derives its phase lengths", "[idfxx][spi][hw]") {
    auto bus = make_test_bus();
    auto dev_cfg = default_device_config();
    dev_cfg.flags = device_flags::halfduplex;
    auto device = master_device::make(bus, dev_cfg);
    TEST_ASSERT_TRUE(device.has_value());

    // Receive-only with just an rx_buffer: the derived length is 0 (the driver rejects a MOSI
    // phase alongside the MISO phase on chips that can't combine them) and the derived rx_length
    // covers the whole buffer
    std::array<uint8_t, 3> rx{};
    transaction trans{};
    trans.rx_buffer = rx;
    TEST_ASSERT_TRUE(device->try_transmit(trans).has_value());
    TEST_ASSERT_TRUE(device->try_polling_transmit(trans).has_value());
    auto f = device->try_queue_trans(trans);
    TEST_ASSERT_TRUE(f.has_value());
    TEST_ASSERT_TRUE(f->try_wait().has_value());

    // An explicit rx_length still applies
    trans.rx_length = 16;
    TEST_ASSERT_TRUE(device->try_transmit(trans).has_value());

    // An explicit 0 means no MISO phase
    uint8_t tx[2] = {0x01, 0x02};
    transaction write{};
    write.tx_buffer = tx;
    write.rx_length = 0;
    TEST_ASSERT_TRUE(device->try_transmit(write).has_value());

    TEST_ASSERT_TRUE(device->try_receive(rx).has_value());
    TEST_ASSERT_TRUE(device->try_polling_receive(rx).has_value());
}

TEST_CASE("master_device queue_trans/wait per-transaction", "[idfxx][spi][hw]") {
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

TEST_CASE("master_device transaction vector storage and queueing", "[idfxx][spi][hw]") {
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
        t.tx_buffer = data[i];
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

TEST_CASE("master_device future copy shares completion", "[idfxx][spi][hw]") {
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

TEST_CASE("master_device dropped future slot is eventually reclaimed", "[idfxx][spi][hw]") {
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
        t.tx_buffer = {&byte, 1};
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

TEST_CASE("master_device try_lock succeeds when uncontended", "[idfxx][spi]") {
    // Regression: try_lock previously passed a finite timeout to
    // spi_device_acquire_bus, which ESP-IDF rejects — it always returned false.
    auto bus = make_test_bus();
    auto device = master_device::make(bus, default_device_config());
    TEST_ASSERT_TRUE(device.has_value());

    TEST_ASSERT_TRUE(device->try_lock());
    device->unlock();

    // Re-acquirable after release.
    device->lock();
    device->unlock();
}

TEST_CASE("master_device lock is recursive", "[idfxx][spi]") {
    auto bus = make_test_bus();
    auto device = master_device::make(bus, default_device_config());
    TEST_ASSERT_TRUE(device.has_value());

    device->lock();
    device->lock();
    TEST_ASSERT_TRUE(device->try_lock());
    device->unlock();
    device->unlock();
    device->unlock();

    // Fully released: acquirable again from scratch.
    TEST_ASSERT_TRUE(device->try_lock());
    device->unlock();
}

TEST_CASE("master_device lock excludes other threads", "[idfxx][spi]") {
    auto bus = make_test_bus();
    auto device = master_device::make(bus, default_device_config());
    TEST_ASSERT_TRUE(device.has_value());

    device->lock();
    bool acquired = true;
    std::thread contender([&]() {
        acquired = device->try_lock();
        if (acquired) {
            device->unlock();
        }
    });
    contender.join();
    TEST_ASSERT_FALSE(acquired);
    device->unlock();

    std::thread successor([&]() {
        acquired = device->try_lock();
        if (acquired) {
            device->unlock();
        }
    });
    successor.join();
    TEST_ASSERT_TRUE(acquired);
}

TEST_CASE("master_device polling transactions while holding lock", "[idfxx][spi][hw]") {
    auto bus = make_test_bus();
    auto device = master_device::make(bus, default_device_config());
    TEST_ASSERT_TRUE(device.has_value());

    std::lock_guard lk(*device);
    uint8_t tx[] = {0x9F};
    uint8_t rx[3] = {};
    TEST_ASSERT_TRUE(device->try_polling_transmit(tx).has_value());
    TEST_ASSERT_TRUE(device->try_polling_receive(rx).has_value());
}
