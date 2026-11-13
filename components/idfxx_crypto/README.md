# idfxx_crypto

Type-safe cryptographic primitives: message digests (hashing) and message authentication codes
(MACs).

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
- Message authentication codes: HMAC with each of the hash algorithms above, and AES-CMAC (128-,
  192- and 256-bit keys), one type per algorithm (`idfxx::crypto::mac::hmac_sha256`,
  `idfxx::crypto::mac::cmac`, ...) with the same one-shot, incremental and constant-time checking
  API
- Truncated MAC tags, with the tag size in the type: `mac::authenticator<mac::algorithm::cmac, 4>`
  produces the leftmost 4 bytes of each AES-CMAC tag
- `idfxx::crypto::mac::operation` for a MAC algorithm chosen at runtime, taken from the key's
  policy (algorithm and tag length), with the same operations writing tags into a caller-supplied
  buffer
- `mac::tag_size()` for an algorithm's full tag length, usable in constant expressions
- Keys held by the crypto library (`idfxx::crypto::key`), imported or randomly generated, with a
  fixed usage policy: the operations and algorithm they permit
- Messages and digests given as an `idfxx::byte_view` (any `std::byte` or `uint8_t` range, or
  `{pointer, length}`), and messages also as `std::string_view`
- Hardware SHA and AES acceleration where the chip has it, behind the same API
- `crypto::errc` error codes, comparable with `std::errc` where a POSIX equivalent exists

## Requirements

- ESP-IDF 5.5 or later
- C++23 compiler
- idfxx_core (installed automatically)
- For keys and MACs (`<idfxx/crypto/key>`, `<idfxx/crypto/mac>`), the mbedTLS threading layer:
  `CONFIG_MBEDTLS_THREADING_C=y`, on by default from ESP-IDF 6.0 (see Important Notes for ESP-IDF
  5.5). Hashing doesn't need it

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

### Message Authentication

If `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled:

```cpp
#include <idfxx/crypto/mac>
#include <idfxx/log>

#include <array>
#include <cstdint>

try {
    // A key shared with the other party (or idfxx::crypto::mac::hmac_sha256::generate_key())
    std::array<uint8_t, 32> secret = load_shared_secret();
    auto key = idfxx::crypto::mac::hmac_sha256::import_key(secret);

    // One-shot: the tag of a complete message
    auto tag = idfxx::crypto::mac::hmac_sha256::compute(key, "hello, world");

    // Check a received message against its tag, in constant time
    if (!idfxx::crypto::mac::hmac_sha256::compare(key, "hello, world", tag)) {
        idfxx::log::warn("MAC", "Message was altered");
    }

    // Incremental: a message supplied in pieces
    idfxx::crypto::mac::hmac_sha256 hmac{key}; // the key must outlive the authenticator
    hmac.update("hello, ");
    hmac.update("world");
    if (!hmac.verify(tag)) { // the authenticator is ready for a new message
        idfxx::log::warn("MAC", "Message was altered");
    }
} catch (const std::system_error& e) {
    idfxx::log::error("MAC", "Authentication failed: {}", e.what());
}
```

If `CONFIG_COMPILER_CXX_EXCEPTIONS` is *not* enabled:

```cpp
#include <idfxx/crypto/mac>
#include <idfxx/log>

auto key = idfxx::crypto::mac::hmac_sha256::try_import_key(secret);
if (!key) {
    idfxx::log::error("MAC", "Key import failed: {}", key.error().message());
    return;
}

auto tag = idfxx::crypto::mac::hmac_sha256::try_compute(*key, "hello, world");
if (!tag) {
    idfxx::log::error("MAC", "Authentication failed: {}", tag.error().message());
    return;
}

