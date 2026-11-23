// The menu's descriptor, see menubuild.h.
#include <stdio.h>
#include "menubuild.h"
#include "volume.h"
#include "tone.h"
#include "mediamounts.h"
#include "home.h"
#include "hotkeys.h"
#include "baseset.h"

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
    return first;
}
