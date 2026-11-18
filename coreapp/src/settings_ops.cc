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
    if (c->config[0]) {                                   // T198: the file is the config; deleting it would lose its mounts and its name from the list
        snprintf(o.msg, o.msglen, "This run uses the config \"%s\". Reset is for the core's own settings: start the core plain, or change the settings here.", c->config);
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
    snprintf(o.msg, o.msglen, "Settings written to %s", c->settings->path());
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

// CONTROL LOAD_CONFIG of the command interface (control_target.cc:load_config, configio.cc S_read_from_file): the text of a .cfg staged by the caller is taken line by line as
// the firmware's reader does - "[store]" lines, "item=value" lines, what cannot be applied logged as "Line n: ..." - and the live settings it changed are applied once each.
// Any store name is accepted (the keys of this core are unique, as in load_item above). Returns OPE_FAILED when a line was malformed (the firmware's "config file had errors").
static char g_stage[16384];
static int g_stage_len;
static char g_log[512];
int settings_stage(const char *text, int len)
{
    g_stage_len = len < (int)sizeof(g_stage) ? len : (int)sizeof(g_stage);
    memcpy(g_stage, text, g_stage_len);
    return g_stage_len;
}
const char *settings_load_log(void) { return g_log; }

static void log_add(const char *fmt, int line, const char *a, const char *b)
{
    int n = (int)strlen(g_log);
    if (n < (int)sizeof(g_log) - 1) {
        snprintf(g_log + n, sizeof(g_log) - n, fmt, line, a, b);
    }
}

static int op_load_staged(CoreCtx *c, const OpArgs &, OpOut &o)
{
    g_log[0] = 0;
    if (!c->settings) {
        snprintf(o.msg, o.msglen, "The settings are not loaded yet");
        return OPE_FAILED;
    }
    bool allok = true, in_store = false;
    static bool changed[SETTINGS_MAX];
    memset(changed, 0, sizeof(changed));
    int nset = c->desc->nsettings < SETTINGS_MAX ? c->desc->nsettings : SETTINGS_MAX;
    int linenr = 0;
    for (int pos = 0; pos < g_stage_len;) {
        char line[128];
        int n = 0;
        while (pos < g_stage_len && g_stage[pos] != 0x0A) {
            if (g_stage[pos] != 0x0D && n < 127) {
                line[n++] = g_stage[pos];
            }
            pos++;
        }
        pos++;
        line[n] = 0;
        linenr++;
        if (line[0] == '#' || line[0] == ';') {
            continue;
        }
        if (line[0] == '[') {
            in_store = strchr(line, ']') != 0;
            continue;
        }
        if (!line[0]) {
            continue;
        }
        if (!in_store) {
            log_add("Line %d: Not inside valid store.\n", linenr, "", "");
            allok = false;
            continue;
        }
        char *eq = strchr(line, '=');
        char name[40];
        int nl = eq ? (int)(eq - line) : (int)strlen(line);
        nl = nl < 39 ? nl : 39;
        memcpy(name, line, nl);
        name[nl] = 0;
        while (nl > 0 && (name[nl - 1] == ' ' || name[nl - 1] == '\t')) {
            name[--nl] = 0;
        }
        const char *nm = name;
        while (*nm == ' ' || *nm == '\t') {
            nm++;
        }
        if (!*nm) {
            log_add("Line %d: No item name.\n", linenr, "", "");
            allok = false;
            continue;
        }
        if (!eq) {
            log_add("Line %d: No value given for item '%s'.\n", linenr, nm, "");
            allok = false;
            continue;
        }
        const char *val = eq + 1;
        int i = c->settings->find(nm);
        if (i < 0 || i >= nset) {
            log_add("Line %d: Item '%s' not found in this core; ignored.\n", linenr, nm, "");
            continue;
        }
        const Setting &s = c->desc->settings[i];
        if (s.kind == 3) {
            OpIn in = in_str("path", val);
            OpResult *r = new OpResult;
            ops_call(c, s.apply, &in, 1, r);
            delete r;
        } else if (s.kind) {
            if (strcmp(c->settings->text(i), val)) {
                c->settings->set_text(i, val);
                changed[i] = true;
            }
        } else {
            int at = -1;
            for (int v = 0; v < s.nvalues && at < 0; v++) {
                at = cfgfile_label_equal(s.values[v], val) ? v : -1;
            }
            for (int v = 0; v < s.nvalues && at < 0; v++) {
                at = cfgfile_label_head_equal(s.values[v], val) ? v : -1;
            }
            if (at < 0) {
                log_add("Line %d: Value '%s' is not a valid choice for item %s\n", linenr, val, s.label ? s.label : s.key);
                allok = false;
            } else if (at != c->settings->value(i)) {
                c->settings->set(i, at);
                changed[i] = true;
            }
        }
    }
    const char *done_op[48];
    uint16_t done_reg[48];
    int nop = 0, nreg = 0;
    for (int i = 0; i < nset; i++) {                                // each apply once, as a store is effectuated once
        const Setting &s = c->desc->settings[i];
        if (!changed[i] || s.when != WHEN_LIVE) {
            continue;
        }
        bool seen = false;
        if (s.apply) {
            for (int k = 0; k < nop; k++) {
                seen |= done_op[k] == s.apply || !strcmp(done_op[k], s.apply);
            }
            if (!seen && nop < 48) {
                done_op[nop++] = s.apply;
            }
        } else if (s.bind) {
            for (int k = 0; k < nreg; k++) {
                seen |= done_reg[k] == s.bind->reg;
            }
            if (!seen && nreg < 48) {
                done_reg[nreg++] = s.bind->reg;
            }
        }
        if (!seen) {
            regset_apply_setting(c, i);
        }
    }
    snprintf(o.msg, o.msglen, "%s", allok ? "Settings loaded" : "The config file had errors");
    return allok ? OPE_OK : OPE_FAILED;
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
    r.add_op("settings.load_staged", 0, 0, op_load_staged);
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