auto hmac = idfxx::crypto::mac::hmac_sha256::make(*key); // the key must outlive the authenticator
if (!hmac) {
    idfxx::log::error("MAC", "Key rejected: {}", hmac.error().message());
    return;
}
for (std::string_view piece : {"hello", ", ", "world"}) {
    if (auto r = hmac->try_update(piece); !r) {
        idfxx::log::error("MAC", "Authentication failed: {}", r.error().message());
        return;
    }
}
auto match = hmac->try_verify(*tag);
if (!match) {
    idfxx::log::error("MAC", "Authentication failed: {}", match.error().message());
} else if (!*match) {
    idfxx::log::warn("MAC", "Message was altered");
}
```

#### MAC algorithm chosen at runtime

`mac::operation` takes the algorithm and tag length from the key's policy, so a key created for an
algorithm chosen at runtime is all it needs:

```cpp
#include <idfxx/crypto/mac>

#include <array>
#include <cstddef>

// alg came from, for example, configuration naming the MAC algorithm
auto key = idfxx::crypto::key::import(
    {.type = idfxx::crypto::key_type::hmac,
     .usage = idfxx::crypto::key_usage::sign_message | idfxx::crypto::key_usage::verify_message,
     .algorithm = alg},
    secret
);
idfxx::crypto::mac::operation op{key}; // the key must outlive the operation
op.update(header);
op.update(body);
std::array<std::byte, 64> buf; // large enough for any tag
auto tag = op.finish(buf);     // the first op.tag_size() bytes of buf
```

`mac::operation::make(key)` is the result-based way to create one.

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

### Keys (`<idfxx/crypto/key>`)

**Types:**
- `key_type` - `raw_data`, `hmac`, `derive`, `aes`
- `key_usage` - The operations a key permits, combined into `flags<key_usage>`: `export_key`,
  `copy`, `encrypt`, `decrypt`, `sign_message`, `verify_message`, `sign_hash`, `verify_hash`,
  `derive`, `verify_derivation`
- `key_algorithm` - The algorithm a key may be used with; constructed implicitly from a
  `mac::algorithm`; none by default. For truncated MAC tags, `key_algorithm{alg, tag_size}` with a
  tag size known at compile time (an invalid one is a compile error), or
  `key_algorithm::truncated(alg, tag_size)` / `try_truncated(alg, tag_size)` →
  `result<key_algorithm>` with one known only at runtime
- `key::config` - `{.type, .usage, .algorithm}`; `.type` is required

**Creation** (static):
- `key::import(cfg, material)` / `try_import(cfg, material)` - A key from key material, its size
  taken from the material
- `key::generate(cfg, bits)` / `try_generate(cfg, bits)` - A key from random key material

**Methods:**
- `copy(usage)` → `key` / `try_copy(usage)` → `result<key>` - A new key with the same key material,
  permitting only the given operations among this key's (needs `key_usage::copy`)
- `export_to(out)` → `byte_span` / `try_export_to(out)` → `result<byte_span>` - Copy the key
  material into a buffer and return the filled prefix (needs `key_usage::export_key`)
- `type()`, `bits()`, `usage()`, `algorithm()` - The key's attributes
- `idf_handle()` - The underlying PSA key identifier

**Lifetime:**
- Non-copyable and move-only; destroying a key destroys its key material. A moved-from object must
  not be used: any operation other than destruction or assignment is undefined behavior

### Message Authentication (`<idfxx/crypto/mac>`)

**Algorithms** — each is an `authenticator<mac::algorithm::...>` alias, present only when enabled
in the mbedTLS configuration (see Important Notes):

| Alias | Tag size | Key |
|-------|----------|-----|
| `hmac_md5` | 16 bytes | any length |
| `hmac_sha1` | 20 bytes | any length |
| `hmac_sha224` | 28 bytes | any length |
| `hmac_sha256` | 32 bytes | any length |
| `hmac_sha384` | 48 bytes | any length |
| `hmac_sha512` | 64 bytes | any length |
| `hmac_sha3_224` | 28 bytes | any length |
| `hmac_sha3_256` | 32 bytes | any length |
| `hmac_sha3_384` | 48 bytes | any length |
| `hmac_sha3_512` | 64 bytes | any length |
| `cmac` | 16 bytes | AES-128, AES-192 or AES-256 |

**Truncated tags:** `authenticator<mac::algorithm::..., TagSize>` produces the leftmost `TagSize`
bytes of each tag, for a `TagSize` from 4 bytes up to the full tag length; at the full length it is
the alias itself. It has the same API as the aliases.

**Constants and types** (on each alias and truncated authenticator):
- `algorithm` - The `mac::algorithm` enumerator
- `tag_size` - Size in bytes (`TagSize`)
- `tag_type` - `std::array<std::byte, tag_size>`

**Keys** (static): permitted to compute and check tags with this algorithm and tag length only
- `import_key(material)` / `try_import_key(material)` - A key from key material
- `generate_key()` / `try_generate_key()` - A random key: as long as the hash's output for HMAC
  (the length RFC 2104 recommends, whatever the tag size), 128 bits for AES-CMAC
- `generate_key(bits)` / `try_generate_key(bits)` - A random key of a chosen size (for example 256
  bits for AES-CMAC)

**One-shot** (static; `data` is an `idfxx::byte_view` or a `std::string_view`, and `expected` an
`idfxx::byte_view`):
- `compute(key, data)` → `tag_type` / `try_compute(key, data)` → `result<tag_type>` - The tag of a
  complete message (needs `key_usage::sign_message`)
- `compare(key, data, expected)` → `bool` / `try_compare(key, data, expected)` → `result<bool>` -
  Whether a message has the expected tag, compared in constant time (needs
  `key_usage::verify_message`)

**Incremental:**
- `authenticator(key)` / `make(key)` → `result<authenticator>` (static) - An authenticator ready
  for a new message, once the key's algorithm and usage are checked
- `update(data)` / `try_update(data)` - Add data to the message
- `finish()` → `tag_type` / `try_finish()` → `result<tag_type>` - Complete the message and return
  its tag (needs `key_usage::sign_message`)
- `verify(expected)` → `bool` / `try_verify(expected)` → `result<bool>` - Complete the message and
  check it against the expected tag, in constant time (needs `key_usage::verify_message`)
- `reset()` - Discard the partially authenticated message

**Lifetime:**
- Non-copyable and move-only, and cannot be cloned; destroying an authenticator discards its
  partially authenticated message. A moved-from object must not be used: any operation other than
  destruction or assignment is undefined behavior

### Runtime algorithm: `mac::operation` (`<idfxx/crypto/mac>`)

For a MAC algorithm known only at runtime: the algorithm and tag length come from the key's policy
(`key::algorithm()`). When the algorithm is known at compile time, prefer its `authenticator` alias,
which also checks that a key is for it. Tags are written to a caller-supplied `idfxx::byte_span` of
at least the tag size, and the filled prefix is returned as a `byte_span`.

- `mac::operation(key)` / `make(key)` → `result<mac::operation>` - An operation ready for a new
  message, once the key is checked to permit a MAC algorithm and computing or checking tags
- `algorithm()`, `tag_size()` - The key's MAC algorithm (at its full length) and its tag size in
  bytes (shorter for a key permitted truncated tags)
- `compute(key, data, tag)` → `byte_span` / `try_compute(...)` → `result<byte_span>` - The tag of a
  complete message (needs `key_usage::sign_message`)
- `compare(key, data, expected)` → `bool` / `try_compare(...)` → `result<bool>` - Whether a
  message has the expected tag, compared in constant time (needs `key_usage::verify_message`)
- `update(data)` / `try_update(data)`, `verify(expected)` / `try_verify(expected)`, `reset()` - As
  for `authenticator`
- `finish(tag)` → `byte_span` / `try_finish(tag)` → `result<byte_span>` - Complete the message and
  write its tag (needs `key_usage::sign_message`)
- Non-copyable and move-only, like `authenticator`. A moved-from object must not be used: any
  operation other than destruction or assignment is undefined behavior

**Sizes:**
- `mac::tag_size(alg)` - `constexpr` full tag length in bytes, 0 for a value that isn't a known
  MAC algorithm

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

Keys and MACs add errors for arguments and policies the crypto library rejects:
- `crypto::errc::invalid_argument` - Key material that is empty or not a valid size for the key
  type, a key created for a MAC algorithm its type doesn't take (an AES key for HMAC, or an HMAC
  key for AES-CMAC), or a tag length for `key_algorithm::try_truncated()` of fewer than 4 bytes or
  more than the algorithm's full tag length
- `crypto::errc::not_permitted` - An operation the key's usage doesn't permit (such as computing a
  tag with a key that may only check them, or exporting a key without `key_usage::export_key`), or
  a key used with an algorithm other than the one it permits (including the same MAC algorithm with
  another tag length)
- `crypto::errc::not_supported` - A key size the chip can't use with the algorithm (see AES-192 in
  Important Notes), reported when a message starts: by `compute()` and `compare()`, or by an
  authenticator's or `mac::operation`'s first `update()`, `finish()` or `verify()` of a message
- `crypto::errc::buffer_too_small` - A buffer too small for `export_to()`, or a tag buffer smaller
  than the tag for `mac::operation`. `finish()` then leaves the message unchanged, so it can be
  finished again with a larger buffer

`mac::operation` refuses a key permitted no MAC algorithm (such as one created without an
`algorithm`) with `crypto::errc::not_permitted`.

A tag that doesn't match is not an error either: MAC `compare()` and `verify()` return `false`,
including for an expected tag of the wrong length, which is decided without starting the message
(so a key size the chip can't use isn't reported then). A key that doesn't permit checking tags is
reported as an error whatever the expected tag's length.

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
  enabled together with SHA-256, by `CONFIG_MBEDTLS_SHA256_C`. HMAC is available with every
  enabled hash algorithm. AES-CMAC needs `CONFIG_MBEDTLS_CMAC_C=y`: off by default on ESP-IDF 5.5
  and on by default in 6.x, and selected by the Wi-Fi and Bluetooth stacks.
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

  HMAC hashes with the same implementation: in hardware wherever the table lists its hash
  algorithm in hardware, so HMAC with SHA-3 is always computed in software. AES-CMAC uses the
  chip's AES engine with `CONFIG_MBEDTLS_HARDWARE_AES` (on by default). This is what ESP-IDF
  selects for each configuration; the speed of each path hasn't been measured.
- **AES-CMAC with 192-bit keys.** Only the esp32 and esp32s2 AES engines take 192-bit keys. On the
  other supported targets with `CONFIG_MBEDTLS_HARDWARE_AES`, an AES-192 key imports, but on
  ESP-IDF 5.5 using it with `cmac` fails with `crypto::errc::not_supported` when a message starts
  (creating the authenticator succeeds); on ESP-IDF 6.x, don't rely on it either. Use AES-128 or
  AES-256 keys, which work everywhere.
- **ESP32 (classic): long-lived hashers and HMAC authenticators hold the SHA engine.** A hasher
  that has started hashing (taken in at least one full block) keeps the SHA engine for its
  algorithm until `finish()`, `verify()`, `reset()` or its destruction. An HMAC `authenticator` or
  `mac::operation` keeps it from its first `update()` of a message until `finish()`, `verify()`,
  `reset()` or its destruction. Meanwhile, other hashes and HMACs with the same hash algorithm are
  computed in software: correctly, but more slowly. SHA-384 and SHA-512 share one engine. Finish or
  reset long-lived hashers and authenticators promptly. Clones are computed in software.
- **MD5 and SHA-1 are not collision-resistant.** Use them only to interoperate with existing formats
  and protocols that require them, never for security.
- **Checking digests.** Use `compare()` or `verify()` rather than comparing digests with `==`: they
  take the same time wherever the digests differ, and return `false` (not an error) on a mismatch,
  including for an expected digest of the wrong length.
- **Checking tags.** The same goes for MAC tags: use `compare()` or `verify()`, never `==`.
- **Truncated tags.** `mac::authenticator<alg, N>` keeps the leftmost `N` bytes of each tag. `N`
  must be at least 4 (the crypto library's minimum) and at most the full tag length; any other `N`
  is a compile error, as it is for `key_algorithm{alg, N}`; with a length known only at runtime,
  `key_algorithm::try_truncated(alg, n)` fails with `crypto::errc::invalid_argument` instead.
  Shorter tags are easier to forge by guessing, so use the length your protocol specifies. The tag
  length is part of a key's policy: a key from `authenticator<alg, N>::import_key()` or
  `generate_key()` (or created with `key_algorithm{alg, N}`) works only with
  `authenticator<alg, N>`, and the aliases (`hmac_sha256`, `cmac`, ...), which are full length,
  refuse it with `crypto::errc::not_permitted`, as `authenticator<alg, N>` refuses a full-length
  key.
- **Reuse.** After `finish()`, `verify()` or `reset()`, and after a failed `update()`, `finish()`
  or `verify()`, a hasher or authenticator is ready for a new message: a failure discards the
  partial message. A failed `clone()` leaves a hasher unchanged. The same holds for
  `hash::operation` and `mac::operation`, except that their `finish()` failing with
  `buffer_too_small` leaves the message unchanged.
- **Keys are volatile.** A `key` exists only in RAM: it is destroyed with the `key` object, and
  none survive a reboot. Store key material yourself (for example in encrypted NVS) and import it
  again after a restart.
- **Key material can't be empty.** Importing empty key material fails with
  `crypto::errc::invalid_argument`, so HMAC with an empty key is not possible.
- **An authenticator uses its key, without owning it.** The key must outlive every `authenticator`
  or `mac::operation` constructed from it: it may be moved, but not destroyed or assigned over,
  while one uses it. Several can share one key.
- **Key usage decides what an authenticator can do.** Computing a tag (`compute()`, `finish()`)
  needs `key_usage::sign_message`, and checking one (`compare()`, `verify()`) needs
  `key_usage::verify_message`. Keys from `import_key()` and `generate_key()` permit both; a key
  imported with only `verify_message`, for example, can only check tags.
- **Creating a key checks its type against its algorithm.** `key::import()` and `key::generate()`
  refuse a MAC algorithm that doesn't take the key type (HMAC takes `key_type::hmac`, AES-CMAC
  `key_type::aes`) with `crypto::errc::invalid_argument`.
- **Creating an authenticator checks its key's policy.** `authenticator(key)` throws, and
  `make(key)` fails, if the key permits another algorithm, or permits neither computing nor
  checking tags. `mac::operation(key)` and `mac::operation::make(key)` check the same against the
  algorithm the key permits, and refuse a key permitted no MAC algorithm. Neither sets anything up
  in the crypto library: a key size the chip can't use (see AES-192 above) is reported when a
  message starts, by its first `update()`, `finish()` or `verify()`. Otherwise, with a key that
  passes, `update()` fails only on a fault in the crypto library or hardware.
- **Threads.** A hasher, `hash::operation`, authenticator or `mac::operation` is not safe to use
  from several tasks at once; separate ones can be used concurrently, and can share a key. For keys
  and MACs, that needs the mbedTLS threading layer, so `<idfxx/crypto/key>` (which
  `<idfxx/crypto/mac>` includes) is a compile error without `CONFIG_MBEDTLS_THREADING_C=y`. ESP-IDF
  6.x enables it by default. On ESP-IDF 5.5 it is off by default, and its default implementation
  (`CONFIG_MBEDTLS_THREADING_ALT`) has working locks only in the ROM port, so set all three:

  ```
  CONFIG_MBEDTLS_THREADING_C=y
  CONFIG_MBEDTLS_THREADING_PTHREAD=y
  # CONFIG_MBEDTLS_THREADING_ALT is not set
  ```

## License

Apache License 2.0 - see [LICENSE](LICENSE) for details.
