#include "settings.h"
#include "cfgfile.h"
#include "picture.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>

static uint32_t s_generation;

Settings::Settings(const Setting *items, int n, const char *store, CoreFs *fs, const char *cfg_path, FlashLayer *flash)
    : items_(items), gen_(++s_generation), n_(n > SETTINGS_MAX ? SETTINGS_MAX : n), store_(store), fs_(fs), flash_(flash),
      save_failed_(false), notice_pending_(false)
{
    strncpy(cfg_path_, cfg_path ? cfg_path : "", sizeof(cfg_path_) - 1);
    cfg_path_[sizeof(cfg_path_) - 1] = 0;
    notice_[0] = 0;
    ntext_ = 0;
    autosave_ = 2;
    dirty_ = false;
    memset(held_, 0, sizeof(held_));
    memset(extra_, 0, sizeof(extra_));
    memset(mnt_, 0, sizeof(mnt_));
    for (int i = 0; i < SETTINGS_MAX; i++) {
        host_[i] = core_[i] = ovr_[i] = -1;
        tix_[i] = hix_[i] = -1;
    }
}

int Settings::label_index(int i, const char *label) const
{
    for (int v = 0; v < items_[i].nvalues; v++) {
        if (cfgfile_label_equal(items_[i].values[v], label)) {
            return v;
        }
    }
    for (int v = 0; v < items_[i].nvalues; v++) {
        if (cfgfile_label_head_equal(items_[i].values[v], label)) {
            return v;
        }
    }
    return -1;
}

void Settings::remap(int i, const char *const *old_values, int old_n)
{
    int16_t *layer[2] = { &host_[i], &core_[i] };
    for (int k = 0; k < 2; k++) {
        int x = *layer[k];
        if (x < 0) {
            continue;
        }
        *layer[k] = (x < old_n && old_values[x]) ? (int16_t)label_index(i, old_values[x]) : (int16_t)-1;
    }
}

int Settings::find(const char *key) const
{
    for (int i = 0; i < n_; i++) {
        if (!strcasecmp(items_[i].key, key)) {
            return i;
        }
    }
    return -1;
}

void Settings::on_item(void *user, const char *store, const char *key, const char *value)
{
    Settings *s = (Settings *)user;
    if (strcasecmp(store, s->store_) != 0) {
        return;                                      // a section of another machine: ignored, as Gideon's reader does
    }
    int i = s->find(key);
    if (i < 0) {
        if (!strncasecmp(key, "Pad ", 4)) {
            s->extra_set_mem(key, value);
        } else if (!strncasecmp(key, "Mount ", 6)) {
            s->mount_set_mem(key, value);
        }
        return;
    }
    if (s->items_[i].kind) {                          // a text: the line's text as it stands
        if (s->tix_[i] < 0 && s->ntext_ < TEXT_N) {
            s->tix_[i] = (int8_t)s->ntext_++;
        }
        if (s->tix_[i] >= 0) {
            snprintf(s->tpool_[s->tix_[i]], TEXT_LEN, "%s", value);
            s->core_[i] = 0;
        }
        return;
    }
    int v = s->label_index(i, value);
    if (v >= 0) {
        s->core_[i] = (int16_t)v;
    }
}

