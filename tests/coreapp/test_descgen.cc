// T239.3 D17 step 3, first half: a core whose data description comes from its .cor. The VIC-20's description (cores/vc20/app/desc.toml) is compiled by the real tool
// (tools/descrec.py, through tests/coreapp/gen_bundle.py), read back by the real generic reader (descrec.cc: descrec_parse, descrec_core; bundle.cc and start.cc:
// the bundle's record found, hashed, parsed, the descriptor made), and the descriptor is compared field by field with the compiled-in vc20_desc: every string, count,
// table and entry of it. The function hooks are not data: the test hands the reader the hooks of vc20_desc, as the compiled-in code provides them, and reports each.
// What the record does not cover is asserted to be empty in vc20_desc, so a table the reader does not read cannot hide behind equal zeros.
//
//   test_descgen DESC_VC20.bin VC20.cor VC20_OLD.cor
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include "check.h"
#include "cores.h"
#include "fakes.h"
#include "bundle.h"
#include "start.h"
#include "handover.h"
#include "hotkeys.h"
#include "desc_check.h"

const CoreDesc *const core_table[] = { 0 };               // no descriptor is compiled in for the start of this test: the bundle's record is all there is

typedef std::vector<uint8_t> Bytes;
static Bytes slurp(const std::string &p)
{
    std::ifstream f(p.c_str(), std::ios::binary);
    std::stringstream s;
    s << f.rdbuf();
    std::string t = s.str();
    return Bytes(t.begin(), t.end());
}

static int g_fields;                                       // what has been compared

#define EQN(what, a, b) do { g_fields++; CHECK_EQ((long long)(a), (long long)(b)); (void)(what); } while (0)
#define EQS(what, a, b) do { g_fields++; CHECK_STR((a), (b)); (void)(what); } while (0)
#define NUL(what, a) do { g_fields++; CHECK((a) == 0); (void)(what); } while (0)

static void eq_binds(const OpBind *a, int na, const OpBind *b, int nb)
{
    EQN("nbind", na, nb);
    for (int i = 0; i < na && i < nb; i++) {
        EQS("bind arg", a[i].arg, b[i].arg);
        EQN("bind kind", a[i].kind, b[i].kind);
        EQN("bind i", a[i].i, b[i].i);
        EQS("bind s", a[i].s, b[i].s);
    }
}

static void eq_actions(const FileAction *a, int na, const FileAction *b, int nb)
{
    EQN("nactions", na, nb);
    for (int i = 0; i < na && i < nb; i++) {
        EQS("action label", a[i].label, b[i].label);
        EQS("action op", a[i].op, b[i].op);
        eq_binds(a[i].bind, a[i].nbind, b[i].bind, b[i].nbind);
        EQN("action closes_menu", a[i].closes_menu, b[i].closes_menu);
        EQS("action ask", a[i].ask, b[i].ask);
        EQN("action ask_off", a[i].ask_off, b[i].ask_off);
        EQN("action nsize_in", a[i].nsize_in, b[i].nsize_in);
        for (int k = 0; k < a[i].nsize_in && k < b[i].nsize_in; k++) {
            EQN("action size_in", a[i].size_in[k], b[i].size_in[k]);
        }
        EQN("action key", a[i].key, b[i].key);
        EQS("action show_state", a[i].show_state, b[i].show_state);
        EQS("action repeat_op", a[i].repeat_op, b[i].repeat_op);
        EQN("action index", a[i].index, b[i].index);
        EQN("action show", a[i].show, b[i].show);
    }
}

