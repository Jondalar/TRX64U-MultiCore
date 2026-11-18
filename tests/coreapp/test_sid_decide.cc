// T228 R20: the decisions of the SID socket detection (sid_decide.h): 16 agreeing OSC3 reads name the chip, a disagreement gives unknown, and a new
// 6581 leaves its socket off (FW u64_config.cc U64SidSockets::detect). Happy path only; the registers are the board's.
#include "check.h"
#include "sid_decide.h"

static void fill(uint8_t *p, uint8_t first, uint8_t second)
{
    for (int i = 0; i < 16; i++) {
        p[i] = first;
        p[16 + i] = second;
    }
}

int main()
{
    uint8_t p[32];
    fill(p, 0, 2);
    CHECK_EQ(sid_osc3_type(p), 2);                 // 8580
    fill(p, 1, 2);
    CHECK_EQ(sid_osc3_type(p), 1);                 // 6581
    p[9] = 0;                                      // one read of sixteen disagrees
    CHECK_EQ(sid_osc3_type(p), 0);
    fill(p, 1, 2);
    p[16 + 5] = 3;
    CHECK_EQ(sid_osc3_type(p), 0);
    fill(p, 0x55, 0x55);                           // no SID, an emulation
    CHECK_EQ(sid_osc3_type(p), 0);

    SidChange c = sid_type_change(1);              // a new 6581: the socket is off until the user enables it, caps off, shunt on
    CHECK_EQ(c.enable, 0);
    CHECK_EQ(c.caps, 0);
    CHECK_EQ(c.shunt, 1);
    c = sid_type_change(2);
    CHECK_EQ(c.enable, 1);
    CHECK_EQ(c.caps, 1);
    CHECK_EQ(c.shunt, 0);
    c = sid_type_change(0);                        // nothing found: the socket stays as it is
    CHECK_EQ(c.enable, -1);
    c = sid_type_change(5);                        // another chip: enabled
    CHECK_EQ(c.enable, 1);
    CHECK_EQ(c.caps, -1);
    DONE("sid_decide");
}
