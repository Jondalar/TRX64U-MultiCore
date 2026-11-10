#include "settings_ops.h"
#include "regset.h"
#include "ops_glue.h"
#include "cfgfile.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>

static int find_value(const Setting &s, const char *label)
{
    for (int v = 0; v < s.nvalues; v++) {
        if (!strcasecmp(s.values[v], label)) {
            return v;
        }
    }
    for (int v = 0; v < s.nvalues; v++) {
        if (cfgfile_label_head_equal(s.values[v], label)) {
            return v;
        }
    }
    return -1;
}

static int op_set(CoreCtx *c, const OpArgs &a, OpOut &o)
{
    if (!c->settings) {
        snprintf(o.msg, o.msglen, "The settings are not loaded yet");
        return OPE_FAILED;
    }
    int i = c->settings->find(a.s(0));
    if (i < 0) {
        snprintf(o.msg, o.msglen, "This core has no setting \"%s\"", a.s(0));
        return OPE_NOT_FOUND;
    }
    const Setting &s = c->desc->settings[i];
    if (s.kind == 3) {                                            // a selected file: the core's operation checks the path and keeps it
        OpIn in = in_str("path", a.s(1));
        OpResult r;
        int rc = ops_call(c, s.apply, &in, 1, &r);
        snprintf(o.msg, o.msglen, "%s", r.msg);
        return rc;
    }
    if (s.kind) {                                                 // a text: the value is the text
        bool ok = c->settings->set_text(i, a.s(1));
        if (s.when == WHEN_LIVE && s.apply) {
            regset_apply_setting(c, i);
        }
        snprintf(o.msg, o.msglen, "%s: %s", s.label ? s.label : s.key, s.kind == 2 ? "set" : a.s(1));
        return ok ? OPE_OK : OPE_IO;
    }
    int v = find_value(s, a.s(1));
    if (v < 0) {
        int w = snprintf(o.msg, o.msglen, "%s has no value \"%s\" (", s.label ? s.label : s.key, a.s(1));
        for (int k = 0; k < s.nvalues && w < o.msglen; k++) {
            w += snprintf(o.msg + w, o.msglen - w, "%s%s", k ? ", " : "", s.values[k]);
        }
        if (w < o.msglen) {
            snprintf(o.msg + w, o.msglen - w, ")");
        }
        return OPE_ARG;
    }
    bool saved = c->settings->set(i, v);
    if (s.when == WHEN_LIVE) {
        regset_apply_setting(c, i);
    }
    const char *notice = c->settings->take_notice();
    if (!saved && notice[0]) {
        snprintf(o.msg, o.msglen, "%s", notice);                  // the value holds for this session; the .cfg could not be written
        return OPE_IO;
    }
    // The name the menu shows: a key may carry its own "(at next reset)" marker, and the notice says it once.
    snprintf(o.msg, o.msglen, "%s: %s%s", s.label ? s.label : s.key, s.values[v], s.when == WHEN_RESET ? " (takes effect at the next reset)" : "");
    return OPE_OK;
}

static int op_get(CoreCtx *c, const OpArgs &a, OpOut &o)
{
    int i = c->settings ? c->settings->find(a.s(0)) : -1;
    if (i < 0) {
        snprintf(o.msg, o.msglen, "This core has no setting \"%s\"", a.s(0));
        return OPE_NOT_FOUND;
    }
    const Setting &s = c->desc->settings[i];
    snprintf(o.msg, o.msglen, "%s", s.kind == 2 ? "" : s.kind ? c->settings->text(i) : c->settings->label(i));
    return OPE_OK;
}

static int op_reset(CoreCtx *c, const OpArgs &, OpOut &o)
{
    if (!c->settings) {
        snprintf(o.msg, o.msglen, "The settings are not loaded yet");
        return OPE_FAILED;
    }
    bool ok = c->settings->reset_to_host();
    for (int i = 0; i < c->desc->nsettings; i++) {
        if (c->desc->settings[i].when == WHEN_LIVE) {
            regset_apply_setting(c, i);
        }
    }
    const char *notice = c->settings->take_notice();
    if (ok) {
        snprintf(o.msg, o.msglen, "Settings are the host's again (reset-time items at the next reset)");
        return OPE_OK;
    }
    snprintf(o.msg, o.msglen, "%s", notice[0] ? notice : "The settings file could not be removed");
    return OPE_IO;
}

// What the settings screen's chip says: whether a change reaches the .cfg (it is written at once, so "saved" means it can be).
static const char *state_saved(CoreCtx *c)
{
    return c->settings && c->settings->dirty() ? "Unsaved" : (c->settings && c->settings->saving_possible() ? "Saved" : "Not saved");
}

// Apply of a setting that is stored and read where it is used (nothing to write into the machine).
static int op_noop(CoreCtx *, const OpArgs &, OpOut &) { return OPE_OK; }

