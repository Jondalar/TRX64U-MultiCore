#include "mediamounts.h"
#include "engines.h"
#include "boot.h"
#include "cfgfile.h"
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
    p.group = "system";
    out->settings = store;
    out->nsettings = d->nsettings + 1;
    return 0;
}

bool persist_on(CoreCtx *c)
{
    if (!c->settings) {
        return false;
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
                add_note(notes, sizeof(notes), name, msg[0] ? msg : "not mounted");
            } else {
                printf("[core] stored medium %s = %s: mounted\n", name, val);
            }
        }
        if (notes[0]) {
            snprintf(c->notice, sizeof(c->notice), "Stored media not mounted - %s", notes);
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

// ---- media sets
bool mediaset_dir(CoreCtx *c, char *out, int n)
{
    out[0] = 0;
    if (!c->cfg_path[0]) {
        return false;
    }
    const char *slash = strrchr(c->cfg_path, '/');
    if (!slash || slash == c->cfg_path || (int)(slash - c->cfg_path) + 7 >= n) {
        return false;
    }
    int dl = (int)(slash - c->cfg_path);
    memcpy(out, c->cfg_path, (size_t)dl);
    strcpy(out + dl, "/media");
    return true;
}

static bool name_ok(const char *name)
{
    int l = (int)strlen(name);
    if (l == 0 || l > MEDIASET_NAME_MAX || name[0] == '.' || name[l - 1] == ' ') {
        return false;
    }
    for (int i = 0; i < l; i++) {
        unsigned char ch = (unsigned char)name[i];
        if (ch < 0x20 || ch > 0x7E || strchr("/\\:*?\"<>|", ch)) {
            return false;
        }
    }
    return true;
}

static bool set_path(CoreCtx *c, const char *name, char *out, int n)
{
    char dir[200];
    if (!mediaset_dir(c, dir, sizeof(dir))) {
        return false;
    }
    return snprintf(out, n, "%s/%s.mnt", dir, name) < n;
}

int mediaset_list(CoreCtx *c, char names[][MEDIASET_NAME_MAX + 1], int max)
{
    char dir[200];
    if (!mediaset_dir(c, dir, sizeof(dir))) {
        return -1;
    }
    static FsEntryShort raw[128];
    int n = c->fs->list_short(dir, raw, 128);
    int k = 0;
    for (int i = 0; i < n && k < max; i++) {
        int l = (int)strlen(raw[i].name);
        if (raw[i].dir || raw[i].cut || l < 5 || l - 4 > MEDIASET_NAME_MAX || strcasecmp(raw[i].name + l - 4, ".mnt")) {
            continue;
        }
        memcpy(names[k], raw[i].name, (size_t)(l - 4));
        names[k][l - 4] = 0;
        k++;
    }
    return k;
}

int mediaset_save(CoreCtx *c, const char *name, char *msg, int msglen)
{
    if (!mounts_have_slots(c->desc)) {
        snprintf(msg, msglen, "This core has no media a set can hold.");
        return 1;
    }
    if (!name_ok(name)) {
        snprintf(msg, msglen, name[0] ? "\"%s\" is not a name for a media set." : "Give the media set a name.", name);
        return 1;
    }
    char dir[200], path[260];
    if (!mediaset_dir(c, dir, sizeof(dir)) || !set_path(c, name, path, sizeof(path))) {
        snprintf(msg, msglen, "This core has no folder of its own (it did not start from a .cor), so there is nowhere to keep a set.");
        return 1;
    }
    static char buf[4096];
    int pos = 0, count = 0;
    bool ok = cfgfile_begin(buf, sizeof(buf), &pos, c->desc->store);
    for (int i = 0; i < mounts_slots(c) && ok; i++) {
        const char *spec = mounts_slot_spec(c, i);
        if (spec) {
            char key[24];
            key_of(c, i, key, sizeof(key));
            ok = cfgfile_item(buf, sizeof(buf), &pos, key, spec);
            count++;
        } else if (slot_mounted(c, i)) {
            snprintf(msg, msglen, "A medium in slot %d has a path too long for a set.", i);   // mounted, but nothing can be said about it in a file
            return 1;
        }
    }
    ok = ok && cfgfile_end(buf, sizeof(buf), &pos);
    if (!ok) {
        snprintf(msg, msglen, "The media set does not fit its file.");
        return 1;
    }
    c->fs->mkdir(dir);
    if (!c->fs->save(path, buf, (uint32_t)pos)) {
        snprintf(msg, msglen, "The media set cannot be saved to %s (medium read only, full or removed).", dir);
        return 1;
    }
    snprintf(msg, msglen, "Media set \"%s\" saved: %d medi%s", name, count, count == 1 ? "um" : "a");
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

int mediaset_load(CoreCtx *c, const char *name, char *msg, int msglen)
{
    if (!mounts_have_slots(c->desc)) {
        snprintf(msg, msglen, "This core has no media a set can hold.");
        return 1;
    }
    char path[260];
    if (!name_ok(name) || !set_path(c, name, path, sizeof(path))) {
        snprintf(msg, msglen, name[0] ? "There is no media set \"%s\"." : "Give the name of a media set.", name);
        return 1;
    }
    static char buf[4096];
    int got = c->fs->load(path, buf, sizeof(buf));
    if (got < 0) {
        snprintf(msg, msglen, "There is no media set \"%s\".", name);
        return 1;
    }
    enum { SLOTS = WIN_SLOTS_MAX + SEC_UNITS_MAX + MOUNT_SLOTS_MAX };
    static char val[SLOTS][Settings::MOUNT_VAL_LEN];
    bool has[SLOTS];
    memset(val, 0, sizeof(val));
    memset(has, 0, sizeof(has));
    SetRead r = { c, val, has };
    cfgfile_parse(buf, got, set_item, &r);
    // D4: what the machine has pending goes to its media before any of them is let go; one that cannot be written keeps everything as it is, and no reset follows
    if (media_writeback(c, WB_WHY_RESET, msg, msglen)) {
        return 1;
    }
    bool was_ready = c->mounts_ready;
    c->mounts_ready = false;                                  // the slots are stored once, when all of them are done
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
            add_note(notes, sizeof(notes), slot, m[0] ? m : "not mounted");
            if (slot_mounted(c, i)) {
                rc = eject_idx(c, i, m, sizeof(m));            // the set names a medium that is not there: the slot is empty, not what it held before
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
    if (was_ready) {
        store_all(c);                                           // D2: what is mounted now is what the next start mounts
    }
    if (rc) {
        return 1;                                               // a medium that would not be let go: the machine is not reset
    }
    char rm[160];
    rm[0] = 0;
    bool waits = c->desc->needs_program && !c->program_loaded;   // a game cartridge is the program: a set that leaves the slot empty leaves nothing to reset into
    if (!waits && boot_cold_reset(c, rm, sizeof(rm))) {
        snprintf(msg, msglen, "Media set \"%s\" is mounted, but the core did not reset: %s", name, rm);
        return 1;
    }
    int w = snprintf(msg, msglen, "Media set \"%s\" loaded: %d medi%s, %s", name, mounted, mounted == 1 ? "um" : "a", waits ? "no program: the core waits for one" : "core reset");
    if (notes[0] && w < msglen) {
        snprintf(msg + w, msglen - w, ". Left empty - %s", notes);
    }
    return 0;
}

static int op_save_set(CoreCtx *c, const OpArgs &a, OpOut &o) { return mediaset_save(c, a.s(0), o.msg, o.msglen) ? OPE_FAILED : OPE_OK; }
static int op_load_set(CoreCtx *c, const OpArgs &a, OpOut &o) { return mediaset_load(c, a.s(0), o.msg, o.msglen) ? OPE_FAILED : OPE_OK; }

static const char *state_sets(CoreCtx *c)
{
    static char text[8];
    static char names[MEDIASETS_MAX][MEDIASET_NAME_MAX + 1];
    int n = mediaset_list(c, names, MEDIASETS_MAX);
    if (n <= 0) {
        return 0;
    }
    snprintf(text, sizeof(text), "%d", n);
    return text;
}

static const ArgSpec apply_args[] = { ARG_INT("value", 0, 1) };
static const ArgSpec set_args[] = { ARG_STRING("name", MEDIASET_NAME_MAX) };

void mounts_register(OpRegistry &r)
{
    r.add_op(PERSIST_OP, apply_args, 1, op_persist_apply);
    r.add_op("media.save_set", set_args, 1, op_save_set);
    r.add_op("media.load_set", set_args, 1, op_load_set);
    r.add_state("media.sets", state_sets);
}
