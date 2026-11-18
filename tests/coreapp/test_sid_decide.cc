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

    // the FPGASID's bytes (sid_device_fpgasid.cc getByte30/31): stereo 1 (4 with a Dukestah), output mode << 3, LED bit 7 per SID, voices << 4
    FsidSet f = {};
    f.mode = 1;
    f.outmode = 1;
    f.leds = 3;
    f.sid[0] = { 1, 0, 1, 0, 3, 2, -5, -8, 3 };
    f.sid[1] = { 0, 1, 0, 1, 0, 0, 127, 7, 7 };
    uint8_t r[8];
    fsid_bytes(f, false, r);
    CHECK_EQ(r[0], 0x80 | 0x30 | 0x08 | 0x01);
    CHECK_EQ(r[1], 1 | 4 | (3 << 4) | (2 << 6));
    CHECK_EQ(r[2], 0x08);
    CHECK_EQ(r[3], 0xFB);
    CHECK_EQ(r[4], 0x80 | 0x70);
    CHECK_EQ(r[5], 2 | 8);
    CHECK_EQ(r[6], 7);
    CHECK_EQ(r[7], 127);
    fsid_bytes(f, true, r);
    CHECK_EQ(r[0], 0x80 | 0x30 | 0x08 | 0x04);
    DONE("sid_decide");
}