void Settings::load()
{
    dirty_ = false;
    memset(held_, 0, sizeof(held_));                                  // every core value is dropped below and read again: nothing is held back any more
    ntext_ = 0;
    memset(extra_, 0, sizeof(extra_));
    memset(mnt_, 0, sizeof(mnt_));
    int nh = 0;
    for (int i = 0; i < n_; i++) {
        host_[i] = core_[i] = ovr_[i] = -1;
        tix_[i] = hix_[i] = -1;
        char val[96];
        if (flash_ && flash_->get(store_, items_[i].key, val, sizeof(val))) {
            if (!items_[i].kind) {
                int hv = label_index(i, val);
                if (hv < 0 && !strcasecmp(val, "Disabled")) {                // T212 D14: the host says Disabled/Enabled where the core shows No/Yes
                    hv = label_index(i, "No");
                } else if (hv < 0 && !strcasecmp(val, "Enabled")) {
                    hv = label_index(i, "Yes");
                }
                host_[i] = (int16_t)hv;
            } else if (val[0] && nh < HTEXT_N) {                           // T212 S9: a text the host has (Home Directory) is this core's default
                snprintf(hpool_[nh], TEXT_LEN, "%s", val);
                hix_[i] = (int8_t)nh++;
                host_[i] = 0;
            }
        }
    }
    if (!cfg_path_[0]) {
        return;
    }
    uint64_t size = 0;
    bool dir = false;
    if (!fs_->stat(cfg_path_, &size, &dir) || dir) {
        return;                                      // no .cfg: every value is the host's or the default
    }
    static char buf[16384];
    int got = size <= sizeof(buf) ? fs_->load(cfg_path_, buf, sizeof(buf)) : -1;
    if (got < 0) {
        fail("The settings file cannot be read");
        return;
    }
    cfgfile_parse(buf, got, on_item, this);
}

int Settings::value(int i) const
{
    if (ovr_[i] >= 0) {
        return ovr_[i];
    }
    return stored_value(i);
}

void Settings::override_value(int i, int v)
{
    if (i >= 0 && i < n_ && v >= 0 && v < items_[i].nvalues && !items_[i].kind) {
        ovr_[i] = (int16_t)v;
    }
}

int Settings::stored_value(int i) const
{
    if (core_[i] >= 0) {
        return core_[i];
    }
    if (host_[i] >= 0) {
        return host_[i];
    }
    return items_[i].def;
}

Settings::Layer Settings::layer(int i) const
{
    return core_[i] >= 0 ? L_CORE : (host_[i] >= 0 ? L_HOST : L_DEFAULT);
}

void Settings::fail(const char *what)
{
    if (!save_failed_) {
        snprintf(notice_, sizeof(notice_), "%s. The settings hold until the next start.", what);
        notice_pending_ = true;
    }
    save_failed_ = true;
}

// The text of the file: the items the core layer holds (all = every item, at the value in force: a config is a full copy, T198 D6), the extras, and the
// mounts (those the settings keep, or `mk`/`mv` when a caller brings the live ones).
bool Settings::build(char *buf, int max, int *pos, bool all, const char *const *mk, const char *const *mv, int nm) const
{
    bool ok = cfgfile_begin(buf, max, pos, store_);
    for (int i = 0; i < n_ && ok; i++) {
        if (core_[i] >= 0 || all) {
            const char *val = items_[i].kind ? (core_[i] >= 0 && tix_[i] >= 0 ? tpool_[tix_[i]] : text(i)) : items_[i].values[stored_value(i)];   // never the override (T224 D2)
            char head[96];
            if (!items_[i].kind && items_[i].apply && !strcmp(items_[i].apply, PICTURE_OP) && !strcasecmp(items_[i].key, PICTURE_KEY) && stored_value(i) == 0) {   // the Picture item's Auto carries a display note that goes stale with the host's mode: the file keeps the label (T235: the other values end in their rate, "(50 Hz)", and keep it)
                snprintf(head, sizeof(head), "%s", val);
                char *n = strstr(head, " (");
                if (n) {
                    *n = 0;
                }
                val = head;
            }
            ok = cfgfile_item(buf, max, pos, items_[i].key, val);
        }
    }
    for (int i = 0; i < EXTRA_MAX && ok; i++) {
        if (extra_[i].used) {
            ok = cfgfile_item(buf, max, pos, extra_[i].key, extra_[i].val);
        }
    }
    if (mk) {
        for (int i = 0; i < nm && ok; i++) {
            ok = cfgfile_item(buf, max, pos, mk[i], mv[i]);
        }
    } else {
        for (int i = 0; i < MOUNT_MAX && ok; i++) {
            if (mnt_[i].used) {
                ok = cfgfile_item(buf, max, pos, mnt_[i].key, mnt_[i].val);
            }
        }
    }
    return ok && cfgfile_end(buf, max, pos);
}

