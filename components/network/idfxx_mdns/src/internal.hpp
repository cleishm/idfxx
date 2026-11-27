// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

#include <idfxx/error>
#include <idfxx/mdns>
#include <idfxx/sched>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <freertos/FreeRTOS.h>
#include <mdns.h>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace idfxx::mdns::internal {

inline constexpr const char* TAG = "idfxx::mdns";

// Whether the service is running. The C library answers most calls made before
// mdns_init() (or after mdns_free()) with ESP_ERR_INVALID_ARG rather than
// ESP_ERR_INVALID_STATE, and several calls (mdns_hostname_exists,
// mdns_service_exists, the service mutators) take its service lock without
// checking that it exists, which crashes. So every call checks this flag first.
// Atomic because it is read without a lock by every call. init() and deinit()
// write it under their own mutex.
extern std::atomic<bool> running;

// Incremented by every deinit(). Services, delegated hosts and browsers record
// it when created, and skip their C teardown if it has changed since, so an
// object that outlives deinit() cannot remove an identically named one created
// after the next init().
extern std::atomic<uint32_t> generation;

// Whether the service is running and has not been deinitialized since `gen` was recorded.
[[nodiscard]] inline bool live(uint32_t gen) noexcept {
    return running.load() && gen == generation.load();
}

// Converts the esp_err_t of an mdns_* call that can fail because a capacity
// is exhausted, not only because an allocation fails. Only these calls go
// through here:
//
// - mdns_service_add_for_host*(): CONFIG_MDNS_MAX_SERVICES services are in use.
// - mdns_register_netif(): all CONFIG_MDNS_MAX_INTERFACES slots are in use.
// - calls that pass work to the mDNS task through its action queue
//   (CONFIG_MDNS_ACTION_QUEUE_LEN), which mdns_priv_queue_action() posts to
//   without waiting: mdns_hostname_set(), mdns_instance_name_set(),
//   mdns_delegate_hostname_add() / _remove() / _set_address(), and the
//   blocking queries (mdns_query_generic(), mdns_query_a()).
//
// The C library reports each of these with ESP_ERR_NO_MEM, the same code as a
// failed allocation, so ESP_ERR_NO_MEM becomes errc::limit_reached here: a
// full queue is transient, and retrying later can succeed. A real allocation
// failure in these calls is reported the same way, as the codes cannot be
// told apart.
//
// mdns_query_async_new() and mdns_browse_new*() queue an action too, but
// return a pointer: once the wrapper has validated the arguments, NULL means
// a full queue or a failed allocation, and is reported as limit_reached by
// the same rule.
//
// Every other mdns_* call returns ESP_ERR_NO_MEM only when an allocation
// fails, and goes through core wrap() / error(esp_err_t), which applies the
// usual out-of-memory policy (std::bad_alloc, or abort).
[[nodiscard]] inline result<void> wrap_limited(esp_err_t e) {
    if (e == ESP_ERR_NO_MEM) {
        return error(errc::limit_reached);
    }
    return wrap(e);
}

// Retries a call that queues an action for the mDNS task while the queue is
// full, for about 100 ms, so that teardown in a destructor survives a burst.
// Returns the last result.
template<typename F>
[[nodiscard]] esp_err_t retry_while_queue_full(F&& f) noexcept {
    constexpr TickType_t max_ticks = pdMS_TO_TICKS(100) > 0 ? pdMS_TO_TICKS(100) : 1;
    esp_err_t err = f();
    for (TickType_t waited = 0; err == ESP_ERR_NO_MEM && waited < max_ticks; ++waited) {
        idfxx::delay(idfxx::next_tick);
        err = f();
    }
    return err;
}

// Longest hostname, instance name, delegated host name, service type or
// query name accepted. Each is a single DNS label, which holds at most 63
// bytes. The C library writes a name's length as its label length byte
// (append_string(), mdns_send.c) without checking, and keeps at most
// MDNS_NAME_BUF_LEN - 1 characters (silently truncating longer ones).
inline constexpr size_t max_name_len = 63;
static_assert(MDNS_NAME_BUF_LEN - 1 >= max_name_len);

