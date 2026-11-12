# idfxx_crypto

Type-safe cryptographic primitives, starting with message digests (hashing).

📚 **[Full API Documentation](https://cleishm.github.io/idfxx/group__idfxx__crypto.html)**

## Features

- Message digests with SHA-2 (SHA-224, SHA-256, SHA-384, SHA-512), SHA-3 (SHA3-224, SHA3-256,
  SHA3-384, SHA3-512), SHA-1 and MD5
- One type per algorithm (`idfxx::crypto::hash::sha256`, `idfxx::crypto::hash::sha3_256`, ...), with
  the digest size in the type: digests are `std::array<std::byte, N>`
- One-shot hashing of a complete message with `compute()`
- Incremental hashing of a message supplied in pieces with `update()` and `finish()`, and `clone()`
  to branch off a partially hashed message
- Constant-time digest checks with `compare()` and `verify()`
- `idfxx::crypto::hash::operation` for an algorithm chosen at runtime (for example named in a
  manifest), with the same operations writing digests into a caller-supplied buffer
- `hash::digest_size()` and `hash::block_size()` for an algorithm's sizes, usable in constant
  expressions
- Messages and digests given as an `idfxx::byte_view` (any `std::byte` or `uint8_t` range, or
  `{pointer, length}`), and messages also as `std::string_view`
- Hardware SHA acceleration where the chip has it, behind the same API
- `crypto::errc` error codes, comparable with `std::errc` where a POSIX equivalent exists

## Requirements

- ESP-IDF 5.5 or later
- C++23 compiler
- idfxx_core (installed automatically)

## Installation

### ESP-IDF Component Manager

Add to your project's `idf_component.yml`:

```yaml
dependencies:
  cleishm/idfxx_crypto:
    version: "^1.0.0"
```

Or add `idfxx_crypto` to the `REQUIRES` list in your component's `CMakeLists.txt`.

## Usage

### Basic Example

If `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled:

```cpp
#include <idfxx/crypto/hash>
#include <idfxx/log>

#include <array>
#include <cstdint>

try {
    // One-shot: the digest of a complete message
    auto digest = idfxx::crypto::hash::sha256::compute("hello, world");

    // Incremental: a message supplied in pieces
    std::array<uint8_t, 4> header{0x01, 0x02, 0x03, 0x04};
    std::array<uint8_t, 3> body{0x05, 0x06, 0x07};
    idfxx::crypto::hash::sha256 hasher;
    hasher.update(header);
    hasher.update(body);
    auto message_digest = hasher.finish(); // the hasher is ready for a new message

    // Check a message against an expected digest, in constant time
    if (!idfxx::crypto::hash::sha256::compare("hello, world", digest)) {
        idfxx::log::warn("HASH", "Message was altered");
    }

    // The same check for a message supplied in pieces
    hasher.update(header);
    hasher.update(body);
    if (!hasher.verify(message_digest)) {
        idfxx::log::warn("HASH", "Message was altered");
    }
} catch (const std::system_error& e) {
    idfxx::log::error("HASH", "Hashing failed: {}", e.what());
}
```

### Result-based API

If `CONFIG_COMPILER_CXX_EXCEPTIONS` is *not* enabled, the result-based API must be used:

```cpp
#include <idfxx/crypto/hash>
#include <idfxx/log>

auto digest = idfxx::crypto::hash::sha256::try_compute("hello, world");
if (!digest) {
    idfxx::log::error("HASH", "Hashing failed: {}", digest.error().message());
    return;
}

idfxx::crypto::hash::sha256 hasher;
for (std::string_view piece : {"hello", ", ", "world"}) {
    if (auto r = hasher.try_update(piece); !r) {
        idfxx::log::error("HASH", "Hashing failed: {}", r.error().message());
        return;
    }
}
auto match = hasher.try_verify(*digest);
if (!match) {
    idfxx::log::error("HASH", "Hashing failed: {}", match.error().message());
} else if (!*match) {
    idfxx::log::warn("HASH", "Message was altered");
}
```

### Algorithm chosen at runtime

When the algorithm is only known at runtime, use `hash::operation`. Its digests go into a buffer
you supply; `finish()` and `compute()` return the filled part of it:

```cpp
#include <idfxx/crypto/hash>

#include <array>
#include <cstddef>

// alg came from, for example, a manifest naming the digest algorithm
idfxx::crypto::hash::operation op{alg}; // throws if the algorithm isn't enabled
op.update(header);
op.update(body);
std::array<std::byte, 64> buf; // large enough for any digest
auto digest = op.finish(buf);  // the first op.digest_size() bytes of buf
```

`hash::operation::make(alg)` is the result-based way to create one.

## API Overview

The component provides two API styles:
- **Exception-based API** (requires `CONFIG_COMPILER_CXX_EXCEPTIONS`) - Methods throw
  `std::system_error` on error
- **Result-based API** (always available) - Methods prefixed with `try_*` return `result<T>`

### Hashing (`<idfxx/crypto/hash>`)

**Algorithms** — each is a `hasher<hash::algorithm::...>` alias, present only when enabled in the
mbedTLS configuration (see Important Notes):

| Alias | Digest size | Block size |
|-------|-------------|------------|
| `md5` | 16 bytes | 64 bytes |
| `sha1` | 20 bytes | 64 bytes |
| `sha224` | 28 bytes | 64 bytes |
| `sha256` | 32 bytes | 64 bytes |
| `sha384` | 48 bytes | 128 bytes |
| `sha512` | 64 bytes | 128 bytes |
| `sha3_224` | 28 bytes | 144 bytes |
| `sha3_256` | 32 bytes | 136 bytes |
| `sha3_384` | 48 bytes | 104 bytes |
| `sha3_512` | 64 bytes | 72 bytes |

**Constants and types** (on each alias):
- `algorithm` - The `hash::algorithm` enumerator
- `digest_size`, `block_size` - Sizes in bytes
- `digest_type` - `std::array<std::byte, digest_size>`

**One-shot** (static; `data` is an `idfxx::byte_view` or a `std::string_view`, and `expected` an
`idfxx::byte_view`):
- `compute(data)` → `digest_type` / `try_compute(data)` → `result<digest_type>` - The digest of a
  complete message
- `compare(data, expected)` → `bool` / `try_compare(data, expected)` → `result<bool>` - Whether a
  message has the expected digest, compared in constant time

**Incremental:**
- `hasher()` - A hasher ready for a new message
- `update(data)` / `try_update(data)` - Add data to the message
- `finish()` → `digest_type` / `try_finish()` → `result<digest_type>` - Complete the message and
  return its digest (the empty message's digest if nothing was added)
- `verify(expected)` → `bool` / `try_verify(expected)` → `result<bool>` - Complete the message and
  check it against the expected digest, in constant time
- `clone()` / `try_clone()` - An independent copy, including the partially hashed message
- `reset()` - Discard the partially hashed message

**Lifetime:**
- Non-copyable and move-only; destroying a hasher discards its partially hashed message. A
  moved-from object must not be used: any operation other than destruction or assignment is
  undefined behavior

### Runtime algorithm: `hash::operation` (`<idfxx/crypto/hash>`)

For an algorithm known only at runtime. When it is known at compile time, prefer its `hasher` alias.
Digests are written to a caller-supplied `idfxx::byte_span` of at least the digest size, and the
filled prefix is returned as a `byte_span`.

- `hash::operation(alg)` / `make(alg)` → `result<hash::operation>` - An operation ready for a new
  message; fails if the algorithm isn't enabled or isn't a hash algorithm
- `algorithm()`, `digest_size()`, `block_size()` - The algorithm and its sizes in bytes
- `compute(alg, data, digest)` → `byte_span` / `try_compute(...)` → `result<byte_span>` - The
  digest of a complete message
- `compare(alg, data, expected)` → `bool` / `try_compare(...)` → `result<bool>` - Whether a
  message has the expected digest, compared in constant time
- `update(data)` / `try_update(data)`, `verify(expected)` / `try_verify(expected)`, `clone()` /
  `try_clone()`, `reset()` - As for `hasher`
- Non-copyable and move-only, like `hasher`. A moved-from object must not be used: any operation
  other than destruction or assignment is undefined behavior
- `finish(digest)` → `byte_span` / `try_finish(digest)` → `result<byte_span>` - Complete the
  message and write its digest

**Sizes:**
- `hash::digest_size(alg)`, `hash::block_size(alg)` - `constexpr` sizes in bytes, 0 for a value that
  isn't a known hash algorithm

### Errors (`<idfxx/crypto/error>`)

- `crypto::errc` - Error codes, following the PSA Certified Crypto API
- `crypto_category()`, `make_error_code(errc)`

## Error Handling

Errors are `idfxx::crypto::errc` values. Every argument a `hasher` function accepts is valid, so
its errors always mean the crypto library itself failed. The first operation initializes the PSA
Crypto library and returns any initialization failure; a fault while hashing is reported with the
matching code (for example `crypto::errc::hardware_failure`). A library status with no matching code
is reported as `crypto::errc::generic_error`.

`hash::operation` also reports invalid arguments:
- `crypto::errc::not_supported` - The algorithm isn't supported, for example because it isn't
  enabled in the mbedTLS configuration
- `crypto::errc::invalid_argument` - The value isn't a hash algorithm
- `crypto::errc::buffer_too_small` - The digest buffer is smaller than the digest. `finish()` then
  leaves the message unchanged, so it can be finished again with a larger buffer

A digest that doesn't match is not an error: `compare()` and `verify()` return `false`, including
for an expected digest of the wrong length.

Running out of memory is not reported as an error: following idfxx's policy, it throws
`std::bad_alloc`, or aborts when exceptions are disabled, in the `try_*` functions too.

Codes with a standard POSIX synonym also compare equal to it:

| `crypto::errc`                        | `std::errc`                 |
|---------------------------------------|-----------------------------|
| `not_supported`                       | `not_supported`             |
| `not_permitted`                       | `operation_not_permitted`   |
| `buffer_too_small`                    | `value_too_large`           |
| `already_exists`                      | `file_exists`               |
| `does_not_exist`                      | `no_such_file_or_directory` |
| `invalid_argument`                    | `invalid_argument`          |
| `insufficient_storage`                | `no_space_on_device`        |
| `storage_failure`, `hardware_failure` | `io_error`                  |
| `invalid_handle`                      | `bad_file_descriptor`       |

## Important Notes

- **Available algorithms are set at compile time** by the mbedTLS configuration (`Component config`
  → `mbedTLS` in menuconfig); an algorithm that isn't enabled has no alias or `hash::algorithm`
  enumerator, so using it is a compile error. ESP-IDF's defaults enable MD5, SHA-1, SHA-224,
  SHA-256, SHA-384 and SHA-512. SHA-3 needs `CONFIG_MBEDTLS_SHA3_C=y`. On ESP-IDF 6.x, SHA-224 is
  enabled together with SHA-256, by `CONFIG_MBEDTLS_SHA256_C`.
- **Hardware acceleration.** With `CONFIG_MBEDTLS_HARDWARE_SHA` (on by default), algorithms the
  chip's SHA engine supports are computed in hardware and the rest in software, with the same API
  and results:

  | Target | In hardware | In software |
  |--------|-------------|-------------|
  | esp32 | SHA-1, SHA-256, SHA-384, SHA-512 | SHA-224, SHA-3, MD5 |
  | esp32s2, esp32s3 | SHA-1, SHA-224, SHA-256, SHA-384, SHA-512 | SHA-3, MD5 |
  | esp32c3, esp32c6, esp32h2 | SHA-1, SHA-224, SHA-256 | SHA-384, SHA-512, SHA-3, MD5 |

  MD5 uses the implementation in the chip's ROM when `CONFIG_MBEDTLS_ROM_MD5=y` (the default).
  Without `CONFIG_MBEDTLS_HARDWARE_SHA`, every algorithm is computed in software.
- **ESP32 (classic): long-lived hashers hold the SHA engine.** A hasher that has started hashing
  (taken in at least one full block) keeps the SHA engine for its algorithm until `finish()`,
  `verify()`, `reset()` or its destruction. Meanwhile, other hashes of the same algorithm are
  computed in software: correctly, but more slowly. SHA-384 and SHA-512 share one engine. Finish or
  reset long-lived hashers promptly. Clones are computed in software.
- **MD5 and SHA-1 are not collision-resistant.** Use them only to interoperate with existing formats
  and protocols that require them, never for security.
- **Checking digests.** Use `compare()` or `verify()` rather than comparing digests with `==`: they
  take the same time wherever the digests differ, and return `false` (not an error) on a mismatch,
  including for an expected digest of the wrong length.
- **Reuse.** After `finish()`, `verify()` or `reset()`, and after a failed `update()`, `finish()`
  or `verify()`, a hasher is ready for a new message; a failed `clone()` leaves it unchanged. The
  same holds for `hash::operation`, except that `finish()` failing with `buffer_too_small` leaves
  the message unchanged.
- **Threads.** A hasher or `hash::operation` is not safe to use from several tasks at once;
  separate ones can be used concurrently.

## License

Apache License 2.0 - see [LICENSE](LICENSE) for details.
