// T156 P1/P2/P3 (application side): the pad service against the real NES and SNES descriptors and a model of their register blocks (FakeIo): plug
// order, the default map (D3), the register words, the write only when the word changes, the unplug (S5), a custom map and its text, the .cfg round trip
// ("Pad N map", "Pad map"), the capture the mapping wizard uses, a core without pad support. The descriptors are those of hidpad.cc's test (a Switch-Pro-
// like D-input report and a clone pad with X/Y as the D-pad); the 8BitDo SN30 Pro is NOT among them: its descriptor and button numbering are not known
// yet, so the default map's Button numbers (pad.h PAD_D3_*) stay an assumption.
#include <vector>
#include <fstream>
#include <iterator>
#include "check.h"
#include "fakes.h"
#include "cores.h"
extern const CoreDesc atari800_desc;     // cores.h lists the shared app's cores; the Atari 800 XL is its own app (cores/atari800/app/lib)
#include "pad.h"
#include "ops_glue.h"
#include "descrec.h"

typedef std::vector<uint8_t> V;

static const V d_hat = {
    0x05, 0x01, 0x09, 0x05, 0xA1, 0x01,
    0x15, 0x00, 0x25, 0x01, 0x35, 0x00, 0x45, 0x01, 0x75, 0x01, 0x95, 0x0E, 0x05, 0x09, 0x19, 0x01, 0x29, 0x0E, 0x81, 0x02,
    0x95, 0x02, 0x81, 0x01,
    0x05, 0x01, 0x25, 0x07, 0x46, 0x3B, 0x01, 0x75, 0x04, 0x95, 0x01, 0x65, 0x14, 0x09, 0x39, 0x81, 0x42,
    0x65, 0x00, 0x95, 0x01, 0x81, 0x01,
    0x26, 0xFF, 0x00, 0x46, 0xFF, 0x00, 0x09, 0x30, 0x09, 0x31, 0x09, 0x32, 0x09, 0x35, 0x75, 0x08, 0x95, 0x04, 0x81, 0x02,
    0x06, 0x00, 0xFF, 0x09, 0x20, 0x95, 0x01, 0x81, 0x02,
    0xC0 };
static const V d_mouse = { 0x05, 0x01, 0x09, 0x02, 0xA1, 0x01, 0x09, 0x01, 0xA1, 0x00, 0x05, 0x09, 0x19, 0x01, 0x29, 0x03, 0x15, 0x00, 0x25, 0x01, 0x95, 0x03,
                           0x75, 0x01, 0x81, 0x02, 0x95, 0x01, 0x75, 0x05, 0x81, 0x01, 0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x15, 0x81, 0x25, 0x7F, 0x75, 0x08,
                           0x95, 0x02, 0x81, 0x06, 0xC0, 0xC0 };

// a report of d_hat: buttons (bit n = Button n+1), hat 0..7 or 15, X/Y centred
static V rep(uint16_t buttons, uint8_t hat = 15, uint8_t x = 0x80, uint8_t y = 0x80)
{
    return V{ (uint8_t)buttons, (uint8_t)(buttons >> 8), hat, x, y, 0x80, 0x80, 0 };
}

struct Rig {
    FakeIo io;
    FakeFs fs;
    CoreCtx c;
    Settings *st;
    explicit Rig(const CoreDesc *d, const char *cfg = "/sd/cores/x/x.cfg")
    {
        memset(&c, 0, sizeof(c));
        c.desc = d; c.io = &io; c.fs = &fs;
        st = new Settings(d->settings, d->nsettings, d->store, &fs, cfg, 0);
        st->load();
        c.settings = st;
    }
    uint16_t word(uint32_t reg) { return (uint16_t)(io.regs[reg] | io.regs[reg + 1] << 8); }
    int writes(uint32_t reg) { int n = 0; for (auto &w : io.log) n += (w.reg == reg); return n; }
};

static int k1, k2, k3;
// the default map of a spec for a pad that is not in the table (the generic numbering)
static const uint8_t *gen_map(const PadSpec *sp)
{
    static uint8_t m[2][16];
    static int n;
    pad_default_map(sp, 0, 0, m[n & 1]);
    return m[n++ & 1];
}                                   // three interfaces' identities

static void unplug_all() { pad_detach(&k1); pad_detach(&k2); pad_detach(&k3); }

int main(int argc, char **argv)
{
    setvbuf(stdout, 0, _IONBF, 0);
    // ---- a core without a pad record: reports are taken, nothing is written
    {
        Rig r(&atari800_desc);
        pad_configure(0);
        CHECK(pad_attach(&k1, "pad", 0x1234, 0x0001, d_hat.data(), (int)d_hat.size()));
        V p = rep(0xFFFF, 0);
        pad_report(&k1, p.data(), (int)p.size());
        CHECK(!pad_pace(&r.c));
        CHECK_EQ((int)r.io.log.size(), 0);
        unplug_all();
    }
    DONE("pad");
}
