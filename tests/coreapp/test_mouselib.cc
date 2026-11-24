// T104.1 5.6 the mouse library: a stream of USB mouse reports - what usb_hid.cc hands to usb_hid_set_mouse_sink() - reaches the backend the
// core's descriptor names. The Amiga's counters (registers 0x1C0-0x1C2) and the C64's 1351 (the pots and joystick port 1 through MousePots).
// Negative controls inside the test: a library that is not configured for a core writes nothing, and neither does a core without a mouse record.
#include "check.h"
#include "amiga_model.h"
#include "engines.h"
#include "mousesink.h"

// a core whose mouse is a 1351 on the POT lines (the C64 family's, TRX64-Ultimate's trx64u): the descriptor of the made-up core of this test (T239.3 P1-8)
static const MouseSinkSpec pot_mouse = { 0, 0, 0, 0, 0, 0, MOUSE_POT1351 };
static CoreDesc pot_core = amiga_desc;

struct FakePots : MousePots {
    int16_t x = 0, y = 0;
    uint8_t joy = 0x1F;
    int pos_calls = 0, btn_calls = 0;
    void position(int16_t px, int16_t py) { x = px; y = py; pos_calls++; }
    void buttons(uint8_t j) { joy = j; btn_calls++; }
};

// The reports of a mouse: (dx, dy, HID buttons). Exactly what the hook passes, after sensitivity and acceleration.
struct Rep { int dx, dy; uint8_t b; };

static void feed(const Rep *r, int n)
{
    for (int i = 0; i < n; i++) {
        CHECK(mouse_sink_usb_report(r[i].dx, r[i].dy, r[i].b));    // always consumed: the driver's own handling never runs
    }
}

int main()
{
    FakeFs fs;
    AmigaRig r(&fs);
    FakePots pots;

    // ---- the Amiga: counters ----
    engines_of(&r.c);                                   // configures the sink from amiga_desc.mouse
    CHECK(amiga_desc.mouse && amiga_desc.mouse->kind == MOUSE_COUNTERS);
    mouse_sink_reset();
    mouse_sink_set_pots(&pots);
    r.io.log.clear();
    static const Rep stream[] = { { 10, -4, 0 }, { 5, 0, 1 }, { -3, 2, 1 }, { 0, 0, 1 } };
    feed(stream, 4);
    int sx = 0, sy = 0, pairs = 0;
    for (int i = 0; i < 100; i++) {
        r.io.delay_ms(2);
        if (!mouse_sink_pace(&r.c)) {
            break;
        }
    }
    for (auto &w : r.io.log) {
        if (w.reg == AM_MOUSE_DX) sx += (int8_t)w.v;
        if (w.reg == AM_MOUSE_DY) { sy += (int8_t)w.v; pairs++; }
    }
    CHECK_EQ(sx, 12);                                   // 10 + 5 - 3
    CHECK_EQ(sy, -2);                                   // -4 + 0 + 2
    CHECK(pairs >= 1);
    CHECK_EQ(r.io.regs[AM_MOUSE_BTN], 1);               // the left button, written when it changed
    int btn_writes = 0;
    for (auto &w : r.io.log) btn_writes += w.reg == AM_MOUSE_BTN;
    CHECK(btn_writes >= 1);
    CHECK_EQ(pots.pos_calls, 0);                        // the Amiga's reports never touch the 1351

    // the middle and right buttons arrive in HID order
    r.io.log.clear();
    static const Rep btns[] = { { 0, 0, 7 } };
    feed(btns, 1);
    r.io.delay_ms(2);
    mouse_sink_pace(&r.c);
    CHECK_EQ(r.io.regs[AM_MOUSE_BTN], 7);

    // ---- a 1351 on the POT lines ----
    pot_core.mouse = &pot_mouse;
    CoreCtx c64c = r.c;
    c64c.eng = 0;
    c64c.desc = &pot_core;
    engines_of(&c64c);                                  // the sink is now the C64's
    mouse_sink_reset();
    r.io.log.clear();
    static const Rep c64stream[] = { { 3, 1, 0 }, { 4, -2, 1 }, { 0, 0, 3 }, { -1, 0, 0 } };
    feed(c64stream, 1);
    CHECK_EQ(pots.x, 3);
    CHECK_EQ(pots.y, -1);                               // Y subtracts, as the driver's applyRelativeMotion did
    CHECK_EQ(pots.joy, 0x1F);                           // no button
    CHECK_EQ(pots.btn_calls, 0);                        // the buttons are written when they change
    feed(c64stream + 1, 1);
    CHECK_EQ(pots.x, 7);
    CHECK_EQ(pots.y, 1);
    CHECK_EQ(pots.joy, 0x1F & ~0x10);                   // left = joystick fire
    CHECK_EQ(pots.btn_calls, 1);
    feed(c64stream + 2, 1);
    CHECK_EQ(pots.joy, 0x1F & ~0x10 & ~0x01);           // left + right
    CHECK_EQ(pots.btn_calls, 2);
    feed(c64stream + 3, 1);
    CHECK_EQ(pots.x, 6);
    CHECK_EQ(pots.joy, 0x1F);                           // released
    CHECK_EQ(pots.btn_calls, 3);
    static const Rep mid[] = { { 0, 0, 4 } };
    feed(mid, 1);
    CHECK_EQ(pots.joy, 0x1F & ~0x02);                   // middle
    CHECK_EQ(r.io.log.size(), 0);                       // the 1351 writes no register of any core
    r.io.delay_ms(10);
    CHECK(!mouse_sink_pace(&c64c));                     // nothing to pace
    CHECK_EQ(r.io.log.size(), 0);

    // ---- negative controls: nothing is written ----
    // a library that is not configured (no core has announced its record yet): the report is taken, dropped
    mouse_sink_configure(0);
    mouse_sink_reset();
    FakePots quiet;
    mouse_sink_set_pots(&quiet);
    r.io.log.clear();
    static const Rep any[] = { { 50, 50, 7 } };
    feed(any, 1);
    r.io.delay_ms(10);
    mouse_sink_pace(&r.c);
    CHECK_EQ(quiet.pos_calls, 0);
    CHECK_EQ(quiet.btn_calls, 0);
    CHECK_EQ(r.io.log.size(), 0);

    // a core without a mouse record (the descriptor names no backend): the same
    {
        CoreDesc none = amiga_desc;
        none.mouse = 0;
        CoreCtx c2 = r.c;
        c2.eng = 0;
        c2.desc = &none;
        engines_of(&c2);
        feed(any, 1);
        r.io.delay_ms(10);
        CHECK(!mouse_sink_pace(&c2));
        CHECK_EQ(quiet.pos_calls, 0);
        CHECK_EQ(r.io.log.size(), 0);
    }

    // the 1351 backend with no pots on the board: nothing to write, nothing crashes
    mouse_sink_configure(&pot_mouse);
    mouse_sink_set_pots(0);
    feed(any, 1);
    CHECK_EQ(quiet.pos_calls, 0);

    DONE("test_mouselib");
}
