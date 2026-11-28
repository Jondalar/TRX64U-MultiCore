// T239.3 D17 step 3: a core whose data description comes from its .cor, table-driven. Every cores/*/app/desc.toml (tests/coreapp/gen_bundle.py globs them) is compiled by the
// real tool (tools/descrec.py), read back by the real generic reader (descrec.cc: descrec_parse, descrec_core; bundle.cc and start.cc: the bundle's record found, hashed, parsed,
// the descriptor made), and the descriptor is compared field by field with the compiled-in descriptor of the same core: every string, count, table and entry of it. The
// function hooks are not data: the test hands the reader the hooks of the compiled-in descriptor, as the compiled-in code provides them, and reports each. What the record has no
// field for is asserted empty, so a table the reader does not read cannot hide behind equal zeros. One line per core: fields compared, failed.
//
//   test_descgen DIR        DIR/index.txt (index_app_<core>.txt in the build of a core's own app): one line per core, "<id> <name of the core>"; DIR/<id>.bin the record, DIR/<id>.cor the bundle with it, DIR/<id>_old.cor without
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
#include "desc_tables_cmp.h"
#include "ops_glue.h"

const CoreDesc *const core_table[] = { 0 };               // no descriptor is compiled in for the start of this test: the bundle's record is all there is
extern const CoreDesc *const compiled_in_table[];         // the application's real table (compiled_table.cc), the compiled-in side of every comparison

typedef std::vector<uint8_t> Bytes;
static Bytes slurp(const std::string &p)
{
    std::ifstream f(p.c_str(), std::ios::binary);
    std::stringstream s;
    s << f.rdbuf();
    std::string t = s.str();
    return Bytes(t.begin(), t.end());
}

static int g_core_fields;                                  // what has been compared, summed over one core's two readings

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

