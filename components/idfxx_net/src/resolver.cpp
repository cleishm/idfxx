// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#include "detail/sockaddr.hpp"

#include <idfxx/net/resolver>

#include <cstdio>
#include <cstring>
#include <lwip/inet.h>
#include <lwip/netdb.h>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

// Verify resolver_options::proto values match IPPROTO_* constants so that this
// enum remains a subset of any future ip_protocol type.
static_assert(std::to_underlying(idfxx::net::resolver_options::proto::tcp) == IPPROTO_TCP);
static_assert(std::to_underlying(idfxx::net::resolver_options::proto::udp) == IPPROTO_UDP);

namespace idfxx::net {

namespace {

void fill_hints(addrinfo& hints, const resolver_options& opts) noexcept {
    hints.ai_family = detail::family_to_af(opts.family);
    int flags = 0;
    if (opts.numeric_host) {
        flags |= AI_NUMERICHOST;
    }
    if (opts.numeric_port) {
        flags |= AI_NUMERICSERV;
    }
    if (opts.passive) {
        flags |= AI_PASSIVE;
    }
    hints.ai_flags = flags;

    if (opts.proto) {
        switch (*opts.proto) {
        case resolver_options::proto::tcp:
            hints.ai_socktype = SOCK_STREAM;
            break;
        case resolver_options::proto::udp:
            hints.ai_socktype = SOCK_DGRAM;
            break;
        }
    } else {
        hints.ai_socktype = 0;
    }
}

struct addrinfo_deleter {
    void operator()(addrinfo* p) const noexcept { lwip_freeaddrinfo(p); }
};
using addrinfo_ptr = std::unique_ptr<addrinfo, addrinfo_deleter>;

// Iterates getaddrinfo results, calling `sink(endpoint)` for each. Sink returns
// true to keep iterating, false to stop early.
template<typename Sink>
result<void> for_each_addrinfo(const char* host, const char* service, const resolver_options& opts, Sink&& sink) {
    addrinfo hints{};
    fill_hints(hints, opts);

    addrinfo* raw = nullptr;
    int rc = lwip_getaddrinfo(host, service, &hints, &raw);
    if (rc != 0) {
        return error(gai_to_error_code(rc));
    }
    addrinfo_ptr res(raw);

    for (auto* p = res.get(); p != nullptr; p = p->ai_next) {
        if (p->ai_addr == nullptr) {
            continue;
        }
        if (auto ep = detail::from_sockaddr(p->ai_addr, p->ai_addrlen)) {
            if (!sink(*ep)) {
                break;
            }
        }
    }
    return {};
}

void format_port(char (&buf)[6], port_number port) noexcept {
    std::snprintf(buf, sizeof(buf), "%u", static_cast<unsigned>(port));
}

// Max DNS name length per RFC 1035 is 253 characters; one byte for NUL terminator.
constexpr size_t host_buf_size = 254;
// Service names rarely exceed a few characters; this comfortably fits both names and numeric ports.
constexpr size_t service_buf_size = 32;

template<size_t N>
[[nodiscard]] bool copy_to_cstr(std::string_view view, char (&buf)[N]) noexcept {
    if (view.size() >= N) {
        return false;
    }
    std::memcpy(buf, view.data(), view.size());
    buf[view.size()] = '\0';
    return true;
}

result<std::vector<endpoint>> resolve_all(const char* host, const char* service, const resolver_options& opts) {
    std::vector<endpoint> out;
    auto r = for_each_addrinfo(host, service, opts, [&out](endpoint ep) {
        out.push_back(ep);
        return true;
    });
    if (!r) {
        return error(r.error());
    }
    return out;
}

} // namespace

result<std::vector<endpoint>> try_resolve(std::string_view host, port_number port, const resolver_options& opts) {
    char host_buf[host_buf_size];
    if (!copy_to_cstr(host, host_buf)) {
        return error(errc::invalid_argument);
    }
    char port_buf[6];
    format_port(port_buf, port);
    resolver_options o = opts;
    o.numeric_port = true;
    return resolve_all(host_buf, port_buf, o);
}

result<std::vector<endpoint>>
try_resolve(std::string_view host, std::string_view service, const resolver_options& opts) {
    char host_buf[host_buf_size];
    char service_buf[service_buf_size];
    if (!copy_to_cstr(host, host_buf) || !copy_to_cstr(service, service_buf)) {
        return error(errc::invalid_argument);
    }
    return resolve_all(host_buf, service_buf, opts);
}

result<endpoint> try_resolve_one(std::string_view host, port_number port, const resolver_options& opts) {
    char host_buf[host_buf_size];
    if (!copy_to_cstr(host, host_buf)) {
        return error(errc::invalid_argument);
    }
    char port_buf[6];
    format_port(port_buf, port);
    resolver_options o = opts;
    o.numeric_port = true;

    std::optional<endpoint> first;
    auto r = for_each_addrinfo(host_buf, port_buf, o, [&first](endpoint ep) {
        first = ep;
        return false;
    });
    if (!r) {
        return error(r.error());
    }
    if (!first) {
        return error(errc::name_not_found);
    }
    return *first;
}

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
std::vector<endpoint> resolve(std::string_view host, port_number port, const resolver_options& opts) {
    return idfxx::unwrap(try_resolve(host, port, opts));
}

std::vector<endpoint> resolve(std::string_view host, std::string_view service, const resolver_options& opts) {
    return idfxx::unwrap(try_resolve(host, service, opts));
}

endpoint resolve_one(std::string_view host, port_number port, const resolver_options& opts) {
    return idfxx::unwrap(try_resolve_one(host, port, opts));
}
#endif

} // namespace idfxx::net
