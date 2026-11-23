// The menu's descriptor (T187): the core's own descriptor plus the items the application adds to every core, in the one order the board shows them.
// run.cc and every bench that shows "what the board shows" call this; nothing builds its own chain.
#ifndef COREAPP_MENUBUILD_H
#define COREAPP_MENUBUILD_H

#include "desc.h"
#include "picture.h"

// out = *desc with Picture (and Scale, Smoothing), Volume, Persist medium mounts, Home Directory / Enter Home on Startup, the hotkey map and the application's look, joystick and mouse settings appended, each with the setting group
// the base UI TOML names for it (coreapp/base_ui.toml; where each stands in the menu, tab and order, is that file's and a core's UI TOML places, menutree.h);
// `store` holds the merged settings and has to outlive `out`. An extension that has no room or does not apply is named on stdout ("[core] <name>: <why>")
// and skipped; the return value is the first reason, null when every extension went in. If Picture is refused, `out` starts as a copy of *desc.
const char *menu_build(const CoreDesc *desc, CoreDesc *out, Setting *store, const PictureEnv *env);

// T239.3 D32: a row the application adds to every core, as the base UI TOML (coreapp/base_ui.toml, compiled into base_rows.inc by tools/gen_base_ui.py) says it: the key, the label, the kind, the
// values and the default. The extend functions take their rows from here and keep only what a row cannot say (whether the core gets it, a default the core's own map or the gateware sets, a provider's
// values, the condition that depends on another row).
struct BaseRow {
    const char *key;
    const char *label;          // null: the key
    const char *const *values;  // a text row's values[0] is its default text
    int nvalues;
    int def;
    int32_t base;               // a range row (Mouse Sensitivity): the operation gets base + index; a text row: its longest length
    uint8_t kind;               // Setting::kind: 0 a choice among values, 1 a text
    const char *apply;
    const char *show_state;     // the condition that lists the row; null: always
    const char *provider;       // null: the values are the row's; else the name of what fills them at run time ("video.modes", "hotkeys.modifiers", "hotkeys.keys") and `values` is what the row shows until it has
};
// The row of an operation (and its key, when the operation serves several rows; the case of the key does not matter), null when the base UI TOML has none.
const BaseRow *menu_base_row(const char *apply, const char *key = 0);
const BaseRow *menu_base_rows(int *n);
// `s` = the row, as a setting that applies at once (WHEN_LIVE): every field of Setting is written, so the store may be stack memory.
void menu_base_setting(Setting &s, const BaseRow &r);

#endif
