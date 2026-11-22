// T104.1 S8, D6, Q9: a contributor's poll is served while the menu is open. A fake core asks for one sector every 10 ms; the menu is
// opened, in Overlay mode (the machine keeps running under it), and kept open for 2 s; every request has to be answered within the
// poll's period plus one tick. The old loop gated the service on "menu closed" (run.cc, T93): its control is a build in which the gate
// reads the menu again, and it has to fail. A frozen machine is not served unless the contributor declares poll_when_frozen.
#include "check.h"
#include "hostos.h"
#include "service.h"

struct FakeCore {
    VirtOs *os;
    uint32_t pending[512];
    int n_pending = 0;
    int asked = 0, answered = 0;
    uint32_t worst = 0;
    int answered_while_frozen = 0;
    bool frozen_now = false;
    void ask() { pending[n_pending++] = os->t; asked++; }
};
static FakeCore g_core;

static int h_service(CoreCtx *, const OpArgs &, OpOut &o)
{
    FakeCore &f = g_core;
    bool work = f.n_pending > 0;
    for (int i = 0; i < f.n_pending; i++) {
        uint32_t lat = f.os->t - f.pending[i];
        if (lat > f.worst) {
            f.worst = lat;
        }
        f.answered++;
        f.answered_while_frozen += f.frozen_now;
    }
    f.n_pending = 0;
    o.more = false;
    (void)work;
    return OPE_OK;
}

static int h_busy(CoreCtx *, const OpArgs &, OpOut &o) { o.more = true; return OPE_OK; }

// T239.3 S3: the polls of the libraries. Each op counts its own calls.
static int g_calls[4];
template <int N> static int h_count(CoreCtx *, const OpArgs &, OpOut &) { g_calls[N]++; return OPE_OK; }
static int h_arg(CoreCtx *, const OpArgs &, OpOut &) { return OPE_OK; }

#define PERIOD 10
#define TICK 1

struct Scene {
    int menu_from, menu_to;               // the menu is open in [from, to)
    bool freeze;                          // opening the menu freezes the machine
    bool when_frozen;
};

// 3 s of virtual time in 1 ms steps; the poll task is stepped when its own clock says so, the menu loop is in no way involved.
static void run(const Scene &sc, FakeCore *out)
{
    VirtOs os;
    CoreLock lock(&os);
    OpRegistry reg;
    reg.contributor("fake");
    reg.add_op("fake.service", 0, 0, h_service);
    CoreCtx c;
    memset(&c, 0, sizeof(c));
    c.started = true;
    g_core = FakeCore();
    g_core.os = &os;
    PollTask poll(&os, &lock, &reg, &c, "fake.service", PERIOD, sc.when_frozen);
    uint32_t wake = 0;
    for (os.t = 0; os.t < 3000; os.t++) {
        bool open = (int)os.t >= sc.menu_from && (int)os.t < sc.menu_to;
        c.menu_open = open;
        c.frozen = open && sc.freeze;
        g_core.frozen_now = c.frozen;
        if (os.t % PERIOD == 3 && os.t >= 100 && os.t < 2900) {
            g_core.ask();
        }
        if (os.t >= wake) {
            wake = os.t + poll.step();
        }
    }
    *out = g_core;
}

