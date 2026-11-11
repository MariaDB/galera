//
// Copyright (C) 2014 Codership Oy <info@codership.com>
//

#ifdef GU_DBUG_ON

#include "gu_debug_sync.hpp"
#include "gu_lock.hpp"
#include <map>


namespace {
    gu::Mutex sync_mutex(0);
    struct Waiter
    {
        gu::Cond cond;
        bool     signalled;
        Waiter() : cond(0), signalled(false) { }
    };
    typedef std::multimap<std::string, Waiter*> SyncMap;
    SyncMap sync_waiters;
}



void gu_debug_sync_wait(const std::string& sync)
{
    gu::Lock lock(sync_mutex);
    Waiter waiter;
    log_debug << "enter sync wait '" << sync << "'";
    SyncMap::iterator i(
        sync_waiters.insert(std::make_pair(sync, &waiter)));
    // pthread_cond_wait() (and hence gu::Lock::wait()) may return without
    // any matching signal (a spurious wakeup), so the real predicate must
    // be re-checked in a loop rather than trusting a single wait() call.
    while (!waiter.signalled)
    {
        lock.wait(waiter.cond);
    }
    sync_waiters.erase(i);
    log_debug << "leave sync wait '" << sync << "'";
}


void gu_debug_sync_signal(const std::string& sync)
{
    gu::Lock lock(sync_mutex);
    std::pair<SyncMap::iterator, SyncMap::iterator>
        range(sync_waiters.equal_range(sync));
    for (SyncMap::iterator i(range.first); i != range.second; ++i)
    {
        log_debug << "signalling waiter";
        i->second->signalled = true;
        i->second->cond.signal();
    }
}


void gu_debug_sync_test_spurious_wakeup(const std::string& sync)
{
    gu::Lock lock(sync_mutex);
    std::pair<SyncMap::iterator, SyncMap::iterator>
        range(sync_waiters.equal_range(sync));
    for (SyncMap::iterator i(range.first); i != range.second; ++i)
    {
        i->second->cond.signal();
    }
}


std::string gu_debug_sync_waiters()
{
    std::string ret;
    gu::Lock lock(sync_mutex);
    for (SyncMap::iterator i(sync_waiters.begin());
         i != sync_waiters.end();)
    {
        ret += i->first;
        ++i;
        if (i != sync_waiters.end()) ret += " ";
    }
    return ret;
}

#endif // GU_DBUG_ON
