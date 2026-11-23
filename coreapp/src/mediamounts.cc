#include "mediamounts.h"
#include "engines.h"
#include "boot.h"
#include "cfgfile.h"
#include "configlist.h"
#include "ops_glue.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>

static const char *const persist_values[] = { "No", "Yes" };

const char *persist_extend(const CoreDesc *d, CoreDesc *out, Setting *store)
{
    *out = *d;
    if (!mounts_have_slots(d)) {
        return 0;
    }
    for (int i = 0; i < d->nsettings; i++) {
        if (!strcasecmp(d->settings[i].key, PERSIST_KEY)) {
            return 0;
        }
    }
    if (d->nsettings + 1 > SETTINGS_MAX) {
        return "settings: no room for the Persist item the application adds";
    }
    if (store != d->settings) {
        for (int i = 0; i < d->nsettings; i++) {
            store[i] = d->settings[i];
        }
    }
    Setting &p = store[d->nsettings];
    p = Setting();
    p.key = PERSIST_KEY;
    p.values = persist_values;
    p.nvalues = 2;
    p.def = 0;                                     // Alex, 2026-11-04: "off by default, it has to be set actively"
    p.when = WHEN_LIVE;
    p.apply = PERSIST_OP;
    p.group = "storage";
    out->settings = store;
    out->nsettings = d->nsettings + 1;
    return 0;
}

bool persist_on(CoreCtx *c)
{
    if (!c->settings) {
        return false;
    }
    if (c->config[0]) {
        return true;                               // T198 D5: a run with a config keeps its mounts in the config
    }
    int i = c->settings->find(PERSIST_KEY);
    return i >= 0 && c->settings->value(i) == 1;
}

// ---- the slots
static int nwin(CoreCtx *c) { return c->desc->window_media ? c->desc->window_media->nslots : 0; }
static int nsec(CoreCtx *c) { return c->desc->sector_media ? c->desc->sector_media->nunits : 0; }
static int neng(CoreCtx *c) { return c->desc->mount_slots ? c->desc->mount_slots->n : 0; }

int mounts_slots(CoreCtx *c)
{
    return nwin(c) + nsec(c) + neng(c);
}

void mounts_slot_name(CoreCtx *c, int idx, char *out, int n)
{
    if (idx < nwin(c)) {
        snprintf(out, n, "%s%d", c->desc->window_media->prefix, idx);
    } else if (idx < nwin(c) + nsec(c)) {
        snprintf(out, n, "%s%d", c->desc->sector_media->prefix, idx - nwin(c));
    } else {
        snprintf(out, n, "%s", c->desc->mount_slots->names[idx - nwin(c) - nsec(c)]);
    }
}

// The engine's text of a slot, null when it is empty. A text too long for a key of the file is as good as none for the file (the slot is mounted all the same).
static const char *engine_spec(CoreCtx *c, int idx)
{
    const MountSlots *ms = c->desc->mount_slots;
    return ms->spec(c, idx - nwin(c) - nsec(c));
}

const char *mounts_slot_spec(CoreCtx *c, int idx)
{
    if (idx >= nwin(c) + nsec(c)) {
        const char *s = engine_spec(c, idx);
        return s && strlen(s) < Settings::MOUNT_VAL_LEN ? s : 0;
    }
    if (!c->eng) {
        return 0;
    }
    if (idx < nwin(c)) {
        const WinSlot &f = c->eng->win.slot[idx];
        return f.mounted && f.spec[0] ? f.spec : 0;
    }
    const SecUnit &h = c->eng->sec.unit[idx - nwin(c)];
    return h.mounted && h.spec[0] ? h.spec : 0;
}

int mounts_unit_source(CoreCtx *c, const char *slot, char *buf, int n)
{
    if (!c || !c->desc || !slot || !buf || n <= 0) {
        return -1;
    }
    buf[0] = 0;
    size_t sl = strlen(slot);
    sl -= sl && slot[sl - 1] == ':';
    for (int i = 0; i < nwin(c) + nsec(c); i++) {
        char name[32];
        mounts_slot_name(c, i, name, sizeof(name));
        if (strlen(name) == sl && !strncasecmp(name, slot, sl)) {
            const char *spec = mounts_slot_spec(c, i);
            if (!spec || !spec[0]) {
                return 0;
            }
            snprintf(buf, n, "%s", spec);
            return 1;
        }
    }
    return -1;
}

