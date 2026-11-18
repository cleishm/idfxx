// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#include <idfxx/task>
#include <unity.h>

#include <atomic>
#include <chrono>
#include <idfxx/chrono>
#include <idfxx/sched>
#include <memory>
#include <system_error>

using namespace idfxx;
using namespace std::chrono_literals;

TEST_CASE("task error category name", "[idfxx][task][error]") {
    TEST_ASSERT_EQUAL_STRING("task::Error", task_category().name());
}

TEST_CASE("task::errc::would_deadlock has a message", "[idfxx][task][error]") {
    auto ec = make_error_code(task::errc::would_deadlock);
    TEST_ASSERT_EQUAL_STRING("task::Error", ec.category().name());
    TEST_ASSERT_FALSE(ec.message().empty());
}

TEST_CASE("task::errc::would_deadlock equivalent to std::errc::resource_deadlock_would_occur",
         "[idfxx][task][error]") {
    auto ec = make_error_code(task::errc::would_deadlock);
    TEST_ASSERT_TRUE(ec == task::errc::would_deadlock);
    TEST_ASSERT_TRUE(ec == std::errc::resource_deadlock_would_occur);
}

TEST_CASE("try_join from inside the task itself returns would_deadlock", "[idfxx][task][error][join]") {
    std::shared_ptr<task> t;
    std::atomic<bool> done{false};
    std::atomic<bool> code_matches_errc{false};
    std::atomic<bool> code_matches_std{false};
    std::atomic<bool> category_matches{false};

    t = std::make_shared<task>(
        task::config{.name = "self_join"},
        [&](task::self&) {
            // Wait briefly so the shared_ptr is fully assigned in the parent.
            idfxx::delay(20ms);
            auto r = t->try_join();
            if (!r.has_value()) {
                code_matches_errc.store(r.error() == task::errc::would_deadlock);
                code_matches_std.store(r.error() == std::errc::resource_deadlock_would_occur);
                category_matches.store(&r.error().category() == &task_category());
            }
            done.store(true);
        }
    );

    // Wait for task to finish observing the deadlock
    for (int i = 0; i < 100 && !done.load(); ++i) {
        idfxx::delay(10ms);
    }
    TEST_ASSERT_TRUE(done.load());
    TEST_ASSERT_TRUE(code_matches_errc.load());
    TEST_ASSERT_TRUE(code_matches_std.load());
    TEST_ASSERT_TRUE(category_matches.load());
}
