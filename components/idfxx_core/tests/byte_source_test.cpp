// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Unit tests for idfxx byte sources
// Uses ESP-IDF Unity test framework with compile-time static_asserts

#include "idfxx/byte_source.hpp"
#include "unity.h"

#include <array>
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <span>
#include <system_error>
#include <type_traits>
#include <vector>

using namespace idfxx;

// =============================================================================
// Compile-time tests (static_assert)
// These verify correctness at compile time - if this file compiles, they pass.
// =============================================================================

namespace {

struct wrong_return_source {
    result<int> try_read(std::span<uint8_t>);
};

struct wrong_buffer_source {
    result<size_t> try_read(std::span<std::byte>);
};

struct throwing_only_source {
    size_t read(std::span<uint8_t>);
};

} // namespace

// The provided sources model byte_source
static_assert(byte_source<memory_source>);
static_assert(byte_source<file_source>);

// Types without a matching try_read do not
static_assert(!byte_source<int>);
static_assert(!byte_source<wrong_return_source>);
static_assert(!byte_source<wrong_buffer_source>);
static_assert(!byte_source<throwing_only_source>);

// Reading requires a mutable source
static_assert(!byte_source<const memory_source>);

// Sources are explicitly constructed and copyable
static_assert(!std::is_default_constructible_v<memory_source>);
static_assert(!std::is_convertible_v<std::span<const uint8_t>, memory_source>);
static_assert(std::is_copy_constructible_v<memory_source>);
static_assert(std::is_copy_assignable_v<memory_source>);
static_assert(std::is_nothrow_constructible_v<memory_source, std::span<const uint8_t>>);

static_assert(!std::is_default_constructible_v<file_source>);
static_assert(!std::is_convertible_v<FILE*, file_source>);
static_assert(std::is_copy_constructible_v<file_source>);
static_assert(std::is_copy_assignable_v<file_source>);
static_assert(std::is_nothrow_constructible_v<file_source, FILE*>);

// =============================================================================
// Helpers
// =============================================================================

namespace {

constexpr std::array<uint8_t, 10> test_data{0, 1, 2, 3, 4, 5, 6, 7, 8, 9};

// Drains any byte source in chunks of chunk_size, returning everything read.
std::vector<uint8_t> drain(byte_source auto& src, size_t chunk_size) {
    std::vector<uint8_t> out;
    std::vector<uint8_t> buf(chunk_size);
    while (true) {
        auto n = src.try_read(buf);
        TEST_ASSERT_TRUE(n.has_value());
        TEST_ASSERT_TRUE(*n <= chunk_size);
        if (*n == 0) {
            break;
        }
        out.insert(out.end(), buf.begin(), buf.begin() + *n);
    }
    return out;
}

FILE* open_test_file() {
    // fmemopen does not modify the buffer in read mode, but takes a non-const pointer
    static std::array<uint8_t, test_data.size()> file_data;
    file_data = test_data;
    FILE* f = fmemopen(file_data.data(), file_data.size(), "rb");
    TEST_ASSERT_NOT_NULL(f);
    return f;
}

// Bytes a failing stream delivers before its reads start failing with EIO.
struct failing_stream {
    std::span<const uint8_t> good;
};

// funopen's read callback signature differs between newlib and picolibc, so
// derive it from funopen itself and build a matching callback.
template<typename Ret, typename Cookie, typename ReadFn, typename... Rest>
ReadFn funopen_read_fn(Ret (*)(Cookie, ReadFn, Rest...));

template<typename Fn>
struct failing_read;

template<typename R, typename Cookie, typename Buf, typename Size>
struct failing_read<R (*)(Cookie, Buf, Size)> {
    static R read(Cookie cookie, Buf buf, Size size) {
        auto* s = static_cast<failing_stream*>(cookie);
        if (s->good.empty()) {
            errno = EIO;
            return -1;
        }
        const size_t n = std::min<size_t>(s->good.size(), size);
        std::memcpy(buf, s->good.data(), n);
        s->good = s->good.subspan(n);
        return static_cast<R>(n);
    }
};

// Opens a stream that delivers s.good, then fails every read with EIO.
FILE* open_failing_file(failing_stream& s) {
    using read_fn = decltype(funopen_read_fn(&funopen));
    FILE* f = funopen(&s, &failing_read<read_fn>::read, nullptr, nullptr, nullptr);
    TEST_ASSERT_NOT_NULL(f);
    return f;
}

} // namespace

// =============================================================================
// Runtime tests (Unity TEST_CASE)
// =============================================================================

TEST_CASE("memory_source reads all data into a large buffer", "[idfxx][byte_source]") {
    memory_source src{test_data};
    TEST_ASSERT_EQUAL(test_data.size(), src.remaining());

    std::array<uint8_t, 16> buf{};
    auto n = src.try_read(buf);
    TEST_ASSERT_TRUE(n.has_value());
    TEST_ASSERT_EQUAL(test_data.size(), *n);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(test_data.data(), buf.data(), test_data.size());
    TEST_ASSERT_EQUAL(0, src.remaining());
}

