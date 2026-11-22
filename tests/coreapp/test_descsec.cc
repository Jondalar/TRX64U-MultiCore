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
        CoreHooks hooks;
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

    // ---- 2. what no descriptor has together
    {
        Bytes rec = slurp(argv[2]);
        DescRecord *d = new DescRecord;
        CHECK_EQ(parse(rec, d, why, sizeof(why)), 0);
        if (g_fails) {
            printf("%s\n", why);
        }
        CoreHooks hooks;
        memset(&hooks, 0, sizeof(hooks));
        CoreDesc got;
        // the ROM check, the keys that are down: the code has neither -> refused, by section
        CHECK_EQ(descrec_core(d, &hooks, &got, why, sizeof(why)), -1);
        CHECK(strstr(why, "section roms") != 0);
        hooks.rom_check[0] = check_a;
        CHECK_EQ(descrec_core(d, &hooks, &got, why, sizeof(why)), -1);
        CHECK(strstr(why, "section raw keys") != 0);
        hooks.rawkeys_held = held_a;
        CHECK_EQ(descrec_core(d, &hooks, &got, why, sizeof(why)), 0);
        g_fields = 0;
        EQN("nroms", got.nroms, 2);
        CHECK(got.roms[0].check == check_a);
        CHECK(got.roms[1].check == 0);
        CHECK(got.rawkeys && got.rawkeys->held == held_a && got.rawkeys->hid == 0 && got.rawkeys->mod == 0 && got.rawkeys->nhid == 0 && got.rawkeys->caps_usage == 0xFF);
        CHECK(got.alt_video != 0);
        if (got.alt_video) {
            const VideoDesc &v = *got.alt_video;
            CHECK_EQ(v.w, 640u); CHECK_EQ(v.h, 200u); CHECK_EQ(v.scale, 2); CHECK_EQ(v.x0, -8); CHECK_EQ(v.y0, 40);
            CHECK_EQ(v.fb_base, 0x02000000u); CHECK_EQ(v.fb_stride, 0x80000u); CHECK_EQ(v.nbuf, 3);
            CHECK_EQ(v.reg_latest, 0xD0); CHECK_EQ(v.reg_seq, 0xD1); CHECK_EQ(v.reg_fb_base, 0xD4); CHECK_EQ(v.reg_fb_stride, 0xD8);
            CHECK_EQ(v.bpp, 2); CHECK_EQ(v.pxfmt, 0); CHECK_EQ(v.h60, 192);
        }
        CHECK(got.romset != 0 && got.romset->version == 0 && got.romset->pick == 0 && got.romset->max == 4 && got.romset->sizes[0] == 8192 && got.romset->sizes[1] == 0);
        CHECK_STR(got.romset->labels->text[0], "Auto");
        CHECK_EQ(got.nmedia, 3);
        if (got.nmedia == 3) {
            CHECK_STR(got.media[0].replace, "Replace Cartridge");
            CHECK(got.media[1].replace == 0);
            CHECK_STR(got.media[2].replace, "Replace Disk");
        }
        CHECK(got.reset_writes == 0 && got.nreset_writes == 0 && got.window_media == 0 && got.sector_media == 0 && got.mouse == 0 && got.fbwin == 0 && got.nreport_regs == 0);
        // the ROM set names functions in its flags: the extra record has none, so any hooks are not used
        hooks.romset_version = version_a;
        hooks.romset_pick = pick_a;
        CHECK_EQ(descrec_core(d, &hooks, &got, why, sizeof(why)), 0);
        CHECK(got.romset->version == 0 && got.romset->pick == 0);
    }

    // ---- 3. damage, by section
    {
        Bytes rec = slurp(argv[1]);
        DescRecord *d = new DescRecord;
        struct S { uint32_t id; const char *name; };
        const S secs[] = { { 7, "reset writes" }, { 8, "id feature" }, { 9, "report registers" }, { 10, "raw keys" }, { 12, "picture window" }, { 13, "mouse" },
                           { 14, "rom set" }, { 15, "window media" }, { 16, "sector media" } };
        for (size_t i = 0; i < sizeof(secs) / sizeof(secs[0]); i++) {
            const uint32_t at = section_at(rec, secs[i].id);
            CHECK(at != 0);
            if (!at) {
                continue;
            }
            Bytes b = rec;
            patch(b, at + 4, rd(rec, at + 4) + 4);             // a word too long
            CHECK_EQ(parse(b, d, why, sizeof(why)), -1);
            CHECK(strstr(why, secs[i].name) != 0);
            b = rec;
            patch(b, at + 8, 0xFFFFFFF0u);                     // the first word: a count, a register or a string offset, out of range in each
            int r = parse(b, d, why, sizeof(why));
            CHECK(r != 0 || secs[i].id == 12);
            if (r) {
                CHECK(strstr(why, secs[i].name) != 0);
            }
        }
        // the extra record: media replace in the wrong order, a slot that is not there
        Bytes ex = slurp(argv[2]);
        const uint32_t at = section_at(ex, 17);
        CHECK(at != 0);
        if (at) {
            Bytes b = ex;
            patch(b, at + 8 + 4, 2);                           // the first entry names slot 2, the second slot 2 as well
            CHECK_EQ(parse(b, d, why, sizeof(why)), -1);
            CHECK(strstr(why, "media replace") != 0);
            b = ex;
            patch(b, at + 8 + 4 + 8, 9);                       // a slot that is not there
            CHECK_EQ(parse(b, d, why, sizeof(why)), -1);
            CHECK(strstr(why, "media replace") != 0);
        }
        // the ROM flag bit 2 is known, bit 3 is not
        const uint32_t ra = section_at(ex, 3);
        CHECK(ra != 0);
        if (ra) {
            Bytes b = ex;
            patch(b, ra + 8 + 4 + 8, rd(b, ra + 8 + 4 + 8) | 8);
            CHECK_EQ(parse(b, d, why, sizeof(why)), -1);
            CHECK(strstr(why, "section roms") != 0);
        }
    }
    DONE("descsec");
}
