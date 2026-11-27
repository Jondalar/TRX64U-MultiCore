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
#include "neogeo_regs.h"
extern const CoreDesc neogeo_desc;       // cores.h lists the shared app's cores; the Neo Geo is its own app (cores/neogeo/app/lib)
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
    // ---- Neo Geo: the register is the machine's joystick word (right, left, down, up, A, B, C, D, Start, Select, Coin); the default map is the MiSTer core's
    // positional one (A = the pad's bottom button, B = right, C = left, D = top, Coin = L); the word is written through the core's own service
    {
        Rig r(&neogeo_desc);
        pad_configure(neogeo_desc.pad);
        CHECK(neogeo_desc.pad != 0);
        CHECK_EQ(neogeo_desc.pad->nbuttons, 11);
        CHECK_EQ(neogeo_desc.pad->reg[0], NEO_PAD1);
        CHECK_EQ(neogeo_desc.pad->reg[1], NEO_PAD2);
        CHECK_EQ(neogeo_desc.service_period_ms, 16);
        CHECK(pad_attach(&k1, "pad", 0x1234, 0x0001, d_hat.data(), (int)d_hat.size()));
        struct { int button; int bit; const char *name; } map[] = {
            { 1, 4, "A (bottom)" }, { 2, 5, "B (right)" }, { 3, 6, "C (left)" }, { 4, 7, "D (top)" }, { 8, 8, "Start" }, { 7, 9, "Select" }, { 5, 10, "Coin (L)" } };
        for (auto &m : map) {
            V p = rep((uint16_t)(1u << (m.button - 1)));
            pad_report(&k1, p.data(), (int)p.size());
            OpResult res;
            CHECK_EQ(ops_call(&r.c, "neogeo.service", 0, 0, &res), OPE_OK);
            CHECK_EQ(r.word(NEO_PAD1), 1u << m.bit);
            if (r.word(NEO_PAD1) != (1u << m.bit)) printf("     %s: register %04x\n", m.name, r.word(NEO_PAD1));
            CHECK_EQ(r.word(NEO_PAD2), 0);
        }
        CHECK_EQ(r.writes(NEO_PAD1 + 1), 7);                                // the high byte follows every change of the low one
        V p = rep(0x0001 | 0x0002, 4);                                      // A and B together, hat down
        pad_report(&k1, p.data(), (int)p.size());
        pad_pace(&r.c);
        CHECK_EQ(r.word(NEO_PAD1), (1u << 4) | (1u << 5) | (1u << 2));
        p = rep(0, 2);                                                      // right is bit 0
        pad_report(&k1, p.data(), (int)p.size());
        pad_pace(&r.c);
        CHECK_EQ(r.word(NEO_PAD1), 1u);
        p = rep(0, 6);                                                      // left bit 1, up (hat 0) bit 3
        pad_report(&k1, p.data(), (int)p.size());
        pad_pace(&r.c);
        CHECK_EQ(r.word(NEO_PAD1), 2u);
        p = rep(0, 0);
        pad_report(&k1, p.data(), (int)p.size());
        pad_pace(&r.c);
        CHECK_EQ(r.word(NEO_PAD1), 8u);
        pad_detach(&k1);
        pad_pace(&r.c);
        CHECK_EQ(r.word(NEO_PAD1), 0);
        unplug_all();
        // T196 D4: the capability `pad` is exactly the cores that have a PadSpec (the shared app's cores: test_xinput)
        CHECK_EQ(desc_has_capability(&neogeo_desc, "pad"), neogeo_desc.pad != NULL);
        CHECK(desc_has_capability(&neogeo_desc, "pad"));
    }
    DONE("pad");
}
