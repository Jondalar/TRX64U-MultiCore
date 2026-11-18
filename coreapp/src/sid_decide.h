// The decisions of the SID socket detection, apart from the registers (u64cfg.cc reads and writes them): what the OSC3 read-outs say and what a changed
// type does to the socket's settings. FW u64/u64_config.cc: S_SidDetector (the consistency check), U64SidSockets::detect (the type change).
#ifndef COREAPP_SID_DECIDE_H
#define COREAPP_SID_DECIDE_H

#include <stdint.h>

// p: 32 bytes of one socket, the OSC3 reads of DetectSidImpl: p[0..15] the first read of 16 rounds, p[16..31] the second read. 2 = 8580, 1 = 6581, 0 =
// unknown (a pattern that is neither, or the 16 rounds do not all agree).
static inline int sid_osc3_type(const uint8_t *p)
{
    int t = 0;
    if (p[17] == 2 && p[1] == 0) {
        t = 2;
    } else if (p[17] == 2 && p[1] == 1) {
        t = 1;
    }
    for (int i = 2; i < 16 && t; i++) {
        if (p[i] != p[1] || p[16 + i] != p[17]) {
            t = 0;
        }
    }
    return t;
}

// What a type that differs from the stored one does to the socket (-1: leave the setting as it is). A new 6581 switches the socket off until the user
// enables it (caps off, shunt on); an 8580 enables it (caps on, shunt off); none leaves it; any other chip enables it.
struct SidChange {
    int enable, caps, shunt;
};
static inline SidChange sid_type_change(int type)
{
    SidChange c = { -1, -1, -1 };
    switch (type) {
    case 1: c.enable = 0; c.caps = 0; c.shunt = 1; break;
    case 2: c.enable = 1; c.caps = 1; c.shunt = 0; break;
    case 0: break;
    default: c.enable = 1; break;
    }
    return c;
}

// The FPGASID's configuration bytes (FW sid_device_fpgasid.cc getByte30Sid1/2, getByte31Sid1/2, S_effectuate): the values of the settings, the bytes written to its
// registers 30, 31, 28, 29 of SID 1 (r[0..3]) and of SID 2 (r[4..7]). With a Dukestah adapter the stereo mode is 4, else 1.
struct FsidChip {
    int filter, crunchy, mixed, delay, readback, extin, digifix, bias, voices;
};
struct FsidSet {
    int mode, outmode, leds;
    FsidChip sid[2];
};
static inline void fsid_bytes(const FsidSet &f, bool dukestah, uint8_t r[8])
{
    for (int k = 0; k < 2; k++) {
        const FsidChip &c = f.sid[k];
        uint8_t b30 = 0;
        if (!k) {
            b30 = f.mode == 1 ? (dukestah ? 0x04 : 0x01) : 0x00;
            b30 |= (uint8_t)(f.outmode << 3);
        }
        if (f.leds & (1 << k)) {
            b30 |= 0x80;
        }
        b30 |= (uint8_t)(c.voices << 4);
        uint8_t b31 = (uint8_t)(c.filter | (c.crunchy << 1) | (c.mixed << 2) | (c.delay << 3) | (c.readback << 4) | (c.extin << 6));
        r[4 * k] = b30;
        r[4 * k + 1] = b31;
        r[4 * k + 2] = (uint8_t)((int8_t)c.bias & 0x0F);
        r[4 * k + 3] = (uint8_t)(int8_t)c.digifix;
    }
}

#endif
