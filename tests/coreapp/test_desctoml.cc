// T239.3 P1-1 (D30, D34): TOML A, the description record of version 7 (sections 20 .. 22). The fixture is compiled from tests/coreapp/golden/desc_toml_a.toml with the SDK's module maps
// (app/sdkpkg/maps) by the real tool (gen_bundle.py), so the two implementations are tested against each other: the reader of the board (descrec.cc) reads the io bases, the identity
// and every block, property, field and state, hands a block out like a window.
//
//   test_desctoml DESC_TOML_A.bin
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include "check.h"
#include "descrec.h"

typedef std::vector<uint8_t> Bytes;

static Bytes slurp(const char *p)
{
    std::ifstream f(p, std::ios::binary);
    std::stringstream s;
    s << f.rdbuf();
    std::string t = s.str();
    return Bytes(t.begin(), t.end());
}

static int parse(const Bytes &b, DescRecord *d, char *why)
{
    uint8_t *exact = new uint8_t[b.size() ? b.size() : 1];                 // kept: the strings of the record point into it
    memcpy(exact, b.data(), b.size());
    return descrec_parse(exact, (uint32_t)b.size(), d, why, 200);
}

int main(int argc, char **argv)
{
    setvbuf(stdout, 0, _IONBF, 0);
    CHECK(argc == 2);
    Bytes good = slurp(argv[1]);
    DescRecord *d = new DescRecord;
    char why[200];

    CHECK_EQ(parse(good, d, why), 0);
    CHECK_EQ(good[4], 7);                                                  // version 7
    CHECK(d->map != 0 && d->sec == 0 && !d->has_core && d->nwindows == 0 && d->nkeys == 0);
    const DescMap &m = *d->map;
    CHECK_EQ(m.iobase, 0x04000000u);
    CHECK_EQ(m.u2p_io_base, 0xA0000000u);
    CHECK_STR(m.ident_id, "TSTA");
    CHECK(m.has_build);
    CHECK_EQ(m.build, 0x1234ABCDu);
    CHECK_STR(m.ident_block, "ident");
    CHECK_EQ(m.nblocks, 4);

    const DescBlock *ib = descrec_block(*d, "ident");
    CHECK(ib && !ib->core);
    CHECK_STR(ib->module, "trx_io");
    CHECK_EQ(ib->win.address, 0xA0000800u);
    CHECK_EQ(ib->win.size, 0x100u);
    const DescProp *caps = descrec_prop(*d, *ib, "caps");
    CHECK(caps && caps->width == 8 && caps->dir == DR_DIR_R && caps->offset == 8 && caps->nfield == 2);
    const DescField *dbg = descrec_field(*d, *caps, "debug");
    CHECK(dbg && dbg->shift == 3 && dbg->bits == 1 && dbg->nstate == 0);
    CHECK(descrec_prop(*d, *ib, "nope") == 0 && descrec_field(*d, *caps, "nope") == 0 && descrec_block(*d, "nope") == 0 && descrec_block(*d, 0) == 0);

    // the map of the SDK's c64cart, in the block of its register range: a field with states, a 32 bit property, a 16 bit one
    const DescBlock *cf = descrec_block(*d, "cart_flash");
    CHECK(cf && !cf->core && cf->nprop == 14);
    const DescProp *evt = descrec_prop(*d, *cf, "evt");
    CHECK(evt && evt->width == 32 && evt->dir == DR_DIR_R && evt->offset == 0x20 && evt->nfield == 4);
    const DescField *op = descrec_field(*d, *evt, "op");
    CHECK(op && op->shift == 30 && op->bits == 2 && op->nstate == 3);
    CHECK_STR(m.states[op->first_state + 2].name, "chip_erase");
    CHECK_EQ(m.states[op->first_state + 2].value, 3u);
    const DescProp *mg = descrec_prop(*d, *cf, "fl_magic1");
    CHECK(mg && mg->width == 16 && mg->dir == DR_DIR_W);
    CHECK(descrec_prop(*d, *descrec_block(*d, "cmd_if"), "slot_enable") != 0);
    // a property with an element count: the 43 byte registers of the cartridge descriptor; a single register has the count 1
    const DescProp *desc = descrec_prop(*d, *cf, "desc");
    CHECK(desc && desc->width == 8 && desc->dir == DR_DIR_W && desc->offset == 0x40 && desc->count == 43);
    CHECK_EQ(evt->count, 1u);
    CHECK_EQ(mg->count, 1u);

    // a core's block with a property of its own states; one property read and one write share a byte
    const DescBlock *mx = descrec_block(*d, "mixer");
    CHECK(mx && mx->core && mx->nprop == 4);
    const DescProp *mode = descrec_prop(*d, *mx, "mode"), *st = descrec_prop(*d, *mx, "status");
    CHECK(mode && st && mode->offset == st->offset && mode->dir == DR_DIR_W && st->dir == DR_DIR_R);
    CHECK_EQ(mode->nstate, 1u);
    CHECK_STR(m.states[mode->first_state].name, "reset");

    // two interleaved runs: the level and the pan word of four voices, 4 bytes from one element to the next (element i of the second is 2 bytes above the same element of the first)
    const DescProp *lvl = descrec_prop(*d, *mx, "lvl"), *pan = descrec_prop(*d, *mx, "pan");
    CHECK(lvl && pan && lvl->width == 16 && pan->width == 16 && lvl->count == 4 && pan->count == 4 && lvl->stride == 4 && pan->stride == 4);
    CHECK(lvl->offset == 0x04 && pan->offset == 0x06 && lvl->dir == DR_DIR_W && pan->dir == DR_DIR_W);
    CHECK_EQ(mode->stride, 1u);                                       // a single register: the stride is the width in bytes
    CHECK_EQ(desc->stride, 1u);                                       // consecutive registers: the same

    // io.window hands a block out like a window
    const DescWindow *w = descrec_window(*d, "mixer");
    CHECK(w && w->address == 0xA0000500u && w->size == 0x14u);
    CHECK(descrec_window(*d, "ident") == 0 && descrec_window(*d, "no_such") == 0);       // a block of the base is no window: the shared drivers reach it by descrec_block
    // a block of the core outside the I/O span is read (T239.3 V-3c, decided 2026-11-24: refused at the grant, not at the read; a block never handed out as a window may lie anywhere) and is
    // not granted: descrec_window gives it out, io.window (libsvc.cc) checks the span with descrec_in_io_span. The address word of "mixer" (0xA0000500) is patched in the record
    const unsigned char at[4] = { 0x00, 0x05, 0x00, 0xA0 };
    size_t pos = 0;
    int hits = 0;
    for (size_t i = 0; i + 4 <= good.size(); i++) {
        if (!memcmp(&good[i], at, 4)) {
            pos = i;
            hits++;
        }
    }
    CHECK_EQ(hits, 1);
    const uint32_t bad[2] = { 0x11100550u, 0x04060000u };                     // an alias above the span; below it (the firmware's I/O space, not the TOML A's)
    for (int k = 0; k < 2 && hits == 1; k++) {
        Bytes patched = good;
        for (int j = 0; j < 4; j++) {
            patched[pos + j] = (uint8_t)(bad[k] >> (8 * j));
        }
        DescRecord *p = new DescRecord;
        CHECK_EQ(parse(patched, p, why), 0);
        const DescWindow *pw = descrec_window(*p, "mixer");
        CHECK(pw && pw->address == bad[k] && !descrec_in_io_span(pw->address, pw->size));      // read as written; the grant of io.window refuses it
        delete p;
    }
    delete d;

    DONE("test_desctoml");
}
