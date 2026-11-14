// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Unit tests for hashing and authenticating input held in PSRAM, just written by the CPU (so still
// in the cache, with other bytes in the PSRAM behind it).

#include <sdkconfig.h>

#if CONFIG_SPIRAM

#include "crypto_test_utils.hpp"
#include "idfxx/crypto/hash"
#include "idfxx/hex"
#include "test_cleanup.hpp"
#include "unity.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <esp_cache.h>
#include <esp_heap_caps.h>
#include <esp_memory_utils.h>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#ifdef CONFIG_MBEDTLS_THREADING_C
#include "idfxx/crypto/mac"
#endif

using namespace idfxx;
using namespace idfxx_crypto_test;

namespace idfxx::crypto {
class key; // defined only with CONFIG_MBEDTLS_THREADING_C
} // namespace idfxx::crypto

namespace {

// Writes the test message of n bytes, XORed with `flip`. The message is the output of a 32-bit
// linear congruential generator; in Python:
//     x = 1
//     for _ in range(n):
//         x = (x * 1103515245 + 12345) & 0xffffffff
//         out.append((x >> 16) & 0xff)
void write_message(std::byte* p, std::size_t n, uint8_t flip) {
    uint32_t x = 1;
    for (std::size_t i = 0; i < n; ++i) {
        x = x * 1103515245u + 12345u;
        p[i] = std::byte{static_cast<uint8_t>((x >> 16) ^ flip)};
    }
}

// The digests of the test message of each size, from Python's hashlib, and its HMAC-SHA256 tag
// under the key "Jefe", from Python's hmac module: one SHA block, just over one 4096-byte piece,
// and many pieces and blocks.
struct psram_case {
    std::size_t size;
    const char* sha1;
    const char* sha256;
    const char* sha512;
    const char* hmac_sha256;
};

const psram_case psram_cases[] = {
    {64,
     "79ef6b6f311c8fe7492cfa74bdbfa6d5d4910976",
     "a17eb65f63398a17ca56f962a00ebc8306dc2c7fc0bb07c39840fd71b6f8e72e",
     "4cdb362c051f9140d1b9deb831510a9dbad54235d983e75b2fd44dca997917da"
     "1714e8fadcee62d6a51112f674f32581f858b4f8da877bbcecf5115485c32db0",
     "1860b8e7cfb46160825d42f276190c3e6878aabe62d2b905a7a9b0bdc82a2f9b"},
    {4097,
     "bf89c2225e4b2cbcf99840dbf2176641b657d3af",
     "71d1b776aad81c3e3f56dd23641e021d8c5247699ca29d8ec6d9c2ce16c98375",
     "a246a66db16244bba580308b5197b85610a77e5421e17b69934c23a31b3ab695"
     "611d0bafc89bc9604657a895856b2686d0a08de68217d466e1aeb2a667fe0e84",
     "3f6d0463622ff0e334a7432376bf1641e90d4114330d9446c72222d9034bb749"},
    {65539,
     "de80689ebbd4d7e7145cddeec3bfbefbca115cb8",
     "6070dfd095107ba5cb6bd5b27b42040759e1ac7b92acfcaa8f5584f2bf73bbae",
     "08b1302ce0f3b79b9b16e5d16aa39a3ec9871142b34f7915cb58718708b867c7"
     "c37bc6022136bf57ec606a7296b7c04d6c0a3f234fae958ee84084ef7899df58",
     "b8f285d73823260ba6bf92e42e49749ec9edcaa5da3bdee1fda1aac30b8f1ca7"},
};

// Where the message starts in the buffer, whose start is aligned to 64 bytes (a cache line)
constexpr std::size_t psram_offsets[] = {0, 1, 3};
constexpr std::size_t psram_buffer_size = 65539 + 3;

struct psram_algorithm {
    const char* name;
    crypto::hash::algorithm alg; // unused for HMAC
    bool hmac;
    const char* psram_case::* expected;
};

const psram_algorithm psram_algorithms[] = {
    {"SHA-1", crypto::hash::algorithm::sha1, false, &psram_case::sha1},
    {"SHA-256", crypto::hash::algorithm::sha256, false, &psram_case::sha256},
    {"SHA-512", crypto::hash::algorithm::sha512, false, &psram_case::sha512},
#ifdef CONFIG_MBEDTLS_THREADING_C
    {"HMAC-SHA256", crypto::hash::algorithm::sha256, true, &psram_case::hmac_sha256},
#endif
};

std::byte* psram_buffer = nullptr;

void free_psram_buffer() {
    heap_caps_free(psram_buffer);
    psram_buffer = nullptr;
}

// Writes the message to p, after writing the inverted message and writing it back from the cache
// to the PSRAM: the CPU then sees the message, and the PSRAM behind it holds other bytes.
void write_over_stale(std::byte* p, std::size_t n) {
    write_message(p, n, 0xff);
    TEST_ASSERT_EQUAL_INT32(
        ESP_OK, esp_cache_msync(p, n, ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED)
    );
    write_message(p, n, 0);
}

// Data in pieces of varying size, as several updates
std::vector<byte_view> pieces_of(const std::byte* p, std::size_t n) {
    const std::size_t sizes[] = {1, 63, 4095, 4097};
    std::vector<byte_view> pieces;
    for (std::size_t pos = 0, i = 0; pos < n; ++i) {
        std::size_t len = std::min(sizes[i % std::size(sizes)], n - pos);
        pieces.push_back(byte_view{p + pos, len});
        pos += len;
    }
    return pieces;
}

std::string to_hex(byte_span s) {
    return idfxx::to_string(idfxx::hex(std::span<const std::byte>{s.data(), s.size()}));
}

// The digest or tag of the n bytes at p as lowercase hex, computed in one call, or from several
// updates
std::string digest_of(
    const psram_algorithm& a, [[maybe_unused]] const crypto::key* k, const std::byte* p,
    std::size_t n, bool in_pieces
) {
    std::array<std::byte, 64> out{};
#ifdef CONFIG_MBEDTLS_THREADING_C
    if (a.hmac) {
        if (!in_pieces) {
            return to_hex(ok(crypto::mac::operation::try_compute(*k, byte_view{p, n}, out)));
        }
        auto op = ok(crypto::mac::operation::make(*k));
        for (auto piece : pieces_of(p, n)) {
            ok(op.try_update(piece));
        }
        return to_hex(ok(op.try_finish(out)));
    }
#endif
    if (!in_pieces) {
        return to_hex(ok(crypto::hash::operation::try_compute(a.alg, byte_view{p, n}, out)));
    }
    auto op = ok(crypto::hash::operation::make(a.alg));
    for (auto piece : pieces_of(p, n)) {
        ok(op.try_update(piece));
    }
    return to_hex(ok(op.try_finish(out)));
}

} // namespace

