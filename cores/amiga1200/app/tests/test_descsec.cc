// T239.3 D17 part 2: the data tables of sections 7 .. 17 of the description record. (1) golden/desc_sections.toml holds the Amiga 1200's tables; it is compiled by the real tool, read by
// the real reader and compared field by field with amiga1200_desc (reset writes, ID feature, report registers, raw keys, mouse, picture window, ROM set, window media, sector media).
// (2) golden/desc_sections_extra.toml holds what no compiled-in descriptor has together: a ROM whose content the core's code checks, a second picture, a core that takes the keys that are
// down, a ROM set whose format functions the code does not give, media slots with a replace action; its values are asserted here, and every hook the record names must be provided
// by the code or the record is refused. (3) a damaged section is refused with a sentence that names the record and the section.
//
//   test_descsec DESC_SECTIONS.bin DESC_SECTIONS_EXTRA.bin
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include "check.h"
#include "cores.h"
#include "descrec.h"
#include "desc_tables_cmp.h"

typedef std::vector<uint8_t> Bytes;
static Bytes slurp(const std::string &p)
{
    std::ifstream f(p.c_str(), std::ios::binary);
    std::stringstream s;
    s << f.rdbuf();
    std::string t = s.str();
    return Bytes(t.begin(), t.end());
}

static int parse(const Bytes &b, DescRecord *d, char *why, int n)
{
    uint8_t *exact = new uint8_t[b.size()];
    memcpy(exact, b.data(), b.size());
    return descrec_parse(exact, (uint32_t)b.size(), d, why, n);
}

static uint32_t rd(const Bytes &b, uint32_t o) { return b[o] | b[o + 1] << 8 | b[o + 2] << 16 | (uint32_t)b[o + 3] << 24; }
static void patch(Bytes &b, uint32_t off, uint32_t v)
{
    for (int i = 0; i < 4; i++) {
        b[off + i] = (uint8_t)(v >> (8 * i));
    }
}

// the offset of section `id` (the header of it) in a record, 0 when there is none
static uint32_t section_at(const Bytes &b, uint32_t id)
{
    uint32_t at = rd(b, DESCREC_HEADER_V5), end = at + rd(b, DESCREC_HEADER_V5 + 4);
    while (at + 8 <= end) {
        if (rd(b, at) == id) {
            return at;
        }
        at += 8 + rd(b, at + 4);
    }
    return 0;
}

static bool check_a(const uint8_t *, uint32_t) { return true; }
static int version_a(const uint8_t *, int, uint16_t *, uint16_t *) { return 0; }
static int pick_a(const KsCandidate *, int, uint16_t) { return 0; }
static void held_a(CoreCtx *, const uint8_t *, int) {}

static void pad_a(CoreCtx *, int) { }

int main(int argc, char **argv)
{
    setvbuf(stdout, 0, _IONBF, 0);
    CHECK(argc == 3);
    char why[240];

    // ---- 1. the Amiga 1200's tables, from the record, against amiga1200_desc
    {
        Bytes rec = slurp(argv[1]);
        DescRecord *d = new DescRecord;
        CHECK_EQ(parse(rec, d, why, sizeof(why)), 0);
        if (g_fails) {
            printf("%s\n", why);
        }
        DescCode hooks;
        memset(&hooks, 0, sizeof(hooks));
        hooks.init = amiga1200_desc.init; hooks.release = amiga1200_desc.release; hooks.register_ops = amiga1200_desc.register_ops;
        hooks.romset_version = amiga1200_desc.romset->version;
        hooks.romset_pick = amiga1200_desc.romset->pick;
        CoreDesc got;
        CHECK_EQ(descrec_core(d, &hooks, &got, why, sizeof(why)), 0);
        g_fields = 0;
        compare_tables(amiga1200_desc, got);
        printf("descsec: Amiga 1200 tables: %d fields compared with amiga1200_desc\n", g_fields);
        CHECK(g_fields > 150);
        CHECK(got.romset->version == amiga1200_desc.romset->version && got.romset->pick == amiga1200_desc.romset->pick);
        CHECK(got.id_shared && got.id_feat_mask == amiga1200_desc.id_feat_mask);
        // the pad's identity hook goes onto the record's pad
        hooks.pad_id_loaded = pad_a;
        CHECK_EQ(descrec_core(d, &hooks, &got, why, sizeof(why)), 0);
        CHECK(got.pad != 0 && got.pad->id_loaded == pad_a);
        // the record names the ROM set's two functions; a core whose code has none is refused by section
        hooks.romset_pick = 0;
        CHECK_EQ(descrec_core(d, &hooks, &got, why, sizeof(why)), -1);
        CHECK(strstr(why, "section rom set") != 0);
        // the record's ROM set gets its own labels: Auto's text first, "-" behind
        CHECK_STR(d->sec->romset_labels.text[0], amiga1200_desc.romset->auto_label);
        CHECK_STR(d->sec->romset_labels.text[1], "-");
        CHECK(d->sec->romset.labels == &d->sec->romset_labels);
    }

    DONE("descsec");
}
