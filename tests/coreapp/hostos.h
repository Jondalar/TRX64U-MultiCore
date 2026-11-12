// OsPort for the native tests (T104.1): threads for the executor and the poll tasks, and a virtual clock for the scenarios that are
// about time (S8), where a real clock would make the verdict depend on the load of the machine.
#ifndef TESTS_COREAPP_HOSTOS_H
#define TESTS_COREAPP_HOSTOS_H

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>
#include "exec.h"

class HostOs : public OsPort {
public:
    struct Sem {
        std::mutex m;
        std::condition_variable cv;
        int n = 0;
    };
    ~HostOs() { for (auto &t : threads_) { if (t.joinable()) { t.join(); } } }

    void *mutex_new() { return new std::mutex(); }
    void lock(void *m) { static_cast<std::mutex *>(m)->lock(); }
    void unlock(void *m) { static_cast<std::mutex *>(m)->unlock(); }
    void *sem_new() { return new Sem(); }
    void give(void *s)
    {
        Sem *q = static_cast<Sem *>(s);
        std::lock_guard<std::mutex> g(q->m);
        q->n = 1;                                           // binary, as the board's: a give is remembered once
        q->cv.notify_one();
    }
    bool take(void *s, uint32_t ms)
    {
        Sem *q = static_cast<Sem *>(s);
        std::unique_lock<std::mutex> g(q->m);
        if (ms == OS_FOREVER) {
            q->cv.wait(g, [q] { return q->n > 0; });
        } else if (!q->cv.wait_for(g, std::chrono::milliseconds(ms), [q] { return q->n > 0; })) {
            return false;
        }
        q->n--;
        return true;
    }
    bool task_new(const char *, void (*fn)(void *), void *arg, int, uint32_t)
    {
        threads_.emplace_back([fn, arg] { fn(arg); });
        return true;
    }
    void *self() { static thread_local char marker; return &marker; }
    uint32_t now_ms()
    {
        using namespace std::chrono;
        return (uint32_t)duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
    }
    void sleep_ms(uint32_t ms) { if (ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); } else { std::this_thread::yield(); } }
    void join_all() { for (auto &t : threads_) { if (t.joinable()) { t.join(); } } }

private:
    std::vector<std::thread> threads_;
};

// Time the test moves by hand. No tasks, no blocking: a scenario calls PollTask::step() itself when its virtual clock says so.
class VirtOs : public OsPort {
public:
    uint32_t t = 0;
    void *mutex_new() { return this; }
    void lock(void *) { }
    void unlock(void *) { }
    void *sem_new() { return this; }
    void give(void *) { }
    bool take(void *, uint32_t) { return true; }
    bool task_new(const char *, void (*)(void *), void *, int, uint32_t) { return false; }
    void *self() { return this; }
    uint32_t now_ms() { return t; }
    void sleep_ms(uint32_t) { }
};

#endif
