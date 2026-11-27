// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#include "answer_fill.hpp"
#include "internal.hpp"

#include <idfxx/error>
#include <idfxx/future>
#include <idfxx/mdns>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <limits>
#include <mdns.h>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// Asynchronous queries (start_query)
// ==================================
//
// The C library's asynchronous search has an awkward lifecycle:
//
// - Its notifier has no user argument, and runs on the mDNS task while that
//   task holds the mDNS service lock (search_finish(), mdns_querier.c). It is
//   called just before the search's done semaphore is given.
// - The results are fetched once: mdns_query_async_get_results() takes that
//   binary semaphore.
// - Once finished, the search is detached from the library's list, so
//   mdns_free() no longer frees it. Only mdns_query_async_delete() (search
//   only) and mdns_query_results_free() (results) do. Both take the service
//   lock, so neither may be called from the notifier, and both require the
//   lock to still exist, which mdns_free() destroys.
//
// So the wrapper keeps a record per search in a file-static registry. The
// notifier finds the record by the search pointer that the record holds:
//
// - The notifier marks the record finished and wakes its waiters. It queues
//   the record for the reaper, which collects it at once: under the record's
//   collect mutex, the reaper fetches the results, converts them, frees them,
//   deletes the search and keeps the answers for every wait.
// - A waiter waits for the finished flag, then takes the collect mutex. It
//   finds the answers that the reaper collected, or collects them itself if
//   it takes the collect mutex first. The record stays in the registry until
//   the collection is done, so deinit() sees it and waits for it (on the
//   collect mutex) before calling mdns_free().
// - Dropping the last copy of the future never blocks. If the search has
//   finished and is not collected yet, its pointer is queued for the reaper
//   to free. Otherwise the record is marked abandoned, and the notifier
//   queues the search to free when it finishes. Ownership of the search
//   pointer moves out of the record exactly once, under registry_mtx, so it
//   is freed exactly once. The record's search pointer is NULL from then on,
//   so the notifier can't match a later search at the same address.
// - The reaper is a one-shot esp_timer (dispatched on the esp_timer task, the
//   same task the C library's own timer locks the service from). It frees
//   the queued searches and collects the queued records.
// - deinit() waits for the searches still running to finish (each ends at its
//   timeout, driven by the C library's timer), collects them into their
//   records so live futures still complete, frees and collects the queued
//   ones, and only then calls mdns_free().
//
// The conversion reads the interface that each answer arrived on (esp_netif).
// The C library holds only its raw pointer, so the interface can be destroyed
// before the conversion. Collecting at once on the reaper narrows this window,
// but does not close it: the C library holds the raw pointer while the search
// runs, and the blocking queries have the same exposure.
//
// Deadlock freedom rests on one rule: registry_mtx and each record's mtx are
// leaves. Nothing calls an mdns_* function that takes the service lock while
// holding either (mdns_query_async_new() takes no lock, and only queues an
// action without waiting). The notifier holds the service lock and takes
// them, so lock order is always service lock -> registry_mtx -> record mtx.
// reap_mtx and the collect mutexes are only ever taken without the service
// lock, before calling into the library, in the order reap_mtx -> collect
// mutex -> registry_mtx. The reaper takes a record off its queue under
// registry_mtx and releases it before it takes the record's collect mutex.
//
// Fetching the results after the notifier has run cannot block for long:
// search_finish() gives the semaphore right after the notifier returns.
//
// The reaper converts the answers on the esp_timer task, whose stack is 3584
// bytes by default (CONFIG_ESP_TIMER_TASK_STACK_SIZE). The conversion copies
// strings and addresses into vectors and calls no deep code, so it is
// expected to fit (by inspection, not measured).

namespace idfxx::mdns {

namespace {

using internal::TAG;

struct query_args {
    std::optional<std::string> name;
    std::optional<std::string> service;
    std::optional<enum protocol> protocol;
    enum record_type rtype = record_type::any;
    uint32_t timeout_ms = 0;
    uint8_t max_results = 0; // 0 collects until the timeout