// every field of the descriptor `want` (the compiled-in one) against `got` (made of the record)
static void compare(const CoreDesc &want, const CoreDesc &got)
{
    EQN("id", memcmp(want.id, got.id, 4), 0);
    EQS("cor_name", want.cor_name, got.cor_name);
    EQS("title", want.title, got.title);
    EQS("short_title", want.short_title, got.short_title);
    EQS("folder", want.folder, got.folder);
    EQS("store", want.store, got.store);
    EQN("min_contract", want.min_contract, got.min_contract);

    EQN("nroms", want.nroms, got.nroms);
    for (int i = 0; i < want.nroms && i < got.nroms; i++) {
        const RomSpec &a = want.roms[i], &b = got.roms[i];
        EQS("rom role", a.role, b.role);
        for (int k = 0; k < ROM_NAMES_MAX; k++) {
            EQS("rom name", a.names[k], b.names[k]);
        }
        for (int k = 0; k < ROM_SIZES_MAX; k++) {
            EQN("rom size", a.sizes[k], b.sizes[k]);
        }
        EQN("rom required", a.required, b.required);
        EQS("rom put_op", a.put_op, b.put_op);
        EQN("rom set", a.set, b.set);
        EQN("rom shared", a.shared, b.shared);
        NUL("rom check (code)", a.check);
        NUL("rom check (code, record)", b.check);
    }

    EQN("nsettings", want.nsettings, got.nsettings);
    for (int i = 0; i < want.nsettings && i < got.nsettings; i++) {
        const Setting &a = want.settings[i], &b = got.settings[i];
        EQS("setting key", a.key, b.key);
        EQN("setting nvalues", a.nvalues, b.nvalues);
        for (int k = 0; k < a.nvalues && k < b.nvalues; k++) {
            EQS("setting value", a.values[k], b.values[k]);
        }
        EQN("setting def", a.def, b.def);
        EQN("setting when", a.when, b.when);
        EQS("setting apply", a.apply, b.apply);
        EQN("setting has bind", a.bind != 0, b.bind != 0);
        if (a.bind && b.bind) {
            EQN("bind reg", a.bind->reg, b.bind->reg);
            EQN("bind shift", a.bind->shift, b.bind->shift);
            EQN("bind mask", a.bind->mask, b.bind->mask);
            EQN("bind has bytes", a.bind->bytes != 0, b.bind->bytes != 0);
            for (int k = 0; a.bind->bytes && b.bind->bytes && k < a.nvalues; k++) {
                EQN("bind byte", a.bind->bytes[k], b.bind->bytes[k]);
            }
        }
        EQN("setting base", a.base, b.base);
        EQS("setting group", a.group, b.group);
        EQN("setting kind", a.kind, b.kind);
        EQS("setting show_state", a.show_state, b.show_state);
        EQS("setting label", a.label, b.label);
        EQS("setting lock_state", a.lock_state, b.lock_state);
    }

    EQN("ntasks", want.ntasks, got.ntasks);
    for (int i = 0; i < want.ntasks && i < got.ntasks; i++) {
        const Task &a = want.tasks[i], &b = got.tasks[i];
        EQS("task label", a.label, b.label);
        EQS("task op", a.op, b.op);
        eq_binds(a.bind, a.nbind, b.bind, b.nbind);
        EQN("task closes_menu", a.closes_menu, b.closes_menu);
        EQS("task ask", a.ask, b.ask);
        EQS("task ask_state", a.ask_state, b.ask_state);
        EQN("task pick_dir", a.pick_dir, b.pick_dir);
        EQS("task pick_label", a.pick_label, b.pick_label);
        EQS("task group", a.group, b.group);
        EQN("task hotkey", a.hotkey, b.hotkey);
        EQS("task show_state", a.show_state, b.show_state);
    }

    EQN("nfiletypes", want.nfiletypes, got.nfiletypes);
    for (int i = 0; i < want.nfiletypes && i < got.nfiletypes; i++) {
        const FileType &a = want.filetypes[i], &b = got.filetypes[i];
        EQS("filetype ext", a.ext, b.ext);
        eq_actions(a.actions, a.nactions, b.actions, b.nactions);
        EQS("filetype inner_ext", a.inner_ext, b.inner_ext);
        eq_actions(a.inner, a.ninner, b.inner, b.ninner);
    }

    EQN("nmedia", want.nmedia, got.nmedia);
    for (int i = 0; i < want.nmedia && i < got.nmedia; i++) {
        const MediaSlot &a = want.media[i], &b = got.media[i];
        EQS("media name", a.name, b.name);
        EQS("media state", a.state, b.state);
        EQS("media eject_op", a.eject_op, b.eject_op);
        eq_binds(a.eject_bind, a.neject_bind, b.eject_bind, b.neject_bind);
        EQS("media group", a.group, b.group);
        EQS("media exts", a.exts, b.exts);
        EQS("media mount", a.mount, b.mount);
        EQS("media empty_tasks", a.empty_tasks, b.empty_tasks);
        EQS("media mounted_tasks", a.mounted_tasks, b.mounted_tasks);
        EQN("media eject_closes", a.eject_closes, b.eject_closes);
        EQS("media replace", a.replace, b.replace);
    }

    const VideoDesc &va = want.video, &vb = got.video;
    EQN("video w", va.w, vb.w); EQN("video h", va.h, vb.h); EQN("video scale", va.scale, vb.scale);
    EQN("video x0", va.x0, vb.x0); EQN("video y0", va.y0, vb.y0);
    EQN("video fb_base", va.fb_base, vb.fb_base); EQN("video fb_stride", va.fb_stride, vb.fb_stride); EQN("video nbuf", va.nbuf, vb.nbuf);
    EQN("video reg_latest", va.reg_latest, vb.reg_latest); EQN("video reg_seq", va.reg_seq, vb.reg_seq);
    EQN("video reg_fb_base", va.reg_fb_base, vb.reg_fb_base); EQN("video reg_fb_stride", va.reg_fb_stride, vb.reg_fb_stride);
    EQN("video bpp", va.bpp, vb.bpp); EQN("video pxfmt", va.pxfmt, vb.pxfmt); EQN("video h60", va.h60, vb.h60);
    EQN("freeze reg_status", want.freeze.reg_status, got.freeze.reg_status);
    EQN("freeze mask", want.freeze.mask, got.freeze.mask);
    EQN("freeze timeout_ms", want.freeze.timeout_ms, got.freeze.timeout_ms);
    EQN("freeze word", want.freeze.word, got.freeze.word);

    EQN("reset_ms", want.reset_ms, got.reset_ms);
    EQS("service_op", want.service_op, got.service_op);
    EQN("service_period_ms", want.service_period_ms, got.service_period_ms);
    EQN("poll_when_frozen", want.poll_when_frozen, got.poll_when_frozen);
    EQS("needs_program", want.needs_program, got.needs_program);
    EQS("program_ext", want.program_ext, got.program_ext);
    EQN("ncaps", want.ncaps, got.ncaps);
    for (int i = 0; i < want.ncaps && i < got.ncaps; i++) {
        EQS("cap", want.caps[i], got.caps[i]);
    }
    EQN("board_caps", want.board_caps, got.board_caps);
    EQN("id_shared", want.id_shared, got.id_shared);
    EQS("type_op", want.type_op, got.type_op);
    EQS("run_op", want.run_op, got.run_op);
    EQN("cold_ram_bytes", want.cold_ram_bytes, got.cold_ram_bytes);
    EQN("id_feat_reg", want.id_feat_reg, got.id_feat_reg);
    EQN("id_feat_mask", want.id_feat_mask, got.id_feat_mask);
    EQN("id_feat_val", want.id_feat_val, got.id_feat_val);
    EQN("menu_rec_n", want.menu_rec_n, got.menu_rec_n);
    EQS("core_id", want.core_id, got.core_id);
    EQN("nreport_regs", want.nreport_regs, got.nreport_regs);
    EQN("own_volume", want.own_volume, got.own_volume);
    EQN("live_ms", want.live_ms, got.live_ms);
    EQN("menu_first", want.menu_first, got.menu_first);
    EQN("no_tone", want.no_tone, got.no_tone);
    EQS("machine_info", want.machine_info, got.machine_info);
    EQN("cold_ram_only", want.cold_ram_only, got.cold_ram_only);

    // what the record has no section for yet: empty in the compiled-in descriptor and in the made one (a table the reader skips cannot hide behind equal zeros)
    NUL("reset_writes", want.reset_writes); NUL("nreset_writes", want.nreset_writes);
    NUL("romset", want.romset); NUL("window_media", want.window_media); NUL("sector_media", want.sector_media);
    NUL("mouse", want.mouse); NUL("fbwin", want.fbwin); NUL("menu_rec", want.menu_rec); NUL("report_regs", want.report_regs);
    NUL("rawkeys", want.rawkeys); NUL("pad", want.pad); NUL("alt_video", want.alt_video);
    NUL("got reset_writes", got.reset_writes); NUL("got nreset_writes", got.nreset_writes);
    NUL("got romset", got.romset); NUL("got window_media", got.window_media); NUL("got sector_media", got.sector_media);
    NUL("got mouse", got.mouse); NUL("got fbwin", got.fbwin); NUL("got menu_rec", got.menu_rec); NUL("got report_regs", got.report_regs);
    NUL("got rawkeys", got.rawkeys); NUL("got pad", got.pad); NUL("got alt_video", got.alt_video);

    // the mount slots: the count and the names are the record's, the three functions are the core's code
    EQN("has mount_slots", want.mount_slots != 0, got.mount_slots != 0);
    if (want.mount_slots && got.mount_slots) {
        EQN("mount n", want.mount_slots->n, got.mount_slots->n);
        for (int i = 0; i < want.mount_slots->n && i < got.mount_slots->n; i++) {
            EQS("mount name", want.mount_slots->names[i], got.mount_slots->names[i]);
        }
    }

    // the hotkeys
    EQN("has hotkeys", want.hotkeys != 0, got.hotkeys != 0);
    if (want.hotkeys && got.hotkeys) {
        const HkSpec &a = *want.hotkeys, &b = *got.hotkeys;
        EQN("hk mod_a", a.def.mod_a, b.def.mod_a);
        EQN("hk mod_b", a.def.mod_b, b.def.mod_b);
        for (int i = 0; i < HK_NACT; i++) {
            EQN("hk act", a.def.act[i], b.def.act[i]);
        }
        EQN("hk ntask", a.def.ntask, b.def.ntask);
        for (int i = 0; i < HK_TASKS_MAX; i++) {
            EQN("hk task", a.def.task[i], b.def.task[i]);
        }
        EQS("hk vol_key", a.vol_key, b.vol_key);
        EQS("hk swap_key", a.swap_key, b.swap_key);
        EQN("hk own_keys lo", (uint32_t)a.own_keys, (uint32_t)b.own_keys);
        EQN("hk own_keys hi", (uint32_t)(a.own_keys >> 32), (uint32_t)(b.own_keys >> 32));
        EQS("hk core_op", a.core_op, b.core_op);
        EQS("hk core_key", a.core_key, b.core_key);
        EQS("hk core_label", a.core_label, b.core_label);
        EQN("hk pc_keys", a.pc_keys, b.pc_keys);
    }
}