static bool slot_mounted(CoreCtx *c, int idx)
{
    if (idx >= nwin(c) + nsec(c)) {
        return engine_spec(c, idx) != 0;
    }
    return c->eng && (idx < nwin(c) ? c->eng->win.slot[idx].mounted : c->eng->sec.unit[idx - nwin(c)].mounted);
}

static void key_of(CoreCtx *c, int idx, char *out, int n)
{
    char name[16];
    mounts_slot_name(c, idx, name, sizeof(name));
    snprintf(out, n, "Mount %s", name);
}

// A line for the screen from the settings (a .cfg that cannot be written), told once
static void take_settings_notice(CoreCtx *c)
{
    const char *n = c->settings->take_notice();
    if (n[0]) {
        snprintf(c->notice, sizeof(c->notice), "%s", n);
        c->notice_pending = true;
    }
}

static void store_slot(CoreCtx *c, int idx)
{
    char key[24];
    key_of(c, idx, key, sizeof(key));
    if (!c->settings->mount_put(key, mounts_slot_spec(c, idx))) {
        take_settings_notice(c);
    }
}

static void changed_idx(CoreCtx *c, int idx)
{
    if (!c->mounts_ready || !persist_on(c)) {
        return;
    }
    if (idx >= 0 && idx < mounts_slots(c)) {
        store_slot(c, idx);
    }
}

void mounts_changed(CoreCtx *c, bool sector, int n)
{
    changed_idx(c, (sector ? nwin(c) : 0) + n);
}

// CoreCtx::mount_changed: slot n of the core's own engine
void mounts_engine_changed(CoreCtx *c, int n)
{
    changed_idx(c, nwin(c) + nsec(c) + n);
}

// SvcCtx::mount_notify: the slots a library marked since the last pass are stored, once each. Under the core lock. Marks wait until the start has mounted what the
// .cfg stores (mounts_ready), as a change of the core's own engine does.
void mounts_pass(CoreCtx *c)
{
    if (!c->mounts_ready) {
        return;
    }
    for (int i = 0; i < 32 && i < neng(c); i++) {
        if (c->mounts_dirty[i]) {
            c->mounts_dirty[i] = 0;                                 // before the spec is read: a mark that comes in now is stored at the next pass
            changed_idx(c, nwin(c) + nsec(c) + i);
        }
    }
}

// What is mounted now is what the file says (item on), or the file says nothing (item off)
static void store_all(CoreCtx *c)
{
    if (!c->settings) {
        return;
    }
    if (!persist_on(c)) {
        if (!c->settings->mounts_clear()) {
            take_settings_notice(c);
        }
        return;
    }
    for (int i = 0; i < mounts_slots(c); i++) {
        store_slot(c, i);
    }
}

// ---- mounting a stored spec
// "folder <path>[, <size> GB]" -> sec_mount_folder's "<path>/<size>"
static int mount_folder_spec(CoreCtx *c, int u, const char *spec, char *msg, int msglen)
{
    char arg[260];
    char path[240];
    char size[16];
    snprintf(path, sizeof(path), "%s", spec + 7);
    const char *gb = "";
    char *comma = strrchr(path, ',');
    if (comma && comma[1] == ' ') {
        int l = (int)strlen(comma + 2);
        const char *t = comma + 2;
        bool digits = l > 3 && !strcmp(t + l - 3, " GB");
        for (int i = 0; digits && i < l - 3; i++) {
            digits = (t[i] >= '0' && t[i] <= '9') || t[i] == '.' || t[i] == ',';
        }
        if (digits) {
            int sl = l - 3 < 15 ? l - 3 : 15;
            memcpy(size, t, (size_t)sl);
            size[sl] = 0;
            gb = size;
            *comma = 0;
        }
    }
    int pl = (int)strlen(path);
    while (pl > 1 && path[pl - 1] == '/') {
        path[--pl] = 0;
    }
    snprintf(arg, sizeof(arg), "%s/%s", path, gb);
    return sec_mount_folder(c, u, arg, msg, msglen);
}