// Auto Save Config: 0 Off, 1 Ask, 2 Auto (the .cfg is written at once). Off and Ask keep a change until "Save to flash".
static int op_autosave(CoreCtx *c, const OpArgs &a, OpOut &)
{
    if (c->settings) {
        c->settings->set_autosave(a.i(0));
    }
    return OPE_OK;
}

static int op_save(CoreCtx *c, const OpArgs &, OpOut &o)
{
    if (!c->settings || !c->settings->has_path()) {
        snprintf(o.msg, o.msglen, "There is no settings file to write");
        return OPE_FAILED;
    }
    if (!c->settings->save()) {
        const char *n = c->settings->take_notice();
        snprintf(o.msg, o.msglen, "%s", n[0] ? n : "The settings could not be written");
        return OPE_IO;
    }
    snprintf(o.msg, o.msglen, "Settings written to %s", c->cfg_path);
    return OPE_OK;
}

// settings.reset_defaults and settings.clear are reset_to_host under their own names: the .cfg of this core goes, every value is the host's or the default
// again. The menu asks first and names what is lost.
static const char *state_unsaved(CoreCtx *c)
{
    return c->settings && c->settings->dirty() ? "unsaved" : 0;
}

// "Load Settings" (a .cfg picked in the Library): every item of the file whose key this core has is set, whatever store the section names
struct LoadCtx { CoreCtx *c; int n; };
static void load_item(void *user, const char *, const char *key, const char *value)
{
    LoadCtx *l = (LoadCtx *)user;
    int i = l->c->settings->find(key);
    if (i < 0) {
        return;
    }
    const Setting &s = l->c->desc->settings[i];
    if (s.kind) {
        l->c->settings->set_text(i, value);
    } else {
        int at = -1;
        for (int v = 0; v < s.nvalues && at < 0; v++) {
            at = cfgfile_label_equal(s.values[v], value) ? v : -1;
        }
        for (int v = 0; v < s.nvalues && at < 0; v++) {
            at = cfgfile_label_head_equal(s.values[v], value) ? v : -1;
        }
        if (at >= 0) {
            l->c->settings->set(i, at);
        }
    }
    l->n++;
}
static int op_load_file(CoreCtx *c, const OpArgs &a, OpOut &o)
{
    static char buf[16384];
    int got = c->fs->load(a.s(0), buf, sizeof(buf));
    if (got < 0) {
        snprintf(o.msg, o.msglen, "Cannot read %s (a settings file is at most 16 KB)", a.s(0));
        return OPE_FAILED;
    }
    LoadCtx l = { c, 0 };
    cfgfile_parse(buf, got, load_item, &l);
    for (int i = 0; i < c->desc->nsettings; i++) {
        if (c->desc->settings[i].when == WHEN_LIVE) {
            regset_apply_setting(c, i);
        }
    }
    snprintf(o.msg, o.msglen, "%d setting(s) taken from %s", l.n, a.s(0));
    return l.n ? OPE_OK : OPE_FAILED;
}
// "Discard": what was changed since the .cfg was written goes; the file's values are read again and the live ones written into the machine
static int op_discard(CoreCtx *c, const OpArgs &, OpOut &o)
{
    if (!c->settings) {
        return OPE_FAILED;
    }
    c->settings->load();
    for (int i = 0; i < c->desc->nsettings; i++) {
        if (c->desc->settings[i].when == WHEN_LIVE) {
            regset_apply_setting(c, i);
        }
    }
    snprintf(o.msg, o.msglen, "The changes are gone: the saved settings are back");
    return OPE_OK;
}
static const ArgSpec path_arg[] = { ARG_PATH("path") };
static const ArgSpec noop_args[] = { ARG_INT("value", -2000, 2000) };
static const ArgSpec set_args[] = { ARG_STRING("key", 63), ARG_STRING("value", 63) };
static const ArgSpec get_args[] = { ARG_STRING("key", 63) };

void settings_register(OpRegistry &r)
{
    r.add_op("settings.set", set_args, 2, op_set);
    r.add_op("settings.get", get_args, 1, op_get);
    r.add_op("settings.reset_to_host", 0, 0, op_reset);
    r.add_op("settings.reset_defaults", 0, 0, op_reset);
    r.add_op("settings.clear", 0, 0, op_reset);       // Clear Config: the same, asked with the name of the file that goes
    r.add_op("settings.noop", noop_args, 1, op_noop);
    r.add_op("settings.load_file", path_arg, 1, op_load_file);
    r.add_op("settings.autosave", noop_args, 1, op_autosave);
    r.add_op("settings.save", 0, 0, op_save);
    r.add_op("settings.discard", 0, 0, op_discard);
    r.add_state("settings.saved", state_saved);
    r.add_state("settings.unsaved", state_unsaved);
}

int settings_set(CoreCtx *c, const char *key, const char *value, OpResult *r)
{
    OpIn in[2] = { in_str("key", key), in_str("value", value) };
    return ops_call(c, "settings.set", in, 2, r);
}