// the hooks of the compiled-in descriptor: what the record cannot hold
static CoreHooks hooks_of(const CoreDesc &d)
{
    CoreHooks h;
    memset(&h, 0, sizeof(h));
    h.init = d.init; h.release = d.release; h.warm_reset = d.warm_reset; h.cold_start = d.cold_start; h.media_flush = d.media_flush;
    h.media_pending = d.media_pending; h.register_ops = d.register_ops; h.rom_key = d.rom_key; h.mount_slots = d.mount_slots;
    return h;
}

static void report_hooks(const CoreDesc &want, const CoreDesc &got)
{
    struct H { const char *name; bool want, got; bool same; };
    const H hs[] = {
        { "init", want.init != 0, got.init != 0, want.init == got.init },
        { "release", want.release != 0, got.release != 0, want.release == got.release },
        { "warm_reset", want.warm_reset != 0, got.warm_reset != 0, want.warm_reset == got.warm_reset },
        { "cold_start", want.cold_start != 0, got.cold_start != 0, want.cold_start == got.cold_start },
        { "register_ops", want.register_ops != 0, got.register_ops != 0, want.register_ops == got.register_ops },
        { "media_flush", want.media_flush != 0, got.media_flush != 0, want.media_flush == got.media_flush },
        { "media_pending", want.media_pending != 0, got.media_pending != 0, want.media_pending == got.media_pending },
        { "rom_key", want.rom_key != 0, got.rom_key != 0, want.rom_key == got.rom_key },
        { "mount_slots.spec", want.mount_slots && want.mount_slots->spec, got.mount_slots && got.mount_slots->spec,
          want.mount_slots && got.mount_slots && want.mount_slots->spec == got.mount_slots->spec },
        { "mount_slots.mount", want.mount_slots && want.mount_slots->mount, got.mount_slots && got.mount_slots->mount,
          want.mount_slots && got.mount_slots && want.mount_slots->mount == got.mount_slots->mount },
        { "mount_slots.eject", want.mount_slots && want.mount_slots->eject, got.mount_slots && got.mount_slots->eject,
          want.mount_slots && got.mount_slots && want.mount_slots->eject == got.mount_slots->eject },
    };
    for (size_t i = 0; i < sizeof(hs) / sizeof(hs[0]); i++) {
        CHECK(hs[i].same);
        printf("hook %-18s %s (provided by the compiled-in code)\n", hs[i].name, hs[i].want ? "set" : "null");
    }
}