static int mount_spec(CoreCtx *c, int idx, const char *spec, BootUi *ui, char *msg, int msglen)
{
    if (idx < nwin(c)) {
        return win_mount(c, idx, spec, msg, msglen);
    }
    if (idx >= nwin(c) + nsec(c)) {
        return c->desc->mount_slots->mount(c, idx - nwin(c) - nsec(c), spec, msg, msglen);
    }
    int u = idx - nwin(c);
    if (!strncmp(spec, "folder ", 7)) {
        if (ui) {
            ui->message("Media", "Mounting the folder...", false);
        }
        int rc = mount_folder_spec(c, u, spec, msg, msglen);
        if (ui) {
            ui->clear();
        }
        return rc;
    }
    return sec_mount(c, u, spec, msg, msglen);
}

static int eject_idx(CoreCtx *c, int idx, char *msg, int msglen)
{
    if (idx >= nwin(c) + nsec(c)) {
        return c->desc->mount_slots->eject(c, idx - nwin(c) - nsec(c), msg, msglen);
    }
    return idx < nwin(c) ? win_eject(c, idx, msg, msglen) : sec_eject(c, idx - nwin(c), msg, msglen);
}

// A note for the slots that could not be mounted: "HD0: <reason>; DF0: <reason>"
static void add_note(char *notes, int n, const char *slot, const char *why)
{
    int l = (int)strlen(notes);
    if (l < n - 1) {
        snprintf(notes + l, n - l, "%s%s: %s", l ? "; " : "", slot, why);
    }
}

void mounts_restore(CoreCtx *c, BootUi *ui)
{
    if (!mounts_have_slots(c->desc)) {
        return;
    }
    if (c->settings && persist_on(c)) {
        c->media_restoring = true;                // the machine is held and boot_start releases it: a mount does not reset (desc.h MountSlots)
        char notes[200];
        notes[0] = 0;
        for (int i = 0; i < mounts_slots(c); i++) {
            char key[24], val[Settings::MOUNT_VAL_LEN], name[16], msg[160];
            key_of(c, i, key, sizeof(key));
            if (!c->settings->mount_get(key, val, sizeof(val)) || !val[0]) {
                continue;
            }
            mounts_slot_name(c, i, name, sizeof(name));
            msg[0] = 0;
            if (mount_spec(c, i, val, ui, msg, sizeof(msg))) {
                printf("[core] stored medium %s = %s: not mounted: %s\n", name, val, msg);
                if (c->config[0]) {                                   // T198 D12: the note names the medium that is missing
                    char why[240];
                    snprintf(why, sizeof(why), "%s (%s)", val, msg[0] ? msg : "not mounted");
                    add_note(notes, sizeof(notes), name, why);
                } else {
                    add_note(notes, sizeof(notes), name, msg[0] ? msg : "not mounted");
                }
            } else {
                printf("[core] stored medium %s = %s: mounted\n", name, val);
            }
        }
        if (notes[0]) {
            if (c->config[0]) {
                snprintf(c->notice, sizeof(c->notice), "Config \"%s\": left empty - %s. The config is not changed: put the medium back and load it again", c->config, notes);
            } else {
                snprintf(c->notice, sizeof(c->notice), "Stored media not mounted - %s", notes);
            }
            c->notice_pending = true;
        }
        c->media_restoring = false;
    }
    c->mounts_ready = true;
}

// ---- the item's operation
static int op_persist_apply(CoreCtx *c, const OpArgs &, OpOut &)
{
    if (c->mounts_ready && mounts_have_slots(c->desc)) {
        store_all(c);
    }
    return OPE_OK;
}