bool Settings::write_cfg()
{
    if (!cfg_path_[0]) {
        return false;
    }
    if (safe_) {                                       // safe mode: the change runs, the stored file keeps what it had (set_safe)
        dirty_ = true;
        return true;
    }
    static char buf[16384];
    int pos = 0;
    if (!build(buf, sizeof(buf), &pos, false, 0, 0, 0) || !fs_->save(cfg_path_, buf, pos)) {
        fail("The settings cannot be saved (medium read only, full or removed)");
        return false;
    }
    save_failed_ = false;
    dirty_ = false;
    memset(held_, 0, sizeof(held_));
    return true;
}

bool Settings::export_config(const char *path, const char *const *mk, const char *const *mv, int nm, int *nsettings) const
{
    static char buf[16384];
    int pos = 0;
    if (!build(buf, sizeof(buf), &pos, true, mk, mv, nm) || !fs_->save(path, buf, (uint32_t)pos)) {
        return false;
    }
    if (nsettings) {
        *nsettings = n_;
    }
    return true;
}

void Settings::rebind(const char *path)
{
    gen_ = ++s_generation;
    snprintf(cfg_path_, sizeof(cfg_path_), "%s", path ? path : "");
    safe_ = false;                                     // a config loaded on purpose: its changes are written as always
    save_failed_ = false;
    notice_pending_ = false;
    dirty_ = false;
    memset(held_, 0, sizeof(held_));
    load();
}

bool Settings::save()
{
    safe_ = false;                                     // a save on purpose: from now on the file is written as it always is
    return write_cfg();
}

bool Settings::set(int i, int v)
{
    if (i < 0 || i >= n_ || v < 0 || v >= items_[i].nvalues) {
        return false;
    }
    core_[i] = (int16_t)v;
    ovr_[i] = -1;                                      // T224 D2: the person chose a value here: it is stored like any other
    if (!cfg_path_[0]) {
        fail("There is no bundle path, so there is no settings file");
        return false;
    }
    if (autosave_ != 2) {
        dirty_ = true;
        held_[i] = true;
        return true;
    }
    return write_cfg();
}

const char *Settings::text(int i) const
{
    if (i < 0 || i >= n_ || !items_[i].kind) {
        return "";
    }
    if (core_[i] >= 0 && tix_[i] >= 0) {
        return tpool_[tix_[i]];
    }
    return hix_[i] >= 0 ? hpool_[hix_[i]] : (items_[i].values ? items_[i].values[0] : "");
}

bool Settings::set_text(int i, const char *s)
{
    if (i < 0 || i >= n_ || !items_[i].kind) {
        return false;
    }
    if (tix_[i] < 0) {
        if (ntext_ >= TEXT_N) {
            return false;
        }
        tix_[i] = (int8_t)ntext_++;
    }
    snprintf(tpool_[tix_[i]], TEXT_LEN, "%s", s ? s : "");
    core_[i] = 0;
    if (!cfg_path_[0]) {
        fail("There is no bundle path, so there is no settings file");
        return false;
    }
    if (autosave_ != 2) {
        dirty_ = true;
        held_[i] = true;
        return true;
    }
    return write_cfg();
}

bool Settings::reset_to_host()
{
    memset(extra_, 0, sizeof(extra_));
    memset(mnt_, 0, sizeof(mnt_));
    for (int i = 0; i < n_; i++) {
        core_[i] = -1;
    }
    if (!cfg_path_[0]) {
        return true;
    }
    uint64_t size = 0;
    bool dir = false;
    if (!fs_->stat(cfg_path_, &size, &dir)) {
        return true;                                 // nothing to remove
    }
    if (fs_->remove(cfg_path_) || write_cfg()) {     // an empty section is the same as no file
        save_failed_ = false;
        return true;
    }
    return false;
}

