// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Cleanup that runs after every test, from Unity's tearDown() (see main.cpp). A failed
// Unity assertion longjmps out of the test, skipping destructors; tearDown() still runs.

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
/// the same function again makes it the most recently registered, without running it twice, so that
/// registering cleanup as each thing is created has it undone in the reverse order. fn must do
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
