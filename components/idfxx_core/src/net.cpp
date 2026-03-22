// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#include <idfxx/net>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>

namespace idfxx {

// =============================================================================
// Parsing helpers
// =============================================================================

static bool is_digit(char c) {
    return c >= '0' && c <= '9';
}

static bool is_hex(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

static unsigned hex_val(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return 10 + (c - 'a');
    }
    return 10 + (c - 'A');
}

// Parse colon-separated hex groups from a string_view. Returns the number of
// groups parsed, or -1 on error. At most max_groups are accepted.
static int parse_hex_groups(std::string_view s, uint16_t* out, int max_groups) {
    int count = 0;
    size_t pos = 0;
    while (pos < s.size()) {
        if (count > 0) {
            if (s[pos] != ':') {
                return -1;
            }
            ++pos;
        }
        if (count >= max_groups) {
            return -1;
        }

        size_t start = pos;
        unsigned val = 0;
        while (pos < s.size() && is_hex(s[pos])) {
            val = (val << 4) | hex_val(s[pos]);
            ++pos;
        }
        if (pos == start) {
            return -1;
        }
        if (pos - start > 4) {
            return -1;
        }
        out[count++] = static_cast<uint16_t>(val);
    }
    return count;
}

// =============================================================================
// Parsing
// =============================================================================

std::optional<net::ip4_addr> net::ip4_addr::parse(std::string_view s) noexcept {
    uint8_t octets[4];
    size_t pos = 0;
    for (int i = 0; i < 4; ++i) {
        if (i > 0) {
            if (pos >= s.size() || s[pos] != '.') {
                return std::nullopt;
            }
            ++pos;
        }
        if (pos >= s.size() || !is_digit(s[pos])) {
            return std::nullopt;
        }
        unsigned val = 0;
        size_t start = pos;
        while (pos < s.size() && is_digit(s[pos])) {
            val = val * 10 + (s[pos] - '0');
            if (val > 255) {
                return std::nullopt;
            }
            ++pos;
        }
        // Reject leading zeros (except "0" itself)
        if (pos - start > 1 && s[start] == '0') {
            return std::nullopt;
        }
        octets[i] = static_cast<uint8_t>(val);
    }
    if (pos != s.size()) {
        return std::nullopt;
    }
    return ip4_addr(octets[0], octets[1], octets[2], octets[3]);
}

std::optional<net::ip6_addr> net::ip6_addr::parse(std::string_view s) noexcept {
    if (s.empty()) {
        return std::nullopt;
    }

    // Strip %zone suffix
    uint8_t zone = 0;
    auto pct = s.find('%');
    if (pct != std::string_view::npos) {
        auto zone_str = s.substr(pct + 1);
        if (zone_str.empty()) {
            return std::nullopt;
        }
        unsigned z = 0;
        for (char c : zone_str) {
            if (!is_digit(c)) {
                return std::nullopt;
            }
            z = z * 10 + (c - '0');
            if (z > 255) {
                return std::nullopt;
            }
        }
        zone = static_cast<uint8_t>(z);
        s = s.substr(0, pct);
    }

    uint16_t groups[8] = {};

    auto dcolon = s.find("::");
    if (dcolon == std::string_view::npos) {
        // No :: — must have exactly 8 groups
        if (parse_hex_groups(s, groups, 8) != 8) {
            return std::nullopt;
        }
    } else {
        auto before = s.substr(0, dcolon);
        auto after = s.substr(dcolon + 2);

        // Only one :: allowed
        if (after.find("::") != std::string_view::npos) {
            return std::nullopt;
        }

        uint16_t head[7] = {};
        uint16_t tail[7] = {};
        int nhead = 0, ntail = 0;

        if (!before.empty()) {
            nhead = parse_hex_groups(before, head, 7);
            if (nhead < 0) {
                return std::nullopt;
            }
        }
        if (!after.empty()) {
            ntail = parse_hex_groups(after, tail, 7);
            if (ntail < 0) {
                return std::nullopt;
            }
        }

        // :: must represent at least one group of zeros
        if (nhead + ntail > 7) {
            return std::nullopt;
        }

        for (int i = 0; i < nhead; ++i) {
            groups[i] = head[i];
        }
        int tail_start = 8 - ntail;
        for (int i = 0; i < ntail; ++i) {
            groups[tail_start + i] = tail[i];
        }
    }

    // Convert groups to bytes in network order, then to uint32_t words
    uint8_t bytes[16];
    for (int i = 0; i < 8; ++i) {
        bytes[i * 2] = uint8_t(groups[i] >> 8);
        bytes[i * 2 + 1] = uint8_t(groups[i]);
    }
    std::array<uint32_t, 4> words;
    std::memcpy(words.data(), bytes, 16);
    return ip6_addr(words, zone);
}

// =============================================================================
// String conversions
// =============================================================================

std::string to_string(net::ip4_addr addr) {
    uint32_t a = addr.addr();
    char buf[16];
    snprintf(
        buf,
        sizeof(buf),
        "%u.%u.%u.%u",
        static_cast<unsigned>(a & 0xFF),
        static_cast<unsigned>((a >> 8) & 0xFF),
        static_cast<unsigned>((a >> 16) & 0xFF),
        static_cast<unsigned>((a >> 24) & 0xFF)
    );
    return buf;
}

std::string to_string(net::ip6_addr addr) {
    // Extract 8 groups of 16 bits from raw address bytes (network byte order in memory)
    uint16_t groups[8];
    const auto* bytes = reinterpret_cast<const uint8_t*>(addr.addr().data());
    for (int i = 0; i < 8; ++i) {
        groups[i] = (uint16_t(bytes[i * 2]) << 8) | bytes[i * 2 + 1];
    }

    // Find the longest run of consecutive zero groups (RFC 5952: >= 2, first wins ties)
    int zstart = -1, zlen = 0;
    for (int i = 0, cs = -1, cl = 0; i < 8; ++i) {
        if (groups[i] == 0) {
            if (cs < 0) {
                cs = i;
            }
            if (++cl > zlen && cl >= 2) {
                zstart = cs;
                zlen = cl;
            }
        } else {
            cs = -1;
            cl = 0;
        }
    }

    // Format address groups with :: compression
    char buf[48];
    int pos = 0;
    for (int i = 0; i < 8;) {
        if (i == zstart) {
            buf[pos++] = ':';
            buf[pos++] = ':';
            i += zlen;
            continue;
        }
        if (i > 0 && (zstart < 0 || i != zstart + zlen)) {
            buf[pos++] = ':';
        }
        pos += snprintf(buf + pos, sizeof(buf) - pos, "%x", groups[i]);
        ++i;
    }
    buf[pos] = '\0';

    std::string result(buf, pos);
    if (addr.zone() != 0) {
        result += '%';
        result += std::to_string(addr.zone());
    }
    return result;
}

std::string to_string(net::ip4_info info) {
    return "ip=" + to_string(info.ip) + ", netmask=" + to_string(info.netmask) + ", gw=" + to_string(info.gateway);
}

} // namespace idfxx