    [[nodiscard]] const char* name_c() const noexcept { return name ? name->c_str() : nullptr; }
    [[nodiscard]] const char* service_c() const noexcept {
        return service ? service->c_str() : nullptr;
    }
    [[nodiscard]] const char* proto_c() const noexcept {
        return protocol ? internal::protocol_label(*protocol) : nullptr;
    }
    [[nodiscard]] uint16_t type_c() const noexcept { return std::to_underlying(rtype); }
};

// Converts the C library's results for a query to answers.
std::vector<answer> to_query_answers(const mdns_result_t* results, const query_args& args) {
    auto answers = internal::to_answers(results);
    internal::fill_instance_fields(answers, args.rtype, args.name, args.service, args.protocol);
    return answers;
}

struct query_record {
    explicit query_record(query_args a) noexcept
        : args(std::move(a)) {}

    // The search, until someone takes ownership to free it. Guarded by
    // registry_mtx. The notifier finds the record by it: it is set to NULL
    // under registry_mtx before the search is freed, so a later search that
    // the C library allocates at the same address can't match this record.
    mdns_search_once_t* search = nullptr;
    // The last future copy is gone. Guarded by registry_mtx.
    bool abandoned = false;
    // What the search asked for, to complete its answers.
    const query_args args;

    // Set by the notifier under mtx. Also read without it by done().
    std::atomic<bool> finished{false};
    std::mutex mtx;
    std::condition_variable cv;

