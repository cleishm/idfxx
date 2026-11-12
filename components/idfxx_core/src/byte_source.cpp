// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#include <idfxx/byte_source>

#include <cerrno>
#include <cstdio>
#include <system_error>

namespace idfxx {

result<size_t> file_source::try_read(mutable_byte_view buf) {
    if (buf.empty()) {
        return 0;
    }
    // Clear errno so a stale value from an earlier call is not reported as
    // the cause of a read error that set none.
    errno = 0;
    const size_t n = std::fread(buf.data(), 1, buf.size(), _file);
    // Bytes read before an error are returned as a short read; the stream's
    // error indicator stays set, so the error is reported by the next read.
    if (n == 0 && std::ferror(_file)) {
        // The stream's error indicator is set, so an I/O error occurred even
        // if it reported no errno.
        const int err = errno;
        return error(std::error_code(err != 0 ? err : EIO, std::generic_category()));
    }
    return n;
}

} // namespace idfxx