static void patch(Bytes &b, uint32_t off, uint32_t v)
{
    for (int i = 0; i < 4; i++) {
        b[off + i] = (uint8_t)(v >> (8 * i));
    }
}
static uint32_t rd(const Bytes &b, uint32_t o) { return b[o] | b[o + 1] << 8 | b[o + 2] << 16 | (uint32_t)b[o + 3] << 24; }

// the record in a buffer of exactly its size: a read beyond it is the AddressSanitizer's to see
static int parse(const Bytes &b, DescRecord *d, char *why, int n)
{
    uint8_t *exact = new uint8_t[b.size()];
    memcpy(exact, b.data(), b.size());
    return descrec_parse(exact, (uint32_t)b.size(), d, why, n);
}

int main(int argc, char **argv)
{
    setvbuf(stdout, 0, _IONBF, 0);
    CHECK(argc == 4);
    Bytes rec = slurp(argv[1]);
    CHECK(rec.size() > DESCREC_HEADER_V6);
    const CoreHooks hooks = hooks_of(vc20_desc);

    // ---- 1. the record, read by the generic reader, against vc20_desc
    {
        DescRecord *d = new DescRecord;
        char why[240];
        CHECK_EQ(parse(rec, d, why, sizeof(why)), 0);
        if (g_fails) {
            printf("%s\n", why);
        }
        CHECK(d->has_core && d->has_own);
        CoreDesc got;
        CHECK_EQ(descrec_core(d, &hooks, &got, why, sizeof(why)), 0);
        g_fields = 0;
        compare(vc20_desc, got);
        printf("descgen: record -> descriptor: %d fields compared with vc20_desc\n", g_fields);
        report_hooks(vc20_desc, got);
        CHECK(desc_check(&got) == 0);                          // the made descriptor passes the application's own descriptor check as the compiled-in one does
        if (desc_check(&got)) {
            printf("desc_check: %s\n", desc_check(&got));
        }
        // the descriptor with no code: nothing is taken from another descriptor
        CoreDesc bare;
        CHECK_EQ(descrec_core(d, 0, &bare, why, sizeof(why)), -1);      // the record names a mount slot, and no code serves it
        CHECK(strstr(why, "mount slots") != 0);
        CoreHooks none = hooks;
        none.mount_slots = 0;
        none.init = none.release = 0;
        CHECK_EQ(descrec_core(d, &none, &bare, why, sizeof(why)), -1);
    }

    // ---- 2. the same through the start: no descriptor compiled in, the bundle the hand-over names makes it
    {
        FakeFs fs;
        FakeIo io;
        fs.put("/usb0/cores/vc20.cor", slurp(argv[2]));
        fs.put("/usb0/cores/vc20_old.cor", slurp(argv[3]));
        BundleDescSource src(&fs, &hooks);
        Handover h;
        char msg[400];
        const CoreDesc *d = 0;
        handover_fill(&h, "/usb0/cores/vc20.cor", "VIC-20", "0.3.0", 0x820eded2u);
        CHECK_EQ(start_identify((const uint8_t *)"VC20", &h, &d, msg, sizeof(msg), 0, &src), START_OK);
        CHECK(d != 0);
        if (d) {
            g_fields = 0;
            compare(vc20_desc, *d);
            printf("descgen: bundle -> start_identify: %d fields compared with vc20_desc\n", g_fields);
            report_hooks(vc20_desc, *d);
        }
        CHECK_STR(msg, "[core] VC20 VIC-20, cor /usb0/cores/vc20.cor");
        // without a source the start is what it was
        CHECK_EQ(start_identify((const uint8_t *)"VC20", &h, &d, msg, sizeof(msg)), START_NO_DESCRIPTOR);
        // the ID the FPGA answers is not the record's
        CHECK_EQ(start_identify((const uint8_t *)"VC21", &h, &d, msg, sizeof(msg), 0, &src), START_NO_DESCRIPTOR);
        CHECK(strstr(msg, "VC20") != 0 && strstr(msg, "VC21") != 0);
        // a bundle with no record: the core stays held, and the sentence names the bundle and the record
        handover_fill(&h, "/usb0/cores/vc20_old.cor", "VIC-20", "0.3.0", 0x820eded2u);
        CHECK_EQ(start_identify((const uint8_t *)"VC20", &h, &d, msg, sizeof(msg), 0, &src), START_NO_DESCRIPTOR);
        CHECK(strstr(msg, "vc20_old.cor") != 0 && strstr(msg, "no description record") != 0);
        // a bundle that is not there
        handover_fill(&h, "/usb0/cores/none.cor", "VIC-20", "0.3.0", 0x820eded2u);
        CHECK_EQ(start_identify((const uint8_t *)"VC20", &h, &d, msg, sizeof(msg), 0, &src), START_NO_DESCRIPTOR);
        CHECK(strstr(msg, "none.cor") != 0);
        // no hand-over at all
        CHECK_EQ(start_identify((const uint8_t *)"VC20", 0, &d, msg, sizeof(msg), 0, &src), START_NO_DESCRIPTOR);
        // the code is missing: the record is whole, the core cannot run
        BundleDescSource nocode(&fs, 0);
        handover_fill(&h, "/usb0/cores/vc20.cor", "VIC-20", "0.3.0", 0x820eded2u);
        CHECK_EQ(start_identify((const uint8_t *)"VC20", &h, &d, msg, sizeof(msg), 0, &nocode), START_NO_DESCRIPTOR);
        CHECK(strstr(msg, "mount slots") != 0 || strstr(msg, "control hooks") != 0);
        // the bundle another core's name: the manifest is "VIC-20", the hand-over says another core
        handover_fill(&h, "/usb0/cores/vc20.cor", "Another Core", "0.3.0", 0x820eded2u);
        CHECK_EQ(start_identify((const uint8_t *)"VC20", &h, &d, msg, sizeof(msg), 0, &src), START_COR_MISMATCH);
    }

    // ---- 3. a damaged record, or a section the reader does not know, is refused with a sentence that names the record and the section
    {
        DescRecord *d = new DescRecord;
        char why[240];
        const uint32_t soff = rd(rec, DESCREC_HEADER_V5);
        Bytes b = rec;
        patch(b, soff, 9);                                     // the first section's id: one that does not exist
        CHECK_EQ(parse(b, d, why, sizeof(why)), -1);
        CHECK(strstr(why, "description record") && strstr(why, "section 9"));
        b = rec;
        patch(b, soff + 4, 68);                                // the core section: 72 bytes -> 68
        CHECK_EQ(parse(b, d, why, sizeof(why)), -1);
        CHECK(strstr(why, "section core") != 0);
        b = rec;
        patch(b, soff + 8 + 4 * 1, 0x7FFFFFF0u);               // the core's name: outside the string table
        CHECK_EQ(parse(b, d, why, sizeof(why)), -1);
        CHECK(strstr(why, "section core") != 0);
        // the second section (video): its scale out of range
        uint32_t second = soff + 8 + rd(rec, soff + 4);
        CHECK_EQ(rd(rec, second), 2u);
        b = rec;
        patch(b, second + 8 + 4 * 2, 7);
        CHECK_EQ(parse(b, d, why, sizeof(why)), -1);
        CHECK(strstr(why, "section video") != 0);
        // the flag of the sections off, the record a version 6 still
        b = rec;
        patch(b, 12, rd(b, 12) & ~4u);
        CHECK_EQ(parse(b, d, why, sizeof(why)), -1);
        // cut anywhere: never read beyond the bytes
        for (size_t n = 0; n < rec.size(); n += 61) {
            Bytes c(rec.begin(), rec.begin() + n);
            CHECK(parse(c, d, why, sizeof(why)) != 0 || n == rec.size());
        }
        // every section length of the record, one word short, one word long: named, refused
        uint32_t at = soff;
        for (int k = 0; k < 6; k++) {
            uint32_t len = rd(rec, at + 4);
            Bytes c = rec;
            patch(c, at + 4, len + 4);
            CHECK_EQ(parse(c, d, why, sizeof(why)), -1);
            CHECK(strstr(why, "section") != 0);
            at += 8 + len;
        }
        CHECK_EQ(at, (uint32_t)rec.size());
    }

    DONE("descgen");
}
