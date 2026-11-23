// T239.3 base 3.3: the key table (section 18), the register windows (section 19) and the job task flag of the description record. The fixture is compiled from
// tests/coreapp/golden/desc_keys.toml by the real tool (gen_bundle.py), so the two implementations are tested against each other: the reader of the board (descrec.cc) reads every field of
// every row, finds a window by name (descrec_window), refuses a window that covers a register the base drives itself - each register of basereg.def in turn, with its name and code
// reference in the sentence - and refuses damaged rows and sections by number; every truncation of the record is refused.
//
//   test_deckeys DESC_KEYS.bin
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

// The place of section `id` in the record: the offset of its header (id, length, words), 0 when the record has none.
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

struct Want {
    const char *layout, *node;
    int lo, hi;
    int intent;
    int arg;
    const char *op;
    unsigned flags;
    const char *when;
};

int main(int argc, char **argv)
{
    setvbuf(stdout, 0, _IONBF, 0);
    CHECK(argc == 2);
    Bytes good = slurp(argv[1]);
    DescRecord *d = new DescRecord;
    char why[200];

    // ---- every field of every row, in the order of the file
    CHECK_EQ(parse(good, d, why), 0);
    CHECK(!d->has_core && d->sec == 0);                                    // a record of the key table and the windows alone has no core section and no room for one
    CHECK_EQ(d->nkeys, 10);
    static const Want rows[] = {
        { "terminal", 0, 0x8E, 0x8E, MI_DECLINE, 0, 0, 0, 0 },
        { "terminal", 0, 0x11, 0x11, MI_DECLINE, 0, 0, 0, "!mon.wants_stop" },
        { "terminal", 0, -1, -1, MI_OP, 0, "mon.key", KF_KEYARG | KF_DIRECTIVE | KF_DIRECT, 0 },
        { "timeline", 0, 0x9D, 0x9D, MI_OP, -1, "tw.step", KF_ARG | KF_STATUS, 0 },
        { "timeline", 0, 0x0F, 0x0F, MI_OP, 0, "tw.monitor", KF_DIRECTIVE, 0 },
        { "timeline", 0, 0x1B, 0x1B, MI_OP, 0, "tw.cancel", KF_CLOSE, 0 },
        { "timeline", 0, 0x0D, 0x0D, MI_OP, 0, "tw.resume", KF_STATUS_OK | KF_CLOSE, 0 },
        { "timeline", 0, 32, 126, MI_SWALLOW, 0, 0, 0, 0 },
        { 0, "scope", 'x', 'x', MI_OP, 7, "tw.step", KF_ARG, 0 },
        { 0, "scope", 0x1FE, 0x1FE, MI_SWALLOW, 0, 0, 0, "tw.on" },
    };
    for (int i = 0; i < 10; i++) {
        const CoreKey &k = d->keys[i];
        const Want &w = rows[i];
        CHECK_STR(k.layout, w.layout);
        CHECK_STR(k.node, w.node);
        CHECK_EQ(k.row.stage, KS_CORE);
        CHECK_EQ(k.row.lo, w.lo);
        CHECK_EQ(k.row.hi, w.hi);
        CHECK_EQ(k.row.intent, w.intent);
        CHECK_EQ(k.row.arg, w.arg);
        CHECK_STR(k.row.op, w.op);
        CHECK_EQ(k.row.flags, w.flags);
        CHECK_STR(k.row.when, w.when);
    }
    CHECK_EQ(d->keys[2].row.lo, -1);                                       // "any"
    CHECK_EQ(d->keys[2].row.hi, -1);

    // ---- the windows, by name
    CHECK_EQ(d->nwindows, 5);
    const DescWindow *w = descrec_window(*d, "audio_mixer");
    CHECK(w != 0);
    CHECK_EQ(w ? w->address : 0, 0xA0000500u);
    CHECK_EQ(w ? w->size : 0, 0x14u);
    w = descrec_window(*d, "u64_userport");                                // the runs of the U64 block that a core may write: next to the base's own registers
    CHECK(w && w->address == 0xA0000405u && w->size == 1);
    w = descrec_window(*d, "u64_hdmi_connectors");
    CHECK(w && w->address == 0xA0000408u && w->size == 2);
    w = descrec_window(*d, "speaker_enable");
    CHECK(w && w->address == 0xA000000Cu && w->size == 1);
    CHECK(descrec_window(*d, "palette") != 0);
    CHECK(descrec_window(*d, "no_such_window") == 0);                      // an unknown name
    CHECK(descrec_window(*d, "") == 0);
    CHECK(descrec_window(*d, "AUDIO_MIXER") == 0);
    CHECK(descrec_window(*d, 0) == 0);
    DescRecord *empty = new DescRecord;
    memset((void *)empty, 0, sizeof(*empty));
    CHECK(descrec_window(*empty, "audio_mixer") == 0);                     // a record with none

    // ---- the job flag of a task
    CHECK_EQ(d->ntasks, 3);
    CHECK(d->tasks[0].job && d->tasks[1].job && !d->tasks[2].job);
    CHECK(d->tasks[1].ask != 0);

    // ---- the registers the base drives itself: the table of the reader, and a window on each of them is refused with its name and its code reference
    int nreg = 0;
    const BaseReg *regs = descrec_base_regs(&nreg);
    CHECK(nreg >= 22);
    const uint32_t win = section(good, 19);
    CHECK(win != 0);
    for (int i = 0; i < nreg; i++) {
        CHECK(descrec_base_reg_hit(regs[i].address, 1) == &regs[i]);       // the register itself
        CHECK(descrec_base_reg_hit(regs[i].address + regs[i].bytes - 1, 4) != 0);          // a block that ends on it
        CHECK(regs[i].name[0] && regs[i].bytes >= 1 && strchr(regs[i].ref, ':') != 0);
        // the window "audio_mixer" (the first of the section, name, address, size at +12, +16, +20) moved onto the register: refused
        Bytes b = good;
        wr(b, win + 16, regs[i].address);
        wr(b, win + 20, 1);
        DescRecord *x = new DescRecord;
        CHECK_EQ(parse(b, x, why), -1);
        CHECK(strstr(why, regs[i].name) != 0);
        CHECK(strstr(why, regs[i].ref) != 0);
        CHECK(strstr(why, "audio_mixer") != 0);
        // a block that starts below and runs over it
        b = good;
        wr(b, win + 16, regs[i].address - 2);
        wr(b, win + 20, 4);
        CHECK_EQ(parse(b, x, why), -1);
        // a block that stops just before it (and does not touch another register) and one that starts just behind it are allowed unless another register is there
        b = good;
        wr(b, win + 16, regs[i].address + regs[i].bytes);
        wr(b, win + 20, 1);
        const bool other = descrec_base_reg_hit(regs[i].address + regs[i].bytes, 1) != 0;
        CHECK_EQ(parse(b, x, why), other ? -1 : 0);
        delete x;
    }
    // a size that runs over a register from below: the mixer block at 0xA0000500 grown to cover the U64 block's base registers
    {
        Bytes b = good;
        wr(b, win + 16, 0xA0000400);
        wr(b, win + 20, 0x100);
        DescRecord *x = new DescRecord;
        CHECK_EQ(parse(b, x, why), -1);
        delete x;
    }

    // ---- damage: every truncation, a duplicate name, and rows that break a rule of the reader
    for (size_t n = 0; n < good.size(); n++) {
        Bytes cut(good.begin(), good.begin() + n);
        DescRecord *x = new DescRecord;
        CHECK_EQ(parse(cut, x, why), -1);
        delete x;
    }
    {
        Bytes b = good;
        wr(b, win + 12 + 12, rd(b, win + 12));                              // window 2 takes window 1's name
        DescRecord *x = new DescRecord;
        CHECK_EQ(parse(b, x, why), -1);
        CHECK(strstr(why, "twice") != 0);
        b = good;
        wr(b, win + 20, 0);                                                 // size 0
        CHECK_EQ(parse(b, x, why), -1);
        b = good;
        wr(b, win + 20, DR_WINDOW_MAX + 1);
        CHECK_EQ(parse(b, x, why), -1);
        b = good;
        wr(b, win + 16, 0xFFFFFFF0u);                                       // ends above 4 GiB
        wr(b, win + 20, 0x20);
        CHECK_EQ(parse(b, x, why), -1);
        b = good;
        wr(b, win + 8, 0);                                                  // no windows
        CHECK_EQ(parse(b, x, why), -1);
        wr(b, win + 8, DR_WINDOWS + 1);
        CHECK_EQ(parse(b, x, why), -1);
        delete x;
    }
    const uint32_t ks = section(good, 18);
    CHECK(ks != 0);
    struct Dmg { const char *what; int row; int word; uint32_t value; };
    static const Dmg dmg[] = {
        { "both a layout and a node", 3, 1, 0 },                           // word 1 is the node's string offset: 0 is the empty string, which is a node, not none
        { "do 3", 3, 4, 3 },
        { "an op row with no operation", 3, 5, 0xFFFFFFFFu },
        { "a decline with an operation", 0, 5, 0 },
        { "an unknown flag", 3, 7, KF_ARG | KF_STATUS | 16 },
        { "KF_ARG and KF_KEYARG", 3, 7, KF_ARG | KF_KEYARG },
        { "a decline with a flag", 0, 7, KF_STATUS },
        { "a value without KF_ARG", 5, 6, 5 },
        { "lo above hi", 3, 2, 0x200 },
        { "a key above 0x1FF", 3, 3, 0x200 },
        { "lo any, hi not", 2, 3, 5 },
        { "an empty condition", 1, 8, 0 },
        { "neither layout nor node", 3, 0, 0xFFFFFFFFu },
    };
    for (const Dmg &m : dmg) {
        Bytes b = good;
        wr(b, ks + 8 + 4 + 36 * m.row + 4 * m.word, m.value);
        DescRecord *x = new DescRecord;
        int rc = parse(b, x, why);
        CHECK_EQ(rc, -1);
        if (rc != -1) {
            printf("  not refused: %s\n", m.what);
        }
        delete x;
    }
    {
        Bytes b = good;
        wr(b, ks + 8, 0);                                                   // no rows
        DescRecord *x = new DescRecord;
        CHECK_EQ(parse(b, x, why), -1);
        wr(b, ks + 8, DR_KEYS + 1);
        CHECK_EQ(parse(b, x, why), -1);
        delete x;
    }
    // a section of the core's data (here the id feature, section 8, well formed) without a core section is refused; the key table and the windows are not "core data"
    {
        const uint32_t soff = rd(good, DESCREC_HEADER_V5);
        Bytes b(good.begin(), good.begin() + soff);
        for (uint32_t w : { 8u, 12u, 0x10u, 1u, 0u }) {
            for (int i = 0; i < 4; i++) b.push_back((uint8_t)(w >> (8 * i)));
        }
        wr(b, 8, (uint32_t)b.size());
        wr(b, DESCREC_HEADER_V5 + 4, (uint32_t)b.size() - soff);
        DescRecord *x = new DescRecord;
        CHECK_EQ(parse(b, x, why), -1);
        CHECK(strstr(why, "core") != 0);
        wr(b, soff, 0xFFFFFFFFu);                                           // (an id nobody knows is refused by its number)
        CHECK_EQ(parse(b, x, why), -1);
        delete x;
    }
    // a task with a hotkey or a folder is no job
    {
        Bytes b = good;
        const uint32_t tk = rd(b, 24 + 8 * 12);                              // the task table (K = 12): label op first_bind nbind flags ...
        CHECK(tk != 0);
        DescRecord *x = new DescRecord;
        wr(b, tk + 16, rd(b, tk + 16) | 2);                                 // pick_dir with job
        CHECK_EQ(parse(b, x, why), -1);
        b = good;
        wr(b, tk + 16, rd(b, tk + 16) | ('A' << 8));                        // a hotkey with job
        CHECK_EQ(parse(b, x, why), -1);
        b = good;
        wr(b, tk + 16, rd(b, tk + 16) | 8);                                 // an unknown flag
        CHECK_EQ(parse(b, x, why), -1);
        delete x;
    }
    DONE("test_deckeys");
}
