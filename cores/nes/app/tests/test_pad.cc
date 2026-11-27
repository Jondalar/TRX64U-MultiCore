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
#include "nes_regs.h"
extern const CoreDesc nes_desc;          // cores.h lists the shared app's cores; the NES is its own app (cores/nes/app/lib)
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
    // ---- NES: A and B from the SNES layout's A (right) and B (bottom); the register is the controller's own order
    {
        Rig r(&nes_desc);
        pad_configure(nes_desc.pad);
        CHECK(pad_attach(&k1, "pad", 0x1234, 0x0001, d_hat.data(), (int)d_hat.size()));
        struct { int button; int bit; const char *name; } map[] = { { 2, 0, "A" }, { 1, 1, "B" }, { 7, 2, "Select" }, { 8, 3, "Start" } };
        for (auto &m : map) {
            V p = rep((uint16_t)(1u << (m.button - 1)));
            pad_report(&k1, p.data(), (int)p.size());
            pad_pace(&r.c);
            CHECK_EQ(r.word(NES_PAD1) & 0xFF, 1u << m.bit);
        }
        V p = rep(0x0001 | 0x0002, 4);                                     // A and B together, hat down
        pad_report(&k1, p.data(), (int)p.size());
        pad_pace(&r.c);
        CHECK_EQ(r.word(NES_PAD1) & 0xFF, 0x03u | (1u << 5));
        p = rep(0, 2);                                                     // right is bit 7
        pad_report(&k1, p.data(), (int)p.size());
        pad_pace(&r.c);
        CHECK_EQ(r.word(NES_PAD1) & 0xFF, 0x80u);
        pad_detach(&k1);
        pad_pace(&r.c);
        CHECK_EQ(r.word(NES_PAD1), 0);
        unplug_all();
    }
    // ---- the numbering of a pad: the 8BitDo SN30 Pro (by its id, as on the SNES) gives the NES its A and B from the same positions
    {
        Rig rn(&nes_desc);
        pad_configure(nes_desc.pad);
        CHECK(pad_attach(&k1, "8Bitdo SN30 Pro", 0x2DC8, 0x6001, d_hat.data(), (int)d_hat.size()));
        V p = rep(0x0001);                                                     // SN30 Button 1 is the right button: NES A
        pad_report(&k1, p.data(), (int)p.size());
        pad_pace(&rn.c);
        CHECK_EQ(rn.word(NES_PAD1) & 0xFF, 1u);
        p = rep(0x0002);                                                       // Button 2 is the bottom one: NES B
        pad_report(&k1, p.data(), (int)p.size());
        pad_pace(&rn.c);
        CHECK_EQ(rn.word(NES_PAD1) & 0xFF, 2u);
        unplug_all();
    }
    // ---- T228 R10: a menu record's pad (golden/desc_own.toml, compiled by gen_bundle.py; argv[1] is its bin): one port fed by the first pad (by_id), the host's joystick lines (joy_lines, bits
    // 0..4 up down left right fire, 1 = pressed), pad.port picks port 1 or 2 and releases the one it leaves; no register of the core is written
    {
        static DescRecord rec;
        char why[160];
        CHECK(argc > 1);
        std::vector<uint8_t> own;
        { std::ifstream f(argv[1], std::ios::binary); own.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()); }
        CHECK(!own.empty());
        CHECK_EQ(descrec_parse(own.data(), (uint32_t)own.size(), &rec, why, sizeof(why)), 0);
        CHECK(rec.has_pad && rec.pad.out == PAD_OUT_JOY && rec.pad.by_id && rec.pad.nbuttons == 5);
        CHECK(rec.pad.reg[0] == 0 && rec.pad.reg[1] == 1);
        CHECK_STR(rec.pad.names[0], "Up");
        CHECK_STR(rec.pad.names[4], "Fire");
        CHECK(rec.pad.def_map[0] == PAD_SRC_UP && rec.pad.def_map[3] == PAD_SRC_RIGHT && rec.pad.def_map[4] == PAD_ROLE_BOTTOM);
        CoreDesc d = nes_desc;                                             // the settings store of a real descriptor; the pad is the record's
        d.pad = &rec.pad;
        Rig r(&d);
        unplug_all();
        pad_ensure(&r.c);
        CHECK_EQ(pad_port(), 0);
        pad_set_port(1);                                                   // the setting's default, "Port 2" (pad.port 1)
        CHECK_STR(pad_port_name(), "port 2");
        CHECK(pad_attach(&k1, "pad", 0x1234, 0x0001, d_hat.data(), (int)d_hat.size()));
        CHECK(pad_pace(&r.c));
        CHECK(!r.io.joy_log.empty() && r.io.joy_log.back().port == 1 && r.io.joy_log.back().v == 0);   // port 2, released
        CHECK_EQ(r.io.joy[0], 0);
        CHECK(r.io.log.empty());                                           // nothing went to the core's register block
        V p = rep(0x0001, 0);                                              // Button 1 (bottom: Fire in the generic numbering) and the hat up
        pad_report(&k1, p.data(), (int)p.size());
        CHECK(pad_pace(&r.c));
        CHECK_EQ(r.io.joy[1], 0x11);                                       // up (bit 0) and fire (bit 4) on port 2
        CHECK_EQ(r.io.joy[0], 0);
        CHECK(!pad_pace(&r.c));                                            // nothing changed: nothing written
        p = rep(0, 2);                                                     // hat right
        pad_report(&k1, p.data(), (int)p.size());
        CHECK(pad_pace(&r.c));
        CHECK_EQ(r.io.joy[1], 0x08);
        p = rep(0x0001, 0);
        pad_report(&k1, p.data(), (int)p.size());
        pad_pace(&r.c);
        size_t n0 = r.io.joy_log.size();
        pad_set_port(0);                                                   // "Port 1": port 2 is let go first, then port 1 gets the lines
        CHECK_STR(pad_port_name(), "port 1");
        CHECK(pad_pace(&r.c));
        CHECK_EQ(r.io.joy_log.size() - n0, 2);
        CHECK(r.io.joy_log[n0].port == 1 && r.io.joy_log[n0].v == 0);
        CHECK(r.io.joy_log[n0 + 1].port == 0 && r.io.joy_log[n0 + 1].v == 0x11);
        pad_detach(&k1);                                                   // S5: the pad leaves, its port is released
        CHECK(pad_pace(&r.c));
        CHECK_EQ(r.io.joy[0], 0);
        CHECK_EQ(r.io.joy[1], 0);
        CHECK(r.io.log.empty());
        unplug_all();
    }
    DONE("pad");
}
