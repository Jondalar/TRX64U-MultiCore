// The menu's descriptor, see menubuild.h.
#include <stdio.h>
#include "menubuild.h"
#include "menutree.h"
#include "volume.h"
#include "tone.h"
#include "mediamounts.h"
#include "home.h"
#include "hotkeys.h"
#include "baseset.h"
#include <string.h>
#include <strings.h>

#include "base_rows.inc"

const BaseRow *menu_base_rows(int *n)
{
    if (n) {
        *n = g_base_nrows;
    }
    return g_base_row;
}

const BaseRow *menu_base_row(const char *apply, const char *key)
{
    for (int i = 0; apply && i < g_base_nrows; i++) {
        if (!strcmp(g_base_row[i].apply, apply) && (!key || !strcasecmp(g_base_row[i].key, key))) {
            return &g_base_row[i];
        }
    }
    return 0;
}

void menu_base_setting(Setting &s, const BaseRow &r)
{
    s = Setting();
    s.key = r.key;
    s.label = r.label;
    s.values = r.values;
    s.nvalues = r.nvalues;
    s.def = r.def;
    s.when = WHEN_LIVE;
    s.apply = r.apply;
    s.base = r.base;
    s.kind = r.kind;
    s.show_state = r.show_state;
    s.pick = r.pick;
}

const char *menu_build(const CoreDesc *desc, CoreDesc *out, Setting *store, const PictureEnv *env)
{
    const char *first = 0;
    const char *why = picture_extend(desc, out, store, env);
    if (why) {
        printf("[core] %s: %s\n", desc->cor_name, why);   // desc_check names it in the bench; here the original runs without it
        *out = *desc;
        first = why;
    }
    why = volume_extend(out, out, store);      // T160 D1: the master volume, after Picture
    if (why) {
        printf("[core] %s: %s\n", desc->cor_name, why);
        if (!first) { first = why; }
    }
    why = tone_extend(out, out, store);        // T189: the tone preset, after Volume
    if (why) {
        printf("[core] %s: %s\n", desc->cor_name, why);
        if (!first) { first = why; }
    }
    why = persist_extend(out, out, store);    // T180 D2: "Persist medium mounts", for a core with media slots
    if (why) {
        printf("[core] %s: %s\n", desc->cor_name, why);
        if (!first) { first = why; }
    }
    why = home_extend(out, out, store);       // T180 D10: "Home Directory" and "Enter Home on Startup", for every core
    if (why) {
        printf("[core] %s: %s\n", desc->cor_name, why);
        if (!first) { first = why; }
    }
    why = hk_extend(out, out, store);         // T181 D3: the hotkey map (modifier and one key per action), for a core with a default map
    if (why) {
        printf("[core] %s: %s\n", desc->cor_name, why);
        if (!first) { first = why; }
    }
    why = baseset_extend(out, out, store);    // T239.3 base 3.3: the look, the joystick and the mouse of the generic application, after the hotkeys (their swap item names the joystick setting)
    if (why) {
        printf("[core] %s: %s\n", desc->cor_name, why);
        if (!first) { first = why; }
    }
    if (out->settings == store) {
        menu_place_groups(menu_place_base(), store, out->nsettings);     // D32: the group of every row the base UI TOML places (coreapp/base_ui.toml); the extend functions write none
    }
    return first;
}