// Longest subtype accepted (RFC 6763 section 7.2), as the C library's browser
// enforces. Services use the same limit.
inline constexpr size_t max_subtype_len = 63;

// The C library takes names as NUL-terminated strings, so a name with a NUL
// in it would be cut short there.
[[nodiscard]] inline bool valid_label(std::string_view s, size_t max_len) noexcept {
    return !s.empty() && s.size() <= max_len && s.find('\0') == std::string_view::npos;
}

[[nodiscard]] inline bool valid_name(std::string_view s) noexcept {
    return valid_label(s, max_name_len);
}

[[nodiscard]] inline bool valid_subtype(std::string_view s) noexcept {
    return valid_label(s, max_subtype_len);
}

// Whether the C library treats two subtypes as the same. It compares them
// with strcmp() (mdns_responder.c), so with regard to case.
[[nodiscard]] inline bool same_subtype(const std::string& a, const std::string& b) noexcept {
    return std::strcmp(a.c_str(), b.c_str()) == 0;
}

// mdns_hostname_set() and mdns_delegate_hostname_add() each queue an action
// and then wait on one binary semaphore shared by both, which the mDNS task
// gives once per such action. Two concurrent callers could each take the
// other's signal and return before their own action ran, so both calls are
// made only while holding this mutex. Neither the mDNS task nor any notifier
// takes it. It is also held across delegated_host::make()'s and
// try_set_hostname()'s checks of each other's names. Lock order:
// action_sema_mtx -> the delegated host names' mutex (service.cpp).
extern std::mutex action_sema_mtx;

// Whether a delegated host this wrapper added (and the C library still
// answers for) has this name, compared without regard to case.
[[nodiscard]] bool is_delegated_hostname(std::string_view name);

[[nodiscard]] bool equal_ci(std::string_view a, std::string_view b) noexcept;

[[nodiscard]] const char* protocol_label(enum protocol p) noexcept;
[[nodiscard]] std::optional<enum protocol> parse_protocol(const char* label) noexcept;

[[nodiscard]] answer to_answer(const mdns_result_t& r);
[[nodiscard]] std::vector<answer> to_answers(const mdns_result_t* r);

struct results_deleter {
    void operator()(mdns_result_t* r) const noexcept { mdns_query_results_free(r); }
};
using results_ptr = std::unique_ptr<mdns_result_t, results_deleter>;

[[nodiscard]] bool valid_txt_item(const txt_item& item) noexcept;
[[nodiscard]] bool valid_txt(std::span<const txt_item> items) noexcept;

// C views of TXT items. They point into `items`, which must outlive them.
[[nodiscard]] std::vector<mdns_txt_item_with_value_len_t> to_c_txt(std::span<const txt_item> items);

// C views of subtypes. They point into `subtypes`, which must outlive them.
[[nodiscard]] std::vector<mdns_subtype_item_t> to_c_subtypes(std::span<const std::string> subtypes);
// At most 255 valid subtypes, with no two the same (see same_subtype()).
[[nodiscard]] bool valid_subtypes(std::span<const std::string> subtypes) noexcept;

// A linked address list for the C API. Nodes point at each other, so the
// vector must not be resized after it is built.
[[nodiscard]] std::vector<mdns_ip_addr_t>
to_c_addresses(std::span<const net::ipv4_addr> ipv4, std::span<const net::ipv6_addr> ipv6);

// Lifecycle hooks, called by init() and deinit().
[[nodiscard]] result<void> queries_init();
void queries_shutdown();
void hosts_clear();
void services_clear();
#ifdef CONFIG_MDNS_ENABLE_BROWSE
void browsers_clear();
#endif

} // namespace idfxx::mdns::internal
