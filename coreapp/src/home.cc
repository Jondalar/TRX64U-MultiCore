#include "home.h"
#include "menubuild.h"
#include "ops.h"
#include "settings.h"
#include <strings.h>

// The three rows (label, kind, values, default) are the base UI TOML's (coreapp/base_ui.toml). The host's Disabled/Enabled (CFG_USERIF_START_HOME) is translated in Settings::load, so its value is the default there.
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
    const BaseRow *enter = menu_base_row(HOME_OP, HOME_ENTER_KEY), *dir = menu_base_row(HOME_OP, HOME_DIR_KEY), *reopen = menu_base_row(HOME_OP, REOPEN_KEY);
    if (!enter || !dir || !reopen) {
        return "settings: the base UI TOML has no row for the home folder items the application adds";
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
        menu_base_setting(store[n++], *enter);
    }
    if (!has_dir) {
        menu_base_setting(store[n++], *dir);          // a text: its base is its longest length
    }
    if (!has_reopen) {                                // T192.3 D30: where the menu opens
        menu_base_setting(store[n++], *reopen);
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