// ---- configs (T198; the media sets of T180 D5 are configs without settings, D3)
// A config is "<core folder>/configs/<name>.cfg": the text format of the core's own .cfg, a full copy of the settings (D6) and one "Mount <slot>=<spec>" key per mounted
// slot. A media set of before T198 ("<core folder>/media/<name>.mnt") is listed as a config; the first start or load of one copies it into configs/ (verified, the .mnt stays).
// A run that started with a config, or loaded one, keeps its settings and its mounts in THAT file (D4, D5): the core's own .cfg is not touched.
static bool core_folder(CoreCtx *c, char *out, int n)
{
    out[0] = 0;
    if (!c->cfg_path[0]) {
        return false;
    }
    const char *slash = strrchr(c->cfg_path, '/');
    if (!slash || slash == c->cfg_path || (int)(slash - c->cfg_path) + 1 >= n) {
        return false;
    }
    memcpy(out, c->cfg_path, (size_t)(slash - c->cfg_path));
    out[slash - c->cfg_path] = 0;
    return true;
}

bool configs_dir(CoreCtx *c, char *out, int n)
{
    char cf[260];
    return core_folder(c, cf, sizeof(cf)) && config_dir_path(cf, false, out, n);
}

static bool config_path(CoreCtx *c, const char *name, bool legacy, char *out, int n)
{
    char cf[260];
    return core_folder(c, cf, sizeof(cf)) && config_file_path(cf, name, legacy, out, n);
}

enum { SLOTS_ALL = WIN_SLOTS_MAX + SEC_UNITS_MAX + MOUNT_SLOTS_MAX };
static int s_list_reads;                                   // the directory reads of configs_list (the bench counts them, S15)
int configs_list_reads(void) { return s_list_reads; }

static void list_dir(CoreCtx *c, const char *dir, bool legacy, ConfigList *l)
{
    static FsEntryShort raw[128];
    s_list_reads++;
    int n = c->fs->list_short(dir, raw, 128);
    if (!legacy && n > 0) {
        // an interrupted save (power loss) is put right before the list is made: X.bak without X.cfg is the old config and comes back, leftovers go
        struct G { static const char *get(void *ctx, int i, bool *d) { FsEntryShort *e = (FsEntryShort *)ctx; *d = e[i].dir; return e[i].cut ? 0 : e[i].name; } };
        static ConfigFix fix;
        configlist_fix_plan(&fix, n, G::get, raw);
        bool changed = false;
        for (int i = 0; i < fix.n; i++) {
            char from[400], to[400];
            snprintf(from, sizeof(from), "%s/%s", dir, fix.act[i].file);
            snprintf(to, sizeof(to), "%s/%s", dir, fix.act[i].to);
            changed |= fix.act[i].recover ? c->fs->rename(from, to) : c->fs->remove(from);
        }
        if (changed) {
            s_list_reads++;
            n = c->fs->list_short(dir, raw, 128);
        }
    }
    for (int i = 0; i < n; i++) {
        if (!raw[i].cut) {
            configlist_add(l, raw[i].name, raw[i].dir, legacy);
        }
    }
}

int configs_list(CoreCtx *c, ConfigList *l)
{
    configlist_clear(l);
    char cf[260], d[300];
    if (!core_folder(c, cf, sizeof(cf))) {
        return -1;
    }
    if (config_dir_path(cf, false, d, sizeof(d))) {
        list_dir(c, d, false, l);
    }
    if (config_dir_path(cf, true, d, sizeof(d))) {
        list_dir(c, d, true, l);
    }
    configlist_sort(l);
    return l->n;
}

// The list the menu and the state read: read once and kept until a save, a load or a start changes the folder (S15). The operations below drop it.
static ConfigList s_cache;
static bool s_cache_ok;
static void cache_drop(void) { s_cache_ok = false; }
int configs_cached(CoreCtx *c, const ConfigList **out)
{
    if (!s_cache_ok) {
        configs_list(c, &s_cache);
        s_cache_ok = true;
    }
    *out = &s_cache;
    char cf[260];
    return core_folder(c, cf, sizeof(cf)) ? s_cache.n : -1;
}

