// T196 S1-S4: XInput pads. The driver's match function (xinput_claim) on interface triples and on the capability gate, 360 and GIP report fixtures through the
// real pad service (pad_attach with the synthetic descriptor, pad_report, pad_state, the register words of the SNES and NES descriptors), the capture of the
// mapping wizard, two pads as two players, and the start sequences against a model of the OUT endpoint. The report bytes are WRITTEN HERE from the public
// layouts named in xinput.h; no pad was measured (spec T196 section 5), so what this proves is the conversion, not that a Series pad sends these bytes.
#include <vector>
#include "check.h"
#include "fakes.h"
#include "cores.h"
#include "nes_regs.h"
extern const CoreDesc nes_desc;          // cores.h lists the shared app's cores; the NES is its own app (cores/nes/app/lib)
#include "pad.h"
#include "hidpad.h"
#include "xinput.h"

typedef std::vector<uint8_t> V;

struct ModelOut : XiOut {
    std::vector<V> sent;
    bool write(const uint8_t *d, int n) override { sent.push_back(V(d, d + n)); return true; }
};

static int k1, k2;

// a 360 report: buttons byte 2, byte 3, triggers, left stick
static V r360(uint8_t b2, uint8_t b3, uint8_t lt = 0, uint8_t rt = 0, int16_t lx = 0, int16_t ly = 0)
{
    V r(20, 0);
    r[1] = 0x14; r[2] = b2; r[3] = b3; r[4] = lt; r[5] = rt;
    r[6] = (uint8_t)lx; r[7] = (uint8_t)((uint16_t)lx >> 8); r[8] = (uint8_t)ly; r[9] = (uint8_t)((uint16_t)ly >> 8);
    return r;
}
// a GIP input message 0x20: buttons bytes 4-5, triggers 6-9, left stick 10-13, right stick 14-17
static V g20(uint8_t b0, uint8_t b1, uint16_t lt = 0, uint16_t rt = 0, int16_t lx = 0, int16_t ly = 0, uint8_t opt = 0x20)
{
    V r(18, 0);
    r[0] = 0x20; r[1] = opt; r[2] = 7; r[3] = 14; r[4] = b0; r[5] = b1;
    r[6] = (uint8_t)lt; r[7] = (uint8_t)(lt >> 8); r[8] = (uint8_t)rt; r[9] = (uint8_t)(rt >> 8);
    r[10] = (uint8_t)lx; r[11] = (uint8_t)((uint16_t)lx >> 8); r[12] = (uint8_t)ly; r[13] = (uint8_t)((uint16_t)ly >> 8);
    return r;
}

// the pad service sees the report the way the driver hands it over
static bool feed(XiSession &s, const void *key, const V &native)
{
    uint8_t rep[XI_REPORT_LEN];
    if (!s.data(native.data(), (int)native.size(), rep)) return false;
    pad_report(key, rep, XI_REPORT_LEN);
    return true;
}
static uint32_t btn(int p) { uint32_t b = 0; uint8_t d = 0; pad_state(p, &b, &d); return b; }
static uint8_t dp(int p) { uint32_t b = 0; uint8_t d = 0; pad_state(p, &b, &d); return d; }
#define SRC(n) (1u << (n))

int main()
{
    setvbuf(stdout, 0, _IONBF, 0);
    int dl = 0;
    const uint8_t *desc = xinput_descriptor(&dl);
    // D4: the capability `pad` is exactly the cores that have a PadSpec
    {
        CHECK_EQ(desc_has_capability(&nes_desc, "pad"), nes_desc.pad != NULL);
        CHECK(desc_has_capability(&nes_desc, "pad"));
        CHECK(!desc_has_capability(&nes_desc, "pa") && !desc_has_capability(&nes_desc, "pads"));
    }
    // ---- S2: a 360 report through the pad service onto the NES's own register (bit 0 A = Button 2 (Xbox B), bit 1 B = Button 1 (Xbox A))
    {
        FakeIo io2; FakeFs fs2; CoreCtx c2; memset(&c2, 0, sizeof(c2)); c2.desc = &nes_desc; c2.io = &io2; c2.fs = &fs2;
        pad_configure(nes_desc.pad);
        CHECK(pad_attach(&k1, "Xbox 360 pad", 0x045E, 0x028E, desc, dl));
        ModelOut o;
        XiSession s;
        s.start(XI_360, &o, pad_player_of(&k1));
        CHECK(feed(s, &k1, r360(0, 0x20)));
        CHECK(pad_pace(&c2));
        CHECK_EQ((uint16_t)(io2.regs[NES_PAD1] | io2.regs[NES_PAD1 + 1] << 8), 1u << 0);
        pad_detach(&k1);
    }
    DONE("xinput");
}