// descrec_core with no code refuses at the first section whose function the core's code does not hand over, in the order of descrec_core: the ROM content checks, the rom set's
// functions, the raw keys' held function, the mount slots. The sentence that section gives, from the record's own flags; null when the record needs none of them.
static const char *first_missing_section(const DescRecord &d)
{
    for (int i = 0; i < d.sec->nroms; i++) {
        if (d.sec->rom_check[i]) {
            return "section roms: entry";
        }
    }
    if (d.sec->has_romset && (d.sec->romset_version || d.sec->romset_pick)) {
        return "section rom set:";
    }
    if (d.sec->has_rawkeys && d.sec->rawkeys_held) {
        return "section raw keys:";
    }
    if (d.sec->nmount) {
        return "section mount slots:";
    }
    return 0;
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
        EQN("rom has check (code)", a.check != 0, b.check != 0);
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
    EQS("core_id", want.core_id, got.core_id);
    EQN("own_volume", want.own_volume, got.own_volume);
    EQN("live_ms", want.live_ms, got.live_ms);
    EQN("menu_first", want.menu_first, got.menu_first);
    EQN("no_tone", want.no_tone, got.no_tone);
    EQS("machine_info", want.machine_info, got.machine_info);
    EQN("cold_ram_only", want.cold_ram_only, got.cold_ram_only);

    compare_tables(want, got);
    // the menu record the descriptor carries (menu_rec) is the record itself when it comes from the bundle: the made descriptor has none, and its menu tables are the record's
    NUL("got menu_rec", got.menu_rec);

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
static DescCode hooks_of(const CoreDesc &d)
{
    DescCode h;
    memset(&h, 0, sizeof(h));
    h.init = d.init; h.release = d.release; h.warm_reset = d.warm_reset; h.cold_start = d.cold_start; h.media_flush = d.media_flush;
    h.media_pending = d.media_pending; h.register_ops = d.register_ops; h.rom_key = d.rom_key; h.mount_slots = d.mount_slots;
    for (int i = 0; i < d.nroms && i < ROMS_MAX; i++) {
        h.rom_check[i] = d.roms[i].check;
    }
    if (d.romset) {
        h.romset_version = d.romset->version;
        h.romset_pick = d.romset->pick;
    }
    if (d.rawkeys) {
        h.rawkeys_held = d.rawkeys->held;
    }
    if (d.pad) {
        h.pad_id_loaded = d.pad->id_loaded;
    }
    return h;
}

static void report_hooks(const CoreDesc &want, const CoreDesc &got, bool say)
{
    struct H { const char *name; bool want, got; bool same; };
    int checks = 0, gchecks = 0;
    for (int i = 0; i < want.nroms; i++) {
        checks += want.roms[i].check != 0;
        gchecks += i < got.nroms && got.roms[i].check == want.roms[i].check && want.roms[i].check != 0;
    }
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
          !!want.mount_slots == !!got.mount_slots && (!want.mount_slots || want.mount_slots->spec == got.mount_slots->spec) },
        { "mount_slots.mount", want.mount_slots && want.mount_slots->mount, got.mount_slots && got.mount_slots->mount,
          !!want.mount_slots == !!got.mount_slots && (!want.mount_slots || want.mount_slots->mount == got.mount_slots->mount) },
        { "mount_slots.eject", want.mount_slots && want.mount_slots->eject, got.mount_slots && got.mount_slots->eject,
          !!want.mount_slots == !!got.mount_slots && (!want.mount_slots || want.mount_slots->eject == got.mount_slots->eject) },
        { "rom checks", checks != 0, gchecks != 0, checks == gchecks },
        { "romset.version", want.romset && want.romset->version, got.romset && got.romset->version, !!(want.romset && want.romset->version) == !!(got.romset && got.romset->version) &&
          (!want.romset || want.romset->version == got.romset->version) },
        { "romset.pick", want.romset && want.romset->pick, got.romset && got.romset->pick, !!(want.romset && want.romset->pick) == !!(got.romset && got.romset->pick) &&
          (!want.romset || want.romset->pick == got.romset->pick) },
        { "rawkeys.held", want.rawkeys && want.rawkeys->held, got.rawkeys && got.rawkeys->held, !!(want.rawkeys && want.rawkeys->held) == !!(got.rawkeys && got.rawkeys->held) &&
          (!want.rawkeys || want.rawkeys->held == got.rawkeys->held) },
        { "pad.id_loaded", want.pad && want.pad->id_loaded, got.pad && got.pad->id_loaded, !!(want.pad && want.pad->id_loaded) == !!(got.pad && got.pad->id_loaded) &&
          (!want.pad || want.pad->id_loaded == got.pad->id_loaded) },
    };
    for (size_t i = 0; i < sizeof(hs) / sizeof(hs[0]); i++) {
        CHECK(hs[i].same);
        if (say) {
            printf("  hook %-18s %s (provided by the compiled-in code)\n", hs[i].name, hs[i].want ? "set" : "null");
        }
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

// the compiled-in descriptor of the core the record describes: the ID and the manifest name
static const CoreDesc *compiled_of(const char *name)
{
    for (const CoreDesc *const *p = compiled_in_table; *p; p++) {
        if (!strcasecmp((*p)->cor_name, name)) {
            return *p;
        }
    }
    return 0;
}

// one core: its record, read three ways (the record, the bundle through start_identify, damage), against the compiled-in descriptor; returns the failed checks
static int one_core(const std::string &dir, const std::string &id, const std::string &name)
{
    const int fails0 = g_fails;
    int total = 0;
    g_core_fields = 0;
    Bytes rec = slurp(dir + "/" + id + ".bin");
    const CoreDesc *want = compiled_of(name.c_str());
    CHECK(rec.size() > DESCREC_HEADER_V6);
    CHECK(want != 0);
    if (!want || rec.size() <= DESCREC_HEADER_V6) {
        return g_fails - fails0;
    }
    const DescCode hooks = hooks_of(*want);

    // ---- 1. the record, read by the generic reader, against the compiled-in descriptor
    {
        DescRecord *d = new DescRecord;
        char why[240];
        CHECK_EQ(parse(rec, d, why, sizeof(why)), 0);
        if (g_fails != fails0) {
            printf("%s\n", why);
        }
        CHECK(d->has_core);
        CoreDesc got;
        CHECK_EQ(descrec_core(d, &hooks, &got, why, sizeof(why)), 0);
        g_fields = 0;
        compare(*want, got);
        total += g_fields;
        report_hooks(*want, got, true);
        const char *dc = desc_check(&got);
        CHECK(dc == 0);                                        // the made descriptor passes the application's own descriptor check as the compiled-in one does
        if (dc) {
            printf("desc_check: %s\n", dc);
        }
        if (want->mount_slots) {                               // the descriptor with no code: nothing is taken from another descriptor
            CoreDesc bare;
            CHECK_EQ(descrec_core(d, 0, &bare, why, sizeof(why)), -1);
            const char *fm = first_missing_section(*d);
            CHECK(fm != 0);
            CHECK(fm && strstr(why, fm) != 0);
        }
    }

    // ---- 2. the same through the start: no descriptor compiled in, the bundle the hand-over names makes it
    {
        FakeFs fs;
        FakeIo io;
        fs.put("/usb0/cores/" + id + ".cor", slurp(dir + "/" + id + ".cor"));
        fs.put("/usb0/cores/" + id + "_old.cor", slurp(dir + "/" + id + "_old.cor"));
        BundleDescSource src(&fs, &hooks);
        Handover h;
        char msg[400];
        const CoreDesc *d = 0;
        const std::string cor = "/usb0/cores/" + id + ".cor";
        handover_fill(&h, cor.c_str(), name.c_str(), "0.3.0", 0x820eded2u);
        CHECK_EQ(start_identify((const uint8_t *)want->id, &h, &d, msg, sizeof(msg), 0, &src), START_OK);
        CHECK(d != 0);
        if (d) {
            g_fields = 0;
            compare(*want, *d);
            total += g_fields;
            report_hooks(*want, *d, false);
        }
        char expect[200];
        snprintf(expect, sizeof(expect), "[core] %c%c%c%c %s, cor %s", want->id[0], want->id[1], want->id[2], want->id[3], want->cor_name, cor.c_str());
        CHECK_STR(msg, expect);
        // without a source the start is what it was
        CHECK_EQ(start_identify((const uint8_t *)want->id, &h, &d, msg, sizeof(msg)), START_NO_DESCRIPTOR);
        // the ID the FPGA answers is not the record's
        char other[4] = { want->id[0], want->id[1], want->id[2], (char)(want->id[3] ^ 1) };
        CHECK_EQ(start_identify((const uint8_t *)other, &h, &d, msg, sizeof(msg), 0, &src), START_NO_DESCRIPTOR);
        CHECK(strstr(msg, "the core with the ID") != 0);
        // a bundle with no record: the core stays held, and the sentence names the bundle and the record
        const std::string old = "/usb0/cores/" + id + "_old.cor";
        handover_fill(&h, old.c_str(), name.c_str(), "0.3.0", 0x820eded2u);
        CHECK_EQ(start_identify((const uint8_t *)want->id, &h, &d, msg, sizeof(msg), 0, &src), START_NO_DESCRIPTOR);
        CHECK(strstr(msg, (id + "_old.cor").c_str()) != 0 && strstr(msg, "no description record") != 0);
        // a bundle that is not there
        handover_fill(&h, "/usb0/cores/none.cor", name.c_str(), "0.3.0", 0x820eded2u);
        CHECK_EQ(start_identify((const uint8_t *)want->id, &h, &d, msg, sizeof(msg), 0, &src), START_NO_DESCRIPTOR);
        CHECK(strstr(msg, "none.cor") != 0);
        // no hand-over at all
        CHECK_EQ(start_identify((const uint8_t *)want->id, 0, &d, msg, sizeof(msg), 0, &src), START_NO_DESCRIPTOR);
        // the code is missing: the record is whole, the core cannot run
        BundleDescSource nocode(&fs, 0);
        handover_fill(&h, cor.c_str(), name.c_str(), "0.3.0", 0x820eded2u);
        CHECK_EQ(start_identify((const uint8_t *)want->id, &h, &d, msg, sizeof(msg), 0, &nocode), START_NO_DESCRIPTOR);
        {
            DescRecord *dm = new DescRecord;
            char w2[240];
            CHECK_EQ(parse(rec, dm, w2, sizeof(w2)), 0);
            const char *fm = first_missing_section(*dm);
            const char *expect_part = fm ? fm : "the control hooks init and release";    // none of the four sections needs code: the start's own check for init and release
            CHECK(strstr(msg, expect_part) != 0);
            if (strstr(msg, expect_part) == 0) {
                printf("%s: expected \"%s\" in: %s\n", id.c_str(), expect_part, msg);
            }
        }
        // the bundle another core's name: the manifest says one name, the hand-over another
        handover_fill(&h, cor.c_str(), "Another Core", "0.3.0", 0x820eded2u);
        CHECK_EQ(start_identify((const uint8_t *)want->id, &h, &d, msg, sizeof(msg), 0, &src), START_COR_MISMATCH);
        // the bundle's medium comes up 2.9 s after the start, a stick enumerating (advisor 2026-11-23): the description waits for it, as the start does for the folder
        {
            struct Late : MediumEnv {
                FakeFs *fs;
                uint32_t t = 0, up_at;
                int delays = 0;
                Late(FakeFs *f, uint32_t up) : fs(f), up_at(up) { fs->gone = true; }
                uint32_t now_ms() override { return t; }
                void delay_ms(uint32_t ms) override { t += ms; if (++delays > 500 || t >= up_at) { fs->gone = false; } }
            } late(&fs, 2900);
            BundleDescSource waits(&fs, &hooks, &late);
            handover_fill(&h, cor.c_str(), name.c_str(), "0.3.0", 0x820eded2u);
            CHECK_EQ(start_identify((const uint8_t *)want->id, &h, &d, msg, sizeof(msg), 0, &waits), START_OK);
            CHECK(late.t >= 2900 && late.t < START_MEDIUM_WAIT_MS);
            fs.gone = true;                                    // the same start without the wait: no medium, the core is held
            CHECK_EQ(start_identify((const uint8_t *)want->id, &h, &d, msg, sizeof(msg), 0, &src), START_NO_DESCRIPTOR);
            fs.gone = false;
        }
    }

    // ---- 4. which description wins: the compiled-in one beside it, the record's own tables on a descriptor made of it, no setting twice
    {
        DescRecord *d1 = new DescRecord, *d2 = new DescRecord;
        char why[240];
        CHECK_EQ(parse(rec, d1, why, sizeof(why)), 0);
        CHECK_EQ(parse(rec, d2, why, sizeof(why)), 0);                 // the second parse is what the menu merge reads from the .cor, apart from the one that made the descriptor
        CoreDesc got;
        CHECK_EQ(descrec_core(d1, &hooks, &got, why, sizeof(why)), 0);
        CHECK(!want->from_record && got.from_record);
        // the base's own groups (ops_glue.h ops_record_groups, advisor 2026-11-23): for a core made of its record that registers no operations (its code is a library),
        // not for one whose code registers them, not for a compiled-in descriptor
        {
            static int calls;
            static const CoreDesc *seen;
            calls = 0;
            seen = 0;
            ops_record_groups = [](OpRegistry &, const CoreDesc *dd) { calls++; seen = dd; };
            CoreDesc lib = got, own = got, built = *want;
            lib.register_ops = 0;
            own.register_ops = [](OpRegistry &) { };
            built.register_ops = 0;
            CoreCtx c1, c2, c3;
            memset(&c1, 0, sizeof(c1));
            memset(&c2, 0, sizeof(c2));
            memset(&c3, 0, sizeof(c3));
            c1.desc = &lib;
            c2.desc = &own;
            c3.desc = &built;
            ops_of(&c1);
            CHECK_EQ(calls, 1);
            CHECK(seen == &lib);
            ops_of(&c2);
            ops_of(&c3);
            CHECK_EQ(calls, 1);
            ops_record_groups = 0;
        }
        CHECK(!descrec_applies(*d2, want));                            // compiled in: the record changes nothing (bundle_merge leaves the base as it is)
        CHECK(descrec_applies(*d2, &got));
        CoreDesc out;
        descrec_apply(*d2, &got, &out);
        CHECK_EQ(out.nsettings, got.nsettings);                         // the settings guard: the made descriptor's settings are the record's, none is listed twice
        CHECK_EQ(out.ntasks, got.ntasks);
        CHECK_EQ(out.nmedia, got.nmedia);
        CHECK_EQ(out.nfiletypes, got.nfiletypes);
        for (int i = 0; i < out.nsettings; i++) {
            for (int j = 0; j < i; j++) {
                CHECK(strcasecmp(out.settings[i].key, out.settings[j].key) != 0);
            }
        }
        // an item the application added behind the core's settings (Picture, Hotkeys ...) stays, once, at the end
        DescRecord *d3 = new DescRecord;
        CHECK_EQ(parse(rec, d3, why, sizeof(why)), 0);
        std::vector<Setting> more(got.settings, got.settings + got.nsettings);
        Setting app;
        memset(&app, 0, sizeof(app));
        static const char *const onoff[] = { "Off", "On" };
        app.key = "Application item"; app.values = onoff; app.nvalues = 2; app.apply = "x.y";
        more.push_back(app);
        CoreDesc withapp = got;
        withapp.settings = more.data();
        withapp.nsettings = (int)more.size();
        descrec_apply(*d3, &withapp, &out);
        CHECK_EQ(out.nsettings, got.nsettings + 1);
        CHECK_STR(out.settings[out.nsettings - 1].key, "Application item");
    }

    // ---- 3. a damaged record, or a section the reader does not know, is refused with a sentence that names the record and the section
    {
        DescRecord *d = new DescRecord;
        char why[240];
        const uint32_t soff = rd(rec, DESCREC_HEADER_V5);
        Bytes b = rec;
        patch(b, soff, 99);                                    // the first section's id: one that does not exist
        CHECK_EQ(parse(b, d, why, sizeof(why)), -1);
        CHECK(strstr(why, "description record") && strstr(why, "section 99"));
        b = rec;
        patch(b, soff + 4, rd(rec, soff + 4) - 4);             // the core section: one word short
        CHECK_EQ(parse(b, d, why, sizeof(why)), -1);
        CHECK(strstr(why, "section core") != 0);
        b = rec;
        patch(b, soff + 8 + 4 * 1, 0x7FFFFFF0u);               // the core's name: outside the string table
        CHECK_EQ(parse(b, d, why, sizeof(why)), -1);
        CHECK(strstr(why, "section core") != 0);
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
        int secs = 0;
        while (at < rec.size()) {
            uint32_t len = rd(rec, at + 4);
            Bytes c = rec;
            patch(c, at + 4, len + 4);
            CHECK_EQ(parse(c, d, why, sizeof(why)), -1);
            CHECK(strstr(why, "section") != 0);
            if (len >= 4) {
                c = rec;
                patch(c, at + 4, len - 4);
                CHECK(parse(c, d, why, sizeof(why)) != 0);
                CHECK(strstr(why, "section") != 0);
            }
            at += 8 + len;
            secs++;
        }
        CHECK_EQ(at, (uint32_t)rec.size());
        CHECK(secs >= 1);
    }
    g_core_fields = total;
    return g_fails - fails0;
}

int main(int argc, char **argv)
{
    setvbuf(stdout, 0, _IONBF, 0);
    CHECK(argc == 2);
    const std::string dir = argv[1];
    std::ifstream idx((dir + "/index_app_c64scpu.txt").c_str());
    std::string line;
    int cores = 0, fields = 0, failed = 0;
    while (std::getline(idx, line)) {
        if (line.empty()) {
            continue;
        }
        const size_t sp = line.find(' ');
        CHECK(sp != std::string::npos);
        if (sp == std::string::npos) {
            continue;
        }
        const std::string id = line.substr(0, sp), name = line.substr(sp + 1);
        const int fails0 = g_fails;
        const int bad = one_core(dir, id, name);
        // what was compared: the record's own run (1) and the bundle's (2); `fields` is summed over both
        printf("descgen: %-12s %s: %d fields compared (record, bundle), %d failed\n", id.c_str(), name.c_str(), g_core_fields, bad);
        fields += g_core_fields;
        failed += g_fails - fails0;
        cores++;
    }
    CHECK(cores >= 1);
    printf("descgen: %d cores, %d fields compared, %d failed\n", cores, fields, failed);
    DONE("descgen");
}