int config_save(CoreCtx *c, const char *name, char *msg, int msglen)
{
    if (!config_name_ok(name)) {
        snprintf(msg, msglen, "%s", config_name_why(name));
        return 1;
    }
    char dir[300], path[330];
    if (!configs_dir(c, dir, sizeof(dir)) || !config_path(c, name, false, path, sizeof(path)) || !c->settings) {
        snprintf(msg, msglen, "This core has no folder of its own (it did not start from a .cor), so it has no configs.");
        return 1;
    }
    ConfigList l;
    configs_list(c, &l);
    cache_drop();
    int at = configlist_find(&l, name);
    if (at < 0 && (l.n >= CONFIGS_MAX || l.more)) {
        snprintf(msg, msglen, "A core has at most %d configs - delete one first.", CONFIGS_MAX);
        return 1;
    }
    static char mk[SLOTS_ALL][24], mv[SLOTS_ALL][Settings::MOUNT_VAL_LEN];
    const char *pk[SLOTS_ALL], *pv[SLOTS_ALL];
    int count = 0;
    for (int i = 0; i < mounts_slots(c) && i < SLOTS_ALL; i++) {
        const char *spec = mounts_slot_spec(c, i);
        if (spec) {
            key_of(c, i, mk[count], sizeof(mk[count]));
            snprintf(mv[count], sizeof(mv[count]), "%s", spec);
            pk[count] = mk[count];
            pv[count] = mv[count];
            count++;
        } else if (slot_mounted(c, i)) {
            snprintf(msg, msglen, "A medium in slot %d has a path too long for a config.", i);   // mounted, but nothing can be said about it in a file
            return 1;
        }
    }
    c->fs->mkdir(dir);
    int ns = 0;
    // The file is written whole beside the old one and then takes its place: a write that fails midway leaves the old config as it was (the swap is rename old -> .bak,
    // rename new -> name, delete .bak; FatFs cannot rename over an existing file). A leftover .tmp / .bak is no config (the list takes .cfg only); configs_list puts an interrupted swap right (X.bak without X.cfg comes back).
    char tmp[340], bak[340];
    snprintf(tmp, sizeof(tmp), "%s", path);
    snprintf(bak, sizeof(bak), "%s", path);
    memcpy(tmp + strlen(tmp) - 4, ".tmp", 5);                                       // path ends in ".cfg" (config_path)
    memcpy(bak + strlen(bak) - 4, ".bak", 5);
    const bool replace = at >= 0 && !l.legacy[at];
    bool restore_failed = false;
    bool ok = c->settings->export_config(tmp, pk, pv, count, &ns);
    if (ok && replace) {
        c->fs->remove(bak);                                                       // a stale .bak (power loss before the last remove): FatFs cannot rename over it
        ok = c->fs->rename(path, bak);
    }
    if (ok) {
        ok = c->fs->rename(tmp, path);
        if (!ok && replace) {
            restore_failed = !c->fs->rename(bak, path);                               // the old config goes back; if not, the list recovers X.bak
        }
    }
    if (!ok) {
        c->fs->remove(tmp);
        if (restore_failed) {
            snprintf(msg, msglen, "The config \"%s\" cannot be saved and the old one could not be put back (it is kept as %s.bak and returns with the next list).", name, name);
            return 1;
        }
        snprintf(msg, msglen, "The config \"%s\" cannot be saved to %s (medium read only, full or removed).", name, dir);
        return 1;
    }
    if (replace) {
        c->fs->remove(bak);
    }
    snprintf(msg, msglen, "Config \"%s\" saved: %d medi%s, %d settings", name, count, count == 1 ? "um" : "a", ns);
    return 0;
}

struct SetRead {
    CoreCtx *c;
    char (*val)[Settings::MOUNT_VAL_LEN];
    bool *has;
};

