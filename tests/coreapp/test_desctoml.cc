// T239.3 P1-1 (D30, D34): TOML A, the description record of version 7 (sections 20 .. 22). The fixture is compiled from tests/coreapp/golden/desc_toml_a.toml with the SDK's module maps
// (app/sdkpkg/maps) by the real tool (gen_bundle.py), so the two implementations are tested against each other: the reader of the board (descrec.cc) reads the io bases, the identity
// and every block, property, field and state, hands a block out like a window, and refuses a record that breaks one of the rules by number.
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

static uint32_t rd(const Bytes &b, uint32_t o) { return b[o] | b[o + 1] << 8 | b[o + 2] << 16 | (uint32_t)b[o + 3] << 24; }
static void wr(Bytes &b, uint32_t o, uint32_t v) { for (int i = 0; i < 4; i++) b[o + i] = (uint8_t)(v >> (8 * i)); }

static int parse(const Bytes &b, DescRecord *d, char *why)
{
    uint8_t *exact = new uint8_t[b.size() ? b.size() : 1];                 // kept: the strings of the record point into it
    memcpy(exact, b.data(), b.size());
    return descrec_parse(exact, (uint32_t)b.size(), d, why, 200);
}

static uint32_t section(const Bytes &b, uint32_t id)
{
    uint32_t off = rd(b, DESCREC_HEADER_V5), end = off + rd(b, DESCREC_HEADER_V5 + 4);
    while (off + 8 <= end) {
        if (rd(b, off) == id) {
            return off;
        }
        off += 8 + rd(b, off + 4);
    }
    return 0;
}

static bool refused(const Bytes &b)
{
    DescRecord *x = new DescRecord;
    char why[200];
    bool r = parse(b, x, why) == -1;
    delete x;
    return r;
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
    CHECK(cf && !cf->core && cf->nprop == 13);
    const DescProp *evt = descrec_prop(*d, *cf, "evt");
    CHECK(evt && evt->width == 32 && evt->dir == DR_DIR_R && evt->offset == 0x20 && evt->nfield == 4);
    const DescField *op = descrec_field(*d, *evt, "op");
    CHECK(op && op->shift == 30 && op->bits == 2 && op->nstate == 3);
    CHECK_STR(m.states[op->first_state + 2].name, "chip_erase");
    CHECK_EQ(m.states[op->first_state + 2].value, 3u);
    const DescProp *mg = descrec_prop(*d, *cf, "fl_magic1");
    CHECK(mg && mg->width == 16 && mg->dir == DR_DIR_W);
    CHECK(descrec_prop(*d, *descrec_block(*d, "cmd_if"), "slot_enable") != 0);

    // a core's block with a property of its own states; one property read and one write share a byte
    const DescBlock *mx = descrec_block(*d, "mixer");
    CHECK(mx && mx->core && mx->nprop == 2);
    const DescProp *mode = descrec_prop(*d, *mx, "mode"), *st = descrec_prop(*d, *mx, "status");
    CHECK(mode && st && mode->offset == st->offset && mode->dir == DR_DIR_W && st->dir == DR_DIR_R);
    CHECK_EQ(mode->nstate, 1u);
    CHECK_STR(m.states[mode->first_state].name, "reset");

    // io.window hands a block out like a window
    const DescWindow *w = descrec_window(*d, "mixer");
    CHECK(w && w->address == 0xA0000500u && w->size == 0x14u);
    CHECK(descrec_window(*d, "ident") != 0 && descrec_window(*d, "no_such") == 0);
    delete d;

    // ---- what the reader refuses
    const uint32_t io = section(good, 20), id = section(good, 21), bl = section(good, 22);
    CHECK(io && id && bl);
    {
        Bytes b = good;
        wr(b, 4, 7 | (216u << 16));
        wr(b, 12, 0);                                                      // flags: the sections' bit is missing
        CHECK(refused(b));
        b = good;
        wr(b, 12, 5);                                                      // flags exactly 4
        CHECK(refused(b));
        b = good;
        wr(b, 24, 216);                                                    // a menu table in a TOML A record
        wr(b, 28, 1);
        CHECK(refused(b));
        b = good;
        wr(b, 4, 6 | (216u << 16));                                        // sections 20 .. 22 in a version 6 record
        CHECK(refused(b));
        b = good;
        wr(b, id, 18);                                                     // a section of the menu in a version 7 record (the key table's id)
        CHECK(refused(b));
        b = good;
        wr(b, id, 20);                                                     // section 20 twice, and no identity
        CHECK(refused(b));
    }
    const uint32_t blk = bl + 8 + 16;                                      // the first block's words: name module owner address size first_prop nprop
    struct Mut { uint32_t at, value; const char *what; };
    const Mut muts[] = {
        { id + 8 + 4, 2, "the build flag is 0 or 1" },
        { bl + 8, 0, "no block" },
        { bl + 8, 65, "more blocks than DR_BLOCKS" },
        { bl + 8 + 12, 0xFFFFFFFF, "a state count that does not fit the length" },
        { blk + 8, 2, "an owner that is not 0 or 1" },
        { blk + 16, 0, "a block of no size" },
        { blk + 16, 0x100001, "a block above 1 MiB" },
        { blk + 12, 0xFFFFFF80u, "a block that ends above 4 GiB" },
        { blk + 20, 1, "properties that are not the next run" },
        { blk + 28 + 12, 0xA0000800u, "a second block that overlaps the first" },
    };
    for (size_t i = 0; i < sizeof(muts) / sizeof(muts[0]); i++) {
        Bytes b = good;
        wr(b, muts[i].at, muts[i].value);
        bool r = refused(b);
        CHECK(r);
        if (!r) {
            printf("  not refused: %s\n", muts[i].what);
        }
    }
    // a block of the core on a register the base drives itself: each register of basereg.def in turn, with its name in the sentence
    {
        int n = 0;
        const BaseReg *regs = descrec_base_regs(&n);
        CHECK(n > 0);
        for (int i = 0; i < n; i++) {
            Bytes b = good;
            wr(b, blk + 28 * 3 + 12, regs[i].address);
            wr(b, blk + 28 * 3 + 16, 1);
            DescRecord *x = new DescRecord;
            CHECK_EQ(parse(b, x, why), -1);
            CHECK(strstr(why, regs[i].name) != 0);
            delete x;
        }
    }
    // the identity block has to be the base's and has to have the readable properties "id" and "build": the core's block "mixer" has neither
    {
        Bytes b = good;
        const uint32_t sec = id + 8;
        wr(b, sec + 12, rd(b, blk + 28 * 3));                              // the block's name offset: "mixer"
        CHECK(refused(b));
    }
    // every truncation is refused
    for (uint32_t cut = 1; cut < good.size(); cut += 7) {
        Bytes b(good.begin(), good.begin() + cut);
        CHECK(refused(b));
    }
    DONE("test_desctoml");
}
