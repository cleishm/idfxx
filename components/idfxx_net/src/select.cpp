// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#include "detail/select.hpp"

#include <idfxx/net/error>

#include <errno.h>
#include <lwip/sockets.h>

namespace idfxx::net::detail {

result<void> wait(int fd, idfxx::flags<wait_for> what, std::chrono::milliseconds timeout) noexcept {
    fd_set rd, wr, ex;
    FD_ZERO(&rd);
    FD_ZERO(&wr);
    FD_ZERO(&ex);
    fd_set* prd = nullptr;
    fd_set* pwr = nullptr;
    fd_set* pex = nullptr;
    if (what.contains(wait_for::read)) {
        FD_SET(fd, &rd);
        prd = &rd;
    }
    if (what.contains(wait_for::write)) {
        FD_SET(fd, &wr);
        pwr = &wr;
    }
    if (what.contains(wait_for::except)) {
        FD_SET(fd, &ex);
        pex = &ex;
    }

    timeval tv;
    tv.tv_sec = static_cast<long>(timeout.count() / 1000);
    tv.tv_usec = static_cast<long>((timeout.count() % 1000) * 1000);

    int n = lwip_select(fd + 1, prd, pwr, pex, &tv);
    if (n < 0) {
        return error(errno_to_error_code(errno));
    }
    if (n == 0) {
        return error(errc::timed_out);
    }
    return {};
}

} // namespace idfxx::net::detail
