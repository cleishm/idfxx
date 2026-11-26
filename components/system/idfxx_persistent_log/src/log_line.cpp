// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#include "log_line.hpp"

#include <charconv>

namespace idfxx::persistent_log {

namespace {

// Appends text to a buffer that is known to be large enough.
class writer {
public:
    explicit writer(char* p)
        : _start(p)
        , _p(p) {}

    void put(char c) { *_p++ = c; }

    template<typename T>
    void number(T v) {
        // 20 digits and a sign hold any 64-bit value.
        _p = std::to_chars(_p, _p + 21, v).ptr;
    }

    // A number of exactly `width` digits, zero-padded.
    void padded(unsigned v, int width) {
        for (int i = width - 1; i >= 0; --i) {
            _p[i] = static_cast<char>('0' + v % 10);
            v /= 10;
        }
        _p += width;
    }

    [[nodiscard]] size_t size() const { return static_cast<size_t>(_p - _start); }

private:
    char* _start;
    char* _p;
};

} // namespace

size_t format_prefix(
    std::span<char> out,
    uint32_t boot,
    uint32_t wake,
    std::chrono::milliseconds uptime,
    std::optional<std::chrono::system_clock::time_point> wall
) {
    using namespace std::chrono;
    // With a 64-bit count of milliseconds, the stamp is at most
    // 10 + 1 + 10 + 2 + 20 + 1 + 20 + 1 = 65 bytes, which is within max_prefix_size.
    static_assert(sizeof(milliseconds::rep) <= 8);
    if (out.size() < max_prefix_size) {
        return 0;
    }
    writer w(out.data());
    w.number(boot);
    w.put('.');
    w.number(wake);
    w.put(' ');
    w.put('+');
    w.number(uptime.count());
    w.put(' ');
    if (wall) {
        if (!detail::put_utc(w, *wall)) {
            return 0;
        }
    } else {
        w.put('-');
    }
    w.put(' ');
    return w.size();
}

} // namespace idfxx::persistent_log
