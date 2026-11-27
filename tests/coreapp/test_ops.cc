// T104.1 S1, D1, D2: an operation is called by name with typed arguments, from the menu's shape of call, from a bench and from a
// text-only transport, and the one handler sees the same values; a wrong value is refused before the handler runs. A name has one owner.
// States have revisions, events carry a checked payload. Every operation runs on one task in the order it was queued, and the core
// lock keeps the executor and a poll out of the core together.
#include <atomic>
#include <chrono>
#include <thread>
#include <vector>
#include "check.h"
#include "hostos.h"
#include "ops.h"
#include "exec.h"
#include "service.h"

static const char *const modes[] = { "ro", "rw", "unlinked" };

struct Seen {
    int runs = 0;
    int slot_ok = 0;
    char slot[40], path[80];
    int mode = -1;
} g_mount;

static int h_mount(CoreCtx *, const OpArgs &a, OpOut &o)
{
    g_mount.runs++;
    snprintf(g_mount.slot, sizeof(g_mount.slot), "%s", a.s(0));
    snprintf(g_mount.path, sizeof(g_mount.path), "%s", a.s(1));
    g_mount.mode = a.i(2);
    if (!strcmp(a.s(1), "/bad.d64")) {
        snprintf(o.msg, o.msglen, "not a disk");
        return OPE_FORMAT;
    }
    return OPE_OK;
}

static const ArgSpec mount_args[] = { ARG_SLOT("slot"), ARG_PATH("path"), ARG_ENUM("mode", modes, 3) };

static int g_num_runs, g_num_v, g_flag;
static int h_num(CoreCtx *, const OpArgs &a, OpOut &) { g_num_runs++; g_num_v = a.i(0); g_flag = a.i(1); return OPE_OK; }
static const ArgSpec num_args[] = { ARG_INT("v", 3, 9), ARG_BOOL("on") };

static int g_blob_len;
static int h_blob(CoreCtx *, const OpArgs &a, OpOut &) { g_blob_len = (int)a.len(0); return a.p(0)[0] == 7 ? OPE_OK : OPE_FAILED; }
static const ArgSpec blob_args[] = { ARG_BLOB("data"), ARG_STRING("note", 5) };

static int h_more(CoreCtx *, const OpArgs &, OpOut &o) { o.more = true; return OPE_OK; }

static int g_owner;
static int h_first(CoreCtx *, const OpArgs &, OpOut &) { g_owner = 1; return OPE_OK; }
static int h_second(CoreCtx *, const OpArgs &, OpOut &) { g_owner = 2; return OPE_OK; }

static const char *g_state_v = "a";
static const char *get_state(CoreCtx *) { return g_state_v; }

static int g_ev_n, g_ev_v;
static void on_event(void *user, const char *, const OpArgs &p) { g_ev_n++; g_ev_v = p.i(0) + *(int *)user; }

