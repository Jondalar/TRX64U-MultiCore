// T104 5.2 `mouse_sink`: a USB mouse delivered to the core as deltas. The numbers are the reference's (the Amiga's mouse spec, cores/amiga/app/lib/amiga.cc): the accumulator is clamped
// to +-4000, one pair per 2 ms of at most +-127 each with the remainder kept, the buttons only when they change, DX before DY (the DY write
// sends the pair).
#include "check.h"
#include "amiga_model.h"
#include "engines.h"
#include "mousesink.h"

static int count(const AmigaIo &io, uint32_t reg)
{
    int n = 0;
    for (auto &w : io.log) n += w.reg == reg;
    return n;
}

// Paces until nothing more is sent; returns the sum of the signed DX / DY that reached the core.
static void drain(AmigaRig &r, int *sx, int *sy, int *pairs)
{
    *sx = *sy = *pairs = 0;
    size_t from = r.io.log.size();
    for (int i = 0; i < 1000; i++) {
        r.io.delay_ms(2);
        if (!mouse_sink_pace(&r.c)) {
            break;
        }
    }
    for (size_t i = from; i < r.io.log.size(); i++) {
        if (r.io.log[i].reg == AM_MOUSE_DX) *sx += (int8_t)r.io.log[i].v;
        if (r.io.log[i].reg == AM_MOUSE_DY) { *sy += (int8_t)r.io.log[i].v; (*pairs)++; }
    }
}

int main()
{
    FakeFs fs;
    AmigaRig r(&fs);
    engines_of(&r.c);                                // the sink is made known to the mouse code
    int sx, sy, pairs;

    // the accumulator is clamped: 6000 of motion is 4000, sent in pairs of at most 127
    mouse_sink_reset();
    mouse_sink_motion(3000, 0, 0);
    mouse_sink_motion(3000, 0, 0);
    drain(r, &sx, &sy, &pairs);
    CHECK_EQ(sx, 4000);
    CHECK_EQ(sy, 0);
    CHECK_EQ(pairs, 32);                             // 31 * 127 + 63
    mouse_sink_motion(-3000, 0, 0);
    mouse_sink_motion(-3000, 0, 0);
    drain(r, &sx, &sy, &pairs);
    CHECK_EQ(sx, -4000);

    // the remainder stays: 300 is 127, 127, 46, one pair per 2 ms
    mouse_sink_reset();
    r.io.log.clear();
    mouse_sink_motion(300, -300, 0);
    CHECK(mouse_sink_pace(&r.c));
    CHECK(!mouse_sink_pace(&r.c));                   // not twice in the same 2 ms
    r.io.delay_ms(1);
    CHECK(!mouse_sink_pace(&r.c));
    r.io.delay_ms(1);
    CHECK(mouse_sink_pace(&r.c));
    r.io.delay_ms(2);
    CHECK(mouse_sink_pace(&r.c));
    r.io.delay_ms(2);
    CHECK(!mouse_sink_pace(&r.c));                   // nothing left
    std::vector<int> dx, dy;
    for (auto &w : r.io.log) {
        if (w.reg == AM_MOUSE_DX) dx.push_back((int8_t)w.v);
        if (w.reg == AM_MOUSE_DY) dy.push_back((int8_t)w.v);
    }
    CHECK(dx.size() == 3 && dx[0] == 127 && dx[1] == 127 && dx[2] == 46);
    CHECK(dy.size() == 3 && dy[0] == -127 && dy[1] == -127 && dy[2] == -46);
    // DX is written before DY, and DY last: that write sends the pair
    int last_dx = -1, last_dy = -1;
    for (size_t i = 0; i < r.io.log.size(); i++) {
        if (r.io.log[i].reg == AM_MOUSE_DX) last_dx = (int)i;
        if (r.io.log[i].reg == AM_MOUSE_DY) last_dy = (int)i;
    }
    CHECK(last_dx >= 0 && last_dy == last_dx + 1);

    // the buttons go out when they change, and only then
    mouse_sink_reset();
    r.io.log.clear();
    mouse_sink_motion(0, 0, 1);
    r.io.delay_ms(2);
    mouse_sink_pace(&r.c);
    CHECK_EQ(count(r.io, AM_MOUSE_BTN), 1);
    CHECK_EQ(r.io.regs[AM_MOUSE_BTN], 1);
    mouse_sink_motion(5, 0, 1);
    r.io.delay_ms(2);
    mouse_sink_pace(&r.c);
    CHECK_EQ(count(r.io, AM_MOUSE_BTN), 1);          // the same buttons: not written again
    mouse_sink_motion(0, 0, 5);
    r.io.delay_ms(2);
    mouse_sink_pace(&r.c);
    CHECK_EQ(count(r.io, AM_MOUSE_BTN), 2);
    CHECK_EQ(r.io.regs[AM_MOUSE_BTN], 5);

    // a negative delta reaches the core as a two's complement byte
    mouse_sink_reset();
    r.io.log.clear();
    mouse_sink_motion(-50, 0, 0);
    r.io.delay_ms(2);
    mouse_sink_pace(&r.c);
    CHECK_EQ(r.io.regs[AM_MOUSE_DX], 0xCE);

    // a core without the sink: motion goes nowhere
    {
        CoreCtx c2 = r.c;
        c2.eng = 0;
        CoreDesc d = amiga_desc;
        d.mouse = 0;
        c2.desc = &d;
        engines_of(&c2);
        r.io.log.clear();
        mouse_sink_motion(10, 10, 1);
        r.io.delay_ms(2);
        CHECK(!mouse_sink_pace(&c2));
        CHECK_EQ(r.io.log.size(), 0);
        mouse_sink_configure(amiga_desc.mouse);
    }
    DONE("test_mousesink");
}