TEST_CASE("memory_source reads in short chunks", "[idfxx][byte_source]") {
    memory_source src{test_data};

    std::array<uint8_t, 4> buf{};
    auto n = src.try_read(buf);
    TEST_ASSERT_TRUE(n.has_value());
    TEST_ASSERT_EQUAL(4, *n);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(&test_data[0], buf.data(), 4);
    TEST_ASSERT_EQUAL(6, src.remaining());

    n = src.try_read(buf);
    TEST_ASSERT_TRUE(n.has_value());
    TEST_ASSERT_EQUAL(4, *n);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(&test_data[4], buf.data(), 4);
    TEST_ASSERT_EQUAL(2, src.remaining());

    // Final chunk is short
    n = src.try_read(buf);
    TEST_ASSERT_TRUE(n.has_value());
    TEST_ASSERT_EQUAL(2, *n);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(&test_data[8], buf.data(), 2);
    TEST_ASSERT_EQUAL(0, src.remaining());
}

TEST_CASE("memory_source returns 0 at end of stream, repeatedly", "[idfxx][byte_source]") {
    memory_source src{test_data};
    std::array<uint8_t, 16> buf{};
    TEST_ASSERT_TRUE(src.try_read(buf).has_value());

    for (int i = 0; i < 3; ++i) {
        auto n = src.try_read(buf);
        TEST_ASSERT_TRUE(n.has_value());
        TEST_ASSERT_EQUAL(0, *n);
    }
    TEST_ASSERT_EQUAL(0, src.remaining());
}

TEST_CASE("memory_source empty buffer reads nothing and does not advance", "[idfxx][byte_source]") {
    memory_source src{test_data};

    auto n = src.try_read(std::span<uint8_t>{});
    TEST_ASSERT_TRUE(n.has_value());
    TEST_ASSERT_EQUAL(0, *n);
    TEST_ASSERT_EQUAL(test_data.size(), src.remaining());
}

TEST_CASE("memory_source over empty data is immediately at end of stream", "[idfxx][byte_source]") {
    memory_source src{std::span<const uint8_t>{}};
    TEST_ASSERT_EQUAL(0, src.remaining());

    std::array<uint8_t, 4> buf{};
    auto n = src.try_read(buf);
    TEST_ASSERT_TRUE(n.has_value());
    TEST_ASSERT_EQUAL(0, *n);
}

TEST_CASE("memory_source copies read independently", "[idfxx][byte_source]") {
    memory_source a{test_data};
    std::array<uint8_t, 3> buf{};
    TEST_ASSERT_TRUE(a.try_read(buf).has_value());

    memory_source b = a;
    TEST_ASSERT_TRUE(b.try_read(buf).has_value());
    TEST_ASSERT_EQUAL(7, a.remaining());
    TEST_ASSERT_EQUAL(4, b.remaining());

    // a resumes from where it was copied, not from where b got to
    auto n = a.try_read(buf);
    TEST_ASSERT_TRUE(n.has_value());
    TEST_ASSERT_EQUAL(3, *n);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(&test_data[3], buf.data(), 3);
}

TEST_CASE("memory_source drains through the byte_source concept", "[idfxx][byte_source]") {
    for (size_t chunk : {1u, 3u, 10u, 64u}) {
        memory_source src{test_data};
        auto out = drain(src, chunk);
        TEST_ASSERT_EQUAL(test_data.size(), out.size());
        TEST_ASSERT_EQUAL_UINT8_ARRAY(test_data.data(), out.data(), test_data.size());
    }
}

TEST_CASE("file_source reads all data into a large buffer", "[idfxx][byte_source]") {
    FILE* f = open_test_file();
    file_source src{f};

    std::array<uint8_t, 16> buf{};
    auto n = src.try_read(buf);
    TEST_ASSERT_TRUE(n.has_value());
    TEST_ASSERT_EQUAL(test_data.size(), *n);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(test_data.data(), buf.data(), test_data.size());

    n = src.try_read(buf);
    TEST_ASSERT_TRUE(n.has_value());
    TEST_ASSERT_EQUAL(0, *n);

    std::fclose(f);
}