static void registry()
{
    OpRegistry r;
    r.contributor("fake");
    CHECK_EQ(r.add_op("drive.mount", mount_args, 3, h_mount), OPE_OK);
    CHECK_EQ(r.add_op("test.num", num_args, 2, h_num), OPE_OK);
    CHECK_EQ(r.add_op("test.blob", blob_args, 2, h_blob), OPE_OK);
    CHECK_EQ(r.add_op("test.more", 0, 0, h_more), OPE_OK);
    CHECK_EQ(r.add_op("test.owner", 0, 0, h_first), OPE_OK);
    OpResult res;

    // S1: the same call from three callers - the menu (a file and constants), a bench (typed), a text-only transport
    OpIn menu[] = { in_str("slot", "iec:drive_a"), in_str("path", "/usb0/a.d64"), in_int("mode", 1) };
    OpIn bench[] = { in_str("path", "/usb0/a.d64"), in_int("mode", 1), in_str("slot", "iec:drive_a") };
    OpIn rest[] = { in_str("slot", "iec:drive_a"), in_str("path", "/usb0/a.d64"), in_str("mode", "RW") };
    const OpIn *callers[] = { menu, bench, rest };
    for (int k = 0; k < 3; k++) {
        g_mount = Seen();
        CHECK_EQ(r.call(0, "drive.mount", callers[k], 3, &res), OPE_OK);
        CHECK_EQ(g_mount.runs, 1);
        CHECK_STR(g_mount.slot, "iec:drive_a");
        CHECK_STR(g_mount.path, "/usb0/a.d64");
        CHECK_EQ(g_mount.mode, 1);
    }
    // a wrong enum is refused with ERR_ARG and the handler does not run; the reason names the argument and the labels
    g_mount = Seen();
    OpIn xx[] = { in_str("slot", "iec:drive_a"), in_str("path", "/a.d64"), in_str("mode", "xx") };
    CHECK_EQ(r.call(0, "drive.mount", xx, 3, &res), OPE_ARG);
    CHECK_EQ(g_mount.runs, 0);
    CHECK(strstr(res.msg, "mode") && strstr(res.msg, "unlinked"));
    OpIn idx[] = { in_str("slot", "iec:drive_a"), in_str("path", "/a.d64"), in_int("mode", 3) };
    CHECK_EQ(r.call(0, "drive.mount", idx, 3, &res), OPE_ARG);
    CHECK_EQ(g_mount.runs, 0);
    // missing, unknown, doubled arguments; bad slot, empty path
    CHECK_EQ(r.call(0, "drive.mount", menu, 2, &res), OPE_ARG);
    OpIn extra[] = { in_str("slot", "iec:a"), in_str("path", "/a"), in_int("mode", 0), in_int("speed", 1) };
    CHECK_EQ(r.call(0, "drive.mount", extra, 4, &res), OPE_ARG);
    OpIn twice[] = { in_str("slot", "iec:a"), in_str("slot", "iec:b"), in_str("path", "/a") };
    CHECK_EQ(r.call(0, "drive.mount", twice, 3, &res), OPE_ARG);
    OpIn badslot[] = { in_str("slot", "IEC drive"), in_str("path", "/a"), in_int("mode", 0) };
    CHECK_EQ(r.call(0, "drive.mount", badslot, 3, &res), OPE_ARG);
    OpIn nopath[] = { in_str("slot", "iec:a"), in_str("path", ""), in_int("mode", 0) };
    CHECK_EQ(r.call(0, "drive.mount", nopath, 3, &res), OPE_ARG);
    CHECK_EQ(g_mount.runs, 0);
    // the handler's own refusal comes back with its code and its words
    OpIn bad[] = { in_str("slot", "iec:a"), in_str("path", "/bad.d64"), in_int("mode", 0) };
    CHECK_EQ(r.call(0, "drive.mount", bad, 3, &res), OPE_FORMAT);
    CHECK_STR(res.msg, "not a disk");
    CHECK_EQ(r.call(0, "no.such", 0, 0, &res), OPE_NOT_FOUND);

    // int with a range, bool, in numbers and in text
    g_num_runs = 0;
    OpIn n1[] = { in_int("v", 3), in_int("on", 1) };
    CHECK_EQ(r.call(0, "test.num", n1, 2, &res), OPE_OK);
    CHECK_EQ(g_num_v, 3);
    OpIn n2[] = { in_str("v", "9"), in_str("on", "off") };
    CHECK_EQ(r.call(0, "test.num", n2, 2, &res), OPE_OK);
    CHECK_EQ(g_num_v, 9);
    CHECK_EQ(g_flag, 0);
    OpIn lo[] = { in_int("v", 2), in_int("on", 0) };
    OpIn hi[] = { in_str("v", "10"), in_int("on", 0) };
    OpIn junk[] = { in_str("v", "4x"), in_int("on", 0) };
    OpIn flag[] = { in_int("v", 4), in_int("on", 2) };
    CHECK_EQ(r.call(0, "test.num", lo, 2, &res), OPE_ARG);
    CHECK_EQ(r.call(0, "test.num", hi, 2, &res), OPE_ARG);
    CHECK_EQ(r.call(0, "test.num", junk, 2, &res), OPE_ARG);
    CHECK_EQ(r.call(0, "test.num", flag, 2, &res), OPE_ARG);
    CHECK_EQ(g_num_runs, 2);

    // blob and string length
    uint8_t bytes[4] = { 7, 1, 2, 3 };
    OpIn b1[] = { in_blob("data", bytes, 4), in_str("note", "12345") };
    OpIn b2[] = { in_blob("data", bytes, 4), in_str("note", "123456") };
    OpIn b3[] = { in_str("data", "text"), in_str("note", "x") };
    CHECK_EQ(r.call(0, "test.blob", b1, 2, &res), OPE_OK);
    CHECK_EQ(g_blob_len, 4);
    CHECK_EQ(r.call(0, "test.blob", b2, 2, &res), OPE_ARG);
    CHECK_EQ(r.call(0, "test.blob", b3, 2, &res), OPE_ARG);

    // the poll's "had work" travels in the result
    CHECK_EQ(r.call(0, "test.more", 0, 0, &res), OPE_OK);
    CHECK(res.more);

    // D8: a name has one owner; the first registration keeps it, the second is refused and the sentence names both
    r.contributor("second");
    CHECK_EQ(r.add_op("test.owner", 0, 0, h_second), OPE_FAILED);
    CHECK(strstr(r.error(), "fake") && strstr(r.error(), "second") && strstr(r.error(), "test.owner"));
    CHECK_EQ(r.call(0, "test.owner", 0, 0, &res), OPE_OK);
    CHECK_EQ(g_owner, 1);
    CHECK_STR(r.find_op("test.owner")->owner, "fake");
    CHECK(r.add_op("nodot", 0, 0, h_second) != OPE_OK);
    CHECK(r.add_op("Bad.Name", 0, 0, h_second) != OPE_OK);
    CHECK(r.add_op("x.", 0, 0, h_second) != OPE_OK);
    static const ArgSpec dup_args[] = { ARG_INT("a", 0, 1), ARG_INT("a", 0, 1) };
    CHECK(r.add_op("test.dup", dup_args, 2, h_second) != OPE_OK);

    // states: the value, and a revision that counts the changes seen
    r.contributor("fake");
    CHECK_EQ(r.add_state("fake.slot", get_state), OPE_OK);
    uint32_t rev = 0, rev2 = 0;
    CHECK_STR(r.state(0, "fake.slot", &rev), "a");
    CHECK_STR(r.state(0, "fake.slot", &rev2), "a");
    CHECK_EQ(rev, rev2);
    g_state_v = "b";
    CHECK_STR(r.state(0, "fake.slot", &rev2), "b");
    CHECK_EQ(rev2, rev + 1);
    g_state_v = 0;
    CHECK(r.state(0, "fake.slot", &rev2) == 0);
    CHECK_EQ(rev2, rev + 2);
    CHECK(r.state(0, "fake.none", &rev2) == 0);
    r.contributor("second");
    CHECK(r.add_state("fake.slot", get_state) != OPE_OK);

    // events: a checked payload to every subscriber
    static const ArgSpec ev_payload = ARG_INT("n", 0, 10);
    r.contributor("fake");
    CHECK_EQ(r.add_event("media.changed", &ev_payload), OPE_OK);
    int bias = 100, bias2 = 1000;
    CHECK_EQ(r.subscribe("media.changed", on_event, &bias), OPE_OK);
    CHECK_EQ(r.subscribe("media.changed", on_event, &bias2), OPE_OK);
    OpIn e1 = in_int("n", 4);
    CHECK_EQ(r.emit("media.changed", &e1), 2);
    CHECK_EQ(g_ev_n, 2);
    CHECK_EQ(g_ev_v, 1004);
    OpIn e2 = in_int("n", 11);
    CHECK_EQ(r.emit("media.changed", &e2), OPE_ARG);
    CHECK_EQ(g_ev_n, 2);
    CHECK_EQ(r.emit("no.event", &e1), OPE_NOT_FOUND);
    CHECK_EQ(r.subscribe("no.event", on_event, &bias), OPE_NOT_FOUND);

    // T104.1 D10.3: a mark and a rollback drop what a contributor registered since, and nothing else
    RegMark mk = r.mark();
    int nops0 = r.nops();
    r.contributor("later");
    CHECK_EQ(r.add_op("later.one", 0, 0, h_more), OPE_OK);
    CHECK_EQ(r.add_state("later.state", get_state), OPE_OK);
    CHECK(r.find_op("later.one") != 0);
    r.rollback(mk);
    CHECK(r.find_op("later.one") == 0);
    CHECK(r.find_state("later.state") == 0);
    CHECK_EQ(r.nops(), nops0);
    CHECK(r.find_op("drive.mount") != 0);
    CHECK_EQ(r.add_op("later.one", 0, 0, h_more), OPE_OK);       // the name is free again
}