// QEMU models no cache, so it can't show whether the cache's contents are what is hashed
TEST_CASE("crypto input in PSRAM", "[idfxx][crypto][hw]") {
    if (heap_caps_get_total_size(MALLOC_CAP_SPIRAM) == 0) {
        TEST_IGNORE_MESSAGE("no PSRAM available");
    }
    idfxx::test::run_after_each_test(&free_psram_buffer);
    psram_buffer = static_cast<std::byte*>(
        heap_caps_aligned_alloc(64, psram_buffer_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
    );
    TEST_ASSERT_NOT_NULL(psram_buffer);
    TEST_ASSERT_TRUE(esp_ptr_external_ram(psram_buffer));

#ifdef CONFIG_MBEDTLS_THREADING_C
    auto key = ok(crypto::mac::hmac_sha256::try_import_key(bytes_of(std::string_view{"Jefe"})));
    const crypto::key* k = &key;
#else
    const crypto::key* k = nullptr;
#endif

    int mismatches = 0;
    auto check = [&](const psram_algorithm& a, std::size_t size, std::size_t offset,
                     const char* how, const char* expected, const std::string& actual) {
        if (actual != expected) {
            printf(
                "%s of %u bytes at +%u, %s: expected %s, got %s\n", a.name,
                static_cast<unsigned>(size), static_cast<unsigned>(offset), how, expected,
                actual.c_str()
            );
            ++mismatches;
        }
    };

    for (const auto& c : psram_cases) {
        for (std::size_t offset : psram_offsets) {
            std::byte* p = psram_buffer + offset;
            for (const auto& a : psram_algorithms) {
                const char* expected = c.*a.expected;
                write_over_stale(p, c.size);
                check(a, c.size, offset, "from PSRAM in one call", expected,
                      digest_of(a, k, p, c.size, false));
                write_over_stale(p, c.size);
                check(a, c.size, offset, "from PSRAM in pieces", expected,
                      digest_of(a, k, p, c.size, true));
            }

            // The same bytes, copied to internal RAM
            write_over_stale(p, c.size);
            std::vector<std::byte> copy(p, p + c.size);
            TEST_ASSERT_TRUE(esp_ptr_internal(copy.data()));
            for (const auto& a : psram_algorithms) {
                check(a, c.size, offset, "copied to internal RAM", c.*a.expected,
                      digest_of(a, k, copy.data(), c.size, false));
            }
        }
    }
    TEST_ASSERT_EQUAL_INT32(0, mismatches);
}

#endif // CONFIG_SPIRAM
