// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Cleanup that runs after every test, from Unity's tearDown() (see main.cpp). A failed
// Unity assertion longjmps out of the test, and destructors do not run. tearDown() still runs.

#pragma once

#include <vector>

namespace idfxx::test {

using cleanup_fn = void (*)();

namespace detail {

inline std::vector<cleanup_fn>& cleanups() {
    static std::vector<cleanup_fn> fns;
    return fns;
}

} // namespace detail

/// Registers fn to run after every test from now on, whether the test passed or failed. Registering
/// the same function again makes it the most recently registered function, and it still runs only
/// once. Cleanup runs in reverse order of registration. So if a test registers a cleanup as it
/// creates each thing, the things are cleaned up in the reverse order of creation. fn must do
/// nothing when there is nothing to clean up.
inline void run_after_each_test(cleanup_fn fn) {
    auto& fns = detail::cleanups();
    std::erase(fns, fn);
    fns.push_back(fn);
}

/// Runs the registered cleanup, most recently registered first.
inline void run_cleanups() {
    const auto& fns = detail::cleanups();
    for (auto it = fns.rbegin(); it != fns.rend(); ++it) {
        (*it)();
    }
}

} // namespace idfxx::test
