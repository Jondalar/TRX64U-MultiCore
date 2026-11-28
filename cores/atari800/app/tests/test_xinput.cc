// T196 S1-S4: XInput pads. The driver's match function (xinput_claim) on interface triples and on the capability gate, 360 and GIP report fixtures through the
// real pad service (pad_attach with the synthetic descriptor, pad_report, pad_state, the register words of the SNES and NES descriptors), the capture of the
// mapping wizard, two pads as two players, and the start sequences against a model of the OUT endpoint. The report bytes are WRITTEN HERE from the public
// layouts named in xinput.h; no pad was measured (spec T196 section 5), so what this proves is the conversion, not that a Series pad sends these bytes.
#include <vector>
#include "check.h"
#include "fakes.h"
#include "cores.h"
extern const CoreDesc atari800_desc;     // cores.h lists the shared app's cores; the Atari 800 XL is its own app (cores/atari800/app/lib)
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
    // D4 on the Atari 800 XL (its app, run.sh a8app): the capability `pad` is exactly the cores that have a PadSpec - the Atari has neither
    CHECK_EQ(desc_has_capability(&atari800_desc, "pad"), atari800_desc.pad != NULL);
    CHECK(!desc_has_capability(&atari800_desc, "pad"));
    DONE("xinput");
}