// ---- the executor -----------------------------------------------------------------------------------------------------------
static std::atomic<int> g_gate(0);
static std::vector<int> g_order;
static std::vector<void *> g_threads;
static std::mutex g_rec;
static int h_rec(CoreCtx *, const OpArgs &a, OpOut &)
{
    while (a.i(0) == 0 && !g_gate.load()) { std::this_thread::sleep_for(std::chrono::milliseconds(1)); }   // request 0 holds the executor
    std::lock_guard<std::mutex> g(g_rec);
    g_order.push_back(a.i(0));
    return OPE_OK;
}
static const ArgSpec rec_args[] = { ARG_INT("n", 0, 99) };

static void *tid() { static thread_local char m; return &m; }
static int h_runner(CoreCtx *, const OpArgs &, OpOut &) { std::lock_guard<std::mutex> g(g_rec); g_threads.push_back(tid()); return OPE_OK; }

static std::atomic<int> g_in_core(0), g_max_in_core(0), g_ex_runs(0), g_poll_runs(0);
static void enter_core()
{
    int n = ++g_in_core;
    int m = g_max_in_core.load();
    while (n > m && !g_max_in_core.compare_exchange_weak(m, n)) { }
    std::this_thread::sleep_for(std::chrono::milliseconds(3));
    --g_in_core;
}
static int h_slow(CoreCtx *, const OpArgs &, OpOut &) { enter_core(); g_ex_runs++; return OPE_OK; }
static int h_poll(CoreCtx *, const OpArgs &, OpOut &) { enter_core(); g_poll_runs++; return OPE_OK; }

