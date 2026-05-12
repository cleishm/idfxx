// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#include <idfxx/net/endpoint>

#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>

namespace idfxx::net {

static std::optional<port_number> parse_port(std::string_view s) noexcept {
    if (s.empty() || s.size() > 5) {
        return std::nullopt;
    }
    uint32_t val = 0;
    for (char c : s) {
        if (c < '0' || c > '9') {
            return std::nullopt;
        }
        val = val * 10 + uint32_t(c - '0');
        if (val > 65535) {
            return std::nullopt;
        }
    }
    return static_cast<port_number>(val);
}

std::optional<endpoint> endpoint::parse(std::string_view s) noexcept {
    if (s.empty()) {
        return std::nullopt;
    }

    if (s.front() == '[') {
        // [v6]:port form
        auto rb = s.find(']');
        if (rb == std::string_view::npos || rb + 2 >= s.size() || s[rb + 1] != ':') {
            return std::nullopt;
        }
        auto host = s.substr(1, rb - 1);
        auto port_str = s.substr(rb + 2);
        auto addr = ipv6_addr::parse(host);
        if (!addr) {
            return std::nullopt;
        }
        auto port = parse_port(port_str);
        if (!port) {
            return std::nullopt;
        }
        return endpoint(*addr, *port);
    }

    // v4:port form — find the LAST ':' so that we don't split an unbracketed v6.
    auto colon = s.rfind(':');
    if (colon == std::string_view::npos) {
        return std::nullopt;
    }
    auto host = s.substr(0, colon);
    auto port_str = s.substr(colon + 1);
    auto addr = ipv4_addr::parse(host);
    if (!addr) {
        return std::nullopt;
    }
    auto port = parse_port(port_str);
    if (!port) {
        return std::nullopt;
    }
    return endpoint(*addr, *port);
}

} // namespace idfxx::net

namespace idfxx {

std::string to_string(const net::endpoint& ep) {
    char port_buf[6];
    auto port_len = std::snprintf(port_buf, sizeof(port_buf), "%u", static_cast<unsigned>(ep.port()));
    if (auto v4 = ep.address<net::ipv4_addr>()) {
        std::string out = to_string(*v4);
        out.reserve(out.size() + 1 + port_len);
        out += ':';
        out.append(port_buf, port_len);
        return out;
    }
    if (auto v6 = ep.address<net::ipv6_addr>()) {
        auto inner = to_string(*v6);
        std::string out;
        out.reserve(inner.size() + 3 + port_len);
        out += '[';
        out += inner;
        out += "]:";
        out.append(port_buf, port_len);
        return out;
    }
    return "<empty>";
}

} // namespace idfxx
