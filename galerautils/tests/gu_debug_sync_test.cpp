//
// Copyright (C) 2026 Codership Oy <info@codership.com>
//

// Regression test for MDEV-42139: gu_debug_sync_wait() blocked on a single,
// unlooped condition variable wait, so a spurious wakeup (allowed by POSIX
// for pthread_cond_wait() even without any matching signal) could release a
// waiter without gu_debug_sync_signal() ever having been called for it. In
// galera_3nodes.galera_join_with_cc_B this let a second wait on the same
// sync point name ("process_primary_configuration") disappear from
// wsrep_debug_sync_waiters between a wait_condition.inc poll and the test's
// own follow-up SELECT, producing an empty result instead of the point name.

#include "gu_debug_sync.hpp"
#include "gu_debug_sync_test.hpp"

#ifdef GU_DBUG_ON

#include "gu_threads.h"

#include <string>

namespace
{
    const char* const sync_point = "gu_debug_sync_test_point";

    void* wait_thread_fn(void* arg)
    {
        bool* const entered = static_cast<bool*>(arg);
        *entered = true;
        gu_debug_sync_wait(sync_point);
        return 0;
    }

    // Spin-waits (bounded) until 'pred' becomes true or the deadline passes.
    // Returns whether 'pred' was observed true.
    template <typename Pred>
    bool wait_until(Pred pred, int timeout_ms)
    {
        for (int waited = 0; waited < timeout_ms; waited += 10)
        {
            if (pred()) return true;
            struct timespec ts = { 0, 10 * 1000 * 1000 };
            nanosleep(&ts, 0);
        }
        return pred();
    }

    bool waiters_contain(const char* point)
    {
        return gu_debug_sync_waiters() == std::string(point);
    }
}

START_TEST(check_debug_sync_wait_signal)
{
    bool entered = false;
    gu_thread_t thr;
    gu_thread_create(NULL, &thr, wait_thread_fn, &entered);

    ck_assert_msg(wait_until([&entered]() { return entered; }, 1000),
                  "wait thread did not start");
    ck_assert_msg(wait_until(
                      []() { return waiters_contain(sync_point); }, 1000),
                  "waiter never registered");

    gu_debug_sync_signal(sync_point);

    ck_assert_msg(wait_until(
                      []() { return gu_debug_sync_waiters().empty(); }, 1000),
                  "waiter did not leave after a real signal");

    gu_thread_join(thr, 0);
}
END_TEST

START_TEST(check_debug_sync_survives_spurious_wakeup)
{
    bool entered = false;
    gu_thread_t thr;
    gu_thread_create(NULL, &thr, wait_thread_fn, &entered);

    ck_assert_msg(wait_until(
                      [&entered]() { return entered; }, 1000),
                  "wait thread did not start");
    ck_assert_msg(wait_until(
                      []() { return waiters_contain(sync_point); }, 1000),
                  "waiter never registered");

    // Simulate a spurious condition variable wakeup: the waiter's condvar
    // is signalled directly, without going through gu_debug_sync_signal(),
    // i.e. without the matching 'signalled' bookkeeping. A correct
    // implementation must keep the waiter blocked and registered.
    gu_debug_sync_test_spurious_wakeup(sync_point);

    // Give the (possibly woken) thread ample time to misbehave: with the
    // pre-fix, unlooped wait() this reliably makes the waiter disappear
    // from wsrep_debug_sync_waiters well within this window.
    struct timespec ts = { 0, 300 * 1000 * 1000 };
    nanosleep(&ts, 0);
    ck_assert_msg(waiters_contain(sync_point),
                  "waiter was released by a spurious wakeup, "
                  "not by gu_debug_sync_signal()");

    // Now release it for real and confirm it still responds normally.
    gu_debug_sync_signal(sync_point);
    ck_assert_msg(wait_until(
                      []() { return gu_debug_sync_waiters().empty(); }, 1000),
                  "waiter did not leave after the real signal");

    gu_thread_join(thr, 0);
}
END_TEST

Suite* gu_debug_sync_suite()
{
    Suite* s(suite_create("galerautils DebugSync"));
    TCase* tc(tcase_create("debug_sync"));

    suite_add_tcase(s, tc);
    tcase_add_test(tc, check_debug_sync_wait_signal);
    tcase_add_test(tc, check_debug_sync_survives_spurious_wakeup);

    return s;
}

#else // !GU_DBUG_ON

Suite* gu_debug_sync_suite()
{
    // GU_DBUG_ON is off for this build, so gu_debug_sync_wait()/signal()
    // are not even declared. Register an empty suite so the test runner
    // still lists it.
    return suite_create("galerautils DebugSync (disabled, GU_DBUG_ON off)");
}

#endif // GU_DBUG_ON