static void set_item(void *user, const char *, const char *key, const char *value)
{
    SetRead *r = (SetRead *)user;
    if (strncasecmp(key, "Mount ", 6)) {
        return;
    }
    for (int i = 0; i < mounts_slots(r->c); i++) {
        char k[24];
        key_of(r->c, i, k, sizeof(k));
        if (!strcasecmp(k, key) && strlen(value) < Settings::MOUNT_VAL_LEN) {
            snprintf(r->val[i], Settings::MOUNT_VAL_LEN, "%s", value);
            r->has[i] = value[0] != 0;
        }
    }
}

// The file of a config: configs/<name>.cfg; a media set of before T198 is copied there first (verified, the .mnt stays). path is the file to read and to keep writing to;
// false when the config is not there at all.
static bool config_resolve_file(CoreCtx *c, const char *name, char *path, int n)
{
    uint64_t size;
    bool dir;
    char legacy[330];
    if (!config_path(c, name, false, path, n)) {
        return false;
    }
    if (c->fs->stat(path, &size, &dir) && !dir) {
        return true;
    }
    if (!config_path(c, name, true, legacy, sizeof(legacy)) || !c->fs->stat(legacy, &size, &dir) || dir || size > 4096) {
        return false;
    }
    static char a[4096], b[4096];
    int got = c->fs->load(legacy, a, sizeof(a));
    char cd[300];
    if (got >= 0 && configs_dir(c, cd, sizeof(cd))) {
        c->fs->mkdir(cd);
        if (c->fs->save(path, a, (uint32_t)got) && c->fs->load(path, b, sizeof(b)) == got && !memcmp(a, b, (size_t)got)) {
            printf("[core] config %s: copied from the media set %s\n", name, legacy);
            cache_drop();
            return true;
        }
        c->fs->remove(path);
    }
    snprintf(path, (size_t)n, "%s", legacy);                 // cannot be copied (read only medium): the set is used where it lies
    return true;
}

int config_load(CoreCtx *c, const char *name, char *msg, int msglen)
{
    char path[330];
    if (!config_name_ok(name) || !c->settings || !config_resolve_file(c, name, path, sizeof(path))) {
        snprintf(msg, msglen, name && name[0] ? "There is no config \"%s\"." : "Give the name of a config.", name ? name : "");
        return 1;
    }
    static char buf[16384];
    int got = c->fs->load(path, buf, sizeof(buf));
    if (got < 0) {
        snprintf(msg, msglen, "There is no config \"%s\".", name);
        return 1;
    }
    enum { SLOTS = WIN_SLOTS_MAX + SEC_UNITS_MAX + MOUNT_SLOTS_MAX };
    static char val[SLOTS][Settings::MOUNT_VAL_LEN];
    bool has[SLOTS];
    memset(val, 0, sizeof(val));
    memset(has, 0, sizeof(has));
    SetRead r = { c, val, has };
    cfgfile_parse(buf, got, set_item, &r);
    // D4 of T180: what the machine has pending goes to its media before any of them is let go; one that cannot be written keeps everything as it is, and no reset follows
    if (media_writeback(c, WB_WHY_RESET, msg, msglen)) {
        return 1;
    }
    bool was_ready = c->mounts_ready;
    c->mounts_ready = false;                                  // nothing is stored while the slots change: the config's file is what it is (D12)
    c->media_restoring = true;                                // the machine is reset once, at the end: a mount does not reset it
    char notes[240];
    notes[0] = 0;
    int rc = 0;
    int mounted = 0;
    for (int i = 0; i < mounts_slots(c) && i < SLOTS && !rc; i++) {
        char slot[16], m[160];
        mounts_slot_name(c, i, slot, sizeof(slot));
        m[0] = 0;
        if (!has[i]) {
            if (slot_mounted(c, i)) {
                rc = eject_idx(c, i, m, sizeof(m));
            }
        } else if (mount_spec(c, i, val[i], 0, m, sizeof(m))) {
            char why[200];
            snprintf(why, sizeof(why), "%s (%s)", val[i], m[0] ? m : "not mounted");
            add_note(notes, sizeof(notes), slot, why);
            if (slot_mounted(c, i)) {
                rc = eject_idx(c, i, m, sizeof(m));            // the config names a medium that is not there: the slot is empty, not what it held before
            }
        } else {
            mounted++;
        }
        if (rc) {
            snprintf(msg, msglen, "%s", m);
        }
    }
    c->media_restoring = false;
    c->mounts_ready = was_ready;
    if (rc) {
        return 1;                                               // a medium that would not be let go: the machine is not reset
    }
    // D4, D5: from here the settings live in the config's file; the core's own .cfg stays as it was
    c->settings->rebind(path);
    snprintf(c->config, sizeof(c->config), "%s", name);
    snprintf(c->settings_path, sizeof(c->settings_path), "%s", path);
    cache_drop();
    boot_apply_settings(c, true);                               // every setting of the config goes in, the live ones too
    char rm[160];
    rm[0] = 0;
    bool waits = c->desc->needs_program && !c->program_loaded;   // a game cartridge is the program: a config that leaves the slot empty leaves nothing to reset into
    if (!waits && boot_cold_reset(c, rm, sizeof(rm))) {
        snprintf(msg, msglen, "Config \"%s\" is loaded, but the core did not reset: %s", name, rm);
        return 1;
    }
    int w = snprintf(msg, msglen, "Config \"%s\" loaded: %d medi%s, %s", name, mounted, mounted == 1 ? "um" : "a", waits ? "no program: the core waits for one" : "core reset");
    if (notes[0] && w < msglen) {
        snprintf(msg + w, msglen - w, ". Left empty - %s. The config is not changed: put the medium back and load it again", notes);
    }
    return 0;
}

