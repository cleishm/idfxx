// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#include <idfxx/net/error>

#include <errno.h>
#include <lwip/err.h>
#include <lwip/netdb.h>
#include <string>
#include <system_error>

namespace idfxx {

const net::error_category& net_category() noexcept {
    static const net::error_category instance{};
    return instance;
}

const char* net::error_category::name() const noexcept {
    return "net::Error";
}

std::string net::error_category::message(int ec) const {
    switch (net::errc(ec)) {
    case net::errc::invalid_state:
        return "Operation invalid in current state";
    case net::errc::invalid_argument:
        return "Invalid argument";
    case net::errc::not_supported:
        return "Operation not supported";
    case net::errc::timed_out:
        return "Operation timed out";
    case net::errc::would_block:
        return "Operation would block";
    case net::errc::in_progress:
        return "Operation in progress";
    case net::errc::interrupted:
        return "Operation interrupted by signal";
    case net::errc::address_in_use:
        return "Address already in use";
    case net::errc::address_not_available:
        return "Address not available on this host";
    case net::errc::connection_refused:
        return "Connection refused by peer";
    case net::errc::connection_reset:
        return "Connection reset by peer";
    case net::errc::connection_aborted:
        return "Connection aborted";
    case net::errc::not_connected:
        return "Socket not connected";
    case net::errc::already_connected:
        return "Socket already connected";
    case net::errc::host_unreachable:
        return "Host unreachable";
    case net::errc::network_unreachable:
        return "Network unreachable";
    case net::errc::network_down:
        return "Network is down";
    case net::errc::pipe_broken:
        return "Pipe broken";
    case net::errc::message_too_long:
        return "Message too long for transport";
    case net::errc::too_many_open:
        return "Too many open sockets";
    case net::errc::name_resolution:
        return "Name resolution failure";
    case net::errc::name_not_found:
        return "Host name not found";
    case net::errc::name_temporary:
        return "Temporary name resolution failure";
    case net::errc::name_no_data:
        return "Name has no data of requested family";
    case net::errc::io_error:
        return "I/O error";
    case net::errc::wrong_protocol_type:
        return "Operation does not match socket's family or protocol";
    case net::errc::netconn_buffer_error:
        return "Netconn buffer error";
    case net::errc::netconn_routing:
        return "Netconn routing error";
    case net::errc::netconn_illegal_value:
        return "Illegal value passed to netconn";
    case net::errc::netconn_aborted:
        return "Netconn connection aborted";
    case net::errc::netconn_reset:
        return "Netconn connection reset";
    case net::errc::netconn_closed:
        return "Netconn connection closed";
    }
    return "Unknown net error (" + std::to_string(ec) + ")";
}

bool net::error_category::equivalent(int code, const std::error_condition& condition) const noexcept {
    if (condition.category() != std::generic_category()) {
        return false;
    }
    auto target = std::errc(condition.value());
    switch (net::errc(code)) {
    case net::errc::invalid_state:
        return target == std::errc::bad_file_descriptor;
    case net::errc::invalid_argument:
        return target == std::errc::invalid_argument;
    case net::errc::not_supported:
        return target == std::errc::not_supported || target == std::errc::operation_not_supported;
    case net::errc::timed_out:
        return target == std::errc::timed_out;
    case net::errc::would_block:
        return target == std::errc::operation_would_block || target == std::errc::resource_unavailable_try_again;
    case net::errc::in_progress:
        return target == std::errc::operation_in_progress;
    case net::errc::interrupted:
        return target == std::errc::interrupted;
    case net::errc::address_in_use:
        return target == std::errc::address_in_use;
    case net::errc::address_not_available:
        return target == std::errc::address_not_available;
    case net::errc::connection_refused:
        return target == std::errc::connection_refused;
    case net::errc::connection_reset:
        return target == std::errc::connection_reset;
    case net::errc::connection_aborted:
        return target == std::errc::connection_aborted;
    case net::errc::not_connected:
        return target == std::errc::not_connected;
    case net::errc::already_connected:
        return target == std::errc::already_connected;
    case net::errc::host_unreachable:
        return target == std::errc::host_unreachable;
    case net::errc::network_unreachable:
        return target == std::errc::network_unreachable;
    case net::errc::network_down:
        return target == std::errc::network_down;
    case net::errc::pipe_broken:
        return target == std::errc::broken_pipe;
    case net::errc::message_too_long:
        return target == std::errc::message_size;
    case net::errc::too_many_open:
        return target == std::errc::too_many_files_open;
    case net::errc::io_error:
        return target == std::errc::io_error;
    case net::errc::wrong_protocol_type:
        return target == std::errc::wrong_protocol_type;
    default:
        return false;
    }
}

namespace net {

std::error_code errno_to_error_code(int errno_value) {
    if (errno_value == 0) {
        return {};
    }
    if (errno_value == ENOMEM) {
        raise_no_mem();
    }
    errc mapped;
    switch (errno_value) {
    case EINVAL:
        mapped = errc::invalid_argument;
        break;
    case ENOTSUP:
#if defined(EOPNOTSUPP) && EOPNOTSUPP != ENOTSUP
    case EOPNOTSUPP:
#endif
    case EPROTONOSUPPORT:
#ifdef ESOCKTNOSUPPORT
    case ESOCKTNOSUPPORT:
#endif
#ifdef EPFNOSUPPORT
    case EPFNOSUPPORT:
#endif
    case EAFNOSUPPORT:
    case EPROTOTYPE:
    case ENOPROTOOPT:
        mapped = errc::not_supported;
        break;
    case ENOBUFS:
        mapped = errc::would_block;
        break;
    case ETIMEDOUT:
        mapped = errc::timed_out;
        break;
    case EAGAIN:
#if defined(EWOULDBLOCK) && EWOULDBLOCK != EAGAIN
    case EWOULDBLOCK:
#endif
        mapped = errc::would_block;
        break;
    case EINPROGRESS:
    case EALREADY:
        mapped = errc::in_progress;
        break;
    case EINTR:
        mapped = errc::interrupted;
        break;
    case EADDRINUSE:
        mapped = errc::address_in_use;
        break;
    case EADDRNOTAVAIL:
        mapped = errc::address_not_available;
        break;
    case ECONNREFUSED:
        mapped = errc::connection_refused;
        break;
    case ECONNRESET:
        mapped = errc::connection_reset;
        break;
    case ECONNABORTED:
        mapped = errc::connection_aborted;
        break;
    case ENOTCONN:
        mapped = errc::not_connected;
        break;
    case EISCONN:
        mapped = errc::already_connected;
        break;
    case EHOSTUNREACH:
#ifdef EHOSTDOWN
    case EHOSTDOWN:
#endif
        mapped = errc::host_unreachable;
        break;
    case ENETUNREACH:
        mapped = errc::network_unreachable;
        break;
    case ENETDOWN:
    case ENETRESET:
        mapped = errc::network_down;
        break;
    case EPIPE:
#ifdef ESHUTDOWN
    case ESHUTDOWN:
#endif
        mapped = errc::pipe_broken;
        break;
    case EMSGSIZE:
        mapped = errc::message_too_long;
        break;
    case EMFILE:
    case ENFILE:
        mapped = errc::too_many_open;
        break;
    case EBADF:
    case ENOTSOCK:
        mapped = errc::invalid_state;
        break;
    case EFAULT:
        mapped = errc::invalid_argument;
        break;
    case EACCES:
    case EPERM:
        mapped = errc::not_supported;
        break;
    default:
        mapped = errc::io_error;
        break;
    }
    return make_error_code(mapped);
}

std::error_code lwip_err_to_error_code(int err_t_value) {
    if (err_t_value == ERR_OK) {
        return {};
    }
    if (err_t_value == ERR_MEM) {
        raise_no_mem();
    }
    errc mapped;
    switch (err_t_value) {
    case ERR_BUF:
        mapped = errc::netconn_buffer_error;
        break;
    case ERR_TIMEOUT:
        mapped = errc::timed_out;
        break;
    case ERR_RTE:
        mapped = errc::netconn_routing;
        break;
    case ERR_INPROGRESS:
        mapped = errc::in_progress;
        break;
    case ERR_VAL:
        mapped = errc::netconn_illegal_value;
        break;
    case ERR_WOULDBLOCK:
        mapped = errc::would_block;
        break;
    case ERR_USE:
        mapped = errc::address_in_use;
        break;
    case ERR_ALREADY:
        mapped = errc::in_progress;
        break;
    case ERR_ISCONN:
        mapped = errc::already_connected;
        break;
    case ERR_CONN:
        mapped = errc::not_connected;
        break;
    case ERR_IF:
        mapped = errc::network_down;
        break;
    case ERR_ABRT:
        mapped = errc::netconn_aborted;
        break;
    case ERR_RST:
        mapped = errc::netconn_reset;
        break;
    case ERR_CLSD:
        mapped = errc::netconn_closed;
        break;
    case ERR_ARG:
        mapped = errc::invalid_argument;
        break;
    default:
        mapped = errc::io_error;
        break;
    }
    return make_error_code(mapped);
}

std::error_code gai_to_error_code(int gai) {
    if (gai == EAI_MEMORY) {
        raise_no_mem();
    }
    switch (gai) {
    case 0:
        return {};
    case EAI_NONAME:
        return make_error_code(errc::name_not_found);
    case EAI_SERVICE:
        return make_error_code(errc::name_no_data);
    case EAI_FAIL:
        return make_error_code(errc::name_resolution);
    case EAI_FAMILY:
        return make_error_code(errc::not_supported);
    default:
        return make_error_code(errc::name_resolution);
    }
}

} // namespace net

} // namespace idfxx
