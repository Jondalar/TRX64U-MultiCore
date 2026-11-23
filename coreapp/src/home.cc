#include "home.h"
#include "ops.h"
#include "settings.h"
#include <strings.h>

static const char *const enter_values[] = { "No", "Yes" };                   // the host's Disabled/Enabled (CFG_USERIF_START_HOME) is translated in Settings::load, so its value is the default here
static const char *const dir_default[] = { "" };
static const char *const reopen_values[] = { "Home (F1)", "Last screen" };

const char *home_extend(const CoreDesc *d, CoreDesc *out, Setting *store)
{
    *out = *d;
    bool has_dir = false, has_enter = false, has_reopen = false;
    for (int i = 0; i < d->nsettings; i++) {
        has_dir |= !strcasecmp(d->settings[i].key, HOME_DIR_KEY);
        has_enter |= !strcasecmp(d->settings[i].key, HOME_ENTER_KEY);
        has_reopen |= !strcasecmp(d->settings[i].key, REOPEN_KEY);
    }
    int add = (has_dir ? 0 : 1) + (has_enter ? 0 : 1) + (has_reopen ? 0 : 1);
    if (!add) {
        return 0;
    }
    if (d->nsettings + add > SETTINGS_MAX) {
        return "settings: no room for the home folder items the application adds";
    }
    if (store != d->settings) {
        for (int i = 0; i < d->nsettings; i++) {
            store[i] = d->settings[i];
        }
    }
    int n = d->nsettings;
    if (!has_enter) {
        Setting &e = store[n++];
        e = Setting();
        e.key = HOME_ENTER_KEY;
        e.label = "Enter home on startup";
        e.values = enter_values;
        e.nvalues = 2;
        e.def = 0;
        e.when = WHEN_LIVE;
        e.apply = HOME_OP;
    }
    if (!has_dir) {
        Setting &h = store[n++];
        h = Setting();
        h.key = HOME_DIR_KEY;
        h.label = "Home directory";
        h.values = dir_default;
        h.nvalues = 1;
        h.kind = 1;                                   // a text; base is its longest length
        h.base = 60;
        h.when = WHEN_LIVE;
        h.apply = HOME_OP;
    }
    if (!has_reopen) {                                // T192.3 D30: where the menu opens
        Setting &r = store[n++];
        r = Setting();
        r.key = REOPEN_KEY;
        r.label = "Menu opens at";
        r.values = reopen_values;
        r.nvalues = 2;
        r.def = 0;
        r.when = WHEN_LIVE;
        r.apply = HOME_OP;
    }
    out->settings = store;
    out->nsettings = n;
    return 0;
}

const char *home_dir(CoreCtx *c)
{
    int i = c->settings ? c->settings->find(HOME_DIR_KEY) : -1;
    const char *t = i >= 0 ? c->settings->text(i) : "";
    while (*t == ' ') {
        t++;
    }
    return t;
}

bool home_enter(CoreCtx *c)
{
    int i = c->settings ? c->settings->find(HOME_ENTER_KEY) : -1;
    return i >= 0 && c->settings->value(i) == 1;
}

bool home_reopen_last(CoreCtx *c)
{
    int i = c->settings ? c->settings->find(REOPEN_KEY) : -1;
    return i >= 0 && c->settings->value(i) == 1;
}

static const ArgSpec home_args[] = { ARG_INT("value", -2000, 2000) };
static int op_home(CoreCtx *, const OpArgs &, OpOut &) { return OPE_OK; }   // the F3 Library reads the values when it opens

void home_register(OpRegistry &r)
{
    r.add_op(HOME_OP, home_args, 1, op_home);
}