TEST_CASE("file_source reads in short chunks then returns 0 at end of file", "[idfxx][byte_source]") {
    FILE* f = open_test_file();
    file_source src{f};

    std::array<uint8_t, 4> buf{};
    auto n = src.try_read(buf);
    TEST_ASSERT_TRUE(n.has_value());
    TEST_ASSERT_EQUAL(4, *n);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(&test_data[0], buf.data(), 4);

    n = src.try_read(buf);
    TEST_ASSERT_TRUE(n.has_value());
    TEST_ASSERT_EQUAL(4, *n);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(&test_data[4], buf.data(), 4);

    n = src.try_read(buf);
    TEST_ASSERT_TRUE(n.has_value());
    TEST_ASSERT_EQUAL(2, *n);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(&test_data[8], buf.data(), 2);

    for (int i = 0; i < 2; ++i) {
        n = src.try_read(buf);
        TEST_ASSERT_TRUE(n.has_value());
        TEST_ASSERT_EQUAL(0, *n);
    }

    std::fclose(f);
}

TEST_CASE("file_source empty buffer reads nothing and does not advance", "[idfxx][byte_source]") {
    FILE* f = open_test_file();
    file_source src{f};

    auto n = src.try_read(std::span<uint8_t>{});
    TEST_ASSERT_TRUE(n.has_value());
    TEST_ASSERT_EQUAL(0, *n);
    TEST_ASSERT_EQUAL(0, std::ftell(f));

    std::fclose(f);
}

TEST_CASE("file_source starts at the stream's current position", "[idfxx][byte_source]") {
    FILE* f = open_test_file();
    TEST_ASSERT_EQUAL(0, std::fseek(f, 6, SEEK_SET));
    file_source src{f};

    auto out = drain(src, 3);
    TEST_ASSERT_EQUAL(4, out.size());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(&test_data[6], out.data(), 4);

    std::fclose(f);
}

TEST_CASE("file_source drains through the byte_source concept", "[idfxx][byte_source]") {
    for (size_t chunk : {1u, 3u, 10u, 64u}) {
        FILE* f = open_test_file();
        file_source src{f};
        auto out = drain(src, chunk);
        TEST_ASSERT_EQUAL(test_data.size(), out.size());
        TEST_ASSERT_EQUAL_UINT8_ARRAY(test_data.data(), out.data(), test_data.size());
        std::fclose(f);
    }
}

TEST_CASE("file_source reports a stream read error", "[idfxx][byte_source]") {
    failing_stream s{};
    FILE* f = open_failing_file(s);
    file_source src{f};

    std::array<uint8_t, 4> buf{};
    auto n = src.try_read(buf);
    TEST_ASSERT_FALSE(n.has_value());
    TEST_ASSERT_TRUE(std::ferror(f) != 0);
    TEST_ASSERT_TRUE(n.error() == std::error_code(EIO, std::generic_category()));

    std::fclose(f);
}

TEST_CASE("file_source returns bytes read before an error, then the error", "[idfxx][byte_source]") {
    failing_stream s{std::span{test_data}.first(5)};
    FILE* f = open_failing_file(s);
    file_source src{f};

    // The error cuts the read short; the bytes before it are returned
    std::array<uint8_t, 8> buf{};
    auto n = src.try_read(buf);
    TEST_ASSERT_TRUE(n.has_value());
    TEST_ASSERT_EQUAL(5, *n);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(test_data.data(), buf.data(), 5);
    TEST_ASSERT_TRUE(std::ferror(f) != 0);

    // The next read reports the error
    n = src.try_read(buf);
    TEST_ASSERT_FALSE(n.has_value());
    TEST_ASSERT_TRUE(n.error() == std::error_code(EIO, std::generic_category()));

    std::fclose(f);
}

// =============================================================================
// Exception-based API tests
// =============================================================================

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS

TEST_CASE("memory_source read returns the byte count", "[idfxx][byte_source]") {
    memory_source src{test_data};
    std::array<uint8_t, 8> buf{};

    TEST_ASSERT_EQUAL(8, src.read(buf));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(&test_data[0], buf.data(), 8);
    TEST_ASSERT_EQUAL(2, src.read(buf));
    TEST_ASSERT_EQUAL(0, src.read(buf));
}

TEST_CASE("file_source read returns the byte count", "[idfxx][byte_source]") {
    FILE* f = open_test_file();
    file_source src{f};
    std::array<uint8_t, 8> buf{};

    TEST_ASSERT_EQUAL(8, src.read(buf));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(&test_data[0], buf.data(), 8);
    TEST_ASSERT_EQUAL(2, src.read(buf));
    TEST_ASSERT_EQUAL(0, src.read(buf));

    std::fclose(f);
}

TEST_CASE("file_source read throws on a stream read error", "[idfxx][byte_source]") {
    failing_stream s{};
    FILE* f = open_failing_file(s);
    file_source src{f};

    std::array<uint8_t, 4> buf{};
    bool threw = false;
    try {
        (void)src.read(buf);
    } catch (const std::system_error& e) {
        threw = true;
        TEST_ASSERT_TRUE(e.code() == std::error_code(EIO, std::generic_category()));
    }
    TEST_ASSERT_TRUE(threw);

    std::fclose(f);
}

#endif // CONFIG_COMPILER_CXX_EXCEPTIONS