static void executor()
{
    HostOs os;
    CoreLock lock(&os);
    OpRegistry r;
    r.contributor("t");
    r.add_op("test.rec", rec_args, 1, h_rec);
    r.add_op("test.slow", 0, 0, h_slow);
    r.add_op("test.poll", 0, 0, h_poll);
    CoreCtx c;
    memset(&c, 0, sizeof(c));
    c.started = true;
    Executor ex(&os, &lock, &r, &c);

    // before start() a call runs on the caller; a refused call never reaches the handler either way
    OpResult res;
    OpIn one = in_int("n", 1);
    g_gate = 1;
    CHECK_EQ(ex.call("test.rec", &one, 1, &res), OPE_OK);
    OpIn big = in_int("n", 100);
    CHECK_EQ(ex.call("test.rec", &big, 1, &res), OPE_ARG);
    CHECK_EQ(ex.call("no.such", 0, 0, &res), OPE_NOT_FOUND);
    CHECK_EQ(g_order.size(), (size_t)1);
    g_order.clear();

    // D2: one task, in the order queued. Request 0 holds the executor; 1..5 queue behind it from five other tasks, one after
    // the other; when the gate opens they have to run 0, 1, 2, 3, 4, 5, and every handler on the executor's one task.
    g_gate = 0;
    CHECK(ex.start("exec", 0, 4096));
    std::vector<std::thread> callers;
    for (int i = 0; i <= 5; i++) {
        callers.emplace_back([&ex, i] {
            OpResult rr;
            OpIn in = in_int("n", i);
            ex.call("test.rec", &in, 1, &rr);
        });
        for (int w = 0; w < 1500 && ex.queued() < i + 1; w++) { std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
        CHECK(ex.queued() >= i + 1);
    }
    g_gate = 1;
    for (auto &t : callers) { t.join(); }
    CHECK_EQ(g_order.size(), (size_t)6);
    for (size_t i = 0; i < g_order.size(); i++) { CHECK_EQ(g_order[i], (int)i); }

    // one task: all handlers ran on the thread that the executor reports; a handler run on a caller would differ
    r.add_op("test.runner", 0, 0, h_runner);
    callers.clear();
    for (int i = 0; i < 4; i++) {
        callers.emplace_back([&ex] { OpResult rr; ex.call("test.runner", 0, 0, &rr); });
    }
    for (auto &t : callers) { t.join(); }
    CHECK_EQ(g_threads.size(), (size_t)4);
    bool same = true;
    for (void *p : g_threads) { same &= p == g_threads[0]; }
    CHECK(same);
    CHECK(g_threads[0] != tid());

    // Q9: the executor and a poll never are in the core together
    PollTask poll(&os, &lock, &r, &c, "test.poll", 1, true);
    CHECK(poll.start("poll", 1, 4096));
    for (int i = 0; i < 25; i++) {
        OpResult rr;
        CHECK_EQ(ex.call("test.slow", 0, 0, &rr), OPE_OK);
    }
    poll.stop();
    ex.stop();
    os.join_all();
    CHECK_EQ(g_ex_runs.load(), 25);
    CHECK(g_poll_runs.load() >= 3);
    CHECK_EQ(g_max_in_core.load(), 1);
}

// start() returns when the executor task is running: a task that is slow to begin must not let the first call run on its caller (the executor
// "one task" rule, and a start-up race that made the order check above fail under load). The os starts its thread 80 ms late.
static std::thread::id g_ident;
static int h_ident(CoreCtx *, const OpArgs &, OpOut &) { g_ident = std::this_thread::get_id(); return OPE_OK; }

static void slow_start()
{
    struct SlowOs : HostOs {
        bool task_new(const char *, void (*fn)(void *), void *arg, int, uint32_t)
        {
            late_.emplace_back([fn, arg] { std::this_thread::sleep_for(std::chrono::milliseconds(80)); fn(arg); });
            return true;
        }
        ~SlowOs() { for (auto &t : late_) { if (t.joinable()) { t.join(); } } }
        std::vector<std::thread> late_;
    } os;
    CoreLock lock(&os);
    OpRegistry r;
    r.contributor("t");
    r.add_op("test.ident", 0, 0, h_ident);
    CoreCtx c;
    memset(&c, 0, sizeof(c));
    c.started = true;
    Executor ex(&os, &lock, &r, &c);
    CHECK(ex.start("exec", 0, 4096));
    OpResult res;
    CHECK_EQ(ex.call("test.ident", 0, 0, &res), OPE_OK);
    CHECK(g_ident != std::this_thread::get_id());           // ran on the executor's thread, not on the caller's
    // a caller that holds the core lock (the start hook, a poll) gets the operation run on its own task: queued, it would wait for the lock it holds
    lock.lock();
    CHECK_EQ(ex.call("test.ident", 0, 0, &res), OPE_OK);
    CHECK(g_ident == std::this_thread::get_id());
    lock.unlock();
    ex.stop();
}

int main()
{
    registry();
    executor();
    slow_start();
    DONE("test_ops");
}