const char *Settings::take_notice()
{
    if (!notice_pending_) {
        return "";
    }
    notice_pending_ = false;
    return notice_;
}

bool Settings::extra_get(const char *key, char *val, int n) const
{
    for (int i = 0; i < EXTRA_MAX; i++) {
        if (extra_[i].used && !strcasecmp(extra_[i].key, key)) {
            snprintf(val, n, "%s", extra_[i].val);
            return true;
        }
    }
    return false;
}

bool Settings::extra_set_mem(const char *key, const char *val)
{
    if (strlen(key) >= sizeof(extra_[0].key) || strlen(val) >= sizeof(extra_[0].val)) {
        return false;
    }
    int slot = -1;
    for (int i = 0; i < EXTRA_MAX; i++) {
        if (extra_[i].used && !strcasecmp(extra_[i].key, key)) { slot = i; break; }
        if (!extra_[i].used && slot < 0) slot = i;
    }
    if (slot < 0) {
        return false;
    }
    extra_[slot].used = true;
    snprintf(extra_[slot].key, sizeof(extra_[slot].key), "%s", key);
    snprintf(extra_[slot].val, sizeof(extra_[slot].val), "%s", val);
    return true;
}

bool Settings::extra_set(const char *key, const char *val)
{
    if (strncasecmp(key, "Pad ", 4) || !extra_set_mem(key, val)) {
        return false;
    }
    if (!cfg_path_[0]) {
        fail("There is no bundle path, so there is no settings file");
        return false;
    }
    if (autosave_ != 2) {                              // Auto Save Config is Off or Ask: the map waits with the other changes for save()
        dirty_ = true;
        return true;
    }
    return write_cfg();
}

// ---- T180 D2: the mounted media in the file ----
bool Settings::mount_set_mem(const char *key, const char *val)
{
    if (strlen(key) >= sizeof(mnt_[0].key) || (val && strlen(val) >= sizeof(mnt_[0].val)) || strncasecmp(key, "Mount ", 6)) {
        return false;
    }
    int slot = -1, free_slot = -1;
    for (int i = 0; i < MOUNT_MAX; i++) {
        if (mnt_[i].used && !strcasecmp(mnt_[i].key, key)) { slot = i; break; }
        if (!mnt_[i].used && free_slot < 0) free_slot = i;
    }
    if (!val) {
        if (slot >= 0) {
            memset(&mnt_[slot], 0, sizeof(mnt_[slot]));
        }
        return true;
    }
    if (slot < 0) {
        slot = free_slot;
    }
    if (slot < 0) {
        return false;
    }
    mnt_[slot].used = true;
    snprintf(mnt_[slot].key, sizeof(mnt_[slot].key), "%s", key);
    snprintf(mnt_[slot].val, sizeof(mnt_[slot].val), "%s", val);
    return true;
}

bool Settings::mount_get(const char *key, char *val, int n) const
{
    for (int i = 0; i < MOUNT_MAX; i++) {
        if (mnt_[i].used && !strcasecmp(mnt_[i].key, key)) {
            snprintf(val, n, "%s", mnt_[i].val);
            return true;
        }
    }
    return false;
}

bool Settings::mount_put(const char *key, const char *val)
{
    char was[MOUNT_VAL_LEN];
    bool had = mount_get(key, was, sizeof(was));
    if (had ? (val && !strcmp(was, val)) : !val) {
        return true;                                   // nothing changes, nothing is written
    }
    if (!mount_set_mem(key, val)) {
        fail("A mounted medium cannot be stored (the path is too long or there are too many)");
        return false;
    }
    if (!cfg_path_[0]) {
        fail("There is no bundle path, so there is no settings file");
        return false;
    }
    return write_cfg();
}

bool Settings::mounts_clear()
{
    bool any = false;
    for (int i = 0; i < MOUNT_MAX; i++) {
        any |= mnt_[i].used;
    }
    if (!any) {
        return true;
    }
    memset(mnt_, 0, sizeof(mnt_));
    return cfg_path_[0] ? write_cfg() : true;
}