    // Serializes collection, and guards value.
    std::mutex collect_mtx;
    std::optional<result<std::vector<answer>>> value;
};

std::mutex registry_mtx;
std::vector<std::shared_ptr<query_record>> records;
// Finished searches that nobody waits for, to free.
std::vector<mdns_search_once_t*> pending_reap;
// Finished records with a live future, to collect. Its capacity is kept at
// least its size plus the number of records, so that the notifier never
// allocates when it adds a record.
std::vector<std::shared_ptr<query_record>> pending_collect;

// Held while the reaper frees or collects queued searches, so deinit() can
// wait out the reaper.
std::mutex reap_mtx;
// Created on the first init() and kept for the life of the program.
esp_timer_handle_t reaper = nullptr;

// Takes the finished search's results. Only called once the notifier has run
// for the search, so the semaphore is given almost at once.
internal::results_ptr take_results(mdns_search_once_t* search) {
    mdns_result_t* raw = nullptr;
    while (!mdns_query_async_get_results(search, 10, &raw, nullptr)) {
    }
    return internal::results_ptr(raw);
}

// Deletes a search whose results have been taken.
void delete_search(mdns_search_once_t* search) {
    if (mdns_query_async_delete(search) != ESP_OK) {
        ESP_LOGW(TAG, "Could not delete a finished query");
    }
}

// Fetches the finished search's answers, frees its results and deletes it.
// Must not be called with the service lock held.
std::vector<answer> collect_search(mdns_search_once_t* search, const query_args& args) {
    // The temporary frees the results at the end of the statement.
    auto answers = to_query_answers(take_results(search).get(), args);
    delete_search(search);
    return answers;
}

// Frees a finished search nobody is waiting for.
// Must not be called with the service lock held.
void free_search(mdns_search_once_t* search) {
    take_results(search).reset();
    delete_search(search);
}

// Collects a finished record's answers into its value, if no one has taken
// its search. Called with rec->collect_mtx held, and without registry_mtx or
// the service lock. The search is taken under registry_mtx, but the record
// stays registered until the answers are collected, so that deinit() waits
// for this collection (on the collect mutex) before mdns_free().
void collect_record(const std::shared_ptr<query_record>& rec) {
    mdns_search_once_t* search;
    {
        std::lock_guard lk(registry_mtx);
        search = std::exchange(rec->search, nullptr);
    }
    if (search != nullptr) {
        rec->value = collect_search(search, rec->args);
    }
    std::lock_guard lk(registry_mtx);
    std::erase(records, rec);
}

// Frees every queued search and collects every queued record. Takes
// reap_mtx. Must not be called with registry_mtx, a collect mutex or the
// service lock held.
void drain_pending() {
    std::lock_guard rl(reap_mtx);
    for (;;) {
        mdns_search_once_t* search = nullptr;
        std::shared_ptr<query_record> rec;
        {
            std::lock_guard lk(registry_mtx);
            if (!pending_reap.empty()) {
                search = pending_reap.back();
                pending_reap.pop_back();
            } else if (!pending_collect.empty()) {
                rec = std::move(pending_collect.back());
                pending_collect.pop_back();
            } else {
                return;
            }
        }
        if (search != nullptr) {
            free_search(search);
        } else {
            // A waiter that took the collect mutex first has collected the
            // answers already. If the last future copy went away first,
            // release() took the search, and there is nothing to collect.
            std::lock_guard cl(rec->collect_mtx);
            if (!rec->value) {
                collect_record(rec);
            }
        }
    }
}

void reaper_cb(void* /*arg*/) {
    drain_pending();
}

// Called with registry_mtx held. Arming an esp_timer never blocks. If it is
// already armed, the pending run handles the new entry too.
void kick_reaper() noexcept {
    if (reaper != nullptr) {
        (void)esp_timer_start_once(reaper, 1000);
    }
}

// Called with registry_mtx held. Removes the record and returns its search,
// if no one has taken it yet.
mdns_search_once_t* take_search(const std::shared_ptr<query_record>& rec) noexcept {
    mdns_search_once_t* search = std::exchange(rec->search, nullptr);
    std::erase(records, rec);
    return search;
}

// Waits on a condition variable for at most `timeout`, in steps of at most an
// hour. ESP-IDF's pthread_cond_timedwait() converts its timeout to
// milliseconds in a 32-bit long. A longer one fails at once with ETIMEDOUT
// without releasing the mutex, and wait_for() then spins holding it. The
// remaining time is counted down rather than added to the clock, so even
// milliseconds::max() cannot overflow.
template<typename Pred>
bool wait_up_to(
    std::condition_variable& cv,
    std::unique_lock<std::mutex>& lk,
    std::chrono::milliseconds timeout,
    Pred pred
) {
    constexpr std::chrono::milliseconds max_step = std::chrono::hours(1);
    for (;;) {
        const auto step = std::min(timeout, max_step);
        if (cv.wait_for(lk, step, pred)) {
            return true;
        }
        // wait_for() returned false, so at least `step` has passed.
        timeout -= step;
        if (timeout <= std::chrono::milliseconds::zero()) {
            return false;
        }
    }
}

// The C library's notifier. Runs on the mDNS task with the service lock held.
void on_query_done(mdns_search_once_t* search) {
    std::lock_guard lk(registry_mtx);
    // Matched on the search that the record still holds, not the one it was
    // created for: see query_record::search.
    auto it = std::ranges::find_if(records, [&](const auto& r) { return r->search == search; });
    if (it == records.end()) {
        return;
    }
    std::shared_ptr<query_record> rec = *it;
    {
        std::lock_guard rl(rec->mtx);
        rec->finished.store(true);
    }
    rec->cv.notify_all();
    if (rec->abandoned) {
        if (mdns_search_once_t* s = take_search(rec)) {
            pending_reap.push_back(s);
        }
    } else {
        // Collected at once, while the interface that each answer arrived on
        // most likely still exists. Does not allocate: see pending_collect.
        pending_collect.push_back(std::move(rec));
    }
    kick_reaper();
}

// The last copy of a future is gone.
void release(const std::shared_ptr<query_record>& rec) noexcept {
    std::lock_guard lk(registry_mtx);
    if (rec->search == nullptr) {
        return;
    }
    if (rec->finished.load()) {
        pending_reap.push_back(take_search(rec));
        kick_reaper();
    } else {
        rec->abandoned = true;
    }
}

// Waits for the search to finish, then collects its answers once.
result<std::vector<answer>> wait_record(
    const std::shared_ptr<query_record>& rec,
    std::optional<std::chrono::milliseconds> timeout
) {
    {
        std::unique_lock lk(rec->mtx);
        auto finished = [&] { return rec->finished.load(); };
        if (!timeout) {
            rec->cv.wait(lk, finished);
        } else if (!wait_up_to(rec->cv, lk, *timeout, finished)) {
            return error(idfxx::errc::timeout);
        }
    }
    // The reaper usually has collected the answers already. If this waiter
    // takes the collect mutex first, it collects them itself.
    std::lock_guard cl(rec->collect_mtx);
    if (!rec->value) {
        collect_record(rec);
        if (!rec->value) {
            rec->value = error(idfxx::errc::invalid_state);
        }
    }
    return *rec->value;
}

// Owned by the future's waiter. Its destruction is the last copy going away.
class query_handle {
public:
    explicit query_handle(std::shared_ptr<query_record> rec) noexcept
        : _rec(std::move(rec)) {}