int main()
{
    // S8: Overlay, menu open for 2 s
    FakeCore f;
    Scene overlay = { 500, 2500, false, false };
    run(overlay, &f);
    printf("S8 overlay: %d requests, %d answered, worst latency %u ms (limit %d)\n", f.asked, f.answered, (unsigned)f.worst, PERIOD + TICK);
    CHECK(f.asked > 250);
    CHECK_EQ(f.answered, f.asked);
    CHECK(f.worst <= (uint32_t)(PERIOD + TICK));

    // a frozen machine asks for nothing and is not polled; the requests that piled up are served after it thaws
    FakeCore g;
    Scene freeze = { 500, 2500, true, false };
    run(freeze, &g);
    printf("S8 freeze, no poll_when_frozen: %d requests, %d answered, %d of them while frozen\n", g.asked, g.answered, g.answered_while_frozen);
    CHECK_EQ(g.answered_while_frozen, 0);
    CHECK(g.answered > 0);

    // ... unless the contributor declares it
    FakeCore h;
    Scene both = { 500, 2500, true, true };
    run(both, &h);
    printf("S8 freeze, poll_when_frozen: %d requests, %d answered, %d of them while frozen, worst %u ms\n", h.asked, h.answered, h.answered_while_frozen, (unsigned)h.worst);
    CHECK(h.answered_while_frozen > 100);
    CHECK_EQ(h.answered, h.asked);
    CHECK(h.worst <= (uint32_t)(PERIOD + TICK));

    // not before the core was released
    VirtOs os;
    CoreLock lock(&os);
    OpRegistry reg;
    reg.contributor("fake");
    reg.add_op("fake.service", 0, 0, h_service);
    CoreCtx c;
    memset(&c, 0, sizeof(c));
    g_core = FakeCore();
    g_core.os = &os;
    PollTask poll(&os, &lock, &reg, &c, "fake.service", PERIOD, false);
    g_core.ask();
    for (os.t = 0; os.t < 200; os.t++) { poll.step(); }
    CHECK_EQ(g_core.answered, 0);
    c.started = true;
    for (; os.t < 220; os.t++) { poll.step(); }
    CHECK_EQ(g_core.answered, 1);

    // a poll that always has work does not keep the processor from the tasks below it: it sleeps one period after BURST_MAX_MS
    {
        VirtOs bo;
        CoreLock bl(&bo);
        OpRegistry breg;
        breg.contributor("fake");
        breg.add_op("fake.busy", 0, 0, h_busy);
        CoreCtx bc;
        memset(&bc, 0, sizeof(bc));
        bc.started = true;
        PollTask bp(&bo, &bl, &breg, &bc, "fake.busy", 5, false);
        int run = 0, longest = 0, sleeps = 0;
        for (bo.t = 0; bo.t < 400; bo.t++) {
            if (bp.step() == 0) {
                longest = ++run > longest ? run : longest;
            } else {
                run = 0;
                sleeps++;
            }
        }
        printf("S8 burst: longest stretch without sleeping %d ms, slept %d times in 400 ms\n", longest, sleeps);
        CHECK(longest <= BURST_MAX_MS + 1);
        CHECK(sleeps >= 10);
    }
    // The menu freezes the machine in the one moment between the poll's check and its taking of the lock (open_menu sets frozen, then waits for the
    // lock): the poll that was already past the check must not run into the frozen machine. The lock hook plays the menu.
    {
        struct RaceOs : VirtOs {
            CoreCtx *ctx = 0;
            bool freeze_on_lock = false;
            void lock(void *) { if (freeze_on_lock && ctx) { ctx->frozen = true; } }
        } ro;
        CoreLock rl(&ro);
        OpRegistry rreg;
        rreg.contributor("fake");
        rreg.add_op("fake.service", 0, 0, h_service);
        CoreCtx rc;
        memset(&rc, 0, sizeof(rc));
        rc.started = true;
        ro.ctx = &rc;
        g_core = FakeCore();
        g_core.os = &ro;
        PollTask rp(&ro, &rl, &rreg, &rc, "fake.service", PERIOD, false);
        g_core.ask();
        ro.t = 100;
        ro.freeze_on_lock = true;
        rp.step();
        printf("S8 race: frozen during the wait for the lock: %d answered\n", g_core.answered);
        CHECK_EQ(g_core.answered, 0);
        CHECK_EQ(g_core.n_pending, 1);                // still pending: served after the thaw
        ro.freeze_on_lock = false;
        rc.frozen = false;
        ro.t = 120;
        rp.step();
        CHECK_EQ(g_core.answered, 1);
    }

    // S3: LibPolls. A poll counts calls while its library is there, at its own period, and none after drop(); the slot is used again by the next one.
    {
        VirtOs lo;
        CoreLock ll(&lo);
        OpRegistry lreg;
        CoreCtx lc;
        memset(&lc, 0, sizeof(lc));
        lc.started = true;
        static const ArgSpec one_arg[] = { ARG_INT("n", 0, 9) };
        lreg.contributor("base");
        lreg.add_op("base.noop", 0, 0, h_arg);
        lreg.contributor("liba");
        lreg.add_op("liba.poll", 0, 0, h_count<0>);
        lreg.add_op("liba.arg", one_arg, 1, h_arg);
        lreg.contributor("libb");
        lreg.add_op("libb.poll", 0, 0, h_count<1>);
        LibPolls lp(&lo, &ll, &lreg, &lc, 0);
        char why[120];
        memset(g_calls, 0, sizeof(g_calls));
        CHECK(lp.add("liba", "liba.poll", 10, why, sizeof(why)));
        CHECK(lp.add("libb", "libb.poll", 25, why, sizeof(why)));
        CHECK_EQ(lp.count(), 2);
        auto spin = [&](uint32_t ms) {                      // the slots stepped as their own tasks would be, 1 ms apart
            uint32_t wake[LIB_POLLS_MAX] = { 0 };
            uint32_t end = lo.t + ms;
            for (; lo.t < end; lo.t++) {
                for (int i = 0; i < LIB_POLLS_MAX; i++) {
                    if (lp.slot(i) && lo.t >= wake[i]) {
                        wake[i] = lo.t + lp.slot(i)->step();
                    }
                }
            }
        };
        spin(1000);
        printf("S3 two polls, 1000 ms: liba (10 ms) %d calls, libb (25 ms) %d calls\n", g_calls[0], g_calls[1]);
        CHECK(g_calls[0] >= 98 && g_calls[0] <= 101);
        CHECK(g_calls[1] >= 39 && g_calls[1] <= 41);
        // the library goes: its poll ends before its operations do, and nothing is polled after
        lp.drop("liba");
        lreg.drop_owner("liba");
        int a0 = g_calls[0], b0 = g_calls[1];
        spin(500);
        printf("S3 liba dropped: liba %d -> %d calls, libb %d -> %d calls\n", a0, g_calls[0], b0, g_calls[1]);
        CHECK_EQ(g_calls[0], a0);
        CHECK(g_calls[1] >= b0 + 19);
        CHECK_EQ(lp.count(), 1);
        // the registry moved libb's entry down when liba's went; the poll still calls libb's operation, by name
        lreg.contributor("libc");
        lreg.add_op("libc.poll", 0, 0, h_count<2>);
        CHECK(lp.add("libc", "libc.poll", 10, why, sizeof(why)));       // takes the slot liba left
        CHECK_EQ(lp.count(), 2);
        int c0 = g_calls[2];
        spin(200);
        CHECK(g_calls[2] >= c0 + 18);
        // the same owner again replaces its poll
        CHECK(lp.add("libc", "libc.poll", 50, why, sizeof(why)));
        CHECK_EQ(lp.count(), 2);
        c0 = g_calls[2];
        spin(500);
        CHECK(g_calls[2] >= c0 + 9 && g_calls[2] <= c0 + 11);
        // a poll the service cannot serve is refused with a sentence, and nothing starts
        CHECK(!lp.add("libd", "libd.nothing", 10, why, sizeof(why)) && strstr(why, "libd.nothing") && strstr(why, "not registered"));
        lreg.contributor("libe");
        lreg.add_op("libe.arg", one_arg, 1, h_arg);
        CHECK(!lp.add("libe", "libe.arg", 10, why, sizeof(why)) && strstr(why, "takes arguments"));
        CHECK(!lp.add("libb2", "libb.poll", 0, why, sizeof(why)) && strstr(why, "no period"));
        CHECK_EQ(lp.count(), 2);
        // no room: the slots are all in use
        for (int i = lp.count(); i < LIB_POLLS_MAX; i++) {
            char o[8];
            snprintf(o, sizeof(o), "x%d", i);
            CHECK(lp.add(o, "base.noop", 10, why, sizeof(why)));
        }
        CHECK(!lp.add("one_more", "base.noop", 10, why, sizeof(why)) && strstr(why, "no room"));
        // with every slot used the owner's own slot is reused; a replacement that is refused leaves the old poll running
        CHECK(lp.add("libc", "libc.poll", 20, why, sizeof(why)));
        CHECK_EQ(lp.count(), LIB_POLLS_MAX);
        CHECK(!lp.add("libb", "libb.nothing", 10, why, sizeof(why)));
        CHECK_EQ(lp.count(), LIB_POLLS_MAX);
        int k0 = g_calls[1];
        spin(100);
        CHECK(g_calls[1] >= k0 + 3);
        // a poll that is not allowed (the machine is frozen) is not called; thawed, it is
        lc.frozen = true;
        int f0 = g_calls[1];
        spin(200);
        CHECK_EQ(g_calls[1], f0);
        lc.frozen = false;
        spin(200);
        CHECK(g_calls[1] > f0);
    }
    DONE("test_service");
}