// The start (T198 D7, P4): the host named a config in the hand-over page. The settings are read from its file instead of the .cfg, so the first frame already runs with its settings
// and mounts (mounts_restore, boot_start). A config that is not there, or a core with no folder, starts plain with a line on the screen.
void config_start(CoreCtx *c, const char *name)
{
    cache_drop();
    c->config[0] = 0;
    c->settings_path[0] = 0;
    if (!name || !name[0]) {
        return;
    }
    char path[330];
    if (!c->cfg_path[0] || !c->fs) {
        snprintf(c->notice, sizeof(c->notice), "Config \"%s\": this core has no folder of its own, so it started plain.", name);
        c->notice_pending = true;
        return;
    }
    if (!config_name_ok(name) || !config_resolve_file(c, name, path, sizeof(path))) {
        snprintf(c->notice, sizeof(c->notice), "The config \"%s\" is not there (any more), so the core started plain.", name);
        c->notice_pending = true;
        printf("[core] config %s: not found, plain start\n", name);
        return;
    }
    snprintf(c->config, sizeof(c->config), "%s", name);
    snprintf(c->settings_path, sizeof(c->settings_path), "%s", path);
    printf("[core] config %s: %s\n", name, path);
}

static int op_save(CoreCtx *c, const OpArgs &a, OpOut &o) { return config_save(c, a.s(0), o.msg, o.msglen) ? OPE_FAILED : OPE_OK; }
static int op_load(CoreCtx *c, const OpArgs &a, OpOut &o) { return config_load(c, a.s(0), o.msg, o.msglen) ? OPE_FAILED : OPE_OK; }

static const char *state_count(CoreCtx *c)
{
    static char text[8];
    const ConfigList *l;
    int n = configs_cached(c, &l);
    if (n <= 0) {
        return 0;
    }
    snprintf(text, sizeof(text), "%d", n);
    return text;
}

static const char *state_active(CoreCtx *c) { return c->config[0] ? c->config : 0; }

static const ArgSpec apply_args[] = { ARG_INT("value", 0, 1) };
static const ArgSpec name_args[] = { ARG_STRING("name", CONFIG_NAME_MAX) };

void mounts_register(OpRegistry &r)
{
    r.add_op(PERSIST_OP, apply_args, 1, op_persist_apply);
    r.add_op("config.save", name_args, 1, op_save);
    r.add_op("config.load", name_args, 1, op_load);
    r.add_state("config.count", state_count);
    r.add_state("config.active", state_active);
}