    query_handle(const query_handle&) = delete;
    query_handle& operator=(const query_handle&) = delete;

    query_handle(query_handle&& other) noexcept = default;
    query_handle& operator=(query_handle&& other) = delete;

    ~query_handle() {
        if (_rec) {
            release(_rec);
        }
    }

    [[nodiscard]] const std::shared_ptr<query_record>& record() const noexcept { return _rec; }

private:
    std::shared_ptr<query_record> _rec;
};

// The C library takes the timeout as 32-bit milliseconds. Timeouts are capped
// at INT32_MAX, the documented maximum. The C library's deadline (start time
// plus timeout, mdns_querier.c) is also 32-bit milliseconds, and wraps when
// the uptime plus the timeout exceeds 2^32 ms (about 49.7 days), ending the
// search early. No cap can prevent that, so it is documented as a limitation.
result<uint32_t> to_timeout_ms(std::chrono::milliseconds timeout) {
    static_assert(detail::max_timeout.count() == std::numeric_limits<int32_t>::max());
    if (timeout.count() <= 0 || timeout > detail::max_timeout) {
        return error(idfxx::errc::invalid_arg);
    }
    return static_cast<uint32_t>(timeout.count());
}

result<query_args> validate(const query_config& cfg) {
    if (!internal::running.load()) {
        return error(idfxx::errc::invalid_state);
    }
    // The fields that each record type needs (see query_config).
    const enum record_type rtype = *cfg.record_type;
    const bool needs_service =
        rtype == record_type::ptr || rtype == record_type::srv || rtype == record_type::txt;
    const bool needs_name = rtype == record_type::srv || rtype == record_type::txt ||
        rtype == record_type::a || rtype == record_type::aaaa;
    if ((needs_service && !cfg.service_type) || (needs_name && !cfg.name) ||
        cfg.service_type.has_value() != cfg.protocol.has_value() ||
        (cfg.name && !internal::valid_name(*cfg.name)) ||
        (cfg.service_type && !internal::valid_name(*cfg.service_type)) ||
        (cfg.max_results && *cfg.max_results == 0)) {
        return error(idfxx::errc::invalid_arg);
    }
    auto timeout = to_timeout_ms(cfg.timeout);
    if (!timeout) {
        return error(timeout.error());
    }
    return query_args{
        .name = cfg.name,
        .service = cfg.service_type,
        .protocol = cfg.protocol,
        .rtype = *cfg.record_type,
        .timeout_ms = *timeout,
        .max_results = cfg.max_results.value_or(0),
    };
}

// The C library's default, which mdns_query_async_new() always uses.
enum transmission default_transmission(enum record_type type) noexcept {
    return type == record_type::ptr ? transmission::multicast : transmission::unicast;
}

result<std::vector<answer>> run_query(const query_config& cfg, enum transmission t) {
    auto args = validate(cfg);
    if (!args) {
        return error(args.error());
    }
    mdns_result_t* raw = nullptr;
    esp_err_t err = mdns_query_generic(
        args->name_c(),
        args->service_c(),
        args->proto_c(),
        args->type_c(),
        static_cast<mdns_query_transmission_type_t>(std::to_underlying(t)),
        args->timeout_ms,
        args->max_results,
        &raw
    );
    internal::results_ptr results(raw);
    // ESP_ERR_NO_MEM when the action queue is full.
    if (auto r = internal::wrap_limited(err); !r) {
        return error(r.error());
    }
    return to_query_answers(results.get(), *args);
}

} // namespace

result<void> internal::queries_init() {
    std::lock_guard lk(registry_mtx);
    if (reaper != nullptr) {
        return {};
    }
    const esp_timer_create_args_t args = {
        .callback = reaper_cb,
        .arg = nullptr,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "idfxx_mdns_reap",
        .skip_unhandled_events = true,
    };
    return wrap(esp_timer_create(&args, &reaper));
}

void internal::queries_shutdown() {
    // running is already false, so no new record can be added: try_start_query
    // checks it under registry_mtx.
    std::vector<std::shared_ptr<query_record>> snapshot;
    {
        std::lock_guard lk(registry_mtx);
        snapshot = records;
    }
    for (const auto& rec : snapshot) {
        {
            std::unique_lock lk(rec->mtx);
            rec->cv.wait(lk, [&] { return rec->finished.load(); });
        }
        // The notifier took an abandoned record's search when it finished, so
        // the search is queued for the reaper and drain_pending() frees it below.
        std::lock_guard cl(rec->collect_mtx);
        if (!rec->value) {
            collect_record(rec);
        }
    }
    drain_pending();
}

// =============================================================================
// Blocking queries
// =============================================================================

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
std::vector<answer> query(const query_config& cfg) {
    return unwrap(try_query(cfg));
}

std::vector<answer> query(const query_config& cfg, enum transmission t) {
    return unwrap(try_query(cfg, t));
}

idfxx::future<std::vector<answer>> start_query(const query_config& cfg) {
    return unwrap(try_start_query(cfg));
}
#endif

result<std::vector<answer>> try_query(const query_config& cfg) {
    return run_query(cfg, default_transmission(*cfg.record_type));
}

result<std::vector<answer>> try_query(const query_config& cfg, enum transmission t) {
    return run_query(cfg, t);
}

result<idfxx::future<std::vector<answer>>> try_start_query(const query_config& cfg) {
    auto args = validate(cfg);
    if (!args) {
        return error(args.error());
    }

    auto rec = std::make_shared<query_record>(std::move(*args));
    {
        // Held across mdns_query_async_new() so the notifier, which may run as
        // soon as the search is queued, finds the record. The call takes no
        // service lock and does not block, so this cannot deadlock.
        std::lock_guard lk(registry_mtx);
        if (!internal::running.load()) {
            return error(idfxx::errc::invalid_state);
        }
        // Reserved first, so that nothing allocates once the search exists.
        records.reserve(records.size() + 1);
        pending_collect.reserve(pending_collect.size() + records.size() + 1);
        const query_args& a = rec->args;
        mdns_search_once_t* search = mdns_query_async_new(
            a.name_c(),
            a.service_c(),
            a.proto_c(),
            a.type_c(),
            a.timeout_ms,
            a.max_results,
            on_query_done
        );
        // The arguments are valid, so NULL means the action queue is full
        // (or an allocation failed, see internal::wrap_limited()).
        if (search == nullptr) {
            return error(errc::limit_reached);
        }
        rec->search = search;
        records.push_back(rec);
    }

    return idfxx::future<std::vector<answer>>(
        [handle = query_handle(rec)](std::optional<std::chrono::milliseconds> timeout) {
            return wait_record(handle.record(), timeout);
        },
        [rec]() noexcept { return rec->finished.load(); }
    );
}

// =============================================================================
// Typed queries
// =============================================================================

result<std::vector<answer>> detail::query_ptr(
    std::string_view type,
    enum protocol protocol,
    std::chrono::milliseconds timeout,
    std::optional<uint8_t> max_results
) {
    if (!internal::valid_name(type)) {
        return error(idfxx::errc::invalid_arg);
    }
    return try_query({
        .service_type = std::string(type),
        .protocol = protocol,
        .record_type = record_type::ptr,
        .timeout = timeout,
        .max_results = max_results,
    });
}

namespace {

result<std::optional<answer>> query_instance(
    enum record_type rtype,
    std::string_view instance_name,
    std::string_view type,
    enum protocol protocol,
    std::chrono::milliseconds timeout
) {
    if (!internal::valid_name(instance_name) || !internal::valid_name(type)) {
        return error(idfxx::errc::invalid_arg);
    }
    auto answers = try_query({
        .name = std::string(instance_name),
        .service_type = std::string(type),
        .protocol = protocol,
        .record_type = rtype,
        .timeout = timeout,
        .max_results = 1,
    });
    if (!answers) {
        return error(answers.error());
    }
    if (answers->empty()) {
        return std::optional<answer>{};
    }
    return std::optional<answer>(std::move(answers->front()));
}

} // namespace

result<std::optional<answer>> detail::query_srv(
    std::string_view instance_name,
    std::string_view type,
    enum protocol protocol,
    std::chrono::milliseconds timeout
) {
    return query_instance(record_type::srv, instance_name, type, protocol, timeout);
}

result<std::optional<answer>> detail::query_txt(
    std::string_view instance_name,
    std::string_view type,
    enum protocol protocol,
    std::chrono::milliseconds timeout
) {
    return query_instance(record_type::txt, instance_name, type, protocol, timeout);
}

#ifdef CONFIG_LWIP_IPV4
result<std::optional<net::ipv4_addr>>
detail::query_a(std::string_view hostname, std::chrono::milliseconds timeout) {
    if (!internal::running.load()) {
        return error(idfxx::errc::invalid_state);
    }
    auto timeout_ms = to_timeout_ms(timeout);
    if (!internal::valid_name(hostname) || !timeout_ms) {
        return error(idfxx::errc::invalid_arg);
    }
    esp_ip4_addr_t addr{};
    esp_err_t err = mdns_query_a(std::string(hostname).c_str(), *timeout_ms, &addr);
    if (err == ESP_ERR_NOT_FOUND) {
        return std::optional<net::ipv4_addr>{};
    }
    if (auto r = internal::wrap_limited(err); !r) {
        return error(r.error());
    }
    return std::optional<net::ipv4_addr>(net::ipv4_addr(addr.addr));
}
#endif

#ifdef CONFIG_LWIP_IPV6
// Not mdns_query_aaaa(), which drops the interface the answer arrived on, so
// a link-local address would come back without its zone. The answer
// conversion sets it.
result<std::optional<net::ipv6_addr>>
detail::query_aaaa(std::string_view hostname, std::chrono::milliseconds timeout) {
    // try_query() validates the name and timeout.
    auto answers = try_query({
        .name = std::string(hostname),
        .record_type = record_type::aaaa,
        .timeout = timeout,
        .max_results = 1,
    });
    if (!answers) {
        return error(answers.error());
    }
    for (const auto& a : *answers) {
        if (!a.ipv6_addresses.empty()) {
            return std::optional<net::ipv6_addr>(a.ipv6_addresses.front());
        }
    }
    return std::optional<net::ipv6_addr>{};
}
#endif

} // namespace idfxx::mdns
