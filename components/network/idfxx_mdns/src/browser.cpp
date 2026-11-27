// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#include "sdkconfig.h"

#ifdef CONFIG_MDNS_ENABLE_BROWSE

#include "internal.hpp"

#include <idfxx/error>
#include <idfxx/mdns>

#include <algorithm>
#include <cstdint>
#include <esp_log.h>
#include <mdns.h>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// The C library's browse notifier has no user argument and runs on the mDNS
// task with the service lock held, receiving a temporary result. Browsers are
// therefore kept in a file-static registry keyed, like the library's own
// browses, by type, protocol and subtype compared without regard to case. The
// notifier finds the browser there, deep-copies the result into an answer and
// calls the callback.
//
// Lock order: the notifier holds the service lock and takes registry_mtx, so
// registry_mtx is never held while calling an mdns_* function. A browser is
// registered before mdns_browse_new() (which reports cached instances as soon
// as the service lock is released), and unregistered after
// mdns_browse_delete() (which, under the service lock, stops any further
// notification for it).
//
// mdns_browse_delete() queues its action for the mDNS task, and fails with
// ESP_ERR_NO_MEM, leaving the browse running, while that queue is full. The
// destructor retries for a while. If the delete still fails, the entry stays
// registered without its callback (in `orphans`, until deinit()), so the
// running browse reaches no callback and a later browser with the same key
// fails with invalid_state, as it would while the browser existed.

namespace idfxx::mdns {

namespace {

using internal::TAG;

struct browse_entry {
    std::string type;
    enum protocol protocol;
    std::optional<std::string> subtype;
    browser::callback cb;
    uint32_t generation;

    [[nodiscard]] const char* subtype_c() const noexcept {
        return subtype ? subtype->c_str() : nullptr;
    }

    [[nodiscard]] bool matches(const char* t, const char* proto, const char* sub) const noexcept {
        if (t == nullptr || proto == nullptr || !internal::equal_ci(type, t) ||
            !internal::equal_ci(internal::protocol_label(protocol), proto)) {
            return false;
        }
        if (!subtype) {
            return sub == nullptr || *sub == '\0';
        }
        return sub != nullptr && internal::equal_ci(*subtype, sub);
    }
};

std::mutex registry_mtx;
std::vector<browse_entry*> registry;
// Entries of destroyed browsers whose browse could not be deleted. Guarded by
// registry_mtx.
std::vector<std::unique_ptr<browse_entry>> orphans;

// The C library's notifier. Runs on the mDNS task with the service lock held.
void on_browse_result(mdns_result_t* result) {
    if (result == nullptr) {
        return;
    }
    std::lock_guard lk(registry_mtx);
    auto it = std::ranges::find_if(registry, [&](const browse_entry* e) {
        return e->matches(result->service_type, result->proto, result->subtype);
    });
    if (it == registry.end() || !(*it)->cb) {
        return;
    }
    // The result is freed when this returns, so the callback gets a copy.
    const answer a = internal::to_answer(*result);
    const browse_entry* entry = *it;
    (void)invoke_guarded(TAG, "Browse callback", [&] { entry->cb(a); });
}

} // namespace

void internal::browsers_clear() {
    std::lock_guard lk(registry_mtx);
    registry.clear();
    orphans.clear();
}

struct browser::state {
    // Separately allocated so that it can outlive the browser (see orphans).
    std::unique_ptr<browse_entry> entry;
};

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
browser::browser(config cfg, callback cb)
    : browser(unwrap(make(std::move(cfg), std::move(cb)))) {}
#endif

browser::browser(std::unique_ptr<state> s) noexcept
    : _state(std::move(s)) {}

result<browser> browser::make(config cfg, callback cb) {
    if (!internal::running.load()) {
        return error(idfxx::errc::invalid_state);
    }
    if (!internal::valid_name(*cfg.type) || !cb ||
        (cfg.subtype && !internal::valid_subtype(*cfg.subtype))) {
        return error(idfxx::errc::invalid_arg);
    }

    auto s = std::make_unique<state>(state{
        .entry = std::make_unique<browse_entry>(browse_entry{
            .type = std::move(*cfg.type),
            .protocol = *cfg.protocol,
            .subtype = std::move(cfg.subtype),
            .cb = std::move(cb),
            .generation = internal::generation.load(),
        }),
    });
    browse_entry* entry = s->entry.get();
    const char* proto = internal::protocol_label(entry->protocol);

    {
        std::lock_guard lk(registry_mtx);
        if (std::ranges::any_of(registry, [&](const browse_entry* e) {
                return e->matches(entry->type.c_str(), proto, entry->subtype_c());
            })) {
            return error(idfxx::errc::invalid_state);
        }
        registry.push_back(entry);
    }

    mdns_browse_t* handle = mdns_browse_new_with_subtype(
        entry->type.c_str(), proto, entry->subtype_c(), on_browse_result
    );
    // The arguments are valid and no browse with this key runs (the registry
    // keeps the keys of browses that could not be deleted), so NULL means the
    // action queue is full (or an allocation failed, see
    // internal::wrap_limited()).
    if (handle == nullptr) {
        std::lock_guard lk(registry_mtx);
        std::erase(registry, entry);
        return error(errc::limit_reached);
    }
    return browser(std::move(s));
}

void browser::_release() noexcept {
    if (!_state) {
        return;
    }
    browse_entry* entry = _state->entry.get();
    bool registered;
    {
        std::lock_guard lk(registry_mtx);
        registered = std::ranges::find(registry, entry) != registry.end();
    }
    if (registered) {
        bool deleted = true;
        if (internal::live(entry->generation)) {
            const char* proto = internal::protocol_label(entry->protocol);
            // Returns ESP_FAIL when there is nothing to delete, and
            // ESP_ERR_NO_MEM while the action queue is full.
            esp_err_t err = internal::retry_while_queue_full([&] {
                return mdns_browse_delete_with_subtype(
                    entry->type.c_str(), proto, entry->subtype_c()
                );
            });
            if (err == ESP_ERR_NO_MEM) {
                deleted = false;
                ESP_LOGE(
                    TAG,
                    "Could not stop browsing %s.%s: action queue full",
                    entry->type.c_str(),
                    proto
                );
            }
        }
        std::lock_guard lk(registry_mtx);
        if (deleted) {
            std::erase(registry, entry);
        } else {
            // Under registry_mtx, so the notifier is not running it.
            entry->cb = nullptr;
            orphans.push_back(std::move(_state->entry));
        }
    }
    _state.reset();
}

browser::~browser() {
    _release();
}

browser::browser(browser&& other) noexcept = default;

browser& browser::operator=(browser&& other) noexcept {
    if (this != &other) {
        _release();
        _state = std::move(other._state);
    }
    return *this;
}

} // namespace idfxx::mdns

#endif // CONFIG_MDNS_ENABLE_BROWSE
