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
    DONE("test_service");
}
